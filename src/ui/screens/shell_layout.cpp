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
// 0.92 is measured, not chosen for looks: the weakest text in the app
// (c_muted_foreground) needs a ground no brighter than 0.019 relative luminance
// to hold WCAG AA 4.5:1, and a white area of an image - large display type, a
// blown-out highlight - only falls to that once 92% of the ground colour is
// back on top. Lower it and text over the bright parts of the image drops below
// AA (0.90 measures 4.23:1, 0.88 measures 3.96:1). The cost is that the image
// reads as a faint texture rather than a picture; that is the trade a legible
// dashboard demands, and it is why a busy image is a poor choice here.
constexpr float k_background_scrim = 0.92f;

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
        const float plate_aspect = rect.GetWidth() / ImMax(1.f, rect.GetHeight());
        const float image_aspect = (float)bg->width / (float)bg->height;

        // Crop the overflowing axis in UV space, centred, so the image fills the
        // plate without stretching whatever it is a picture of.
        ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
        if (image_aspect > plate_aspect)
        {
            const float keep = plate_aspect / image_aspect;
            uv0.x = (1.f - keep) * 0.5f;
            uv1.x = 1.f - uv0.x;
        }
        else
        {
            const float keep = image_aspect / plate_aspect;
            uv0.y = (1.f - keep) * 0.5f;
            uv1.y = 1.f - uv0.y;
        }

        dl->AddImageRounded(bg->id, rect.Min, rect.Max, uv0, uv1, IM_COL32_WHITE, px(rounding));
        dl->AddRectFilled(rect.Min, rect.Max,
                          mo::with_alpha(c_background, k_background_scrim), px(rounding));
    }

    return rect;
}
} // namespace szk::shell
