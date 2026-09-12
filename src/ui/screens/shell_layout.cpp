#include "ui/screens/shell.h"

#include "assets/images.h"
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
// The surfaces that carry text - content cards and the sidebar - are opaque, so
// they keep their own ground and the image cannot reach the text inside them.
// That is what allows a scrim this light: the only text it still has to protect
// is what is drawn straight onto the plate, the header row and the footer strip.
//
// 0.50 is matched to the artwork in assets/background, which is near-black
// except for display type in the middle of the frame - the cards cover that
// band, and the header and footer sit over the dark edges. It is not a value
// that is safe for any image: c_muted_foreground needs a ground no brighter
// than 0.019 relative luminance to hold WCAG AA 4.5:1, and a white area only
// falls to that at 0.92 (0.90 measures 4.23:1, 0.88 measures 3.96:1). Swap the
// image for a bright one and this has to come up with it - see
// assets/background/README.md for the value per image type.
constexpr float k_background_scrim = 0.50f;

ImRect plate()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const ImRect rect(window->Pos, window->Pos + window->Size);

    rounded_panel::draw(dl, rect.Min, rect.Max, c_background, px(rounding));

    // Optional background image: cover-fit, rounded to the plate, then scrimmed
    // back to the ground colour so every surface above it keeps its contrast.
    if (const images::texture* bg = images::background();
        bg != nullptr && bg->id != ImTextureID_Invalid && bg->width > 0 && bg->height > 0)
    {
        ImVec2 uv0, uv1;
        background_uv(rect, uv0, uv1);
        dl->AddImageRounded(bg->id, rect.Min, rect.Max, uv0, uv1, IM_COL32_WHITE, px(rounding));
        dl->AddRectFilled(rect.Min, rect.Max,
                          mo::with_alpha(c_background, k_background_scrim), px(rounding));
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
} // namespace szk::shell