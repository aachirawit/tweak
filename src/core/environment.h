#pragma once

#include <string>
#include <string_view>

namespace szk::environment
{
[[nodiscard]] std::string value(std::string_view key);
[[nodiscard]] std::wstring value(std::wstring_view key);
} // namespace szk::environment
