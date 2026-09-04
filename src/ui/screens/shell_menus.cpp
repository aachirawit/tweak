#include "ui/screens/shell_menus.h"

#include "application/brand.h"
#include "assets/avatars.h"
#include "ui/controls/widgets.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/primitives.h"

namespace szk
{
namespace
{

constexpr float k_open = 0.18f;
constexpr float k_stagger = 0.035f;
constexpr float k_delay = 0.05f;
constexpr float k_item_blur = 3.f;

constexpr float k_panel_w = 232.f;
constexpr float k_pad = 4.f;
constexpr float k_row = 34.f;
constexpr float k_account_h = 56.f;
constexpr float k_rule = 9.f;
constexpr float k_label_h = 24.f;

constexpr ImU32 c_avatar = IM_COL32(0xD5, 0xFF, 0x66, 0xFF);

struct action
{
    const char* label;
    icons::id icon;
    const char* badge;
    bool destructive;
};

const action k_profile_actions[] = {
    {"Profile", icons::id::user, nullptr, false},
    {"Preferences", icons::id::settings, nullptr, false},
    {"Sign out", icons::id::log_out, nullptr, true},
};

constexpr int k_profile_action_count = IM_ARRAYSIZE(k_profile_actions);

struct panel_state
{
    bool open = false;
    mo::presence panel;
    ImRect trigger;
    bool have_trigger = false;
};

struct menu_state
{
    panel_state profile;
};

menu_state& state()
{
    static menu_state s;
    return s;
}

struct row_anim
{
    float opacity, dy, blur;
};

row_anim row_at(const panel_state& s, int index, bool exiting, float open)
{
    const float t = exiting ? s.panel.out : s.panel.in;
    const float delay = k_delay + (float)index * k_stagger;
    const float p = mo::EASE_OUT(ImClamp((t - delay) / k_open, 0.f, 1.f));

    row_anim out;
    out.opacity = (exiting ? 1.f - p : p) * open;
    out.dy = px(-6.f) * (exiting ? p : 1.f - p);
    out.blur = px(k_item_blur) * (exiting ? p : 1.f - p);
    return out;
}

void row_text(ImDrawList* dl, ImFont* f, const ImVec2& at, ImU32 col, const char* s, float blur)
{
    if (blur > 0.25f)
        draw_text_blur(dl, f, at, col, s, blur);
    else
        draw_text(dl, f, at, col, s);
}

void panel_surface(ImDrawList* dl, const ImRect& panel, float open)
{
    backdrop_blur(dl, panel, px(24.f), px(12.f), open);
    dl->AddRectFilled(panel.Min, panel.Max, mo::with_alpha(c_card, 0.95f * open), px(12.f));
    dl->AddRect(ImVec2(panel.Min.x + px(0.5f), panel.Min.y + px(0.5f)),
                ImVec2(panel.Max.x - px(0.5f), panel.Max.y - px(0.5f)),
                mo::with_alpha(c_border, open), px(12.f), px(1.f), ImDrawFlags_None);
}

void rule_at(ImDrawList* dl, const ImRect& panel, float y, float opacity)
{
    dl->AddRectFilled(ImVec2(panel.Min.x + px(k_pad), y + px(4.f)),
                      ImVec2(panel.Max.x - px(k_pad), y + px(5.f)),
                      mo::with_alpha(c_border, opacity));
}

void account_block(ImDrawList* dl, const ImRect& panel, float y, const row_anim& a)
{
    const float avatar = px(32.f);
    const ImVec2 at(panel.Min.x + px(12.f), y + a.dy + (px(k_account_h) - avatar) * 0.5f);

    // SZK mark tile, matching the sidebar footer avatar - the account is the
    // licence, so the brand stands in for a profile photo.
    const float radius = avatar * 0.28f;
    dl->AddRectFilled(at, ImVec2(at.x + avatar, at.y + avatar),
                      mo::with_alpha(c_card_raised, a.opacity), radius);
    dl->AddRect(ImVec2(at.x + px(0.5f), at.y + px(0.5f)),
                ImVec2(at.x + avatar - px(0.5f), at.y + avatar - px(0.5f)),
                mo::with_alpha(c_border_strong, a.opacity), radius, px(1.f), ImDrawFlags_None);
    const float glyph = avatar * 0.62f;
    const float inset = (avatar - glyph) * 0.5f;
    icons::draw(icons::id::szk_mark, dl, ImVec2(at.x + inset, at.y + inset), glyph,
                mo::with_alpha(c_accent, a.opacity));

    ImFont* nf = font_medium(text_sm);
    row_text(dl, nf,
             ImVec2(at.x + avatar + px(10.f), y + a.dy + px(12.f) + line_top(nf, px(leading_sm))),
             mo::with_alpha(c_foreground, a.opacity), brand::user_name, a.blur);

    ImFont* ef = font_regular(text_xs);
    row_text(dl, ef,
             ImVec2(at.x + avatar + px(10.f), y + a.dy + px(31.f) + line_top(ef, px(leading_xs))),
             mo::with_alpha(c_muted_foreground, a.opacity), brand::user_github, a.blur);
}

bool action_row(ImDrawList* dl, const ImRect& panel, float y, const row_anim& a, const action& item,
                bool exiting, const ImVec2& mouse)
{
    const ImRect r(ImVec2(panel.Min.x + px(k_pad), y + a.dy),
                   ImVec2(panel.Max.x - px(k_pad), y + a.dy + px(k_row)));

    const bool hot = !exiting && r.Contains(mouse);
    if (hot)
        dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_foreground, 0.06f * a.opacity), px(8.f));

    const ImU32 col = mo::with_alpha(
        item.destructive ? c_destructive : (hot ? c_foreground : c_muted_foreground), a.opacity);

    icons::draw(item.icon, dl, ImVec2(r.Min.x + px(10.f), r.GetCenter().y - px(8.f)), px(16.f),
                col);

    ImFont* f = font_medium(text_sm);
    row_text(dl, f, ImVec2(r.Min.x + px(36.f), r.GetCenter().y - f->LegacySize * 0.5f), col,
             item.label, a.blur);

    if (item.badge)
    {
        ImFont* bf = font_regular(text_xs);
        draw_text(dl, bf,
                  ImVec2(r.Max.x - px(12.f) - text_width(bf, item.badge),
                         r.GetCenter().y - bf->LegacySize * 0.5f),
                  mo::with_alpha(c_muted_foreground, a.opacity), item.badge);
    }

    return hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

// The profile menu is the only dropdown left, so a trigger just toggles it -
// there is no sibling panel to close any more.
bool trigger(panel_state& self, const char* id, const ImRect& rect)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    self.trigger = rect;
    self.have_trigger = true;

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("chip");
    ImGui::SetCursorScreenPos(rect.Min);
    ImGui::ItemSize(ImVec2(0, 0));
    ImGui::ItemAdd(rect, item_id);

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(rect, item_id, &hovered, &held);
    ImGui::PopID();

    if (pressed)
        self.open = !self.open;

    return hovered;
}

bool begin_panel(panel_state& s, const ImRect& panel, int row_count, bool* out_exiting,
                 float* out_open)
{
    const float dt = ImGui::GetIO().DeltaTime;

    if (s.open && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        s.open = false;

    if (!s.panel.update(s.open, dt, k_open + k_delay + (float)row_count * k_stagger))
        return false;

    *out_exiting = s.panel.exiting;
    const float p =
        mo::EASE_OUT(ImClamp((*out_exiting ? s.panel.out : s.panel.in) / k_open, 0.f, 1.f));
    *out_open = (*out_exiting ? 1.f - p : p);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (!*out_exiting && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !panel.Contains(mouse) &&
        !s.trigger.Contains(mouse))
        s.open = false;

    if (!*out_exiting)
        claim_pointer();

    return true;
}
} // namespace

bool profile_menu_open()
{
    return state().profile.open;
}

bool profile_trigger(const ImRect& rect)
{
    menu_state& m = state();
    return trigger(m.profile, "profile", rect);
}

profile_choice profile_menu(const ImRect& viewport, float alpha)
{
    panel_state& s = state().profile;
    if (!s.have_trigger)
        return profile_none;

    ImDrawList* dl = ImGui::GetCurrentWindow()->DrawList;

    const float w = px(k_panel_w);
    const float h = px(k_pad) * 2.f + px(k_account_h) + px(k_rule) +
                    px(k_row) * (float)(k_profile_action_count - 1) + px(k_rule) + px(k_row);

    const float left = ImMin(s.trigger.Min.x, viewport.Max.x - px(16.f) - w);
    const float top = ImMax(s.trigger.Min.y - px(6.f) - h, viewport.Min.y + px(16.f));
    const ImRect panel(ImVec2(left, top), ImVec2(left + w, top + h));

    bool exiting = false;
    float open = 0.f;
    if (!begin_panel(s, panel, k_profile_action_count + 1, &exiting, &open))
        return profile_none;
    open *= alpha;

    panel_surface(dl, panel, open);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    profile_choice chosen = profile_none;
    int index = 0;
    float y = panel.Min.y + px(k_pad);

    account_block(dl, panel, y, row_at(s, index++, exiting, open));
    y += px(k_account_h);
    rule_at(dl, panel, y, open);
    y += px(k_rule);

    for (int i = 0; i < k_profile_action_count; i++)
    {
        if (k_profile_actions[i].destructive)
        {
            rule_at(dl, panel, y, open);
            y += px(k_rule);
        }

        const row_anim a = row_at(s, index++, exiting, open);
        if (action_row(dl, panel, y, a, k_profile_actions[i], exiting, mouse))
        {
            s.open = false;
            if (k_profile_actions[i].destructive)
                chosen = profile_sign_out;
            else if (i == 0)
                chosen = profile_open_page;
            else
                chosen = profile_open_preferences;
        }
        y += px(k_row);
    }

    return chosen;
}
} // namespace szk
