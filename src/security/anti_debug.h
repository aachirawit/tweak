#pragma once

namespace szk::sec
{
// A single sweep of the anti-debug checks. Each returns true if it saw a sign
// of a debugger; the result is OR-ed. Cheap enough to call on a timer.
//
// Read the .cpp before relying on any of this. The short version: these are
// speed bumps. A researcher who knows what SZK is running (all of it is public
// technique) bypasses the lot in minutes with ScyllaHide or a few patches.
// They keep out the casual "attach x64dbg and poke around" attempt, and that
// is the whole of what a hand-rolled anti-debug buys.
//
// Do NOT wire this to anything destructive. exit() or corrupting state on a
// positive gives false-positive crashes on paying customers (some legit tools
// trip these) and gives a cracker a single, obvious branch to neuter. Prefer a
// quiet response: refuse the licence check, or degrade, far from the check
// itself so the cause and effect are not one patchable jump apart.
[[nodiscard]] bool debugger_present();

// The individual checks, exposed so a caller can log which one fired while
// tuning, or skip one that false-positives in their environment.
[[nodiscard]] bool check_peb_being_debugged();   // PEB->BeingDebugged
[[nodiscard]] bool check_peb_nt_global_flag();   // PEB->NtGlobalFlag heap flags
[[nodiscard]] bool check_debug_port();           // NtQueryInformationProcess ProcessDebugPort
[[nodiscard]] bool check_debug_object_handle();  // ...ProcessDebugObjectHandle
[[nodiscard]] bool check_hardware_breakpoints(); // Dr0-Dr3 debug registers
} // namespace szk::sec
