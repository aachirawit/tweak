#pragma once

#include "imgui.h"
#include "imgui_internal.h"

namespace szk
{
namespace shell
{
inline constexpr float width = 1120.f;
inline constexpr float height = 720.f;
inline constexpr float rounding = 16.f;

ImVec2 animate_size(const ImVec2& target);
ImRect plate();

// UV sub-range of the background image covering a screen rect, using the same
// cover-fit the plate uses. A surface that paints this instead of a flat colour
// shows the part of the picture that is behind it, in register with the rest,
// so the window reads as one image rather than a picture with panels laid over
// it. Returns false when no background image is loaded.
bool background_uv(const ImRect& area, ImVec2& uv0, ImVec2& uv1);

// The product mark in its rounded tile, as the sidebar header, the account chip,
// the profile card and the account menu all draw it. One helper because four
// copies of the same tile drifted apart the moment a logo image was added and
// only one of them learned about it: it uses the image from assets/logos when
// there is one and the built-in vector mark when there is not.
void brand_avatar(ImDrawList* dl, const ImVec2& top_left, float size, float alpha);

// How much of the ground colour a surface must lay back over the image to keep
// the text on it above WCAG AA in the worst case - a white area of the picture.
//
// 0.94 is the floor for the barest surface, the sidebar, which lays only this
// scrim over the image: c_muted_foreground measures 4.73:1 there, and 0.92
// measures 4.45:1, under AA. A content card adds its own c_card tint on top and
// so comes out further ahead at 4.94:1. One value covers both rather than each
// surface carrying a number of its own that could drift.
inline constexpr float card_scrim = 0.94f;
} // namespace shell

bool menu_screen(float alpha);
} // namespace szk
