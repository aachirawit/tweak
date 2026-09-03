#include "application/app.h"

#include "auth/auth.h"
#include "ui/controls/loader.h"
#include "ui/controls/theme_toggle.h"
#include "ui/foundation/primitives.h"
#include "ui/screens/shell.h"

namespace szk
{

namespace
{
enum class screen_id
{
    license = 0,
    legal,
    loading,
    menu,
};

struct app_state
{
    screen_id screen = screen_id::license;
    float elapsed = 0.f;

    legal_document document = legal_document::terms;
    screen_id document_return = screen_id::license;

    // Kept so auth_apply_effect() has something to read. There is only one
    // auth screen now, so nothing animates between forms any more.
    auth_view_effect auth_fx;
};

app_state& state()
{
    static app_state s;
    return s;
}

constexpr float k_loading_duration = 0.5f;

constexpr float k_loader_size = 64.f;

void begin_shell_window()
{
    const ImVec2 size = shell::animate_size(px(shell::width, shell::height));
    ui_runtime::host_size = size;

    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowPos(ImVec2(0, 0));

    ImGui::Begin("Shell", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);

    ui_runtime::apply_style();
}

} // namespace

const auth_view_effect& auth_effect()
{
    return state().auth_fx;
}

void auth_apply_effect(ImDrawList* dl, int first_vertex)
{
    if (!dl)
        return;

    const auth_view_effect& fx = auth_effect();
    const int begin = ImClamp(first_vertex, 0, dl->VtxBuffer.Size);
    const float opacity = ImClamp(fx.opacity, 0.f, 1.f);

    if (opacity >= 0.999f)
        return;

    for (int i = begin; i < dl->VtxBuffer.Size; i++)
    {
        ImDrawVert& vertex = dl->VtxBuffer[i];
        const ImU32 alpha = (vertex.col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT;
        const ImU32 faded = (ImU32)(opacity * (float)alpha + 0.5f);
        vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (faded << IM_COL32_A_SHIFT);
    }
}

static void loading_screen(float elapsed)
{
    begin_shell_window();
    {
        const ImRect rect = shell::plate();
        metaballs(ImGui::GetCurrentWindow()->DrawList, rect.GetCenter(), px(k_loader_size), 1.f,
                  elapsed, c_foreground);
    }
    ImGui::End();
}

void application::render_frame()
{
    app_state& s = state();
    const float dt = ImGui::GetIO().DeltaTime;
    s.elapsed += dt;

    theme_tick(dt);

    auto handle = [&](auth_action result, screen_id here)
    {
        switch (result)
        {
        case auth_action::done:
            s.screen = screen_id::loading;
            s.elapsed = 0.f;
            break;
        case auth_action::terms:
        case auth_action::privacy:
            s.document =
                (result == auth_action::terms) ? legal_document::terms : legal_document::privacy;
            s.document_return = here;
            s.screen = screen_id::legal;
            s.elapsed = 0.f;
            break;
        default:
            break;
        }
    };

    switch (s.screen)
    {
    case screen_id::license:
        handle(license_screen(), screen_id::license);
        break;

    case screen_id::legal:
        if (legal_screen(s.document))
        {
            s.screen = s.document_return;
            s.elapsed = 0.f;
        }
        break;

    case screen_id::loading:
        loading_screen(s.elapsed);
        if (s.elapsed >= k_loading_duration)
        {
            s.screen = screen_id::menu;
            s.elapsed = 0.f;
        }
        break;

    case screen_id::menu:

        if (menu_screen(mo::EASE_OUT(ImClamp(s.elapsed / 0.18f, 0.f, 1.f))))
        {
            license_reset();
            s.screen = screen_id::license;
            s.elapsed = 0.f;
        }
        break;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    theme_reveal_draw(ImGui::GetForegroundDrawList(),
                      ImRect(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y)));
}
} // namespace szk
