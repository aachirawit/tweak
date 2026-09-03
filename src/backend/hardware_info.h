#pragma once

namespace solace::backend
{
struct motherboard_info
{
    char product[128] = {};
    bool available = false;
};

// Queries Win32_BaseBoard via WMI. Safe to call repeatedly; each call does
// its own COM init/teardown since this isn't called every frame.
motherboard_info motherboard_query();

// Opens the default browser on a Google search for `query`.
void open_google_search(const char* query);

// Opens Windows' own Settings > System > Recovery page (ms-settings:recovery).
// Doesn't trigger a reset or restore by itself — just navigates there.
void open_windows_recovery_settings();

// Creates a real Windows System Restore point right now (SRSetRestorePointW).
// Purely additive — doesn't change or roll back anything.
bool create_system_restore_point();

// Launches Windows' own System Restore wizard (rstrui.exe) so the user picks
// a restore point and confirms inside Windows' own UI — this never rolls
// the system back on its own.
void open_system_restore_wizard();

// Opens the shop's Discord invite in the default browser.
void open_discord();
} // namespace solace::backend
