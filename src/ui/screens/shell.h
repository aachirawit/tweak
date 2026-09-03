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
} // namespace shell

bool menu_screen(float alpha);
} // namespace szk
