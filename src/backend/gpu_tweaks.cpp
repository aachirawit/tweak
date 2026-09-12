#include "backend/gpu_tweaks.h"

#include "backend/activity_log.h"

#include <windows.h>

#include <Wbemidl.h>
#include <comdef.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <string>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace szk::backend
{
namespace
{
struct com_guard
{
    HRESULT init_result;
    com_guard() : init_result(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~com_guard()
    {
        if (SUCCEEDED(init_result))
            ::CoUninitialize();
    }
    bool ok() const
    {
        return init_result == S_OK || init_result == S_FALSE;
    }
};

// Finds the PNPDeviceID of the first video controller whose name contains
// "NVIDIA", via WMI. Empty string if there isn't one.
std::wstring find_nvidia_pnp_device_id()
{
    com_guard com;
    if (!com.ok())
        return L"";

    IWbemLocator* locator = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<LPVOID*>(&locator))) ||
        !locator)
        return L"";

    IWbemServices* services = nullptr;
    HRESULT hr = locator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0,
                                        nullptr, nullptr, &services);
    if (FAILED(hr) || !services)
    {
        locator->Release();
        return L"";
    }

    ::CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    std::wstring found;
    IEnumWbemClassObject* enumerator = nullptr;
    hr = services->ExecQuery(
        _bstr_t(L"WQL"), _bstr_t(L"SELECT PNPDeviceID, Name FROM Win32_VideoController"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    if (SUCCEEDED(hr) && enumerator)
    {
        IWbemClassObject* object = nullptr;
        ULONG returned = 0;
        while (found.empty() && enumerator->Next(WBEM_INFINITE, 1, &object, &returned) == S_OK &&
               returned > 0)
        {
            VARIANT name;
            ::VariantInit(&name);
            const bool is_nvidia = SUCCEEDED(object->Get(L"Name", 0, &name, nullptr, nullptr)) &&
                                   name.vt == VT_BSTR && name.bstrVal &&
                                   ::StrStrIW(name.bstrVal, L"NVIDIA") != nullptr;
            ::VariantClear(&name);

            if (is_nvidia)
            {
                VARIANT id;
                ::VariantInit(&id);
                if (SUCCEEDED(object->Get(L"PNPDeviceID", 0, &id, nullptr, nullptr)) &&
                    id.vt == VT_BSTR && id.bstrVal)
                    found = id.bstrVal;
                ::VariantClear(&id);
            }
            object->Release();
        }
        enumerator->Release();
    }

    services->Release();
    locator->Release();
    return found;
}

// The device's "Driver" property is a REG_SZ like
// "{4d36e968-e325-11ce-bfc1-08002be10318}\0001" — the actual subkey of
// .../Control/Class that holds its driver-specific settings.
std::wstring find_nvidia_class_subkey()
{
    const std::wstring pnp_id = find_nvidia_pnp_device_id();
    if (pnp_id.empty())
        return L"";

    const std::wstring enum_key = L"SYSTEM\\CurrentControlSet\\Enum\\" + pnp_id;
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, enum_key.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return L"";

    wchar_t driver[256] = {};
    DWORD size = sizeof(driver);
    const bool ok = ::RegQueryValueExW(key, L"Driver", nullptr, nullptr,
                                       reinterpret_cast<BYTE*>(driver), &size) == ERROR_SUCCESS;
    ::RegCloseKey(key);
    if (!ok)
        return L"";

    return L"SYSTEM\\CurrentControlSet\\Control\\Class\\" + std::wstring(driver);
}

// Same shape of query as find_nvidia_pnp_device_id(), just checking for a
// name match instead of returning the device ID — used both to decide
// whether to report "no AMD GPU found" in the AMD tweaks below, and
// (publicly, see has_nvidia_gpu()/has_amd_gpu()) to filter the "All
// Settings" view to the vendor actually installed.
bool video_controller_name_contains(const wchar_t* needle_a, const wchar_t* needle_b)
{
    com_guard com;
    if (!com.ok())
        return false;

    IWbemLocator* locator = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<LPVOID*>(&locator))) ||
        !locator)
        return false;

    IWbemServices* services = nullptr;
    HRESULT hr = locator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0,
                                        nullptr, nullptr, &services);
    if (FAILED(hr) || !services)
    {
        locator->Release();
        return false;
    }

    ::CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    bool found = false;
    IEnumWbemClassObject* enumerator = nullptr;
    hr = services->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT Name FROM Win32_VideoController"),
                             WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                             &enumerator);
    if (SUCCEEDED(hr) && enumerator)
    {
        IWbemClassObject* object = nullptr;
        ULONG returned = 0;
        while (!found && enumerator->Next(WBEM_INFINITE, 1, &object, &returned) == S_OK &&
               returned > 0)
        {
            VARIANT name;
            ::VariantInit(&name);
            if (SUCCEEDED(object->Get(L"Name", 0, &name, nullptr, nullptr)) && name.vt == VT_BSTR &&
                name.bstrVal)
            {
                found = ::StrStrIW(name.bstrVal, needle_a) != nullptr ||
                        (needle_b && ::StrStrIW(name.bstrVal, needle_b) != nullptr);
            }
            ::VariantClear(&name);
            object->Release();
        }
        enumerator->Release();
    }

    services->Release();
    locator->Release();
    return found;
}

bool set_dword(HKEY root, const wchar_t* subkey, const wchar_t* value, DWORD data)
{
    HKEY key = nullptr;
    if (::RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    const bool ok = ::RegSetValueExW(key, value, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data),
                                     sizeof(data)) == ERROR_SUCCESS;
    ::RegCloseKey(key);
    return ok;
}

bool dword_equals(HKEY root, const wchar_t* subkey, const wchar_t* value, DWORD expected)
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    DWORD data = 0, size = sizeof(data), type = 0;
    const bool ok = ::RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(&data),
                                       &size) == ERROR_SUCCESS &&
                    type == REG_DWORD;
    ::RegCloseKey(key);
    return ok && data == expected;
}

bool set_string(HKEY root, const wchar_t* subkey, const wchar_t* value, const wchar_t* data)
{
    HKEY key = nullptr;
    if (::RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    const bool ok =
        ::RegSetValueExW(key, value, 0, REG_SZ, reinterpret_cast<const BYTE*>(data),
                         (DWORD)((wcslen(data) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    ::RegCloseKey(key);
    return ok;
}

bool run_nvidia_smi(const wchar_t* args)
{
    const std::wstring cmd =
        L"\"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvidia-smi.exe\" " + std::wstring(args);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring mutable_cmd = cmd;
    if (!::CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &si, &pi))
        return false;

    ::WaitForSingleObject(pi.hProcess, 10000);
    DWORD exit_code = 1;
    ::GetExitCodeProcess(pi.hProcess, &exit_code);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return exit_code == 0;
}
} // namespace

bool has_nvidia_gpu()
{
    return video_controller_name_contains(L"NVIDIA", nullptr);
}

bool has_amd_gpu()
{
    return video_controller_name_contains(L"AMD", L"Radeon");
}

bool nvidia_disable_hdcp()
{
    const std::wstring subkey = find_nvidia_class_subkey();
    if (subkey.empty())
    {
        log("NVIDIA HDCP", "No NVIDIA GPU found");
        return false;
    }
    const bool ok = set_dword(HKEY_LOCAL_MACHINE, subkey.c_str(), L"RMHdcpKeyglobZero", 1);
    log("NVIDIA HDCP", ok ? "Disabled" : "Failed to write the registry key");
    return ok;
}

bool nvidia_disable_telemetry()
{
    const bool a =
        set_dword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\NvControlPanel2\\Client",
                  L"OptInOrOutPreference", 0);
    const bool b = set_dword(HKEY_LOCAL_MACHINE,
                             L"SYSTEM\\CurrentControlSet\\Services\\nvlddmkm\\Global\\Startup",
                             L"SendTelemetryData", 0);
    const bool ok = a && b;
    log("NVIDIA telemetry", ok ? "Disabled" : "Failed to write one or both registry keys");
    return ok;
}

bool nvidia_disable_ecc()
{
    const bool ok = run_nvidia_smi(L"-e 0");
    log("NVIDIA ECC", ok ? "Disabled (only has an effect on ECC-capable GPUs)"
                         : "nvidia-smi not found, or this GPU doesn't support ECC control");
    return ok;
}

bool nvidia_unrestricted_pstate()
{
    const std::wstring subkey = find_nvidia_class_subkey();
    if (subkey.empty())
    {
        log("NVIDIA P-State", "No NVIDIA GPU found");
        return false;
    }
    const bool ok = set_dword(HKEY_LOCAL_MACHINE, subkey.c_str(), L"DisableDynamicPstate", 1);
    log("NVIDIA P-State", ok ? "Locked to max P-State" : "Failed to write the registry key");
    return ok;
}

bool nvidia_unrestricted_clocks()
{
    const bool ok = run_nvidia_smi(L"-acp 0");
    log("NVIDIA clock policy",
        ok ? "Application Clock Policy unrestricted"
           : "nvidia-smi not found, or this GPU doesn't support clock policy control");
    return ok;
}

namespace
{
constexpr int k_res_nip_exe = 201;
constexpr int k_res_nip_xml = 202;
constexpr int k_res_nip_profile = 203;

bool extract_resource(int resource_id, const std::wstring& out_path)
{
    HMODULE module = ::GetModuleHandleW(nullptr);
    HRSRC res = ::FindResourceW(module, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!res)
        return false;
    HGLOBAL loaded = ::LoadResource(module, res);
    if (!loaded)
        return false;
    const void* data = ::LockResource(loaded);
    const DWORD size = ::SizeofResource(module, res);
    if (!data || size == 0)
        return false;

    HANDLE file = ::CreateFileW(out_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = ::WriteFile(file, data, size, &written, nullptr) && written == size;
    ::CloseHandle(file);
    return ok;
}
} // namespace

bool launch_nvidia_profile_inspector()
{
    wchar_t temp_path[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, temp_path);
    const std::wstring dir = std::wstring(temp_path) + L"numbanineNvidiaTools\\";
    ::CreateDirectoryW(dir.c_str(), nullptr);

    const std::wstring exe_path = dir + L"nvidiaProfileInspector.exe";
    const std::wstring xml_path = dir + L"Reference.xml";
    const std::wstring nip_path = dir + L"Settings.nip";

    if (!extract_resource(k_res_nip_exe, exe_path) || !extract_resource(k_res_nip_xml, xml_path))
    {
        log("NVIDIA Profile Inspector", "Failed to extract the bundled tool");
        return false;
    }
    // The profile is a nice-to-have; a failed extraction here shouldn't
    // stop the tool itself from launching.
    extract_resource(k_res_nip_profile, nip_path);

    const HINSTANCE result =
        ::ShellExecuteW(nullptr, L"open", exe_path.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
    const bool ok = reinterpret_cast<INT_PTR>(result) > 32;
    log("NVIDIA Profile Inspector", ok ? "Launched" : "Failed to launch");
    return ok;
}

bool amd_disable_adrenalin_bloat()
{
    if (!has_amd_gpu())
    {
        log("AMD Software", "No AMD GPU found");
        return false;
    }

    bool ok = true;
    ok &= set_dword(HKEY_CURRENT_USER, L"Software\\AMD\\CN", L"AutoUpdateTriggered", 0);
    ok &= set_dword(HKEY_CURRENT_USER, L"Software\\AMD\\CN", L"AutoUpdate", 0);
    ok &= set_dword(HKEY_CURRENT_USER, L"Software\\AMD\\CN", L"PowerSaverAutoEnable_CUR", 0);
    ok &= set_string(HKEY_CURRENT_USER, L"Software\\AMD\\CN", L"RSXBrowserUnavailable", L"true");
    ok &= set_string(HKEY_CURRENT_USER, L"Software\\AMD\\CN", L"AllowWebContent", L"false");
    ok &= set_string(HKEY_CURRENT_USER, L"Software\\AMD\\CN", L"SystemTray", L"false");
    ok &=
        set_string(HKEY_CURRENT_USER, L"Software\\AMD\\CN", L"CN_Hide_Toast_Notification", L"true");
    ok &= set_string(HKEY_CURRENT_USER, L"Software\\AMD\\CN", L"AnimationEffect", L"false");
    ok &= set_dword(HKEY_CURRENT_USER, L"Software\\AMD\\CN\\OverlayNotification",
                    L"AlreadyNotified", 1);
    ok &= set_dword(HKEY_CURRENT_USER, L"Software\\AMD\\CN\\VirtualSuperResolution",
                    L"AlreadyNotified", 1);
    ok &= set_dword(HKEY_CURRENT_USER, L"Software\\AMD\\DVR", L"DvrEnabled", 0);
    ok &= set_dword(HKEY_CURRENT_USER, L"Software\\AMD\\DVR", L"PrevInstantReplayEnable", 0);
    ok &= set_dword(HKEY_CURRENT_USER, L"Software\\AMD\\DVR", L"PrevInGameReplayEnabled", 0);
    ok &= set_dword(HKEY_CURRENT_USER, L"Software\\AMD\\DVR", L"PrevInstantGifEnabled", 0);
    ok &= set_string(HKEY_CURRENT_USER, L"Software\\AMD\\DVR", L"ShowRSOverlay", L"false");
    ok &= set_dword(HKEY_LOCAL_MACHINE, L"Software\\AMD\\Install", L"AUEP", 1);
    ok &= set_dword(HKEY_LOCAL_MACHINE, L"Software\\AUEP", L"RSX_AUEPStatus", 2);

    log("AMD Software",
        ok ? "Adrenalin update/overlay/DVR noise disabled" : "Some registry keys failed to write");
    return ok;
}

bool amd_disable_chill()
{
    if (!has_amd_gpu())
    {
        log("AMD Chill", "No AMD GPU found");
        return false;
    }
    const bool ok = set_dword(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\amdwddmg",
                              L"ChillEnabled", 0);
    log("AMD Chill", ok ? "Disabled" : "Failed to write the registry key");
    return ok;
}

bool amd_disable_background_services()
{
    if (!has_amd_gpu())
    {
        log("AMD background services", "No AMD GPU found");
        return false;
    }

    static const wchar_t* const k_services[] = {
        L"AMD Crash Defender Service",
        L"AMD External Events Utility",
        L"amdfendr",
        L"amdfendrmgr",
        L"amdlog",
    };

    bool ok = true;
    for (const wchar_t* service : k_services)
    {
        const std::wstring subkey =
            L"System\\CurrentControlSet\\Services\\" + std::wstring(service);
        ok &= set_dword(HKEY_LOCAL_MACHINE, subkey.c_str(), L"Start", 4);
    }

    log("AMD background services", ok ? "Disabled" : "Some services failed to disable");
    return ok;
}

bool check_nvidia_disable_hdcp()
{
    const std::wstring subkey = find_nvidia_class_subkey();
    return !subkey.empty() &&
           dword_equals(HKEY_LOCAL_MACHINE, subkey.c_str(), L"RMHdcpKeyglobZero", 1);
}

bool check_nvidia_disable_telemetry()
{
    return dword_equals(HKEY_LOCAL_MACHINE,
                        L"SOFTWARE\\NVIDIA Corporation\\NvControlPanel2\\Client",
                        L"OptInOrOutPreference", 0);
}

bool check_nvidia_unrestricted_pstate()
{
    const std::wstring subkey = find_nvidia_class_subkey();
    return !subkey.empty() &&
           dword_equals(HKEY_LOCAL_MACHINE, subkey.c_str(), L"DisableDynamicPstate", 1);
}

bool check_amd_disable_chill()
{
    return dword_equals(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\amdwddmg",
                        L"ChillEnabled", 0);
}

bool check_amd_background_services()
{
    static const wchar_t* const k_services[] = {
        L"AMD Crash Defender Service",
        L"AMD External Events Utility",
        L"amdfendr",
        L"amdfendrmgr",
        L"amdlog",
    };
    for (const wchar_t* service : k_services)
    {
        const std::wstring subkey =
            L"System\\CurrentControlSet\\Services\\" + std::wstring(service);
        if (!dword_equals(HKEY_LOCAL_MACHINE, subkey.c_str(), L"Start", 4))
            return false;
    }
    return true;
}
} // namespace szk::backend
