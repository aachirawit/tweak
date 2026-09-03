#pragma once

namespace solace::backend
{
struct reshade_status
{
    bool game_found = false;        // found FiveM.app on this machine
    bool installed = false;         // dxgi.dll present in FiveM.app\plugins
    bool road_mod_present = false;  // QuantV.addon present there
    bool crash_ack_present = false; // CitizenFX.ini has the ReShade5 crash
                                    // acknowledgment line ReShade needs to
                                    // actually run (see reshade_install()).
};

// Looks for FiveM's actual game subprocess (the same versioned
// "FiveM_b<build>_GTAProcess.exe" apply_fivem_qos_priority() finds) and
// reports what's already sitting next to it.
reshade_status reshade_check();

// Copies dxgi.dll, ReShade's ini/preset, and the reshade-shaders effect
// library from this app's own "assets\reshade" folder into
// FiveM.app\plugins (FiveM's own DLL plugin folder). When include_road_mod
// is true, also copies the QuantV addon + its preset; when false, removes
// them if present so the toggle stays truthful.
//
// Also appends the "ReShade5=ID:7323c165 acknowledged that ReShade 5.x has
// a bug that will lead to game crashes" line to CitizenFX.ini's [Addons]
// section if it isn't already there — FiveM refuses to actually load
// ReShade without it (community-documented; without this line the DLL
// sits in plugins\ but never runs). Only appends — never rewrites or
// reorders the rest of CitizenFX.ini, and does nothing if FiveM has never
// been run (the file won't exist yet).
bool reshade_install(bool include_road_mod);

// Removes everything reshade_install() places in the subprocess folder.
bool reshade_uninstall();

// Opens the installed reshade-shaders folder (or the subprocess folder if
// ReShade isn't installed yet) so the user can drop in extra shaders.
void reshade_open_plugins_folder();
} // namespace solace::backend
