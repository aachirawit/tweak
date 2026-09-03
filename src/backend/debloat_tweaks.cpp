#include "backend/debloat_tweaks.h"

#include "backend/activity_log.h"

#include <windows.h>

#include <string>

#pragma comment(lib, "advapi32.lib")

namespace szk::backend
{
namespace
{
bool run_command(const wchar_t* command_line)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring mutable_cmd = command_line;
    if (!::CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &si, &pi))
        return false;

    ::WaitForSingleObject(pi.hProcess, 15000);
    DWORD exit_code = 1;
    ::GetExitCodeProcess(pi.hProcess, &exit_code);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return exit_code == 0;
}

struct dword_entry
{
    HKEY root;
    const wchar_t* subkey;
    const wchar_t* value; // nullptr => the key's default value
    DWORD data;
};

struct string_entry
{
    HKEY root;
    const wchar_t* subkey;
    const wchar_t* value;
    const wchar_t* data;
    DWORD type; // REG_SZ or REG_EXPAND_SZ
};

struct delete_entry
{
    HKEY root;
    const wchar_t* subkey;
};

int apply_dwords(const dword_entry* entries, int count)
{
    int ok = 0;
    for (int i = 0; i < count; i++)
    {
        HKEY key = nullptr;
        if (::RegCreateKeyExW(entries[i].root, entries[i].subkey, 0, nullptr, 0, KEY_SET_VALUE,
                              nullptr, &key, nullptr) != ERROR_SUCCESS)
            continue;
        if (::RegSetValueExW(key, entries[i].value, 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&entries[i].data),
                             sizeof(DWORD)) == ERROR_SUCCESS)
            ok++;
        ::RegCloseKey(key);
    }
    return ok;
}

int apply_strings(const string_entry* entries, int count)
{
    int ok = 0;
    for (int i = 0; i < count; i++)
    {
        HKEY key = nullptr;
        if (::RegCreateKeyExW(entries[i].root, entries[i].subkey, 0, nullptr, 0, KEY_SET_VALUE,
                              nullptr, &key, nullptr) != ERROR_SUCCESS)
            continue;
        const DWORD size = static_cast<DWORD>((wcslen(entries[i].data) + 1) * sizeof(wchar_t));
        if (::RegSetValueExW(key, entries[i].value, 0, entries[i].type,
                             reinterpret_cast<const BYTE*>(entries[i].data), size) == ERROR_SUCCESS)
            ok++;
        ::RegCloseKey(key);
    }
    return ok;
}

void delete_keys(const delete_entry* entries, int count)
{
    // Best-effort; a key that doesn't exist on this Windows version is not
    // an error worth tracking.
    for (int i = 0; i < count; i++)
        ::RegDeleteTreeW(entries[i].root, entries[i].subkey);
}

void set_service_disabled(const wchar_t* service_name)
{
    wchar_t subkey[256];
    ::wsprintfW(subkey, L"SYSTEM\\CurrentControlSet\\Services\\%s", service_name);
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                          nullptr) != ERROR_SUCCESS)
        return;
    const DWORD disabled = 4;
    ::RegSetValueExW(key, L"Start", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&disabled),
                     sizeof(disabled));
    ::RegCloseKey(key);
}
} // namespace

bool apply_gaming_priority_tweaks()
{
    const dword_entry dwords[] = {
        {HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
         L"Win32PrioritySeparation", 26},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
         L"SystemResponsiveness", 0},
        {HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"RawMouseThrottleDuration", 0x14},

        // Lower priority for game-launcher helper processes so the game
        // itself gets more of the CPU.
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\ctfmon.exe\\PerfOptions",
         L"CpuPriorityClass", 5},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\SearchIndexer.exe\\PerfOptions",
         L"CpuPriorityClass", 5},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\OriginWebHelperService.exe\\PerfOptions",
         L"CpuPriorityClass", 5},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\ShareX.exe\\PerfOptions",
         L"CpuPriorityClass", 5},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\EpicWebHelper.exe\\PerfOptions",
         L"CpuPriorityClass", 5},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\UplayWebCore.exe\\PerfOptions",
         L"CpuPriorityClass", 5},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\SocialClubHelper.exe\\PerfOptions",
         L"CpuPriorityClass", 5},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\steamwebhelper.exe\\PerfOptions",
         L"CpuPriorityClass", 5},

        // A few core system processes get a small, well-known priority
        // adjustment (not "5" — that's specifically for launcher helpers).
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\fontdrvhost.exe\\PerfOptions",
         L"CpuPriorityClass", 1},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\fontdrvhost.exe\\PerfOptions",
         L"IoPriority", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\lsass.exe\\PerfOptions",
         L"CpuPriorityClass", 1},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\sihost.exe\\PerfOptions",
         L"CpuPriorityClass", 1},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\sihost.exe\\PerfOptions",
         L"IoPriority", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\sppsvc.exe\\PerfOptions",
         L"CpuPriorityClass", 1},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\sppsvc.exe\\PerfOptions",
         L"IoPriority", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\csrss.exe\\PerfOptions",
         L"CpuPriorityClass", 3},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
         L"Options\\csrss.exe\\PerfOptions",
         L"IoPriority", 3},
    };

    const int total = sizeof(dwords) / sizeof(dwords[0]);
    const int ok = apply_dwords(dwords, total);
    log("Gaming priority tweaks",
        std::to_string(ok) + "/" + std::to_string(total) + " values written");
    return ok == total;
}

bool apply_network_hardening()
{
    const dword_entry dwords[] = {
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\DNSClient",
         L"EnableMulticast", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"RSoPLogging", 0},
        {HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\LanmanServer\\Parameters",
         L"RestrictNullSessAccess", 1},
        {HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Lsa", L"RestrictAnonymous", 1},
    };

    const int total = sizeof(dwords) / sizeof(dwords[0]);
    const int ok = apply_dwords(dwords, total);
    log("Network hardening", std::to_string(ok) + "/" + std::to_string(total) + " values written");
    return ok == total;
}

bool apply_privacy_telemetry_tweaks()
{
    const dword_entry dwords[] = {
        {HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\DiagTrack", L"Start", 4},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection",
         L"AllowTelemetry", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection",
         L"LimitDiagnosticLogCollection", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection",
         L"LimitDumpCollection", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection",
         L"DoNotShowFeedbackNotifications", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
         L"AllowOnlineTips", 0},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\input\\Settings", L"InsightsEnabled", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\TextInput",
         L"AllowLinguisticDataCollection", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\SQMClient\\Windows", L"CEIPEnable", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\SQMClient\\Windows", L"CEIPEnable",
         0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\FTH", L"Enabled", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\PCHealth\\ErrorReporting",
         L"DoReport", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Error Reporting",
         L"Disabled", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System",
         L"EnableActivityFeed", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System",
         L"PublishUserActivities", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System",
         L"UploadUserActivities", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\DeviceHealthAttestationService",
         L"EnableDeviceHealthAttestationService", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System",
         L"EnableFontProviders", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\EdgeUI",
         L"DisableHelpSticker", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\ScriptedDiagnostics",
         L"EnableDiagnostics", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Policies\\Microsoft\\Windows\\ScriptedDiagnosticsProvider\\Policy",
         L"EnableQueryRemoteServer", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\ScheduledDiagnostics",
         L"EnabledExecution", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Policies\\Microsoft\\Windows\\WDI\\{a7a5847a-7511-4e4e-90b1-45ad2a002f51}",
         L"ScenarioExecutionEnabled", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Policies\\Microsoft\\Windows\\WDI\\{186f47ef-626c-4670-800a-4a30756babad}",
         L"ScenarioExecutionEnabled", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Policies\\Microsoft\\Windows\\WDI\\{ecfb03d1-58ee-4cc7-a1b5-9bc6febcb915}",
         L"ScenarioExecutionEnabled", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Policies\\Microsoft\\Windows\\WDI\\{67144949-5132-4859-8036-a737b43825d8}",
         L"ScenarioExecutionEnabled", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Policies\\Microsoft\\Windows\\WDI\\{86432a0b-3c7d-4ddf-a89c-172faa90485d}",
         L"ScenarioExecutionEnabled", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Policies\\Microsoft\\Windows\\WDI\\{eb73b633-3f4e-4ba0-8f60-8f3c6f53168f}",
         L"ScenarioExecutionEnabled", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Policies\\Microsoft\\Windows\\WDI\\{2698178D-FDAD-40AE-9D3C-1371703ADC5B}",
         L"ScenarioExecutionEnabled", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\UEV\\Agent", L"Enabled", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Device Metadata",
         L"PreventDeviceMetadataFromNetwork", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\WindowsMediaPlayer",
         L"PreventLibrarySharing", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search",
         L"ConnectedSearchUseWeb", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Policies\\Microsoft\\Windows\\CurrentVersion\\PushNotifications",
         L"NoCloudApplicationNotification", 1},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SearchSettings",
         L"IsDynamicSearchBoxEnabled", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\PolicyManager\\default\\NewsAndInterests\\"
         L"AllowNewsAndInterests",
         L"value", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Dsh", L"AllowNewsAndInterests", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\AppV\\Client", L"Enabled", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\AppV\\Client", L"Enabled", 0},
    };

    const string_entry strings[] = {
        {HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
         L"POWERSHELL_TELEMETRY_OPTOUT", L"1", REG_SZ},
    };

    const int dword_total = sizeof(dwords) / sizeof(dwords[0]);
    const int string_total = sizeof(strings) / sizeof(strings[0]);
    const int ok = apply_dwords(dwords, dword_total) + apply_strings(strings, string_total);
    const int total = dword_total + string_total;
    log("Privacy & telemetry tweaks",
        std::to_string(ok) + "/" + std::to_string(total) + " values written");
    return ok == total;
}

bool apply_ui_explorer_tweaks()
{
    const dword_entry dwords[] = {
        {HKEY_CURRENT_USER, L"SOFTWARE\\Classes\\CLSID\\{018D5C66-4533-4307-9B53-224DE2ED1FE6}",
         L"System.IsPinnedToNameSpaceTree", 0}, // remove Gallery from Explorer
        {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
         L"EnableTransparency", 0},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
         L"HideFileExt", 0}, // show file extensions
        {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
         L"DisallowShaking", 1},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
         L"ShowSyncProviderNotifications", 0},
        {HKEY_CURRENT_USER, L"Control Panel\\Keyboard", L"PrintScreenKeyForSnippingEnabled", 0},
        {HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\StickyKeys", L"Flags", 506},
        {HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\Keyboard Response", L"Flags", 0},
        {HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\ToggleKeys", L"Flags", 0},
        {HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\FilterKeys", L"Flags", 0},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Attachments",
         L"SaveZoneInformation", 1},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppCompat", L"DisablePCA", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\CloudContent",
         L"DisableSoftLanding", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\SettingSync",
         L"DisableSettingSync", 2},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\SettingSync",
         L"DisableSettingSyncUserOverride", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DWM", L"DisallowFlip3d", 1},
        {HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",
         L"DisableExceptionChainValidation", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
         L"DisableAutomaticRestartSignOn", 1},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.Gaming.GameBar."
         L"PresenceServer.Internal.PresenceWriter",
         L"ActivationType", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\ScPnP", L"EnableScPnP", 0},
        {HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
         L"DisableWpbtExecution", 1},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\DriverSearching",
         L"SearchOrderConfig", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WINEVT\\Channels\\Microsoft-Windows-"
         L"Superfetch/Main",
         L"Enable", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WINEVT\\Channels\\Microsoft-Windows-"
         L"Superfetch/PfApLog",
         L"Enable", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WINEVT\\Channels\\Microsoft-Windows-"
         L"Superfetch/StoreLog",
         L"Enable", 0},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\"
         L"Maintenance",
         L"MaintenanceDisabled", 1},
        {HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power",
         L"HiberbootEnabled", 0},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy",
         L"LetAppsRunInBackground", 2},
        {HKEY_CURRENT_USER,
         L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications",
         L"GlobalUserDisabled", 0},
        {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Search",
         L"BackgroundAppGlobalToggle", 1},
    };

    const string_entry strings[] = {
        {HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"HungAppTimeout", L"2000", REG_SZ},
        {HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WaitToKillAppTimeOut", L"2000", REG_SZ},
        {HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", L"WaitToKillServiceTimeout",
         L"2000", REG_SZ},
        {HKEY_CURRENT_USER,
         L"SOFTWARE\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\Bags\\"
         L"AllFolders\\Shell",
         L"FolderType", L"NotSpecified", REG_SZ},
        {HKEY_CURRENT_USER,
         L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\"
         L"InprocServer32",
         nullptr, L"", REG_SZ}, // restore the classic right-click context menu
        {HKEY_CLASSES_ROOT, L".txt\\ShellNew", L"NullFile", L"", REG_SZ},
        {HKEY_CLASSES_ROOT, L".txt\\ShellNew", L"ItemName",
         L"@%SystemRoot%\\system32\\notepad.exe,-470", REG_EXPAND_SZ},
        {HKEY_CLASSES_ROOT, L"txtfilelegacy", nullptr, L"Text Document", REG_SZ},
    };

    const delete_entry deletes[] = {
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\WindowsUpdate\\Orchestrator\\UScheduler_Oobe\\DevHomeUpdate"},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\WindowsUpdate\\Orchestrator\\UScheduler_Oobe\\OutlookUpdate"},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\Orchestrator\\UScheduler_Oobe\\"
                             L"CrossDeviceUpdate"},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\WindowsUpdate\\Orchestrator\\UScheduler_Oobe\\EdgeUpdate"},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Orchestrator\\"
         L"UScheduler\\DevHomeUpdate"},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Orchestrator\\"
         L"UScheduler\\OutlookUpdate"},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Orchestrator\\"
         L"UScheduler\\CrossDeviceUpdate"},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Orchestrator\\"
         L"UScheduler\\EdgeUpdate"},
    };

    const int dword_total = sizeof(dwords) / sizeof(dwords[0]);
    const int string_total = sizeof(strings) / sizeof(strings[0]);
    const int ok = apply_dwords(dwords, dword_total) + apply_strings(strings, string_total);
    delete_keys(deletes, sizeof(deletes) / sizeof(deletes[0]));

    // powercfg /hibernate off does what setting HibernateEnabled=0 in the
    // registry alone doesn't: it also deletes hiberfil.sys, reclaiming disk
    // space equal to installed RAM. The trade-off is Fast Startup (which
    // depends on partial hibernation) stops working alongside it.
    const bool hibernate_ok = run_command(L"powercfg.exe /hibernate off");

    const int total = dword_total + string_total;
    log("UI & Explorer tweaks", std::to_string(ok) + "/" + std::to_string(total) +
                                    " values written, hibernation " +
                                    (hibernate_ok ? "disabled" : "failed to disable"));
    return ok == total && hibernate_ok;
}

bool apply_debloat_services()
{
    // Excludes anything with a real chance of breaking something on a
    // typical machine: WinDefend/WdNis*/WdFilter/WdBoot (antivirus),
    // Spooler (printing), TermService/UmRdpService (Remote Desktop),
    // WSearch (already covered by Network Optimization's own toggle),
    // e1i68x64 (a specific Intel NIC driver instance), and MsSecCore/
    // MsSecFlt/MsSecWfp (unclear whether these are security-related on a
    // given build).
    const wchar_t* const services[] = {
        L"diagnosticshub.standardcollector.service",
        L"diagsvc",
        L"dmwappushservice",
        L"lfsvc",
        L"MapsBroker",
        L"MessagingService",
        L"MessagingService_4bef7",
        L"OneSyncSvc",
        L"PcaSvc",
        L"RetailDemo",
        L"SessionEnv",
        L"TroubleshootingSvc",
        L"wercplsupport",
        L"WerSvc",
        L"wisvc",
        L"PimIndexMaintenanceSvc",
        L"PimIndexMaintenanceSvc_4bef7",
        L"UserDataSvc",
        L"UserDataSvc_4bef7",
        L"UnistoreSvc",
        L"spectrum",
        L"SpatialGraphFilter",
        L"SharedRealitySvc",
        L"SharedAccess",
        L"SgrmBroker",
        L"sfloppy",
        L"fdc",
        L"flpydisk",
        L"MixedRealityOpenXRSvc",
        L"perceptionsimulation",
        L"FontCache3.0.0.0",
        L"edgeupdate",
        L"edgeupdatem",
        L"MicrosoftEdgeElevationService",
        L"VacSvc",
    };

    const int total = sizeof(services) / sizeof(services[0]);
    for (const wchar_t* name : services)
        set_service_disabled(name);

    log("Debloat services", std::to_string(total) + " service(s) set to disabled");
    return true;
}
} // namespace szk::backend
