#pragma once

namespace solace::backend
{
// Each of these applies a curated, safe-on-any-machine batch of registry
// tweaks in one shot (mirrors SapphireOS/src/Regs/SapphireOS.reg, minus the
// entries that can break a machine or weaken security: Defender/UAC/Windows
// Update disabling, the legacy Photo Viewer file-association hijack, the
// noop.exe IFEO redirects, Print Spooler/Remote Desktop services, Kernel DMA
// Protection, System Restore, and the OEM branding block).
//
// A key/value whose target doesn't exist on a given machine is simply
// created inert — Windows never errors on that, so these are safe to run
// unconditionally.

bool apply_gaming_priority_tweaks(); // foreground priority, mouse polling,
                                     // process priority hints
bool apply_network_hardening();      // LLMNR off, anonymous access restricted
bool apply_privacy_telemetry_tweaks();
bool apply_ui_explorer_tweaks();
bool apply_debloat_services(); // disables ~30 rarely-used services
} // namespace solace::backend
