#include "backend/system_monitor.h"

#include <atomic>
#include <thread>

// winsock2.h has to be included before windows.h: windows.h drags in the
// legacy winsock.h, which redefines everything winsock2.h declares. Keep it in
// a block of its own - .clang-format sorts within a block, and "windows.h"
// sorts ahead of "winsock2.h", which is exactly the broken order.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <iphlpapi.h>

// icmpapi.h uses IPAddr and PIP_OPTION_INFORMATION from iphlpapi.h without
// including it, and "icmpapi.h" sorts ahead of "iphlpapi.h" - so it needs a
// block of its own to stay underneath.
#include <icmpapi.h>

#include <pdh.h>
#include <winternl.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace szk::backend
{
namespace
{
PDH_HQUERY g_query = nullptr;
PDH_HCOUNTER g_cpu_counter = nullptr;
bool g_cpu_ready = false;

void ensure_cpu_counter()
{
    if (g_query)
        return;

    if (PdhOpenQueryW(nullptr, 0, &g_query) != ERROR_SUCCESS)
        return;

    PdhAddEnglishCounterW(g_query, L"\\Processor(_Total)\\% Processor Time", 0, &g_cpu_counter);
    PdhCollectQueryData(g_query);
}

float read_cpu_percent()
{
    ensure_cpu_counter();
    if (!g_query || !g_cpu_counter)
        return 0.f;

    if (PdhCollectQueryData(g_query) != ERROR_SUCCESS)
        return 0.f;

    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(g_cpu_counter, PDH_FMT_DOUBLE, nullptr, &value) !=
        ERROR_SUCCESS)
        return 0.f;

    g_cpu_ready = true;
    return static_cast<float>(value.doubleValue);
}

std::atomic<int> g_ping_ms{-1};
std::atomic<bool> g_ping_thread_started{false};

// The FiveM/GTA V process typically pins the default route to the internet,
// so a well-known public resolver is a reasonable proxy for "is the network
// path healthy" without depending on any particular game server.
constexpr const char* k_ping_target = "1.1.1.1";

void ping_thread_main()
{
    HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE)
        return;

    char send_data[32] = "aspas-settings-ping";
    unsigned char reply_buf[sizeof(ICMP_ECHO_REPLY) + sizeof(send_data) + 8];
    IN_ADDR dest_addr{};
    ::inet_pton(AF_INET, k_ping_target, &dest_addr);
    const IPAddr dest = dest_addr.S_un.S_addr;

    for (;;)
    {
        const DWORD replies = IcmpSendEcho(icmp, dest, send_data, sizeof(send_data), nullptr,
                                           reply_buf, sizeof(reply_buf), 800);
        if (replies > 0)
        {
            const auto* reply = reinterpret_cast<PICMP_ECHO_REPLY>(reply_buf);
            g_ping_ms.store(reply->Status == IP_SUCCESS ? static_cast<int>(reply->RoundTripTime)
                                                        : -1);
        }
        else
        {
            g_ping_ms.store(-1);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}
} // namespace

void system_monitor_init()
{
    ensure_cpu_counter();

    bool expected = false;
    if (g_ping_thread_started.compare_exchange_strong(expected, true))
        std::thread(ping_thread_main).detach();
}

system_snapshot system_monitor_poll()
{
    system_snapshot snap;

    snap.cpu_percent = read_cpu_percent();

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (::GlobalMemoryStatusEx(&mem))
    {
        constexpr double k_gb = 1024.0 * 1024.0 * 1024.0;
        snap.ram_total_gb = static_cast<float>(static_cast<double>(mem.ullTotalPhys) / k_gb);
        const double used = static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys);
        snap.ram_used_gb = static_cast<float>(used / k_gb);
        snap.ram_percent = static_cast<float>(mem.dwMemoryLoad);
    }

    ULARGE_INTEGER free_bytes{}, total_bytes{}, total_free_bytes{};
    if (::GetDiskFreeSpaceExW(L"C:\\", &free_bytes, &total_bytes, &total_free_bytes))
    {
        constexpr double k_gb = 1024.0 * 1024.0 * 1024.0;
        const double total = static_cast<double>(total_bytes.QuadPart);
        const double free_space = static_cast<double>(total_free_bytes.QuadPart);
        snap.disk_total_gb = static_cast<float>(total / k_gb);
        snap.disk_used_gb = static_cast<float>((total - free_space) / k_gb);
        snap.disk_percent =
            total > 0.0 ? static_cast<float>((total - free_space) / total * 100.0) : 0.f;
    }

    snap.ping_ms = g_ping_ms.load();
    snap.cpu_temp_available = false;

    return snap;
}

machine_info system_monitor_machine_info()
{
    machine_info info;

    DWORD name_len = sizeof(info.hostname);
    ::GetComputerNameA(info.hostname, &name_len);

    SYSTEM_INFO sys_info{};
    ::GetSystemInfo(&sys_info);
    info.logical_processors = static_cast<int>(sys_info.dwNumberOfProcessors);

    info.uptime_seconds = ::GetTickCount64() / 1000ULL;

    using RtlGetVersionFn = LONG(NTAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll"))
    {
        if (auto* rtl_get_version =
                reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion")))
        {
            RTL_OSVERSIONINFOW version{};
            version.dwOSVersionInfoSize = sizeof(version);
            if (rtl_get_version(&version) == 0)
            {
                info.os_major = static_cast<int>(version.dwMajorVersion);
                info.os_minor = static_cast<int>(version.dwMinorVersion);
                info.os_build = static_cast<int>(version.dwBuildNumber);
            }
        }
    }

    return info;
}
} // namespace szk::backend
