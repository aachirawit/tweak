#include "ui/foundation/typography/font_cache.h"
#include "ui/foundation/runtime.h"
#include "assets/asset_io.h"
#include "generated/fonts/geist_data.h"
#include "ui/foundation/typography/kerning.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>

#include <windows.h>

#ifdef IMGUI_ENABLE_FREETYPE
#include "imgui_freetype.h"
#endif

namespace
{

// Thai glyphs, merged in on top of Geist, which has no Thai coverage of its own
// (without a fallback Thai text renders as boxes).
//
// A font shipped in assets/fonts wins, because a bundled face is the only way
// every machine renders the app identically - the system fonts differ by
// Windows version and can be replaced. Failing that the system's Thai UI font
// is used: Leelawadee UI on anything since 8.1, Tahoma on older installs. With
// nothing found, nothing is merged and Thai renders as it did before, so this
// can never stop the app from starting.
std::vector<unsigned char> read_file(const std::wstring& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
}

bool name_looks_bold(const std::filesystem::path& file)
{
    std::wstring stem = file.stem().wstring();
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return stem.find(L"bold") != std::wstring::npos || stem.find(L"_bd") != std::wstring::npos;
}

struct thai_faces
{
    std::vector<unsigned char> regular;
    std::vector<unsigned char> bold;
};

// Two weights, because the interface uses three and Thai rendered at one of them
// looks wrong beside the others: a semibold English heading with regular-weight
// Thai in it reads as a mistake. A bundled file whose name says bold fills the
// bold slot, the first of the rest fills the regular slot, and either slot left
// empty falls back to the other.
const thai_faces& thai()
{
    static const thai_faces faces = []
    {
        thai_faces out;

        // Bundled first. asset_io resolves assets/ next to the executable or up
        // to three directories above it, the same search images and slides use.
        for (const std::filesystem::path& file :
             szk::asset_io::font_files(szk::asset_io::asset_directory(L"fonts", L"FONTS")))
        {
            std::vector<unsigned char>& slot = name_looks_bold(file) ? out.bold : out.regular;
            if (slot.empty())
                slot = read_file(file.wstring());
        }

        if (out.regular.empty() && out.bold.empty())
        {
            wchar_t windows_dir[MAX_PATH] = {};
            if (::GetWindowsDirectoryW(windows_dir, MAX_PATH) != 0)
            {
                for (const wchar_t* file : {L"/Fonts/LeelawUI.ttf", L"/Fonts/tahoma.ttf"})
                {
                    out.regular = read_file(std::wstring(windows_dir) + file);
                    if (!out.regular.empty())
                        break;
                }
            }
        }

        if (out.regular.empty())
            out.regular = out.bold;
        if (out.bold.empty())
            out.bold = out.regular;
        return out;
    }();
    return faces;
}

float ttf_em_scale(const std::vector<unsigned char>& blob)
{
    auto u16 = [&](size_t at) -> unsigned int
    { return at + 1 < blob.size() ? (unsigned int)((blob[at] << 8) | blob[at + 1]) : 0u; };
    auto s16 = [&](size_t at) -> int
    {
        const int v = (int)u16(at);
        return v >= 0x8000 ? v - 0x10000 : v;
    };
    auto u32 = [&](size_t at) -> unsigned int
    {
        return at + 3 < blob.size()
                   ? ((unsigned int)blob[at] << 24) | ((unsigned int)blob[at + 1] << 16) |
                         ((unsigned int)blob[at + 2] << 8) | blob[at + 3]
                   : 0u;
    };

    if (blob.size() < 12)
        return 1.f;

    size_t head = 0, hhea = 0;
    const unsigned int table_count = u16(4);
    for (unsigned int i = 0; i < table_count; i++)
    {
        const size_t rec = 12 + (size_t)i * 16;
        if (rec + 16 > blob.size())
            break;

        const char tag[5] = {(char)blob[rec], (char)blob[rec + 1], (char)blob[rec + 2],
                             (char)blob[rec + 3], 0};
        if (strcmp(tag, "head") == 0)
            head = u32(rec + 8);
        else if (strcmp(tag, "hhea") == 0)
            hhea = u32(rec + 8);
    }

    if (head == 0 || hhea == 0)
        return 1.f;

    const float units_per_em = (float)u16(head + 18);
    const float ascent = (float)s16(hhea + 4);
    const float descent = (float)s16(hhea + 6);

    if (units_per_em <= 0.f || ascent - descent <= 0.f)
        return 1.f;

    return (ascent - descent) / units_per_em;
}
} // namespace

namespace szk
{
void font_cache::update()
{
    if (!szk::ui_runtime::fonts_dirty)
        return;

    ImGui::GetIO().Fonts->Clear();

    const std::vector<font_entry> previous = data;
    data.clear();
    for (const font_entry& entry : previous)
        add(*entry.source, entry.size);

    szk::ui_runtime::fonts_dirty = false;
}

ImFont* font_cache::get(const std::vector<unsigned char>& family, float size)
{
    for (const font_entry& entry : data)
        if (entry.source == &family && entry.size == size)
            return entry.font;

    return add(family, size);
}

static float kerning_hook(ImFont* f, float size, unsigned int c_prev, unsigned int c, void*)
{
    const std::vector<unsigned char>* src = fonts.source_of(f);
    if (src == nullptr)
        return 0.f;

    const szk::kerning::data& kd = szk::kerning::get(*src);
    const float v = szk::kerning::pair(kd, c_prev, c) * size / szk::kerning::layout_units(kd);
    return v > 0.f ? floorf(v + 0.5f) : 0.f;
}

void font_cache::install_kerning()
{
    ImGui::GetIO().Fonts->KerningFunc = kerning_hook;
}

const std::vector<unsigned char>* font_cache::source_of(ImFont* f) const
{
    for (const font_entry& entry : data)
        if (entry.font == f)
            return entry.source;
    return nullptr;
}

ImFont* font_cache::add(const std::vector<unsigned char>& family, float size)
{
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;

#ifdef IMGUI_ENABLE_FREETYPE

    cfg.FontLoaderFlags = 0;
#endif

    const float pixels = floorf(size * ttf_em_scale(family) * szk::ui_runtime::scale + 0.5f);
    ImFont* result = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(family.data()), (int)family.size(), pixels, &cfg);

    // Merge Thai on top of the Latin face at the same size. The range is Thai
    // only: GetGlyphRangesThai() would also pull in Latin, and every Latin glyph
    // is already here from the face above.
    // Geist semibold takes the bold Thai; regular and medium take the regular,
    // since the bundled family has no weight between them.
    const thai_faces& faces = thai();
    const std::vector<unsigned char>& thai_blob =
        (&family == &szk::geist_semibold) ? faces.bold : faces.regular;
    if (!thai_blob.empty())
    {
        static const ImWchar thai_range[] = {0x0E00, 0x0E7F, 0};

        ImFontConfig merge;
        merge.FontDataOwnedByAtlas = false;
        merge.MergeMode = true;
#ifdef IMGUI_ENABLE_FREETYPE
        merge.FontLoaderFlags = 0;
#endif
        ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(thai_blob.data()),
                                                   (int)thai_blob.size(), pixels, &merge,
                                                   thai_range);
    }

    data.push_back({&family, size, result});
    return result;
}
} // namespace szk
