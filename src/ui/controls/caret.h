#pragma once
#include "imgui.h"
#include "imgui_internal.h"

namespace szk
{
struct caret
{
    float alpha = 0.f;
    float offset = 0.f;
    float target = 0.f;
};

void draw_caret(ImDrawList* dl, caret& c, const ImRect& field, float height, bool active, ImU32 col,
                float alpha = 1.f);
} // namespace szk
