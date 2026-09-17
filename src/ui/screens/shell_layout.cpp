#include "ui/screens/shell.h"

#include "assets/avatars.h"
#include "assets/images.h"
#include "ui/foundation/icons.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/rounded_panel.h"

namespace szk::shell
{
ImVec2 animate_size(const ImVec2& target)
{
    static mo::spring w, h;
    static float last_scale = ui_runtime::scale;
    const float dt = ImGui::GetIO().DeltaTime;

    // The springs store physical pixels. Preserve their logical position and
    // velocity when Windows moves the window to a monitor with another DPI.
    if (last_scale > 0.f && ui_runtime::scale > 0.f && ui_runtime::scale != last_scale)
    {
        const float ratio = ui_runtime::scale / last_scale;
        if (w.seeded)
        {
            w.value *= ratio;
            w.velocity *= ratio;
        }
        if (h.seeded)
        {
            h.value *= ratio;
            h.velocity *= ratio;
        }
        last_scale = ui_runtime::scale;
    }

    const float x = w.to(target.x, mo::SPRING_LAYOUT, dt);
    const float y = h.to(target.y, mo::SPRING_LAYOUT, dt);

    return ImVec2(ImFloor(x + 0.5f), ImFloor(y + 0.5f));
}

// How much of c_background is laid back over a background image.
//
// What is left over the picture where nothing is written on it: the margins,
// the gaps between cards, the area below the last one. Every surface that does
// carry text lays its own ground over this first - cards and the sidebar at
// shell::card_scrim, the top chrome under the band drawn in shell_screen - so
// this value is free to be light. At 0.50 it was not: the page heading is
// written straight onto the plate, and over a pale part of a picture it
// measured 3.09:1 in dark mode, with its subtitle at 1.03:1.
constexpr float k_background_scrim = 0.28f;

ImRect plate()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const ImRect rect(window->Pos, window->Pos + window->Size);

    rounded_panel::draw(dl, rect.Min, rect.Max, c_background, round_px(rounding));

    // Optional background image: cover-fit, rounded to the plate, then scrimmed
    // back to the ground colour so every surface above it keeps its contrast.
    if (const images::texture* bg = images::background();
        bg != nullptr && bg->id != ImTextureID_Invalid && bg->width > 0 && bg->height > 0)
    {
        ImVec2 uv0, uv1;
        background_uv(rect, uv0, uv1);
        dl->AddImageRounded(bg->id, rect.Min, rect.Max, uv0, uv1, IM_COL32_WHITE,
                            round_px(rounding));
        dl->AddRectFilled(rect.Min, rect.Max,
                          mo::with_alpha(c_background, k_background_scrim), round_px(rounding));
    }

    return rect;
}

bool background_uv(const ImRect& area, ImVec2& uv0, ImVec2& uv1)
{
    const images::texture* bg = images::background();
    if (bg == nullptr || bg->id == ImTextureID_Invalid || bg->width <= 0 || bg->height <= 0)
        return false;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window == nullptr)
        return false;

    const ImRect plate_rect(window->Pos, window->Pos + window->Size);
    if (plate_rect.GetWidth() <= 0.f || plate_rect.GetHeight() <= 0.f)
        return false;

    // Cover-fit for the whole plate: crop the overflowing axis in UV space,
    // centred, so the picture fills the window without being stretched.
    const float plate_aspect = plate_rect.GetWidth() / plate_rect.GetHeight();
    const float image_aspect = (float)bg->width / (float)bg->height;

    ImVec2 full0(0.f, 0.f), full1(1.f, 1.f);
    if (image_aspect > plate_aspect)
    {
        const float keep = plate_aspect / image_aspect;
        full0.x = (1.f - keep) * 0.5f;
        full1.x = 1.f - full0.x;
    }
    else
    {
        const float keep = image_aspect / plate_aspect;
        full0.y = (1.f - keep) * 0.5f;
        full1.y = 1.f - full0.y;
    }

    // Then take the slice of that range the requested rect covers, so a surface
    // drawn anywhere on the plate lands on the part of the picture behind it.
    const ImVec2 t0((area.Min.x - plate_rect.Min.x) / plate_rect.GetWidth(),
                    (area.Min.y - plate_rect.Min.y) / plate_rect.GetHeight());
    const ImVec2 t1((area.Max.x - plate_rect.Min.x) / plate_rect.GetWidth(),
                    (area.Max.y - plate_rect.Min.y) / plate_rect.GetHeight());

    uv0 = ImVec2(full0.x + (full1.x - full0.x) * t0.x, full0.y + (full1.y - full0.y) * t0.y);
    uv1 = ImVec2(full0.x + (full1.x - full0.x) * t1.x, full0.y + (full1.y - full0.y) * t1.y);
    return true;
}

void brand_avatar(ImDrawList* dl, const ImVec2& top_left, float size, float alpha)
{
    const ImVec2 bottom_right(top_left.x + size, top_left.y + size);
    const float radius = round_dev(size * 0.28f);

    dl->AddRectFilled(top_left, bottom_right, mo::with_alpha(c_card_raised, alpha), radius);
    dl->AddRect(ImVec2(top_left.x + px(0.5f), top_left.y + px(0.5f)),
                ImVec2(bottom_right.x - px(0.5f), bottom_right.y - px(0.5f)),
                mo::with_alpha(c_border_strong, alpha), radius, px(1.f), ImDrawFlags_None);

    const float glyph = size * 0.62f;
    const float inset = (size - glyph) * 0.5f;

    brand_mark(dl, ImVec2(top_left.x + inset, top_left.y + inset), glyph, c_accent, alpha);
}

void brand_mark(ImDrawList* dl, const ImVec2& top_left, float size, ImU32 vector_tint, float alpha)
{
    const ImVec2 bottom_right(top_left.x + size, top_left.y + size);

    if (const ImTextureID logo = avatars::logo(0); logo != ImTextureID_Invalid)
    {
        dl->AddImageRounded(logo, top_left, bottom_right, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
                            mo::with_alpha(IM_COL32_WHITE, alpha), size * 0.22f);
        return;
    }

    icons::draw(icons::id::score_ring, dl, top_left, size, mo::with_alpha(vector_tint, alpha));
}
} // namespace szk::shell