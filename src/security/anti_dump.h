#pragma once

namespace szk::sec
{
// Anti-dump: erase the PE headers in the running image so a naive memory dump
// of SZK.exe produces a file that will not load or open cleanly in a PE tool.
//
// ─── Read this before enabling it ───────────────────────────────────────────
//
// This is the most aggressive and the least worthwhile item in the whole
// security set, and it is OFF by default. Turn it on only deliberately.
//
// What it does: zeroes the "MZ"/"PE" magic and the section table in the mapped
// image after load. A one-click process dumper then grabs headerless memory.
//
// Why it barely helps: a competent dumper reconstructs the headers from the
// section layout (Scylla does this automatically), so it stops the lazy tool
// and nobody else - the same audience the anti-debug already stops.
//
// What it costs you, for real:
//   - Crash reporting breaks. Windows Error Reporting and any minidump you
//     collect walk these headers; corrupt them and you lose your own crash
//     telemetry on paying customers' machines.
//   - Antivirus heuristics flag self-modifying image headers. Self-corrupting
//     the PE header is a known malware behaviour, and a false positive on a
//     paid tool is worse than a pirate.
//   - Anything that later walks the module (some overlays, some AV, .NET
//     interop, certain injected legit software) can fault.
//
// The honest recommendation: leave this off, and if you genuinely need
// anti-dump, buy a commercial protector (VMProtect/Themida) that does it with
// the OS integration to not break crash reporting. Hand-rolled, it trades your
// own observability for a speed bump.

// Zeroes the DOS and NT header magic plus the section headers of the current
// module. No-op unless SZK_ENABLE_ANTI_DUMP is defined at build time. Returns
// true if it ran and succeeded.
bool corrupt_own_pe_headers();
} // namespace szk::sec
