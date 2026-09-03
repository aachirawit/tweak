#include "backend/cleanup_tweaks.h"

#include "backend/activity_log.h"

#include <windows.h>

#include <shellapi.h>

#include <filesystem>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace szk::backend
{
namespace
{
namespace fs = std::filesystem;

// Deletes everything directly inside dir (not dir itself). Files locked by
// a running process are skipped rather than aborting the whole sweep.
// Returns how many entries were actually removed.
int clear_directory_contents(const fs::path& dir)
{
    std::error_code ec;
    if (!fs::exists(dir, ec))
        return 0;

    int removed = 0;
    for (const auto& entry :
         fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
    {
        std::error_code remove_ec;
        if (fs::remove_all(entry.path(), remove_ec) > 0)
            removed++;
    }
    return removed;
}

std::wstring env(const wchar_t* name)
{
    wchar_t buf[MAX_PATH] = {};
    ::GetEnvironmentVariableW(name, buf, MAX_PATH);
    return buf;
}
} // namespace

bool clear_temp_files()
{
    const int a = clear_directory_contents(env(L"TEMP"));
    const int b = clear_directory_contents(env(L"WINDIR") + L"\\Temp");
    const int total = a + b;
    log("Temp files",
        total > 0 ? "Cleared " + std::to_string(total) + " item(s)" : "Nothing to clear");
    return true;
}

bool clear_prefetch()
{
    const int n = clear_directory_contents(env(L"WINDIR") + L"\\Prefetch");
    log("Prefetch", n > 0 ? "Cleared " + std::to_string(n) + " item(s)" : "Nothing to clear");
    return true;
}

bool clear_windows_update_cache()
{
    const int n = clear_directory_contents(env(L"WINDIR") + L"\\SoftwareDistribution\\Download");
    log("Windows Update cache",
        n > 0 ? "Cleared " + std::to_string(n) + " item(s)" : "Nothing to clear");
    return true;
}

bool clear_shader_cache()
{
    const std::wstring local = env(L"LOCALAPPDATA");
    const int a = clear_directory_contents(local + L"\\D3DSCache");
    const int b = clear_directory_contents(local + L"\\NVIDIA\\DXCache");
    const int c = clear_directory_contents(local + L"\\NVIDIA\\GLCache");
    const int total = a + b + c;
    log("Shader cache",
        total > 0 ? "Cleared " + std::to_string(total) + " item(s)" : "Nothing to clear");
    return true;
}

bool clear_thumbnail_cache()
{
    const fs::path dir = fs::path(env(L"LOCALAPPDATA")) / L"Microsoft" / L"Windows" / L"Explorer";
    std::error_code ec;
    int removed = 0;
    if (fs::exists(dir, ec))
    {
        for (const auto& entry :
             fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
        {
            const std::wstring name = entry.path().filename().wstring();
            if (name.rfind(L"thumbcache_", 0) != 0)
                continue;
            std::error_code remove_ec;
            if (fs::remove(entry.path(), remove_ec))
                removed++;
        }
    }
    log("Thumbnail cache", removed > 0 ? "Cleared " + std::to_string(removed) + " file(s)"
                                       : "Nothing to clear (Explorer may have them locked)");
    return true;
}

bool empty_recycle_bin()
{
    const HRESULT hr = ::SHEmptyRecycleBinW(
        nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    log("Recycle Bin", SUCCEEDED(hr) ? "Emptied" : "Failed to empty");
    return SUCCEEDED(hr);
}
} // namespace szk::backend
