#include "backend/power_plan.h"

#include "backend/activity_log.h"

#include <windows.h>

#include <powrprof.h>

#include <string>

#pragma comment(lib, "powrprof.lib")

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
} // namespace

power_plan_info power_plan_active()
{
    power_plan_info info;

    GUID* active_scheme = nullptr;
    if (::PowerGetActiveScheme(nullptr, &active_scheme) != ERROR_SUCCESS || !active_scheme)
        return info;

    wchar_t name[128] = {};
    DWORD size = sizeof(name);
    if (::PowerReadFriendlyName(nullptr, active_scheme, nullptr, nullptr,
                                reinterpret_cast<UCHAR*>(name), &size) == ERROR_SUCCESS)
    {
        ::WideCharToMultiByte(CP_UTF8, 0, name, -1, info.name, sizeof(info.name), nullptr, nullptr);
        info.available = true;
    }

    ::LocalFree(active_scheme);
    return info;
}

bool power_plan_rename_active(const wchar_t* name, const wchar_t* description)
{
    GUID* active_scheme = nullptr;
    if (::PowerGetActiveScheme(nullptr, &active_scheme) != ERROR_SUCCESS || !active_scheme)
    {
        log("Power plan", "Could not read the active power scheme");
        return false;
    }

    wchar_t guid_str[64] = {};
    ::StringFromGUID2(*active_scheme, guid_str, 64);
    ::LocalFree(active_scheme);

    // StringFromGUID2 wraps the GUID in braces; powercfg wants it bare.
    std::wstring guid(guid_str);
    if (guid.size() >= 2 && guid.front() == L'{' && guid.back() == L'}')
        guid = guid.substr(1, guid.size() - 2);

    const std::wstring cmd =
        L"powercfg -changename " + guid + L" \"" + name + L"\" \"" + description + L"\"";
    const bool ok = run_system_command(cmd);
    log("Power plan", ok ? "Renamed the active power plan" : "powercfg -changename failed");
    return ok;
}
} // namespace szk::backend
