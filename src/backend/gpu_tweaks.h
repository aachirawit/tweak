#pragma once

namespace szk::backend
{
// Mirrors the NVIDIA scripts under
// github.com/HickerDicker/SapphireOS/tree/main/src/PostInstall/GPU/Nvidia.
// Each finds the NVIDIA GPU's own registry Class subkey dynamically (via
// WMI + the device's "Driver" property) rather than assuming it is always
// \0000, so these are safe to run on any machine — including ones with no
// NVIDIA GPU at all, where they simply report "no NVIDIA GPU found".

// Cheap presence checks (a single WMI query each) so the UI can filter the
// "All Settings" view to the vendor actually installed, instead of showing
// NVIDIA rows on an AMD-only machine or vice versa.
bool has_nvidia_gpu();
bool has_amd_gpu();

bool nvidia_disable_hdcp();
bool nvidia_disable_telemetry(); // global keys, no GPU lookup needed
bool nvidia_disable_ecc();       // needs nvidia-smi; conditional on ECC-capable hardware
bool nvidia_unrestricted_pstate();
bool nvidia_unrestricted_clocks(); // needs nvidia-smi; conditional on supported hardware

// NVIDIA Profile Inspector (by Orbmu2k) is embedded as a resource so the
// app stays a single exe. This extracts it (and its Reference.xml setting
// database) to a temp folder and launches it.
bool launch_nvidia_profile_inspector();

// Curated from github.com/HickerDicker/SapphireOS/tree/main/src/PostInstall/GPU/AMD's
// "AMD Dwords by imribiy.bat" — deliberately excludes that script's
// HKLM\...\Class\{...}\0000\... block: those keys are hardcoded to device
// instance \0000 (wrong on any machine where the AMD GPU isn't the first
// Display-class instance — a laptop with a second/integrated GPU, for
// example), the values are opaque undocumented driver-internal binary
// blobs, and one of them (PP_ThermalAutoThrottlingEnable=0) disables the
// GPU's thermal auto-throttling, which is a real overheating risk rather
// than a tuning tweak. Everything kept below writes to global (not
// per-device-instance) keys instead. Mpo_disable.reg from the same
// folder is already covered by disable_gpu_mpo() in os_tweaks.h — that
// key isn't GPU-vendor-specific.

// Radeon Software (Adrenalin) app: disables its own auto-update, the
// ReLive/Instant Replay background recorder (DVR), the in-app overlay
// browser/web content, first-run notification popups, and UI animations.
// All HKCU, all specific to the Adrenalin app itself.
bool amd_disable_adrenalin_bloat();

// Disables Radeon Chill (the driver's dynamic-framerate power saver) via
// the amdwddmg service's own config key — global, not tied to a specific
// GPU instance.
bool amd_disable_chill();

// Disables a handful of AMD background services SapphireOS's script also
// disables (Start=4): AMD Crash Defender Service, AMD External Events
// Utility, amdfendr/amdfendrmgr, amdlog.
bool amd_disable_background_services();

// Live status checks for the green/red dot next to each settings row —
// see the matching block in os_tweaks.h for what these mean.
bool check_nvidia_disable_hdcp();
bool check_nvidia_disable_telemetry();
bool check_nvidia_unrestricted_pstate();
bool check_amd_disable_chill();
bool check_amd_background_services();
} // namespace szk::backend
