#include "assets/asset_io.h"

#include "core/environment.h"
#include "core/product_info.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <limits>

namespace szk::asset_io
{
namespace
{
std::filesystem::path executable_directory()
{
    auto current_directory = []
    {
        std::error_code error;
        return std::filesystem::current_path(error);
    };

    std::vector<wchar_t> value(512);
    for (;;)
    {
        const DWORD written =
            ::GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
        if (written == 0)
            return current_directory();
        if (written < value.size() - 1)
            return std::filesystem::path(value.data(), value.data() + written).parent_path();
        if (value.size() >= 32768)
            return current_directory();

        value.resize(value.size() * 2);
    }
}

bool is_image(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".jpg" || extension == L".jpeg" || extension == L".png" ||
           extension == L".bmp";
}

bool is_font(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".ttf" || extension == L".otf";
}
} // namespace

std::filesystem::path asset_directory(const wchar_t* name, const wchar_t* environment_key)
{
    if (const std::filesystem::path configured = szk::environment::value(environment_key);
        !configured.empty())
        return configured;

    // assets/brands/<brand>/<name> first, assets/<name> second. Two products
    // ship from this tree and most of what they load is the same file - the
    // fonts, the ReShade payload - so only what actually differs needs a folder
    // under brands/, and anything a brand does not override keeps working
    // without being copied.
    const std::filesystem::path executable = executable_directory();
    std::filesystem::path base = executable;
    for (int depth = 0; depth < 3; ++depth)
    {
        std::error_code error;

        const std::filesystem::path branded =
            base / L"assets" / L"brands" / product_info::asset_brand / name;
        if (std::filesystem::is_directory(branded, error))
            return branded;

        const std::filesystem::path shared = base / L"assets" / name;
        if (std::filesystem::is_directory(shared, error))
            return shared;

        base = base.parent_path();
    }

    return executable / L"assets" / name;
}

std::vector<std::filesystem::path> image_files(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error))
    {
        if (it->is_regular_file(error) && !error && is_image(it->path()))
            files.push_back(it->path());
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::filesystem::path> font_files(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error))
    {
        if (it->is_regular_file(error) && !error && is_font(it->path()))
            files.push_back(it->path());
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::vector<unsigned char> read_binary(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return {};

    const std::streamoff length = input.tellg();
    if (length <= 0 || length > (std::numeric_limits<int>::max)())
        return {};

    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length))
        return {};
    return bytes;
}

std::string stem_utf8(const std::filesystem::path& path)
{
    const std::u8string value = path.stem().u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}
} // namespace szk::asset_io
