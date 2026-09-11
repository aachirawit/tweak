#include "ui/foundation/theme.h"

#include "generated/fonts/geist_data.h"
#include "ui/foundation/typography/font_cache.h"

namespace szk
{
ImFont* font_regular(float size)
{
    return fonts.get(geist_regular, size);
}

ImFont* font_medium(float size)
{
    return fonts.get(geist_medium, size);
}

ImFont* font_semibold(float size)
{
    return fonts.get(geist_semibold, size);
}

namespace
{
struct palette
{
    ImU32 background, foreground, card, card_raised;
    ImU32 muted_foreground;
    ImU32 border, border_strong;

    ImU32 accent, accent_hi;
    ImU32 destructive, success, amber_500, amber_400;
    ImU32 emerald_400, emerald_500, emerald_600;
};

// Light keeps the dark set's relationships rather than inverting its values:
// c_card stays the fill that separates from the ground (darker here, lighter
// in dark), and every accent is darkened until it holds its own on white.
//
// Each functional colour is darkened past what it needs on the plain ground,
// because the pill and toast patterns draw label text in the colour on a 13%
// wash OF THAT SAME COLOUR. That wash pulls the background toward the text and
// costs roughly a full contrast point, so a value that only just clears 4.5:1
// on white fails inside its own badge. These clear 4.5:1 on the wash, which
// makes them comfortable everywhere else.
constexpr palette k_light{
    IM_COL32(0xFB, 0xFB, 0xFC, 0xFF), IM_COL32(0x17, 0x18, 0x1B, 0xFF),
    IM_COL32(0xF1, 0xF2, 0xF4, 0xFF), IM_COL32(0xE4, 0xE6, 0xE9, 0xFF),
    IM_COL32(0x65, 0x67, 0x6D, 0xFF),
    IM_COL32(0x17, 0x18, 0x1B, 0x14), IM_COL32(0x17, 0x18, 0x1B, 0x26),

    IM_COL32(0x04, 0x75, 0x55, 0xFF), IM_COL32(0x06, 0x5F, 0x46, 0xFF),
    IM_COL32(0xC5, 0x1E, 0x27, 0xFF), IM_COL32(0x04, 0x75, 0x55, 0xFF),
    IM_COL32(0xA7, 0x4D, 0x08, 0xFF), IM_COL32(0x9F, 0x53, 0x08, 0xFF),
    IM_COL32(0x05, 0x96, 0x69, 0xFF), IM_COL32(0x04, 0x78, 0x57, 0xFF),
    IM_COL32(0x06, 0x5F, 0x46, 0xFF),
};

constexpr palette k_dark{
    IM_COL32(0x13, 0x14, 0x16, 0xFF), IM_COL32(0xF2, 0xF2, 0xF3, 0xFF),
    IM_COL32(0x1A, 0x1B, 0x1E, 0xFF), IM_COL32(0x23, 0x26, 0x2C, 0xFF),
    IM_COL32(0x8A, 0x8C, 0x91, 0xFF),
    IM_COL32(0xFF, 0xFF, 0xFF, 0x12), IM_COL32(0xFF, 0xFF, 0xFF, 0x21),

    IM_COL32(0x10, 0xB9, 0x81, 0xFF), IM_COL32(0x34, 0xD3, 0x99, 0xFF),
    IM_COL32(0xF0, 0x4F, 0x55, 0xFF), IM_COL32(0x00, 0xBD, 0x6C, 0xFF),
    IM_COL32(0xF9, 0x9C, 0x00, 0xFF), IM_COL32(0xFC, 0xBB, 0x00, 0xFF),
    IM_COL32(0x34, 0xD3, 0x99, 0xFF), IM_COL32(0x10, 0xB9, 0x81, 0xFF),
    IM_COL32(0x05, 0x96, 0x69, 0xFF),
};

bool g_dark = true;
} // namespace

bool is_dark()
{
    return g_dark;
}

void set_dark(bool dark)
{
    g_dark = dark;
    const palette& p = dark ? k_dark : k_light;

    c_background = p.background;
    c_foreground = p.foreground;
    c_card = p.card;
    c_card_raised = p.card_raised;
    c_muted_foreground = p.muted_foreground;
    c_border = p.border;
    c_border_strong = p.border_strong;

    c_primary = p.foreground;
    c_primary_foreground = p.background;

    c_accent = p.accent;
    c_accent_hi = p.accent_hi;

    c_destructive = p.destructive;
    c_success = p.success;
    c_amber_500 = p.amber_500;
    c_amber_400 = p.amber_400;

    c_emerald_400 = p.emerald_400;
    c_emerald_500 = p.emerald_500;
    c_emerald_600 = p.emerald_600;
}
} // namespace szk
