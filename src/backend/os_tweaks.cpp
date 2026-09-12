#include "backend/os_tweaks.h"

#include "backend/activity_log.h"

#include <windows.h>

#include <powrprof.h>

#include <cwctype>
#include <filesystem>
#include <string>

#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "advapi32.lib")

namespace szk::backend
{
namespace
{
bool run_system_command(const std::wstring& command_line)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring mutable_cmd = command_line;
    const BOOL ok = ::CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok)
        return false;

    ::WaitForSingleObject(pi.hProcess, 10000);
    DWORD exit_code = 1;
    ::GetExitCodeProcess(pi.hProcess, &exit_code);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return exit_code == 0;
}

bool set_service_start_type(const wchar_t* service_name, bool disable)
{
    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return false;

    SC_HANDLE svc =
        ::OpenServiceW(scm, service_name, SERVICE_CHANGE_CONFIG | SERVICE_STOP | SERVICE_START);
    if (!svc)
    {
        ::CloseServiceHandle(scm);
        return false;
    }

    const DWORD start_type = disable ? SERVICE_DISABLED : SERVICE_AUTO_START;
    const bool ok =
        ::ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, start_type, SERVICE_NO_CHANGE, nullptr,
                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != 0;
    if (ok)
    {
        if (disable)
        {
            SERVICE_STATUS status{};
            ::ControlService(svc, SERVICE_CONTROL_STOP, &status);
        }
        else
        {
            ::StartServiceW(svc, 0, nullptr);
        }
    }

    ::CloseServiceHandle(svc);
    ::CloseServiceHandle(scm);
    return ok;
}

// GUID_USB_SUSPEND_SUBGROUP and USB_SELECTIVE_SUSPEND_SETTING, the standard
// power-scheme GUIDs Windows uses for this setting (visible via
// `powercfg /q` under "USB settings > USB selective suspend setting").
constexpr GUID k_usb_subgroup = {
    0x2a737441, 0x1930, 0x4402, {0x8d, 0x77, 0xb2, 0xbe, 0xbb, 0xa3, 0x08, 0xa3}};
constexpr GUID k_usb_suspend_setting = {
    0x48e6b7a6, 0x50f5, 0x4782, {0xa5, 0xd4, 0x53, 0xbb, 0x8f, 0x07, 0xe2, 0x26}};
} // namespace

bool set_network_autotuning(bool enable)
{
    // "Optimize" here matches the common gaming-tuning-guide combination:
    // widen the TCP receive window and turn off the offload paths that add
    // latency on some NICs. MTU is intentionally left untouched — the
    // correct value depends on the specific adapter/route and a wrong one
    // can break connectivity outright.
    const bool a = run_system_command(L"netsh int tcp set global autotuninglevel=" +
                                      std::wstring(enable ? L"disabled" : L"normal"));
    const bool b = run_system_command(L"netsh int tcp set global chimney=" +
                                      std::wstring(enable ? L"disabled" : L"enabled"));
    const bool c = run_system_command(L"netsh int tcp set global netdma=" +
                                      std::wstring(enable ? L"disabled" : L"enabled"));

    const bool ok = a && b && c;
    log("Network stack",
        enable ? (ok ? "Autotuning/chimney/netdma tweaks applied" : "Some netsh commands failed")
               : (ok ? "Reverted to Windows defaults" : "Some netsh commands failed"));
    return ok;
}

bool set_background_services_disabled(bool enable)
{
    const bool sysmain = set_service_start_type(L"SysMain", enable);
    const bool wsearch = set_service_start_type(L"WSearch", enable);

    const bool ok = sysmain && wsearch;
    log("Background services", enable ? (ok ? "SysMain and WSearch disabled and stopped"
                                            : "Failed to disable one or more services")
                                      : (ok ? "SysMain and WSearch restored to Automatic"
                                            : "Failed to restore one or more services"));
    return ok;
}

bool set_usb_selective_suspend_disabled(bool enable)
{
    GUID* active_scheme = nullptr;
    if (::PowerGetActiveScheme(nullptr, &active_scheme) != ERROR_SUCCESS || !active_scheme)
    {
        log("USB selective suspend", "Could not read the active power scheme");
        return false;
    }

    const DWORD value =
        enable ? 0 : 1; // 0 = suspend disabled, 1 = suspend enabled (Windows default)
    const bool ac_ok = ::PowerWriteACValueIndex(nullptr, active_scheme, &k_usb_subgroup,
                                                &k_usb_suspend_setting, value) == ERROR_SUCCESS;
    const bool dc_ok = ::PowerWriteDCValueIndex(nullptr, active_scheme, &k_usb_subgroup,
                                                &k_usb_suspend_setting, value) == ERROR_SUCCESS;
    ::PowerSetActiveScheme(nullptr, active_scheme);
    ::LocalFree(active_scheme);

    const bool ok = ac_ok && dc_ok;
    log("USB selective suspend",
        enable ? (ok ? "Disabled on the active power plan" : "Failed to write the power setting")
               : (ok ? "Restored to Windows default" : "Failed to write the power setting"));
    return ok;
}

bool set_low_latency_tcp(bool enable)
{
    HKEY interfaces = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                        L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces", 0,
                        KEY_READ, &interfaces) != ERROR_SUCCESS)
    {
        log("Low latency TCP keys", "Could not open the Tcpip interfaces registry key");
        return false;
    }

    int touched = 0;
    for (DWORD i = 0;; i++)
    {
        wchar_t name[256];
        DWORD name_len = 256;
        if (::RegEnumKeyExW(interfaces, i, name, &name_len, nullptr, nullptr, nullptr, nullptr) !=
            ERROR_SUCCESS)
            break;

        HKEY sub = nullptr;
        if (::RegOpenKeyExW(interfaces, name, 0, KEY_SET_VALUE, &sub) != ERROR_SUCCESS)
            continue;

        if (enable)
        {
            const DWORD one = 1;
            ::RegSetValueExW(sub, L"TcpAckFrequency", 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&one), sizeof(one));
            ::RegSetValueExW(sub, L"TcpNoDelay", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one),
                             sizeof(one));
        }
        else
        {
            ::RegDeleteValueW(sub, L"TcpAckFrequency");
            ::RegDeleteValueW(sub, L"TcpNoDelay");
        }

        ::RegCloseKey(sub);
        touched++;
    }
    ::RegCloseKey(interfaces);

    log("Low latency TCP keys",
        enable
            ? "TcpNoDelay/TcpAckFrequency set on " + std::to_string(touched) + " interface(s)"
            : "TcpNoDelay/TcpAckFrequency cleared on " + std::to_string(touched) + " interface(s)");
    return touched > 0;
}

bool set_bcd_tweaks(bool enable)
{
    const bool a = enable ? run_system_command(L"bcdedit /set disabledynamictick yes")
                          : run_system_command(L"bcdedit /deletevalue disabledynamictick");
    const bool b = enable ? run_system_command(L"bcdedit /set useplatformclock false")
                          : run_system_command(L"bcdedit /deletevalue useplatformclock");

    const bool ok = a && b;
    log("BCD tweaks",
        enable
            ? (ok ? "disabledynamictick/useplatformclock set (restart to take effect)"
                  : "bcdedit reported an error")
            : (ok ? "BCD values cleared (restart to take effect)" : "bcdedit reported an error"));
    return ok;
}

namespace
{
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

bool read_string_value(HKEY root, const wchar_t* subkey, const wchar_t* value, std::wstring& out)
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    wchar_t buf[512] = {};
    DWORD size = sizeof(buf), type = 0;
    const bool ok = ::RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(buf),
                                       &size) == ERROR_SUCCESS &&
                    (type == REG_SZ || type == REG_EXPAND_SZ);
    ::RegCloseKey(key);
    if (ok)
        out = buf;
    return ok;
}

bool string_equals_ci(HKEY root, const wchar_t* subkey, const wchar_t* value,
                      const wchar_t* expected)
{
    std::wstring data;
    return read_string_value(root, subkey, value, data) && _wcsicmp(data.c_str(), expected) == 0;
}

bool string_contains(HKEY root, const wchar_t* subkey, const wchar_t* value, const wchar_t* needle)
{
    std::wstring data;
    return read_string_value(root, subkey, value, data) && data.find(needle) != std::wstring::npos;
}

bool service_start_type_is(const wchar_t* service_name, DWORD expected_start_type)
{
    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return false;
    SC_HANDLE svc = ::OpenServiceW(scm, service_name, SERVICE_QUERY_CONFIG);
    if (!svc)
    {
        ::CloseServiceHandle(scm);
        return false;
    }

    BYTE buf[8192];
    DWORD needed = 0;
    const bool ok = ::QueryServiceConfigW(svc, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf),
                                          sizeof(buf), &needed) != 0;
    const DWORD start_type =
        ok ? reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf)->dwStartType : 0xffffffff;

    ::CloseServiceHandle(svc);
    ::CloseServiceHandle(scm);
    return ok && start_type == expected_start_type;
}
} // namespace

bool disable_action_center()
{
    const bool ok =
        set_dword(HKEY_CURRENT_USER, L"Software\\Policies\\Microsoft\\Windows\\Explorer",
                  L"DisableNotificationCenter", 1);
    log("Action Center", ok ? "Disabled" : "Failed to write the policy key");
    return ok;
}

bool set_classic_alt_tab()
{
    const bool ok = set_dword(HKEY_CURRENT_USER,
                              L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                              L"AltTabSettings", 0);
    log("Alt-Tab", ok ? "Set to classic style" : "Failed to write the registry key");
    return ok;
}

bool disable_gpu_mpo()
{
    const bool ok =
        set_dword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\Dwm", L"OverlayTestMode", 5);
    log("GPU MPO",
        ok ? "Disabled (sign out or restart to take effect)" : "Failed to write the registry key");
    return ok;
}

namespace
{
// The netsh block shared by "SapphireOS Default Network Settings.bat" and
// "Run this if you had to install a network driver.bat".
int run_default_netsh_bundle()
{
    const wchar_t* const commands[] = {
        L"netsh int tcp set global dca=enabled",
        L"netsh int tcp set global netdma=enabled",
        L"netsh interface isatap set state disabled",
        L"netsh int tcp set global timestamps=disabled",
        L"netsh int tcp set global rss=enabled",
        L"netsh int tcp set global nonsackrttresiliency=disabled",
        L"netsh int tcp set global initialRto=2000",
        L"netsh int tcp set supplemental template=custom icw=10",
        L"netsh interface ip set interface ethernet currenthoplimit=64",
        L"netsh int ip set global taskoffload=enabled",
    };

    int failed = 0;
    for (const wchar_t* cmd : commands)
        if (!run_system_command(cmd))
            failed++;
    return failed;
}

// Advanced NIC properties from "Run this if you had to install a network
// driver.bat" — applied to every adapter instance under the Net class GUID.
// Almost all are REG_SZ in the source script (even the numeric-looking
// ones); PnPCapabilities is the one REG_DWORD.
struct nic_property
{
    const wchar_t* name;
    const wchar_t* string_value; // nullptr => use dword_value as REG_DWORD
    DWORD dword_value;
};

const nic_property k_nic_properties[] = {
    {L"*DeviceSleepOnDisconnect", L"0", 0},
    {L"*EEE", L"0", 0},
    {L"*IPChecksumOffloadIPv4", L"3", 0},
    {L"*NumRssQueues", L"2", 0},
    {L"*PMARPOffload", L"1", 0},
    {L"*PMNSOffload", L"1", 0},
    {L"*PriorityVLANTag", L"1", 0},
    {L"*RSS", L"1", 0},
    {L"*WakeOnMagicPacket", L"0", 0},
    {L"AutoPowerSaveModeEnabled", L"0", 0},
    {L"*WakeOnPattern", L"0", 0},
    {L"*TCPChecksumOffloadIPv4", L"3", 0},
    {L"*TCPChecksumOffloadIPv6", L"3", 0},
    {L"*UDPChecksumOffloadIPv4", L"3", 0},
    {L"*UDPChecksumOffloadIPv6", L"3", 0},
    {L"DMACoalescing", L"0", 0},
    {L"EEELinkAdvertisement", L"0", 0},
    {L"EeePhyEnable", L"0", 0},
    {L"ReduceSpeedOnPowerDown", L"0", 0},
    {L"PowerDownPll", L"0", 0},
    {L"WaitAutoNegComplete", L"0", 0},
    {L"WakeOnLink", L"0", 0},
    {L"WakeOnSlot", L"0", 0},
    {L"WakeUpModeCap", L"0", 0},
    {L"AdvancedEEE", L"0", 0},
    {L"EnableGreenEthernet", L"0", 0},
    {L"GigaLite", L"0", 0},
    {L"PnPCapabilities", nullptr, 24},
    {L"PowerSavingMode", L"0", 0},
    {L"S5WakeOnLan", L"0", 0},
    {L"SavePowerNowEnabled", L"0", 0},
    {L"ULPMode", L"0", 0},
    {L"WolShutdownLinkSpeed", L"2", 0},
    {L"LogLinkStateEvent", L"16", 0},
    {L"WakeOnMagicPacketFromS5", L"0", 0},
    {L"Ultra Low Power Mode", L"Disabled", 0},
    {L"System Idle Power Saver", L"Disabled", 0},
    {L"Selective Suspend", L"Disabled", 0},
    {L"Selective Suspend Idle Timeout", L"60", 0},
    {L"Link Speed Battery Saver", L"Disabled", 0},
    {L"*SelectiveSuspend", L"0", 0},
    {L"EnablePME", L"0", 0},
    {L"TxIntDelay", L"0", 0},
    {L"TxDelay", L"0", 0},
    {L"EnableModernStandby", L"0", 0},
    {L"*ModernStandbyWoLMagicPacket", L"0", 0},
    {L"EnableLLI", L"1", 0},
    {L"*SSIdleTimeout", L"60", 0},
};

bool set_string(HKEY key, const wchar_t* value, const wchar_t* data)
{
    return ::RegSetValueExW(key, value, 0, REG_SZ, reinterpret_cast<const BYTE*>(data),
                            static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t))) ==
           ERROR_SUCCESS;
}

int apply_nic_properties()
{
    const wchar_t* class_key =
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}";
    HKEY nic_class = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, class_key, 0, KEY_READ, &nic_class) != ERROR_SUCCESS)
        return 0;

    int adapters_touched = 0;
    for (DWORD i = 0;; i++)
    {
        wchar_t subkey_name[16];
        DWORD name_len = 16;
        if (::RegEnumKeyExW(nic_class, i, subkey_name, &name_len, nullptr, nullptr, nullptr,
                            nullptr) != ERROR_SUCCESS)
            break;

        // Only the numbered instance subkeys ("0000", "0001", ...) hold
        // per-adapter advanced properties.
        if (name_len != 4 || !iswdigit(subkey_name[0]))
            continue;

        HKEY adapter = nullptr;
        if (::RegOpenKeyExW(nic_class, subkey_name, 0, KEY_SET_VALUE, &adapter) != ERROR_SUCCESS)
            continue;

        for (const nic_property& prop : k_nic_properties)
        {
            if (prop.string_value)
                set_string(adapter, prop.name, prop.string_value);
            else
                ::RegSetValueExW(adapter, prop.name, 0, REG_DWORD,
                                 reinterpret_cast<const BYTE*>(&prop.dword_value),
                                 sizeof(prop.dword_value));
        }

        ::RegCloseKey(adapter);
        adapters_touched++;
    }

    ::RegCloseKey(nic_class);
    return adapters_touched;
}
} // namespace

bool apply_sapphire_network_defaults()
{
    // Mirrors SapphireOS/src/PostInstall/Others/Network/
    //   "SapphireOS Default Network Settings.bat"
    const int failed = run_default_netsh_bundle();
    const bool ok = failed == 0;
    log("numbanine network defaults",
        ok ? "All netsh commands applied" : std::to_string(failed) + " netsh command(s) failed");
    return ok;
}

bool apply_network_driver_tweaks()
{
    // Mirrors SapphireOS/src/PostInstall/Others/Network/
    //   "Run this if you had to install a network driver.bat"
    const int adapters = apply_nic_properties();

    const bool bindings_ok = run_system_command(
        L"powershell -NoProfile -Command \"Disable-NetAdapterBinding -Name '*' -ComponentID "
        L"vmware_bridge,ms_lldp,ms_lltdio,ms_implat,ms_tcpip6,ms_rspndr,ms_server,ms_msclient "
        L"-Confirm:$false\"");

    const int failed = run_default_netsh_bundle();
    const bool ok = adapters > 0 && bindings_ok && failed == 0;

    log("Network driver tweaks", "Touched " + std::to_string(adapters) + " adapter(s), bindings " +
                                     (bindings_ok ? "disabled" : "failed") + ", " +
                                     std::to_string(failed) + " netsh command(s) failed");
    return ok;
}

namespace
{
bool write_qos_string(HKEY key, const wchar_t* value, const wchar_t* data)
{
    const DWORD size = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
    return ::RegSetValueExW(key, value, 0, REG_SZ, reinterpret_cast<const BYTE*>(data), size) ==
           ERROR_SUCCESS;
}

// Every QoS policy field is REG_SZ, even the numeric-looking ones — that
// matches how Local Group Policy's Policy-based QoS actually stores them.
bool write_qos_policy(const wchar_t* policy_name, const wchar_t* exe_name,
                      const wchar_t* dscp_value)
{
    wchar_t subkey[256];
    ::wsprintfW(subkey, L"SOFTWARE\\Policies\\Microsoft\\Windows\\QoS\\%s", policy_name);

    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                          nullptr) != ERROR_SUCCESS)
        return false;

    bool ok = true;
    ok &= write_qos_string(key, L"Version", L"1.0");
    ok &= write_qos_string(key, L"Application Name", exe_name);
    ok &= write_qos_string(key, L"Protocol", L"*");
    ok &= write_qos_string(key, L"Local Port", L"*");
    ok &= write_qos_string(key, L"Local IP", L"*");
    ok &= write_qos_string(key, L"Local IP Prefix Length", L"*");
    ok &= write_qos_string(key, L"Remote Port", L"*");
    ok &= write_qos_string(key, L"Remote IP", L"*");
    ok &= write_qos_string(key, L"Remote IP Prefix Length", L"*");
    ok &= write_qos_string(key, L"DSCP Value", dscp_value);
    // "-1" is the QoS policy engine's own sentinel for "unlimited" — unlike
    // a real bits-per-second value (what the excluded 10240/20480 source
    // script would have set), this does not throttle anything.
    ok &= write_qos_string(key, L"Throttle Rate", L"-1");

    ::RegCloseKey(key);
    return ok;
}

// FiveM's actual game process is versioned as
// "FiveM_b<build>_GTAProcess.exe" inside the user's local subprocess
// cache. Finds whatever is there right now rather than assuming a name.
std::wstring find_fivem_game_process_name()
{
    wchar_t local_app_data[MAX_PATH] = {};
    if (::GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH) == 0)
        return L"";

    const std::wstring dir =
        std::wstring(local_app_data) + L"\\FiveM\\FiveM.app\\data\\cache\\subprocess\\";
    const std::wstring pattern = dir + L"FiveM_b*_GTAProcess.exe";

    WIN32_FIND_DATAW find_data{};
    HANDLE handle = ::FindFirstFileW(pattern.c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE)
        return L"";

    std::wstring newest = find_data.cFileName;
    FILETIME newest_time = find_data.ftLastWriteTime;
    while (::FindNextFileW(handle, &find_data))
    {
        if (::CompareFileTime(&find_data.ftLastWriteTime, &newest_time) > 0)
        {
            newest = find_data.cFileName;
            newest_time = find_data.ftLastWriteTime;
        }
    }
    ::FindClose(handle);
    return newest;
}

// Full path to the same subprocess exe, for the registry mechanisms
// (UserGpuPreferences, AppCompatFlags\Layers) that key off a full path
// rather than just the filename.
std::wstring find_fivem_game_process_full_path()
{
    const std::wstring name = find_fivem_game_process_name();
    if (name.empty())
        return L"";

    wchar_t local_app_data[MAX_PATH] = {};
    ::GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    return std::wstring(local_app_data) + L"\\FiveM\\FiveM.app\\data\\cache\\subprocess\\" + name;
}
} // namespace

bool apply_fivem_qos_priority()
{
    const bool launcher_ok = write_qos_policy(L"SZKFiveMLauncher", L"FiveM.exe", L"46");

    const std::wstring game_exe = find_fivem_game_process_name();
    bool game_ok = false;
    if (!game_exe.empty())
        game_ok = write_qos_policy(L"SZKFiveMGame", game_exe.c_str(), L"46");

    if (game_exe.empty())
    {
        log("FiveM QoS priority",
            "Launcher policy written; game subprocess not found (is FiveM installed/has it run?)");
    }
    else
    {
        char narrow_name[256] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, game_exe.c_str(), -1, narrow_name, sizeof(narrow_name),
                              nullptr, nullptr);
        log("FiveM QoS priority",
            "Launcher and game (" + std::string(narrow_name) + ") policies written");
    }

    // A restart of the network stack (or a reboot) is what actually makes
    // Windows pick up new QoS policies — that's inherent to the feature,
    // not something this call can force.
    return launcher_ok && (game_ok || game_exe.empty());
}

bool set_fivem_cache_autoclear(bool enable)
{
    wchar_t local_app_data[MAX_PATH] = {};
    if (::GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH) == 0)
    {
        log("FiveM cache auto-clear", "Could not resolve %LOCALAPPDATA%");
        return false;
    }
    const std::wstring fivem_data = std::wstring(local_app_data) + L"\\FiveM\\FiveM.app\\data\\";
    // server-cache/server-cache-priv hold per-server streamed assets and
    // are just as prone to going stale/corrupt as the main cache folder.
    const std::wstring cache_dirs[] = {fivem_data + L"cache", fivem_data + L"server-cache",
                                       fivem_data + L"server-cache-priv"};

    if (!enable)
    {
        const bool ok = run_system_command(L"schtasks /delete /tn \"SZKFiveMCacheClear\" /f");
        log("FiveM cache auto-clear", ok ? "Logon task removed" : "schtasks /delete failed");
        return ok;
    }

    std::error_code ec;
    for (const std::wstring& dir : cache_dirs)
        std::filesystem::remove_all(dir, ec);

    std::wstring tr = L"cmd.exe /c";
    for (const std::wstring& dir : cache_dirs)
        tr += L" & rd /s /q \"\"" + dir + L"\"\"";
    const std::wstring cmd = L"schtasks /create /tn \"SZKFiveMCacheClear\" /tr \"" + tr +
                             L"\" /sc onlogon /rl highest /f";
    const bool task_ok = run_system_command(cmd);

    log("FiveM cache auto-clear", task_ok
                                      ? "Cache cleared now; will clear again at every logon"
                                      : "Cache cleared now, but the logon task failed to register");
    return task_ok;
}

namespace
{
bool set_ifeo_cpu_priority(const std::wstring& exe_name, bool enable)
{
    const std::wstring subkey =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" +
        exe_name + L"\\PerfOptions";

    if (enable)
        // Per Microsoft's own IFEO PerfOptions documentation: Low=1,
        // Normal=2, High=3, Below Normal=5, Above Normal=6 — this is NOT a
        // plain 1-6 ascending scale. 3 is High; an earlier version of this
        // function used 5, which is actually Below Normal (the opposite of
        // what it claimed to do).
        return set_dword(HKEY_LOCAL_MACHINE, subkey.c_str(), L"CpuPriorityClass", 3);

    HKEY key = nullptr;
    const bool opened = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey.c_str(), 0, KEY_SET_VALUE,
                                        &key) == ERROR_SUCCESS;
    if (opened)
    {
        ::RegDeleteValueW(key, L"CpuPriorityClass");
        ::RegCloseKey(key);
    }
    return opened;
}
} // namespace

bool set_fivem_high_cpu_priority(bool enable)
{
    // Both the launcher (FiveM.exe) and the actual game subprocess get the
    // priority bump — they're separate processes with separate IFEO
    // entries, and boosting only one leaves the other at Normal.
    const bool launcher_ok = set_ifeo_cpu_priority(L"FiveM.exe", enable);

    const std::wstring game_exe = find_fivem_game_process_name();
    bool game_ok = false;
    if (!game_exe.empty())
        game_ok = set_ifeo_cpu_priority(game_exe, enable);

    char narrow_name[256] = {};
    if (!game_exe.empty())
        ::WideCharToMultiByte(CP_UTF8, 0, game_exe.c_str(), -1, narrow_name, sizeof(narrow_name),
                              nullptr, nullptr);

    const std::string detail =
        game_exe.empty() ? std::string("FiveM.exe ") + (launcher_ok ? "done; " : "failed; ") +
                               "game subprocess not found (launch FiveM at least once first)"
                         : std::string("FiveM.exe + ") + narrow_name + " " +
                               ((launcher_ok && game_ok) ? "done" : "partially failed");

    log("FiveM high CPU priority", (enable ? "Set High — " : "Cleared — ") + detail);
    return launcher_ok && (game_ok || game_exe.empty());
}

namespace
{
bool set_string_kv(HKEY root, const wchar_t* subkey, const wchar_t* value, const wchar_t* data)
{
    HKEY key = nullptr;
    if (::RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    const bool ok = set_string(key, value, data);
    ::RegCloseKey(key);
    return ok;
}
} // namespace

bool disable_pcie_aspm()
{
    const bool a = run_system_command(
        L"powercfg /setacvalueindex scheme_current 501a4d13-42af-4429-9fd1-a8218c268e20 "
        L"ee12f906-d277-404b-b6da-e5fa1a576df5 0");
    const bool b = run_system_command(
        L"powercfg /setdcvalueindex scheme_current 501a4d13-42af-4429-9fd1-a8218c268e20 "
        L"ee12f906-d277-404b-b6da-e5fa1a576df5 0");
    const bool c = run_system_command(L"powercfg /setactive scheme_current");
    const bool ok = a && b && c;
    log("PCIe ASPM", ok ? "Disabled on the active power plan" : "powercfg reported an error");
    return ok;
}

bool disable_game_bar_dvr()
{
    bool ok = true;
    ok &= set_dword(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_Enabled", 0);
    // FSEBehaviorMode=2 keeps true Fullscreen Exclusive available to games
    // instead of Windows silently converting them to borderless (which is
    // what DVR/Game Bar being active otherwise forces).
    ok &= set_dword(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", 2);
    ok &= set_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                    L"AppCaptureEnabled", 0);
    ok &= set_dword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR",
                    L"AllowGameDVR", 0);
    log("Game Bar / DVR", ok ? "Capture disabled" : "Some registry keys failed to write");
    return ok;
}

bool set_mouse_raw_1to1()
{
    bool ok = true;
    ok &= set_string_kv(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseSpeed", L"0");
    ok &= set_string_kv(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold1", L"0");
    ok &= set_string_kv(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold2", L"0");
    log("Mouse 1:1", ok ? "Pointer acceleration disabled (sign out to take effect)"
                        : "Some registry keys failed to write");
    return ok;
}

bool set_keyboard_rapid_response()
{
    bool ok = true;
    ok &= set_string_kv(HKEY_CURRENT_USER, L"Control Panel\\Keyboard", L"KeyboardDelay", L"0");
    ok &= set_string_kv(HKEY_CURRENT_USER, L"Control Panel\\Keyboard", L"KeyboardSpeed", L"31");
    ::SystemParametersInfoW(SPI_SETKEYBOARDDELAY, 0, nullptr, SPIF_SENDCHANGE);
    ::SystemParametersInfoW(SPI_SETKEYBOARDSPEED, 31, nullptr, SPIF_SENDCHANGE);
    log("Keyboard rapid response",
        ok ? "Delay/repeat rate maxed" : "Some registry keys failed to write");
    return ok;
}

bool disable_network_throttling()
{
    const bool ok =
        set_dword(HKEY_LOCAL_MACHINE,
                  L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
                  L"NetworkThrottlingIndex", 0xffffffff);
    log("Network throttling", ok ? "Disabled" : "Failed to write the registry key");
    return ok;
}

bool flush_dns_cache()
{
    const bool ok = run_system_command(L"ipconfig /flushdns");
    log("DNS cache", ok ? "Flushed" : "ipconfig reported an error");
    return ok;
}

bool renew_ip_lease()
{
    const bool a = run_system_command(L"ipconfig /release");
    const bool b = run_system_command(L"ipconfig /renew");
    const bool ok = a && b;
    log("IP lease", ok ? "Released and renewed (the connection dropped briefly)"
                       : "ipconfig reported an error");
    return ok;
}

bool clear_arp_cache()
{
    const bool ok = run_system_command(L"netsh interface ip delete arpcache");
    log("ARP cache", ok ? "Cleared" : "netsh reported an error");
    return ok;
}

bool reset_winsock_catalog()
{
    const bool ok = run_system_command(L"netsh winsock reset");
    log("Winsock catalog",
        ok ? "Reset (restart the machine for it to take effect)" : "netsh reported an error");
    return ok;
}

bool register_dns_record()
{
    const bool ok = run_system_command(L"ipconfig /registerdns");
    log("DNS host record", ok ? "Registered" : "ipconfig reported an error");
    return ok;
}

bool reset_filter_keys_timing()
{
    bool ok = true;
    ok &= set_string_kv(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\Keyboard Response",
                        L"Flags", L"122");
    ok &= set_string_kv(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\Keyboard Response",
                        L"DelayBeforeAcceptance", L"0");
    ok &= set_string_kv(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\Keyboard Response",
                        L"AutoRepeatDelay", L"0");
    ok &= set_string_kv(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\Keyboard Response",
                        L"AutoRepeatRate", L"0");
    log("Filter Keys timing", ok ? "Reset" : "Some registry keys failed to write");
    return ok;
}

bool disable_hardware_gpu_scheduling(bool enable)
{
    const bool ok =
        set_dword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
                  L"HwSchMode", enable ? 2 : 1);
    log("Hardware GPU Scheduling",
        ok ? (std::string(enable ? "Enabled" : "Disabled") + " (restart to take effect)")
           : "Failed to write the registry key");
    return ok;
}

bool set_visual_effects_performance()
{
    bool ok = true;
    ok &= set_dword(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects",
                    L"VisualFXSetting", 2);
    ok &= set_string_kv(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"MenuShowDelay", L"0");
    log("Visual effects", ok ? "Set to best performance (sign out to fully take effect)"
                             : "Some registry keys failed to write");
    return ok;
}

namespace
{
constexpr wchar_t k_high_performance_guid[] = L"8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
constexpr wchar_t k_balanced_guid[] = L"381b4222-f694-41f0-9685-ff5bb260df2e";
} // namespace

bool switch_power_plan_high_performance(bool enable)
{
    const std::wstring cmd =
        L"powercfg /setactive " + std::wstring(enable ? k_high_performance_guid : k_balanced_guid);
    const bool ok = run_system_command(cmd);
    log("Power plan",
        ok ? (enable ? "Switched to High performance" : "Switched back to Balanced")
           : "powercfg reported an error (this Windows edition may not ship that scheme)");
    return ok;
}

bool remove_startup_delay()
{
    const bool ok = set_dword(HKEY_CURRENT_USER,
                              L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize",
                              L"StartupDelayInMSec", 0);
    log("Startup delay", ok ? "Removed" : "Failed to write the registry key");
    return ok;
}

bool disable_ntfs_last_access()
{
    const bool ok = set_dword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
                              L"NtfsDisableLastAccessUpdate", 1);
    log("NTFS last access", ok ? "Disabled" : "Failed to write the registry key");
    return ok;
}

bool disable_advertising_id()
{
    const bool ok =
        set_dword(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 0);
    log("Advertising ID", ok ? "Disabled" : "Failed to write the registry key");
    return ok;
}

bool disable_tips_and_suggestions()
{
    const wchar_t* subkey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager";
    bool ok = true;
    ok &= set_dword(HKEY_CURRENT_USER, subkey, L"SilentInstalledAppsEnabled", 0);
    ok &= set_dword(HKEY_CURRENT_USER, subkey, L"SubscribedContent-338389Enabled", 0);
    log("Tips & suggested apps", ok ? "Disabled" : "Failed to write the registry key");
    return ok;
}

bool fivem_gpu_high_performance()
{
    // FiveM.exe is just the launcher/CEF host — the actual D3D11 device
    // (and the window Windows' GPU-preference logic keys off) belongs to
    // the game subprocess, so that's the exe this needs to target, not
    // FiveM.exe.
    const std::wstring exe_path = find_fivem_game_process_full_path();
    if (exe_path.empty())
    {
        log("FiveM GPU preference", "Game subprocess not found (launch FiveM at least once first)");
        return false;
    }
    // Same key Settings > Display > Graphics writes: value name is the
    // exe's full path, data selects which GPU class to prefer.
    const bool ok =
        set_string_kv(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\DirectX\\UserGpuPreferences",
                      exe_path.c_str(), L"GpuPreference=2;");
    log("FiveM GPU preference",
        ok ? "Set to high-performance GPU" : "Failed to write the registry key");
    return ok;
}

bool fivem_disable_fullscreen_optimizations()
{
    // Same target as the GPU preference above, and for the same reason:
    // Fullscreen Optimizations is a per-window behavior, and the game
    // subprocess owns the actual game window, not the FiveM.exe launcher.
    const std::wstring exe_path = find_fivem_game_process_full_path();
    if (exe_path.empty())
    {
        log("FiveM fullscreen optimizations",
            "Game subprocess not found (launch FiveM at least once first)");
        return false;
    }
    const bool ok =
        set_string_kv(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
                      exe_path.c_str(), L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE");
    log("FiveM fullscreen optimizations",
        ok ? "Disabled (true exclusive fullscreen instead of borderless)"
           : "Failed to write the registry key");
    return ok;
}

bool fivem_defender_exclusion(bool enable)
{
    wchar_t local_app_data[MAX_PATH] = {};
    ::GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    const std::wstring fivem_dir = std::wstring(local_app_data) + L"\\FiveM";
    if (!std::filesystem::exists(fivem_dir))
    {
        log("FiveM Defender exclusion", "FiveM isn't installed on this machine");
        return false;
    }

    std::wstring escaped = fivem_dir;
    size_t pos = 0;
    while ((pos = escaped.find(L"'", pos)) != std::wstring::npos)
    {
        escaped.replace(pos, 1, L"''");
        pos += 2;
    }

    const std::wstring verb = enable ? L"Add-MpPreference" : L"Remove-MpPreference";
    const std::wstring cmd = L"powershell.exe -NoProfile -Command \"" + verb +
                             L" -ExclusionPath '" + escaped + L"' -ErrorAction Stop\"";
    const bool ok = run_system_command(cmd);
    log("FiveM Defender exclusion",
        ok ? (enable ? "Added" : "Removed") : "Failed (Defender may be off or managed by policy)");
    return ok;
}

bool check_disable_action_center()
{
    return dword_equals(HKEY_CURRENT_USER, L"Software\\Policies\\Microsoft\\Windows\\Explorer",
                        L"DisableNotificationCenter", 1);
}

bool check_classic_alt_tab()
{
    return dword_equals(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                        L"AltTabSettings", 0);
}

bool check_disable_gpu_mpo()
{
    return dword_equals(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\Dwm",
                        L"OverlayTestMode", 5);
}

bool check_background_services_disabled()
{
    return service_start_type_is(L"SysMain", SERVICE_DISABLED) &&
           service_start_type_is(L"WSearch", SERVICE_DISABLED);
}

bool check_usb_selective_suspend_disabled()
{
    GUID* active_scheme = nullptr;
    if (::PowerGetActiveScheme(nullptr, &active_scheme) != ERROR_SUCCESS || !active_scheme)
        return false;
    DWORD value = 1;
    const bool ok = ::PowerReadACValueIndex(nullptr, active_scheme, &k_usb_subgroup,
                                            &k_usb_suspend_setting, &value) == ERROR_SUCCESS;
    ::LocalFree(active_scheme);
    return ok && value == 0;
}

bool check_fivem_qos_priority()
{
    return string_equals_ci(HKEY_LOCAL_MACHINE,
                            L"SOFTWARE\\Policies\\Microsoft\\Windows\\QoS\\SZKFiveMLauncher",
                            L"DSCP Value", L"46") &&
           string_equals_ci(HKEY_LOCAL_MACHINE,
                            L"SOFTWARE\\Policies\\Microsoft\\Windows\\QoS\\SZKFiveMGame",
                            L"DSCP Value", L"46");
}

bool check_fivem_cache_autoclear()
{
    return run_system_command(L"schtasks /query /tn \"SZKFiveMCacheClear\"");
}

bool check_fivem_high_cpu_priority()
{
    const bool launcher_ok =
        dword_equals(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
                     L"Options\\FiveM.exe\\PerfOptions",
                     L"CpuPriorityClass", 3);

    const std::wstring game_exe = find_fivem_game_process_name();
    if (game_exe.empty())
        return launcher_ok;

    const std::wstring subkey =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" +
        game_exe + L"\\PerfOptions";
    return launcher_ok && dword_equals(HKEY_LOCAL_MACHINE, subkey.c_str(), L"CpuPriorityClass", 3);
}

bool check_game_bar_dvr()
{
    return dword_equals(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_Enabled", 0);
}

bool check_mouse_raw_1to1()
{
    return string_equals_ci(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseSpeed", L"0");
}

bool check_keyboard_rapid_response()
{
    return string_equals_ci(HKEY_CURRENT_USER, L"Control Panel\\Keyboard", L"KeyboardDelay",
                            L"0") &&
           string_equals_ci(HKEY_CURRENT_USER, L"Control Panel\\Keyboard", L"KeyboardSpeed", L"31");
}

bool check_network_throttling()
{
    return dword_equals(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
        L"NetworkThrottlingIndex", 0xffffffff);
}

bool check_filter_keys_timing()
{
    return string_equals_ci(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\Keyboard Response",
                            L"Flags", L"122");
}

bool check_hardware_gpu_scheduling()
{
    return dword_equals(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
                        L"HwSchMode", 2);
}

bool check_visual_effects_performance()
{
    return dword_equals(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects",
                        L"VisualFXSetting", 2);
}

bool check_startup_delay()
{
    return dword_equals(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize",
                        L"StartupDelayInMSec", 0);
}

bool check_ntfs_last_access()
{
    return dword_equals(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
                        L"NtfsDisableLastAccessUpdate", 1);
}

bool check_advertising_id()
{
    return dword_equals(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo",
                        L"Enabled", 0);
}

bool check_tips_and_suggestions()
{
    return dword_equals(HKEY_CURRENT_USER,
                        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
                        L"SilentInstalledAppsEnabled", 0);
}

bool check_fivem_gpu_high_performance()
{
    const std::wstring exe_path = find_fivem_game_process_full_path();
    if (exe_path.empty())
        return false;
    return string_equals_ci(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\DirectX\\UserGpuPreferences",
                            exe_path.c_str(), L"GpuPreference=2;");
}

bool check_fivem_disable_fullscreen_optimizations()
{
    const std::wstring exe_path = find_fivem_game_process_full_path();
    if (exe_path.empty())
        return false;
    return string_contains(
        HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
        exe_path.c_str(), L"DISABLEDXMAXIMIZEDWINDOWEDMODE");
}
} // namespace szk::backend
