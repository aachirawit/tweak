#pragma once

namespace solace::backend
{
struct system_snapshot
{
    float cpu_percent = 0.f;
    float cpu_temp_c = 0.f;
    bool cpu_temp_available = false;

    float ram_used_gb = 0.f;
    float ram_total_gb = 0.f;
    float ram_percent = 0.f;

    float disk_used_gb = 0.f;
    float disk_total_gb = 0.f;
    float disk_percent = 0.f;

    int ping_ms = -1;
};

// Starts the background ping thread. Safe to call more than once.
void system_monitor_init();

// Cheap; call every frame or on a timer. CPU/RAM/disk are synchronous
// syscalls, ping is read from a background thread (never blocks).
system_snapshot system_monitor_poll();

struct machine_info
{
    char hostname[64] = {};
    int os_major = 0;
    int os_minor = 0;
    int os_build = 0;
    int logical_processors = 0;
    unsigned long long uptime_seconds = 0;
};

// A handful of GetComputerNameA/GetSystemInfo/RtlGetVersion/GetTickCount64
// calls; cheap enough to call every frame, but static for the process
// lifetime other than uptime so callers may cache it.
machine_info system_monitor_machine_info();
} // namespace solace::backend
