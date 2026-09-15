#include "backend/task.h"

#include <windows.h>

#include <objbase.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

namespace szk::backend
{
namespace
{
std::mutex g_mutex;
std::thread g_worker;
std::atomic<bool> g_running{false};

// Joins a worker that has already finished. Holding the mutex while joining a
// *running* worker would block the UI thread, which is the one thing this whole
// file exists to avoid, so the caller checks g_running first.
void reap()
{
    if (g_worker.joinable())
        g_worker.join();
}
} // namespace

bool task_begin(std::function<void()> work)
{
    if (!work)
        return false;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running.load(std::memory_order_acquire))
        return false;

    reap();

    g_running.store(true, std::memory_order_release);
    g_worker = std::thread(
        [job = std::move(work)]() mutable
        {
            // The restore point goes through SRSetRestorePointW, which uses COM
            // underneath, and a worker thread starts without it. Initialised
            // here rather than in each job so no job has to remember.
            const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

            job();

            if (SUCCEEDED(com))
                ::CoUninitialize();

            // Released last, so a UI thread that sees this false also sees
            // everything the job wrote before it.
            g_running.store(false, std::memory_order_release);
        });

    return true;
}

bool task_running()
{
    return g_running.load(std::memory_order_acquire);
}

void task_shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    reap();
}
} // namespace szk::backend
