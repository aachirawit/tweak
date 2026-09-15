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

// Written from the background task as well as the UI thread - see task.h - so
// entries are guarded and read back as a copy. A reference into the vector
// would dangle the moment a job pushed an entry and it grew.
void log(const std::string& title, const std::string& detail);
[[nodiscard]] std::vector<log_entry> log_entries();
} // namespace szk::backend
