#include "backend/hardware_info.h"

#include "core/product_info.h"

#include "backend/activity_log.h"

#include <windows.h>

#include <Wbemidl.h>
#include <comdef.h>
#include <shellapi.h>
#include <srrestoreptapi.h>

#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "srclient.lib")

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
        // S_OK / S_FALSE = we (or someone else) initialized COM fine.
        return init_result == S_OK || init_result == S_FALSE;
    }
};
} // namespace

motherboard_info motherboard_query()
{
    motherboard_info info;

    com_guard com;
    if (!com.ok())
        return info;

    IWbemLocator* locator = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<LPVOID*>(&locator))) ||
        !locator)
        return info;

    IWbemServices* services = nullptr;
    HRESULT hr = locator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0,
                                        nullptr, nullptr, &services);
    if (FAILED(hr) || !services)
    {
        locator->Release();
        return info;
    }

    ::CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    IEnumWbemClassObject* enumerator = nullptr;
    hr = services->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT Product FROM Win32_BaseBoard"),
                             WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                             &enumerator);
    if (SUCCEEDED(hr) && enumerator)
    {
        IWbemClassObject* object = nullptr;
        ULONG returned = 0;
        if (enumerator->Next(WBEM_INFINITE, 1, &object, &returned) == S_OK && returned > 0)
        {
            VARIANT value;
            ::VariantInit(&value);
            if (SUCCEEDED(object->Get(L"Product", 0, &value, nullptr, nullptr)) &&
                value.vt == VT_BSTR && value.bstrVal)
            {
                ::WideCharToMultiByte(CP_UTF8, 0, value.bstrVal, -1, info.product,
                                      sizeof(info.product), nullptr, nullptr);
                info.available = info.product[0] != '\0';
            }
            ::VariantClear(&value);
            object->Release();
        }
        enumerator->Release();
    }

    services->Release();
    locator->Release();
    return info;
}

void open_google_search(const char* query)
{
    std::string encoded;
    for (const char* p = query; *p; p++)
    {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))
            encoded += *p;
        else if (*p == ' ')
            encoded += '+';
        else
        {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(*p));
            encoded += buf;
        }
    }

    const std::string url = "https://www.google.com/search?q=" + encoded;
    int wide_len = ::MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wide_url(static_cast<size_t>(wide_len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide_url.data(), wide_len);

    ::ShellExecuteW(nullptr, L"open", wide_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_windows_recovery_settings()
{
    ::ShellExecuteW(nullptr, L"open", L"ms-settings:recovery", nullptr, nullptr, SW_SHOWNORMAL);
}

bool system_restore_available()
{
    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return true; // Cannot tell; let the attempt speak for itself.

    SC_HANDLE service = ::OpenServiceW(scm, L"VSS", SERVICE_QUERY_CONFIG);
    if (!service)
    {
        ::CloseServiceHandle(scm);
        return true;
    }

    DWORD needed = 0;
    ::QueryServiceConfigW(service, nullptr, 0, &needed);

    bool available = true;
    if (needed > 0)
    {
        std::vector<unsigned char> buffer(needed);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (::QueryServiceConfigW(service, config, needed, &needed))
            available = config->dwStartType != SERVICE_DISABLED;
    }

    ::CloseServiceHandle(service);
    ::CloseServiceHandle(scm);
    return available;
}

namespace
{
// Start type for one service, when it is allowed to change it. Kept local: the
// only service this file has any business touching is the one System Restore
// runs on.
bool set_service_manual(const wchar_t* service_name)
{
    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return false;

    SC_HANDLE service = ::OpenServiceW(scm, service_name, SERVICE_CHANGE_CONFIG);
    if (!service)
    {
        ::CloseServiceHandle(scm);
        return false;
    }

    const bool ok = ::ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_DEMAND_START,
                                           SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr,
                                           nullptr, nullptr, nullptr) != 0;
    ::CloseServiceHandle(service);
    ::CloseServiceHandle(scm);
    return ok;
}

// Runs a command and waits. Long timeout: Enable-ComputerRestore is not quick,
// and returning before it finishes would report a result that has not happened.
bool run_and_wait(const std::wstring& command_line, DWORD timeout_ms)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring mutable_command = command_line;
    if (!::CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &si, &pi))
        return false;

    ::WaitForSingleObject(pi.hProcess, timeout_ms);
    DWORD exit_code = 1;
    ::GetExitCodeProcess(pi.hProcess, &exit_code);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return exit_code == 0;
}
} // namespace

bool enable_system_protection()
{
    // The service first: with VSS disabled the rest cannot work, and it is the
    // part this can set directly rather than through a command.
    const bool service_ok = set_service_manual(L"VSS");
    const bool provider_ok = set_service_manual(L"swprv");

    // CreateProcess does not expand %SystemDrive%, so the drive is resolved
    // here and pasted into both commands.
    wchar_t drive[8] = {};
    if (::GetEnvironmentVariableW(L"SystemDrive", drive, 8) == 0)
        wcscpy_s(drive, L"C:");

    // Enable-ComputerRestore is the documented way to switch protection on for
    // a drive; there is no Win32 entry point for it. The drive is built inside
    // PowerShell rather than written as C:\ here, because a trailing backslash
    // in front of the closing quote is the one thing command-line quoting gets
    // wrong.
    const std::wstring enable =
        std::wstring(L"powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
                     L"\"Enable-ComputerRestore -Drive ('") +
        drive + L"' + [char]92)\"";
    const bool enabled = run_and_wait(enable, 120000);

    // Protection that is on but has no disk allowance keeps no restore points,
    // so a checkpoint would succeed and then quietly vanish. 5% is what Windows
    // offers itself on a modern disk. Failing here is not fatal - the machine
    // may already have an allowance.
    const std::wstring resize = std::wstring(L"vssadmin resize shadowstorage /For=") + drive +
                                L" /On=" + drive + L" /MaxSize=5%";
    run_and_wait(resize, 60000);

    const bool ok = enabled && system_restore_available();
    log("System Restore",
        ok ? "System Protection turned on for the system drive"
           : (service_ok && provider_ok
                  ? "Could not turn System Protection on - Windows refused the change"
                  : "Could not re-enable the Volume Shadow Copy service; run as administrator"));
    return ok;
}

restore_point create_restore_point()
{
    // Asked first so a machine with System Restore switched off answers
    // immediately, rather than after SRSetRestorePointW has spent seconds
    // failing - which reads as the button doing nothing.
    if (!system_restore_available())
    {
        log("System Restore",
            "Turned off on this machine - the Volume Shadow Copy service is disabled, so Windows "
            "cannot take one. Turn System Protection on for C: to use this.");
        return restore_point::unavailable;
    }

    RESTOREPOINTINFOW info{};
    info.dwEventType = BEGIN_SYSTEM_CHANGE;
    info.dwRestorePtType = APPLICATION_INSTALL;
    wcsncpy_s(info.szDescription, (std::wstring(product_info::name_wide) + L" checkpoint").c_str(),
              _TRUNCATE);

    STATEMGRSTATUS status{};
    if (::SRSetRestorePointW(&info, &status) != FALSE)
    {
        log("System Restore", "Restore point created");
        return restore_point::created;
    }

    log("System Restore", "Windows refused to create a restore point");
    return restore_point::failed;
}

bool create_system_restore_point()
{
    return create_restore_point() == restore_point::created;
}

void open_system_restore_wizard()
{
    ::ShellExecuteW(nullptr, L"open", L"rstrui.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

void open_discord()
{
    ::ShellExecuteW(nullptr, L"open", L"https://discord.gg/nhb6v5pRtE", nullptr, nullptr,
                    SW_SHOWNORMAL);
}
} // namespace szk::backend
