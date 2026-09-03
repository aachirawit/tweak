#pragma once

namespace solace::backend
{
// One-shot cleanup actions. Each deletes files Windows/the app/drivers
// recreate on demand — nothing here is a setting to revert, so unlike the
// os_tweaks toggles these have no on/off state. Locked files (in use by a
// running process) are skipped rather than treated as a hard failure.

bool clear_temp_files();           // %TEMP% and %WINDIR%\Temp
bool clear_prefetch();             // %WINDIR%\Prefetch
bool clear_windows_update_cache(); // %WINDIR%\SoftwareDistribution\Download
bool clear_shader_cache();         // %LOCALAPPDATA%\D3DSCache + NVIDIA DXCache/GLCache
bool clear_thumbnail_cache(); // thumbcache_*.db under %LOCALAPPDATA%\Microsoft\Windows\Explorer
bool empty_recycle_bin();     // SHEmptyRecycleBinW — permanent, not undoable
} // namespace solace::backend
