#pragma once
#include "imgui.h"

#include "ui/foundation/runtime.h"

namespace szk
{

// Every colour below is swapped by set_dark(); the initialisers are the dark
// values, so anything drawn before the first set_dark() call still reads.
//
// Neutrals carry a faint blue bias rather than sitting on pure grey, so they
// stay out of the way of the emerald accent instead of going warm beside it.
// c_card is the hover/panel fill and must contrast against c_background in
// both themes - lighter than the ground in dark, darker in light.
inline ImU32 c_background = IM_COL32(0x13, 0x14, 0x16, 0xFF);
inline ImU32 c_foreground = IM_COL32(0xF2, 0xF2, 0xF3, 0xFF);
inline ImU32 c_card = IM_COL32(0x1A, 0x1B, 0x1E, 0xFF);
// The raised surface (menus, popovers) is held one notch darker than an
// elevation-only palette would pick it, because secondary text sits on it: at
// #262930 muted text measured 4.33:1, just under AA. The light theme solves the
// same problem from the other side - lightening its raised fill to clear 4.5:1
// would have left it indistinguishable from c_card, so there the muted token is
// darkened instead and the fill keeps its elevation.
inline ImU32 c_card_raised = IM_COL32(0x23, 0x26, 0x2C, 0xFF);
// Secondary text stops here. A third, dimmer tier cannot meet WCAG AA 4.5:1 in
// the light theme (dimmer means lighter means less contrast, and forcing it past
// 4.5:1 lands on top of this token), so hierarchy below this level is carried by
// size and weight, never by a fainter colour.
inline ImU32 c_muted_foreground = IM_COL32(0x8A, 0x8C, 0x91, 0xFF);
inline ImU32 c_border = IM_COL32(0xFF, 0xFF, 0xFF, 0x12);
inline ImU32 c_border_strong = IM_COL32(0xFF, 0xFF, 0xFF, 0x21);
inline ImU32 c_primary = IM_COL32(0xF2, 0xF2, 0xF3, 0xFF);
inline ImU32 c_primary_foreground = IM_COL32(0x13, 0x14, 0x16, 0xFF);

// Measured system state - how the machine is doing, never decoration. The
// primary button stays monochrome so exactly one thing on screen is loudest.
inline ImU32 c_accent = IM_COL32(0x10, 0xB9, 0x81, 0xFF);
inline ImU32 c_accent_hi = IM_COL32(0x34, 0xD3, 0x99, 0xFF);

// Severity. These are no longer constants: a red that reads on #131416 is
// glaring on a light ground, so each theme gets its own.
//
// Each one is pushed past what the plain ground needs, because pills and toasts
// draw their label in the colour on a 13% wash of the same colour - roughly a
// full contrast point - so a value that only just clears 4.5:1 fails inside its
// own badge. #EE343B measured 4.56:1 on the ground but 4.08:1 in a pill.
inline ImU32 c_destructive = IM_COL32(0xF0, 0x4F, 0x55, 0xFF);
inline ImU32 c_success = IM_COL32(0x00, 0xBD, 0x6C, 0xFF);
inline ImU32 c_amber_500 = IM_COL32(0xF9, 0x9C, 0x00, 0xFF);
inline ImU32 c_amber_400 = IM_COL32(0xFC, 0xBB, 0x00, 0xFF);

inline ImU32 c_emerald_400 = IM_COL32(0x34, 0xD3, 0x99, 0xFF);
inline ImU32 c_emerald_500 = IM_COL32(0x10, 0xB9, 0x81, 0xFF);
inline ImU32 c_emerald_600 = IM_COL32(0x05, 0x96, 0x69, 0xFF);

void set_dark(bool dark);
bool is_dark();

inline constexpr float text_xs = 12.f, leading_xs = 16.f;
inline constexpr float text_sm = 14.f, leading_sm = 20.f;
inline constexpr float text_base = 16.f, leading_base = 24.f;
inline constexpr float text_xl = 20.f, leading_xl = 28.f;

inline constexpr float tracking_tight = -0.025f;

inline constexpr float sp_1 = 4.f;
inline constexpr float sp_1_5 = 6.f;
inline constexpr float sp_2 = 8.f;
inline constexpr float sp_3 = 12.f;
inline constexpr float sp_3_5 = 14.f;
inline constexpr float sp_4 = 16.f;
inline constexpr float sp_5 = 20.f;
inline constexpr float sp_6 = 24.f;
inline constexpr float sp_10 = 40.f;
inline constexpr float sp_11 = 44.f;
inline constexpr float sp_12 = 48.f;

inline constexpr float max_w_sm = 384.f;
inline constexpr float rounded_3xl = 24.f;
inline constexpr float rounded_md = 6.f;

inline float px(float v)
{
    return v * ui_runtime::scale;
}
inline ImVec2 px(float x, float y)
{
    return ImVec2(x * ui_runtime::scale, y * ui_runtime::scale);
}

inline float line_top(ImFont* f, float line_height)
{
    return (line_height - (f ? f->LegacySize : 0.f)) * 0.5f;
}

ImFont* font_regular(float size);
ImFont* font_medium(float size);
ImFont* font_semibold(float size);
} // namespace szk
