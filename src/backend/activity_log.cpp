#include "backend/activity_log.h"

#include <ctime>
#include <mutex>

namespace szk::backend
{
namespace
{
constexpr size_t k_max_entries = 200;

std::mutex& mutex()
{
    static std::mutex m;
    return m;
}

std::vector<log_entry>& entries_mut()
{
    static std::vector<log_entry> entries;
    return entries;
}

std::string time_now_label()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);

    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M", &local);
    return buf;
}
} // namespace

void log(const std::string& title, const std::string& detail)
{
    std::lock_guard<std::mutex> lock(mutex());

    std::vector<log_entry>& entries = entries_mut();
    entries.push_back(log_entry{title, detail, time_now_label()});
    if (entries.size() > k_max_entries)
        entries.erase(entries.begin());
}

std::vector<log_entry> log_entries()
{
    std::lock_guard<std::mutex> lock(mutex());
    return entries_mut();
}
} // namespace szk::backend
