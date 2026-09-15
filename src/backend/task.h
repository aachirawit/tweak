#pragma once

#include <functional>

namespace szk::backend
{
// One piece of slow work at a time, off the UI thread.
//
// Applying a tweak is not fast. A restore point is SRSetRestorePointW, which
// takes seconds; two dozen of the tweaks shell out to netsh, powercfg or
// schtasks and wait up to ten seconds each; stopping a service waits on the
// service control manager. All of that used to run inside the draw call, so
// "Optimize now" with a handful of findings stopped the window redrawing long
// enough for Windows to grey it out and offer to close it - while the button
// underneath was showing a spinner that had stopped spinning.
//
// One slot rather than a pool: these jobs write to the registry and to the
// same files, and letting two run at once would be a way to make a mess of
// both. task_begin returns false while one is already running.
bool task_begin(std::function<void()> work);
[[nodiscard]] bool task_running();

// Waits for the running job before the process goes away. Called on shutdown:
// a job halfway through a registry write should finish it.
void task_shutdown();
} // namespace szk::backend
