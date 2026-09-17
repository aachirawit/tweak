#include "backend/reshade_manager.h"

#include "backend/activity_log.h"

#include <windows.h>

#include <shellapi.h>
#include <tlhelp32.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace szk::backend
{
namespace
{
namespace fs = std::filesystem;

// FiveM loads DLLs dropped in FiveM.app\plugins — its own stable plugin
// folder, unrelated to the versioned FiveM_b<build>_GTAProcess.exe cache
// apply_fivem_qos_priority() targets. This is where dxgi.dll actually needs
// to sit; verified against an existing working ReShade install on disk.
fs::path fivem_app_dir()
{
    wchar_t local_app_data[MAX_PATH] = {};
    if (::GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH) == 0)
        return {};

    const fs::path dir = fs::path(local_app_data) / L"FiveM" / L"FiveM.app";
    if (!fs::exists(dir))
        return {};
    return dir;
}

fs::path plugins_dir()
{
    const fs::path app_dir = fivem_app_dir();
    if (app_dir.empty())
        return {};
    return app_dir / L"plugins";
}

// The bundled dxgi.dll / ini / shader library this app ships with, next to
// its own exe (not embedded — the shader library alone is ~100 MB).
fs::path assets_dir()
{
    wchar_t exe_path[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    return fs::path(exe_path).parent_path() / L"assets" / L"reshade";
}

// dxgi.dll gets memory-mapped into FiveM's game process the moment it
// starts, and Windows won't let a locked DLL be overwritten — install and
// uninstall would otherwise fail with a confusing generic error while the
// game is still running. FiveM.exe is the launcher/CEF host; the actual
// renderer is one of the versioned FiveM_b<build>_GTAProcess.exe
// subprocesses, so both names are checked.
bool fivem_is_running()
{
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool found = false;
    if (::Process32FirstW(snapshot, &entry))
    {
        do
        {
            const std::wstring name = entry.szExeFile;
            if (_wcsicmp(name.c_str(), L"FiveM.exe") == 0 ||
                name.find(L"GTAProcess.exe") != std::wstring::npos)
            {
                found = true;
                break;
            }
        } while (::Process32NextW(snapshot, &entry));
    }
    ::CloseHandle(snapshot);
    return found;
}

// ReShade can be loaded through several different proxy DLL names
// depending on which graphics API it's hooking (d3d9/d3d10/d3d11/d3d12/
// dxgi/opengl32/dinput8). FiveM/GTA V uses DX11, so this app only ever
// installs dxgi.dll — but if something else already dropped a same-purpose
// proxy DLL under a different one of those names, both would try to hook
// the same device and one (or both) would likely crash or silently no-op.
bool other_proxy_dll_present(const fs::path& target)
{
    static const wchar_t* const k_other_names[] = {
        L"d3d9.dll",  L"d3d10.dll",    L"d3d10_1.dll", L"d3d11.dll",
        L"d3d12.dll", L"opengl32.dll", L"dinput8.dll",
    };
    for (const wchar_t* name : k_other_names)
        if (fs::exists(target / name))
            return true;
    return false;
}

bool copy_one(const fs::path& source, const fs::path& target)
{
    std::error_code ec;
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

// FiveM has separately acknowledged a ReShade 5.x crash bug and gates
// loading it behind this exact line existing in CitizenFX.ini — without
// it the DLL sits in plugins\ but FiveM never loads it.
constexpr char k_reshade_ack_line[] =
    "ReShade5=ID:7323c165 acknowledged that ReShade 5.x has a bug that will lead to game crashes";

fs::path citizenfx_ini_path()
{
    const fs::path app_dir = fivem_app_dir();
    if (app_dir.empty())
        return {};
    return app_dir / L"CitizenFX.ini";
}

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// ReShade reads exactly one preset, named by PresetPath in ReShade.ini, and
// enables only the techniques that preset lists. Point it at a file that is not
// there and ReShade still loads, still compiles all 900-odd shaders, and renders
// nothing - which is indistinguishable from "it didn't install" from inside the
// game.
//
// The bundled ReShade.ini carried an absolute path into a folder on the machine
// it was captured from, so that is what every install did. Rewriting the line
// after the copy means the shipped ini cannot get this wrong again, whatever it
// happens to say.
bool set_preset_path(const fs::path& ini, const char* preset)
{
    std::string text = read_file(ini);
    if (text.empty())
        return false;

    const std::string line = std::string("PresetPath=") + preset;

    const size_t at = text.find("PresetPath=");
    if (at == std::string::npos)
    {
        // No line to replace: add one under [GENERAL], which is where ReShade
        // writes it itself.
        const size_t general = text.find("[GENERAL]");
        if (general == std::string::npos)
            return false;

        const size_t eol = text.find('\n', general);
        if (eol == std::string::npos)
            return false;

        text.insert(eol + 1, line + "\n");
    }
    else
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        else if (end > at && text[end - 1] == '\r')
            end--;

        text.replace(at, end - at, line);
    }

    std::ofstream out(ini, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    out.write(text.data(), (std::streamsize)text.size());
    return out.good();
}

bool citizenfx_ack_present()
{
    const fs::path ini_path = citizenfx_ini_path();
    if (ini_path.empty() || !fs::exists(ini_path))
        return false;
    return read_file(ini_path).find(k_reshade_ack_line) != std::string::npos;
}

// Append-only: never rewrites or reorders whatever is already in
// CitizenFX.ini (it also holds FiveM's own IVPath/build-number settings).
// Does nothing if FiveM has never run (the file doesn't exist yet).
bool ensure_citizenfx_ack()
{
    const fs::path ini_path = citizenfx_ini_path();
    if (ini_path.empty() || !fs::exists(ini_path))
        return false;

    const std::string content = read_file(ini_path);
    if (content.find(k_reshade_ack_line) != std::string::npos)
        return true;

    std::ofstream out(ini_path, std::ios::binary | std::ios::app);
    if (!out)
        return false;

    if (content.find("[Addons]") == std::string::npos)
        out << "\r\n[Addons]\r\n";
    else
        out << "\r\n";
    out << k_reshade_ack_line << "\r\n";
    return true;
}
} // namespace

reshade_status reshade_check()
{
    reshade_status status;
    const fs::path target = plugins_dir();
    status.game_found = !target.empty();
    if (!status.game_found)
        return status;

    std::error_code ec;
    status.installed = fs::exists(target / L"dxgi.dll", ec);
    status.road_mod_present = fs::exists(target / L"QuantV.addon", ec);
    status.crash_ack_present = citizenfx_ack_present();
    return status;
}

bool reshade_install(bool include_road_mod)
{
    const fs::path target = plugins_dir();
    if (target.empty())
    {
        log("ReShade", "FiveM isn't installed on this machine (no FiveM.app folder)");
        return false;
    }

    const fs::path source = assets_dir();
    if (!fs::exists(source / L"dxgi.dll"))
    {
        log("ReShade", "Bundled ReShade files are missing next to the app (assets\\reshade)");
        return false;
    }

    if (fivem_is_running())
    {
        log("ReShade", "FiveM is running — close it first, dxgi.dll can't be replaced while it's "
                       "loaded");
        return false;
    }

    std::error_code dir_ec;
    fs::create_directories(target, dir_ec);

    if (other_proxy_dll_present(target))
        log("ReShade", "Another proxy DLL (d3d9/d3d10/d3d11/d3d12/opengl32/dinput8) is already in "
                       "the plugins folder — it may conflict with ReShade's dxgi.dll hook");

    bool ok = true;
    ok &= copy_one(source / L"dxgi.dll", target / L"dxgi.dll");
    ok &= copy_one(source / L"ReShade.ini", target / L"ReShade.ini");
    ok &= copy_one(source / L"ReShadePreset.ini", target / L"ReShadePreset.ini");

    std::error_code ec;
    fs::copy(source / L"reshade-shaders", target / L"reshade-shaders",
             fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    ok &= !ec;

    if (include_road_mod)
    {
        ok &= copy_one(source / L"QuantV.addon", target / L"QuantV.addon");
        ok &= copy_one(source / L"QuantV.preset.ini", target / L"QuantV.preset.ini");
    }
    else
    {
        std::error_code remove_ec;
        fs::remove(target / L"QuantV.addon", remove_ec);
        fs::remove(target / L"QuantV.preset.ini", remove_ec);
    }

    // Relative to the folder the DLL is in, so it survives being installed
    // anywhere. With the road mod that is QuantV's own preset, which is the
    // only preset this app ships with anything enabled in it.
    ok &= set_preset_path(target / L"ReShade.ini",
                          include_road_mod ? ".\\QuantV.preset.ini" : ".\\ReShadePreset.ini");

    const bool ack_ok = ensure_citizenfx_ack();
    const char* ack_note = ack_ok ? ""
                                  : " — FiveM hasn't run yet, so CitizenFX.ini doesn't exist; "
                                    "launch FiveM once, then reinstall so it can add the "
                                    "crash-acknowledgment line ReShade needs to actually run";

    log("ReShade", (ok ? std::string(include_road_mod ? "Installed with the 2K Road Mod"
                                                      : "Installed (2K Road Mod not included)")
                       : std::string("Installed, but some files failed to copy — check the "
                                     "plugins folder")) +
                       ack_note);
    return ok;
}

bool reshade_uninstall()
{
    const fs::path target = plugins_dir();
    if (target.empty())
    {
        log("ReShade", "FiveM isn't installed on this machine — nothing to remove");
        return false;
    }

    if (fivem_is_running())
    {
        log("ReShade", "FiveM is running — close it first, dxgi.dll can't be removed while it's "
                       "loaded");
        return false;
    }

    std::error_code ec;
    fs::remove(target / L"dxgi.dll", ec);
    fs::remove(target / L"ReShade.ini", ec);
    fs::remove(target / L"ReShadePreset.ini", ec);
    fs::remove(target / L"QuantV.addon", ec);
    fs::remove(target / L"QuantV.preset.ini", ec);
    fs::remove_all(target / L"reshade-shaders", ec);

    log("ReShade", "Removed from FiveM's plugins folder");
    return true;
}

void reshade_open_plugins_folder()
{
    const fs::path target = plugins_dir();
    const fs::path open_dir = !target.empty() ? target : assets_dir();

    ::ShellExecuteW(nullptr, L"open", open_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
} // namespace szk::backend
