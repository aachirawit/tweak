#pragma once

namespace szk::backend
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
// What happened when a restore point was asked for. "unavailable" is the
// common case and is not a fault in the app: System Restore is off for the
// drive, or the Volume Shadow Copy service is disabled, which several PC
// "optimiser" tools do. Worth telling apart from a real failure, because the
// user can fix one and not the other.
enum class restore_point
{
    created,
    unavailable,
    failed,
};

restore_point create_restore_point();

// Turns System Protection back on for the system drive: re-enables the Volume
// Shadow Copy service, switches restore points on, and gives them somewhere to
// live. Several PC "optimiser" tools switch all three off, which is why a
// machine can arrive with no way to take a checkpoint at all.
//
// This changes a Windows setting rather than applying a tweak, so it is its own
// button and never runs as a side effect of anything else. It only ever turns
// the protection on.
bool enable_system_protection();

// True when System Restore could work at all: VSS is not disabled. Cheap
// enough to ask before offering the button.
[[nodiscard]] bool system_restore_available();

// Kept for callers that only need to know whether it worked.
bool create_system_restore_point();

// Launches Windows' own System Restore wizard (rstrui.exe) so the user picks
// a restore point and confirms inside Windows' own UI — this never rolls
// the system back on its own.
void open_system_restore_wizard();

// Opens the shop's Discord invite in the default browser.
void open_discord();
} // namespace szk::backend
