#pragma once

namespace szk::backend
{
// Every function applies the tweak when enable is true and restores the
// Windows default when enable is false. All require the process to already
// be running elevated (the app manifest requests this).

bool set_network_autotuning(bool enable);
bool set_background_services_disabled(bool enable); // SysMain + WSearch
bool set_usb_selective_suspend_disabled(bool enable);
bool set_low_latency_tcp(bool enable); // TcpNoDelay / TcpAckFrequency, all NICs
bool set_bcd_tweaks(bool enable);      // disabledynamictick / useplatformclock

// One-shot appliers (no on/off state — matches the source scripts, which are
// apply-only). Each is independently real and verifiable.
bool disable_action_center();           // HKCU policy key
bool set_classic_alt_tab();             // HKCU AltTabSettings
bool disable_gpu_mpo();                 // HKLM Dwm\OverlayTestMode
bool apply_sapphire_network_defaults(); // the netsh bundle from
                                        // SapphireOS/PostInstall/Others/Network

// Mirrors "Run this if you had to install a network driver.bat": per-NIC
// advanced-property registry tweaks (power saving off, offloads on),
// disables a set of unneeded adapter bindings, then re-applies the same
// netsh bundle as apply_sapphire_network_defaults().
bool apply_network_driver_tweaks();

// Marks FiveM's traffic with DSCP 46 (Expedited Forwarding — the class
// routers/ISP QoS treat as real-time priority) for both the launcher
// (FiveM.exe) and the actual game subprocess, whose filename FiveM
// versions as "FiveM_b<build>_GTAProcess.exe" — this looks it up under
// %LOCALAPPDATA%\FiveM\...\cache\subprocess each time rather than assuming
// a fixed name. Throttle Rate is set to "-1" (the policy engine's own
// unlimited sentinel) rather than a real bits-per-second cap — some
// source scripts for this same policy use 10240/20480 there, which is a
// bandwidth cap that would throttle FiveM's own traffic to ~1-2 KB/s.
bool apply_fivem_qos_priority();

// Deletes %LOCALAPPDATA%\FiveM\FiveM.app\data\{cache,server-cache,
// server-cache-priv} right now (FiveM rebuilds them on next launch — these
// are the folders community FiveM troubleshooting guides say to clear for
// stale/corrupt streamed assets), then registers a Scheduled Task
// ("SZKFiveMCacheClear") that repeats the same deletion at every logon,
// since a process can't hook "FiveM is about to start" without a
// background service.
bool set_fivem_cache_autoclear(bool enable);

// Sets PerfOptions\CpuPriorityClass=3 (High, per Microsoft's IFEO
// documentation) under Image File Execution Options for both FiveM.exe and
// the actual game subprocess (found the same way as
// apply_fivem_qos_priority — its filename is versioned per FiveM build).
// This is the same documented IFEO key process-priority utilities rely on
// to make Windows launch a named .exe at a fixed priority every time,
// without needing to hook the process at launch ourselves.
bool set_fivem_high_cpu_priority(bool enable);

// One-shot appliers, same style as disable_action_center() etc above.
bool disable_pcie_aspm();           // powercfg: PCI Express Link State Power
                                    // Management ASPM = Off, active scheme
bool disable_game_bar_dvr();        // GameConfigStore/GameDVR/policy keys
bool set_mouse_raw_1to1();          // Control Panel\Mouse: acceleration off
bool set_keyboard_rapid_response(); // Control Panel\Keyboard: delay/repeat
bool disable_network_throttling();  // Multimedia\SystemProfile
                                    // NetworkThrottlingIndex = unlimited

// Network Optimization "tools" — instant one-shot actions (ipconfig/netsh),
// no persistent on/off state; nothing to revert.
bool flush_dns_cache();       // ipconfig /flushdns
bool renew_ip_lease();        // ipconfig /release then /renew —
                              // drops the network connection for a
                              // few seconds while it runs
bool clear_arp_cache();       // netsh interface ip delete arpcache
bool reset_winsock_catalog(); // netsh winsock reset — needs a reboot
                              // to actually take effect
bool register_dns_record();   // ipconfig /registerdns

// More one-shot appliers, curated against an existing similar tool's
// value list (BoostPC) — kept separate from that tool's items that
// duplicate or directly conflict with tweaks this app already ships
// (see the commit/chat notes for which ones and why).
bool reset_filter_keys_timing();                   // Accessibility\Keyboard Response delay/repeat
bool disable_hardware_gpu_scheduling(bool enable); // GraphicsDrivers\HwSchMode
bool set_visual_effects_performance();             // "Adjust for best performance"
bool switch_power_plan_high_performance(bool enable); // powercfg — the
                                                      // built-in High performance scheme,
                                                      // hidden by default but still present
                                                      // and directly activatable by GUID
bool remove_startup_delay();                          // Explorer\Serialize StartupDelayInMSec=0
bool disable_ntfs_last_access();                      // FileSystem NtfsDisableLastAccessUpdate
bool disable_advertising_id();                        // HKCU AdvertisingInfo
bool disable_tips_and_suggestions();                  // ContentDeliveryManager silent-install/ads

// FiveM-specific, alongside the FiveM functions above. Both of these
// target the actual game subprocess (found the same way as
// apply_fivem_qos_priority/set_fivem_high_cpu_priority), not FiveM.exe —
// FiveM.exe is only the launcher/CEF host, so it doesn't own the D3D11
// device or the game window these two keys apply to.
bool fivem_gpu_high_performance();             // Windows Graphics Settings' own
                                               // per-app GPU preference (the same
                                               // UserGpuPreferences key Settings >
                                               // Display > Graphics writes), so a
                                               // laptop's dGPU is always used
bool fivem_disable_fullscreen_optimizations(); // AppCompatFlags\Layers —
                                               // true exclusive fullscreen instead
                                               // of Windows' borderless "Fullscreen
                                               // Optimizations", which can add
                                               // input latency
bool fivem_defender_exclusion(bool enable);    // Add-MpPreference/Remove-MpPreference
                                               // exclusion for the FiveM install folder

// Live status checks for the green/red dot next to each settings row —
// each reads back the actual registry/service/scheduled-task state and
// reports whether it currently matches what the matching apply function
// above sets. Only exists for tweaks with a small, fixed set of keys to
// read; tweaks applied via netsh/bcdedit/powercfg output, nvidia-smi, or a
// PowerShell query, and one-shot actions with no persistent state (DNS
// flush, cache/temp cleanup, etc.), don't get one — there's nothing
// cheap and reliable to read back.
bool check_disable_action_center();
bool check_classic_alt_tab();
bool check_disable_gpu_mpo();
bool check_background_services_disabled(); // SysMain + WSearch
bool check_usb_selective_suspend_disabled();
bool check_fivem_qos_priority();
bool check_fivem_cache_autoclear(); // the SZKFiveMCacheClear task exists
bool check_fivem_high_cpu_priority();
bool check_game_bar_dvr();
bool check_mouse_raw_1to1();
bool check_keyboard_rapid_response();
bool check_network_throttling();
bool check_filter_keys_timing();
bool check_hardware_gpu_scheduling();
bool check_visual_effects_performance();
bool check_startup_delay();
bool check_ntfs_last_access();
bool check_advertising_id();
bool check_tips_and_suggestions();
bool check_fivem_gpu_high_performance();
bool check_fivem_disable_fullscreen_optimizations();
} // namespace szk::backend
