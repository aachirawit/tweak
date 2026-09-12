#pragma once

#include <string>

namespace szk::backend
{
// Config-file tweaks for FiveM and GTA V. Unlike the rest of os_tweaks, these
// write to files the game owns rather than to the registry, so every one of
// them merges: only the keys in the preset are touched and everything else in
// the file is left exactly as it was. The machine-specific entries in
// particular - the GTA V install path, the saved build number, the pool sizes,
// the monitor's resolution and refresh rate, the graphics card name - are never
// written, because the profile these presets came from carries one machine's
// answers to those and they are wrong on any other.
//
// The first apply copies the untouched file to "<name>.numbanine.bak" beside
// it, so there is always a way back to what the game wrote.

// %LOCALAPPDATA%\FiveM\FiveM.app\CitizenFX.ini - the launcher and renderer
// options FiveM reads at startup.
bool apply_fivem_citizenfx_config();
bool check_fivem_citizenfx_config();

// GTA V's settings.xml graphics block. FiveM redirects the game's Documents
// folder into its own data directory, so the file is looked for in FiveM's
// redirect first and in Documents\Rockstar Games\GTA V second; whichever
// already exists is the one written. If neither does - the game has not been
// launched yet - the standard Documents path is created.
bool apply_gta5_graphics_preset();
bool check_gta5_graphics_preset();

// Where the two above resolved to, for the UI to show. Empty when the file
// does not exist and no parent directory for it does either.
std::wstring fivem_citizenfx_path();
std::wstring gta5_settings_path();
} // namespace szk::backend
