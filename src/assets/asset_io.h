#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace szk::asset_io
{
std::filesystem::path asset_directory(const wchar_t* name, const wchar_t* environment_key);

std::vector<std::filesystem::path> image_files(const std::filesystem::path& directory);

// Font files in a directory, sorted by name. Used to pick up a bundled face
// (assets/fonts) in preference to whatever the system happens to have.
std::vector<std::filesystem::path> font_files(const std::filesystem::path& directory);

std::vector<unsigned char> read_binary(const std::filesystem::path& path);
std::string stem_utf8(const std::filesystem::path& path);
} // namespace szk::asset_io
