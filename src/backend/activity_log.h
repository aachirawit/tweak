#pragma once

#include <string>
#include <vector>

namespace szk::backend
{
struct log_entry
{
    std::string title;
    std::string detail;
    std::string time_label;
};

void log(const std::string& title, const std::string& detail);
const std::vector<log_entry>& log_entries();
} // namespace szk::backend
