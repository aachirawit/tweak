#include "application/brand.h"
#include "assets/avatars.h"
#include "assets/images.h"
#include "core/i18n.h"
#include "core/product_info.h"
#include "ui/controls/scroll.h"
#include "ui/controls/theme_toggle.h"
#include "ui/controls/widgets.h"
#include "ui/foundation/draw.h"
#include "ui/foundation/primitives.h"
#include "ui/screens/page_renderer.h"
#include "ui/screens/search_overlay.h"
#include "ui/screens/shell.h"
#include "ui/screens/shell_menus.h"

namespace szk
{

namespace
{

constexpr float k_width_expanded = 256.f;
constexpr float k_width_icon = 68.f;

constexpr float k_footer_h = 68.f;

constexpr float k_content_pad_x = 8.f;
constexpr float k_group_pad_x = 4.f;
constexpr float k_group_pad_y = 6.f;
constexpr float k_group_gap = 8.f;

constexpr float k_item_h = 36.f;
constexpr float k_item_gap = 2.f;
constexpr float k_item_round = 12.f;
constexpr float k_item_pad_x = 12.f;
constexpr float k_item_gap_x = 10.f;
constexpr float k_icon_slot = 20.f;
constexpr float k_icon = 16.f;

constexpr float k_label_h = 28.f;
constexpr float k_label_mb = 4.f;
constexpr float k_label_size = 10.f;
constexpr float k_label_track = 1.4f;

constexpr mo::spring_cfg k_morph{380.f, 35.f, 0.75f};

constexpr float k_label_enter = 0.2f, k_label_enter_delay = 0.08f;
constexpr float k_label_exit = 0.12f;

struct nav_item
{
    const char* label;
    icons::id icon;
    const char* badge;
    const char* sub[9];
    int sub_count;
};

// Route-indexed page title and tab strip, mirroring navigation.h's enum order
// for the non-account routes (assistant..dashboard). It is not what the sidebar
// renders - see k_rail_rows for the menu the user sees.
constexpr nav_item k_items[] = {
    {"About", icons::id::info, nullptr, {nullptr}, 0},         // assistant
    {"This machine", icons::id::inbox, nullptr, {nullptr}, 0}, // messages
    {"Settings",
     icons::id::circle_user_round,
     nullptr,
     {"All tweaks", "Performance", "Network", "Power plan", "NVIDIA", "AMD", "Cleanup", "FiveM",
      "Windows"},
     9},
    {"Auto Reshade", icons::id::building_2, nullptr, {nullptr}, 0}, // presets
    {"Drivers", icons::id::workflow, nullptr, {nullptr}, 0},        // automation
    {"Dashboard", icons::id::layout_grid, nullptr, {nullptr}, 0},   // dashboard
};

constexpr int k_item_count = IM_ARRAYSIZE(k_items);

static_assert(k_item_count == route_index(route::profile));
static_assert(k_items[route_index(route::settings)].sub_count == settings_tab_count);

const char* page_title(route destination, int sub)
{
    if (destination == route::profile)
        return "Profile";
    if (destination == route::preferences)
        return "Preferences";

    const nav_item& item = k_items[ImClamp(route_index(destination), 0, k_item_count - 1)];
    return item.sub_count > 0 ? item.sub[ImClamp(sub, 0, item.sub_count - 1)] : item.label;
}

const search_item k_search[] = {
    {"Dashboard", "CPU, RAM, disk, and ping, live", "metrics reports overview",
     icons::id::layout_grid, route::dashboard, 0},
    {"All tweaks", "Every tweak on one page, and what it is set to", "settings options everything",
     icons::id::settings, route::settings, tab_index(settings_tab::all)},
    {"Performance", "Timer resolution, core parking, and scheduling", "cpu timer fps stutter",
     icons::id::cpu, route::settings, tab_index(settings_tab::performance)},
    {"Network", "Latency, Nagle's algorithm, and TCP tuning", "ping lag latency tcp nagle",
     icons::id::target, route::settings, tab_index(settings_tab::network)},
    {"Power plan", "The active power plan on this machine", "power plan ultimate balanced",
     icons::id::zap, route::settings, tab_index(settings_tab::power_plan)},
    {"NVIDIA", "NVIDIA-specific tweaks for this machine", "graphics gpu nvidia low latency",
     icons::id::sparkles, route::settings, tab_index(settings_tab::nvidia)},
    {"AMD", "AMD/Radeon-specific tweaks for this machine", "graphics gpu amd radeon",
     icons::id::sparkles, route::settings, tab_index(settings_tab::amd)},
    {"Cleanup", "Temp, prefetch, update cache, shader cache, and more", "cleanup clear cache junk",
     icons::id::trash, route::settings, tab_index(settings_tab::cleanup)},
    {"FiveM", "CitizenFX.ini, the GTA V graphics preset, and FiveM's own tweaks",
     "fivem gta citizenfx gta5 roleplay", icons::id::gamepad, route::settings,
     tab_index(settings_tab::fivem)},
    {"Windows", "Explorer, privacy, telemetry and the rest of the desktop",
     "windows explorer privacy telemetry debloat", icons::id::settings, route::settings,
     tab_index(settings_tab::windows)},
    {"Auto ReShade", "Install ReShade and the 2K Road Mod into FiveM", "reshade quantv road mod",
     icons::id::building_2, route::presets, 0},
    {"Drivers", "Look up the installed motherboard online", "drivers lookup board update",
     icons::id::workflow, route::automation, 0},
    {"This machine", "Specs, version, and what is installed", "hardware specs machine info",
     icons::id::inbox, route::messages, 0},
    {"Profile", "Your account and what it may send you", "account me settings",
     icons::id::circle_user_round, route::profile, 0},
    {"About", "Version, support, and Windows recovery", "about support recovery updates",
     icons::id::info, route::assistant, 0},
};

// ── The sidebar the user actually sees ──────────────────────────────────────
//
// Grouped by what a row changes, not by where it lives in the registry, and
// deliberately flat: no accordions, so nothing is one click deeper than it
// looks. Search is not a row - it is Ctrl+K, which already exists.
//
// A row is a destination, not a route: several rows land on route::settings
// with a different tab preselected, so the seven tweak pages get top-level
// billing while page_renderer keeps its single route-indexed switch. The tab
// strip on the page stays in sync in both directions - press a rail row and
// its tab opens, press a tab and the rail follows.
//
// Every route has a row now; search is the exception, reached with Ctrl+K.
struct rail_row
{
    const char* label;
    icons::id icon;
    const char* badge;
    route dest;
    int tab;      // keep_tab for routes without a tab strip
    int tab_span; // tabs this row covers, so Graphics owns NVIDIA and AMD both
};

struct rail_group
{
    const char* label; // nullptr for the ungrouped run at the top
    int first;
    int count;
};

constexpr rail_row k_rail_rows[] = {
    // OVERVIEW - one row, so it carries no heading of its own
    {"Dashboard", icons::id::layout_grid, nullptr, route::dashboard, keep_tab, 0},

    // OPTIMIZE - one row per thing the machine can be made to do differently
    {"Performance", icons::id::cpu, nullptr, route::settings, tab_index(settings_tab::performance),
     1},
    // Opens on NVIDIA and stays lit on AMD: the page's own tab strip picks the
    // vendor, and the rail has no business claiming this machine has only one.
    {"Graphics", icons::id::sparkles, nullptr, route::settings, tab_index(settings_tab::nvidia), 2},
    {"Network", icons::id::target, nullptr, route::settings, tab_index(settings_tab::network), 1},
    {"Power plan", icons::id::zap, nullptr, route::settings, tab_index(settings_tab::power_plan),
     1},
    {"Cleanup", icons::id::trash, nullptr, route::settings, tab_index(settings_tab::cleanup), 1},
    {"FiveM", icons::id::gamepad, nullptr, route::settings, tab_index(settings_tab::fivem), 1},
    {"Windows", icons::id::settings, nullptr, route::settings, tab_index(settings_tab::windows), 1},
    {"Auto ReShade", icons::id::building_2, nullptr, route::presets, keep_tab, 0},
    {"All tweaks", icons::id::settings, nullptr, route::settings, tab_index(settings_tab::all), 1},

    // SYSTEM - what this machine is, rather than how it is tuned
    {"This machine", icons::id::inbox, nullptr, route::messages, keep_tab, 0},
    {"Drivers", icons::id::workflow, nullptr, route::automation, keep_tab, 0},
    {"About", icons::id::info, nullptr, route::assistant, keep_tab, 0},
};

constexpr int k_rail_count = IM_ARRAYSIZE(k_rail_rows);

constexpr rail_group k_rail_groups[] = {
    {nullptr, 0, 1},
    {"OPTIMIZE", 1, 9},
    {"SYSTEM", 10, 3},
};

constexpr int k_rail_group_count = IM_ARRAYSIZE(k_rail_groups);

// Every row is listed exactly once, in order.
static_assert(k_rail_groups[k_rail_group_count - 1].first +
                  k_rail_groups[k_rail_group_count - 1].count ==
              k_rail_count);

// True when this row is the one the shell is currently showing. A row with a
// tab only lights up for the tabs it owns, so the six settings rows never
// light up together - and between them they own all seven tabs, so no tab
// leaves the rail with nothing highlighted.
[[nodiscard]] bool rail_row_active(const rail_row& row, route active, const int* sub_index)
{
    if (row.dest != active)
        return false;
    if (row.tab == keep_tab)
        return true;

    const int open = sub_index[route_index(row.dest)];
    return open >= row.tab && open < row.tab + row.tab_span;
}

// Holds the rail and the Settings tab strip to the same story: every tab is
// owned by exactly one row. Adding a tab without giving it a row, or handing
// one tab to two rows, fails the build rather than the highlight.
[[nodiscard]] constexpr bool rail_covers_settings_tabs()
{
    int owners[settings_tab_count] = {};

    for (const rail_row& row : k_rail_rows)
    {
        if (row.dest != route::settings || row.tab == keep_tab)
            continue;

        for (int tab = row.tab; tab < row.tab + row.tab_span; tab++)
        {
            if (tab < 0 || tab >= settings_tab_count)
                return false;
            owners[tab]++;
        }
    }

    for (const int count : owners)
        if (count != 1)
            return false;

    return true;
}

static_assert(rail_covers_settings_tabs());

constexpr ImU32 c_avatar = IM_COL32(0xD5, 0xFF, 0x66, 0xFF);

// One per rail row, not per route: five rows share route::settings and each
// still hovers and presses on its own.
struct item_anim
{
    color_tween text;
    mo::spring press;
};

struct sidebar_state
{
    bool collapsed = false;
    mo::spring width;
    float label_t = 1e6f;
    bool labels_shown = true;

    route active = route::dashboard;
    int sub_index[route_count] = {};

    mo::spring profile_chevron;

    smooth_scroll rail;
    float rail_content = 0.f;

    mo::spring pill_x, pill_y, pill_w, pill_h;
    bool pill_seeded = false;

    item_anim rows[k_rail_count];
    color_tween trigger_col;
};

sidebar_state& state()
{
    static sidebar_state s;
    return s;
}

// The account avatar is the numbanine mark on a dark rounded tile, not a person's
// photo - the licence is the account, so the brand is the right identity here.
// Shared by the sidebar footer and the profile dropdown so both match. Square
// with a soft radius rather than a circle, to read as an app tile beside the
// round brand tile at the top.
void draw_brand_avatar(ImDrawList* dl, const ImVec2& tl, float size, float alpha)
{
    shell::brand_avatar(dl, tl, size, alpha);
}

// ── Language switch ─────────────────────────────────────────────────────────
// Both languages, side by side, with the active one carried on a sliding
// indicator. This replaces a 40px button that showed only the language it would
// switch to: it read as an abbreviation rather than a control, it never said
// which language was on, and the search trigger was laid out to the same right
// edge, so it sat on top of the button and took the clicks.
struct lang_switch_state
{
    mo::spring indicator;
    mo::spring hover[2];
    mo::spring press[2];
    bool seeded = false;
};

constexpr float k_lang_switch_h = 32.f;
constexpr float k_lang_pad = 10.f;

struct lang_option
{
    i18n::lang value;
    const char* label;
};

constexpr lang_option k_languages[2] = {
    {i18n::lang::en, "EN"},
    {i18n::lang::th, "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2"}, // ไทย
};

// The width both cells share, so the indicator is one size and the switch does
// not resize when the active language changes.
float lang_cell_width(ImFont* f)
{
    float widest = 0.f;
    for (const lang_option& option : k_languages)
        widest = ImMax(widest, text_width(f, option.label));
    return widest + px(k_lang_pad) * 2.f;
}

void language_switch(const ImRect& rect, float alpha)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float dt = ImGui::GetIO().DeltaTime;

    lang_switch_state* st =
        ui_runtime::animation_state<lang_switch_state>(window->GetID("lang-switch"));

    const float cell = rect.GetWidth() * 0.5f;
    const int active = i18n::language() == i18n::lang::en ? 0 : 1;

    if (!st->seeded)
    {
        st->indicator.snap((float)active);
        st->seeded = true;
    }

    dl->AddRectFilled(rect.Min, rect.Max, mo::with_alpha(c_card, alpha), px(9.f));
    dl->AddRect(ImVec2(rect.Min.x + px(0.5f), rect.Min.y + px(0.5f)),
                ImVec2(rect.Max.x - px(0.5f), rect.Max.y - px(0.5f)),
                mo::with_alpha(c_border, alpha), px(9.f), px(1.f), ImDrawFlags_None);

    const float slide = st->indicator.to((float)active, mo::SPRING_SWAP, dt);
    const ImVec2 pill_min(rect.Min.x + px(3.f) + cell * slide, rect.Min.y + px(3.f));
    const ImVec2 pill_max(pill_min.x + cell - px(6.f), rect.Max.y - px(3.f));
    dl->AddRectFilled(pill_min, pill_max, mo::with_alpha(c_card_raised, alpha), px(7.f));
    dl->AddRect(ImVec2(pill_min.x + px(0.5f), pill_min.y + px(0.5f)),
                ImVec2(pill_max.x - px(0.5f), pill_max.y - px(0.5f)),
                mo::with_alpha(c_border_strong, alpha), px(7.f), px(1.f), ImDrawFlags_None);

    ImFont* f = font_medium(text_xs);

    for (int i = 0; i < 2; i++)
    {
        const ImRect cell_rect(ImVec2(rect.Min.x + cell * (float)i, rect.Min.y),
                               ImVec2(rect.Min.x + cell * (float)(i + 1), rect.Max.y));

        ImGui::PushID(i);
        const ImGuiID id = window->GetID("lang-cell");
        ImGui::SetCursorScreenPos(cell_rect.Min);
        ImGui::ItemSize(ImVec2(0, 0));
        ImGui::ItemAdd(cell_rect, id);

        bool hovered = false, held = false;
        const bool pressed =
            !pointer_claimed() && ImGui::ButtonBehavior(cell_rect, id, &hovered, &held);
        ImGui::PopID();

        if (pressed)
            i18n::set_language(k_languages[i].value);

        if (hovered && i != active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // Held state, acknowledged on the cell itself rather than waiting for
        // the indicator to arrive. The indicator is a spring, so on the press
        // that starts it there is otherwise nothing under the finger for the
        // length of the slide.
        const float held_t = st->press[i].to(held ? 1.f : 0.f, mo::SPRING_SWAP, dt);
        if (held_t > 0.004f)
        {
            const float pad = px(3.f);
            dl->AddRectFilled(ImVec2(cell_rect.Min.x + pad, cell_rect.Min.y + pad),
                              ImVec2(cell_rect.Max.x - pad, cell_rect.Max.y - pad),
                              mo::with_alpha(c_card_raised, 0.7f * held_t * alpha), px(7.f));
        }

        // The inactive side lifts towards the foreground on hover so it reads
        // as the other half of a control rather than as static text.
        const float lift =
            st->hover[i].to((hovered && i != active) ? 1.f : 0.f, mo::SPRING_SWAP, dt);
        const ImU32 col =
            i == active ? c_foreground : mo::mix(c_muted_foreground, c_foreground, lift);

        const float w = text_width(f, k_languages[i].label);
        draw_text(dl, f,
                  ImVec2(cell_rect.GetCenter().x - w * 0.5f,
                         cell_rect.GetCenter().y - f->LegacySize * 0.5f),
                  mo::with_alpha(col, alpha), k_languages[i].label);
    }
}
} // namespace

bool menu_screen(float alpha)
{
    bool sign_out = false;

    sidebar_state& s = state();
    const float dt = ImGui::GetIO().DeltaTime;

    const ImVec2 window_size = shell::animate_size(px(shell::width, shell::height));
    ui_runtime::host_size = window_size;

    ImGui::SetNextWindowSize(window_size);
    ImGui::SetNextWindowPos(ImVec2(0, 0));

    ImGui::Begin("Shell", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);
    {
        ui_runtime::apply_style();

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImDrawList* dl = window->DrawList;
        const ImRect plate = shell::plate();

        const bool shortcut = ImGui::IsKeyPressed(ImGuiKey_B, false) &&
                              (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper);

        if (shortcut)
            s.collapsed = !s.collapsed;

        if (s.labels_shown == s.collapsed)
        {
            s.labels_shown = !s.collapsed;
            s.label_t = 0.f;
        }
        s.label_t += dt;

        const float width = s.width.to(s.collapsed ? k_width_icon : k_width_expanded, k_morph, dt);
        const bool collapsed = s.collapsed;

        const float label_p =
            collapsed ? mo::EASE_OUT(ImClamp(s.label_t / k_label_exit, 0.f, 1.f))
                      : mo::EASE_OUT(
                            ImClamp((s.label_t - k_label_enter_delay) / k_label_enter, 0.f, 1.f));
        const float label_a = (collapsed ? 1.f - label_p : label_p) * alpha;
        const float label_dx = collapsed ? -4.f * label_p : -4.f * (1.f - label_p);

        const ImVec2 origin = plate.Min;
        const float bar_w = px(width);

        ImDrawListSplitter splitter;
        splitter.Split(dl, 2);
        splitter.SetCurrentChannel(dl, 1);

        const ImRect nav_box(ImVec2(plate.Min.x + px(1.f), plate.Min.y + px(68.f)),
                             ImVec2(plate.Min.x + px(width), plate.Max.y - px(1.f + k_footer_h)));
        const float nav_scroll = scroll_area(
            s.rail, nav_box, s.rail_content > 0.f ? s.rail_content : nav_box.GetHeight());

        // The sidebar owns an opaque ground for the same reason a card does: its
        // rows are text, and a background image behind the shell must not reach
        // them. Like a card it paints the slice of the picture behind it and
        // lays the ground back over that, so the image stays continuous across
        // the window instead of being interrupted by a flat column.
        {
            const ImVec2 rail_min(origin.x + px(1.f), origin.y + px(1.f));
            const ImVec2 rail_max(origin.x + bar_w, plate.Max.y - px(1.f));
            const ImRect rail_rect(rail_min, rail_max);

            if (ImVec2 uv0, uv1; shell::background_uv(rail_rect, uv0, uv1))
            {
                dl->AddImageRounded(images::background()->id, rail_min, rail_max, uv0, uv1,
                                    mo::with_alpha(IM_COL32_WHITE, alpha), px(shell::rounding),
                                    ImDrawFlags_RoundCornersLeft);
                dl->AddRectFilled(rail_min, rail_max,
                                  mo::with_alpha(c_background, shell::card_scrim * alpha),
                                  px(shell::rounding), ImDrawFlags_RoundCornersLeft);
            }
            else
            {
                dl->AddRectFilled(rail_min, rail_max, mo::with_alpha(c_background, alpha),
                                  px(shell::rounding), ImDrawFlags_RoundCornersLeft);
            }
        }

        dl->AddRectFilled(ImVec2(origin.x + bar_w, origin.y + px(1.f)),
                          ImVec2(origin.x + bar_w + px(1.f), plate.Max.y - px(1.f)),
                          mo::with_alpha(c_border, alpha));

        {
            // Static brand header. This was a "game" switcher from the template
            // (numbanine / Beta / Gamma); a single product has nothing to switch, so
            // it is now just the mark, the name and the version - no dropdown.
            //
            // The brand tile, drawn by the one helper every other copy of it
            // goes through, so the mark cannot drift from the sidebar footer
            // and the account menu again.
            const ImVec2 tile(origin.x + px(20.f), origin.y + px(20.f));
            const float tile_size = px(28.f);

            shell::brand_avatar(dl, tile, tile_size, alpha);

            if (label_a > 0.004f)
            {
                ImFont* f = font_semibold(text_sm);
                draw_text(dl, f,
                          ImVec2(origin.x + px(60.f + label_dx),
                                 origin.y + px(16.f) + line_top(f, px(leading_sm))),
                          mo::with_alpha(c_foreground, label_a), brand::product);

                ImFont* vf = font_regular(text_xs);
                char version[24];
                ImFormatString(version, IM_ARRAYSIZE(version), "v%s", product_info::version);
                draw_text(dl, vf,
                          ImVec2(origin.x + px(60.f + label_dx),
                                 origin.y + px(34.f) + line_top(vf, px(leading_xs))),
                          mo::with_alpha(c_muted_foreground, label_a), version);
            }
        }

        const float menu_x = origin.x + px(k_content_pad_x + k_group_pad_x);
        const float menu_w = bar_w - px((k_content_pad_x + k_group_pad_x) * 2.f);

        float y = origin.y + px(68.f + k_group_pad_y) - nav_scroll;
        const float nav_top_y = y;
        ImRect active_rect;
        bool have_active = false;

        dl->PushClipRect(nav_box.Min, nav_box.Max, true);

        for (int i = 0; i < k_rail_count; i++)
        {
            // The row that opens a group draws that group's heading; the gap
            // above it is what separates one group from the last.
            const char* group_label = nullptr;
            for (int g = 0; g < k_rail_group_count; g++)
            {
                if (k_rail_groups[g].first != i)
                    continue;
                group_label = k_rail_groups[g].label;
                if (g > 0)
                    y += px(6.f + k_group_gap + 4.f);
                break;
            }

            const rail_row& item = k_rail_rows[i];
            item_anim& anim = s.rows[i];

            if (group_label && label_a > 0.004f)
            {
                ImFont* f = font_medium(k_label_size);
                draw_text_tracked(dl, f,
                                  ImVec2(menu_x + px(8.f + label_dx), y + line_top(f, px(16.f))),
                                  mo::with_alpha(c_muted_foreground, label_a),
                                  i18n::tr(group_label), px(k_label_track));
            }
            if (group_label)
                y += px(k_label_h + k_label_mb);

            const ImRect bb(ImVec2(menu_x, y), ImVec2(menu_x + menu_w, y + px(k_item_h)));

            const bool row_visible = bb.Max.y > nav_box.Min.y && bb.Min.y < nav_box.Max.y;

            ImGui::PushID(i);
            const ImGuiID id = window->GetID("nav");
            ImGui::SetCursorScreenPos(bb.Min);
            ImGui::ItemSize(bb.GetSize());
            if (row_visible)
                ImGui::ItemAdd(bb, id);

            bool hovered = false, held = false;
            const bool pressed =
                row_visible && !pointer_claimed() && ImGui::ButtonBehavior(bb, id, &hovered, &held);
            ImGui::PopID();

            if (pressed)
            {
                s.active = item.dest;
                if (item.tab != keep_tab)
                    s.sub_index[route_index(item.dest)] = item.tab;
            }

            const bool is_active = rail_row_active(item, s.active, s.sub_index);
            const float scale = anim.press.to(held ? 0.98f : 1.f, mo::SPRING_PRESS, dt);

            if (is_active)
            {
                active_rect = bb;
                have_active = true;
            }

            const ImU32 text_col = anim.text.update(
                (is_active || hovered) ? c_foreground : c_muted_foreground, dt, 0.15f);

            const ImVec2 centre = bb.GetCenter();
            const ImVec2 half(bb.GetWidth() * 0.5f * scale, bb.GetHeight() * 0.5f * scale);
            const ImRect row(ImVec2(centre.x - half.x, centre.y - half.y),
                             ImVec2(centre.x + half.x, centre.y + half.y));

            const ImVec2 icon_slot(row.Min.x + px(k_item_pad_x), centre.y - px(k_icon_slot) * 0.5f);
            icons::draw(item.icon, dl,
                        ImVec2(icon_slot.x + px((k_icon_slot - k_icon) * 0.5f),
                               icon_slot.y + px((k_icon_slot - k_icon) * 0.5f)),
                        px(k_icon), mo::with_alpha(text_col, alpha));

            if (label_a > 0.004f)
            {
                ImFont* f = font_medium(text_sm);
                draw_text(dl, f,
                          ImVec2(icon_slot.x + px(k_icon_slot + k_item_gap_x + label_dx),
                                 centre.y - f->LegacySize * 0.5f),
                          mo::with_alpha(text_col, label_a), i18n::tr(item.label));

                if (item.badge)
                {
                    ImFont* bf = font_medium(text_xs);
                    const float bw = text_width(bf, item.badge);
                    draw_text(
                        dl, bf,
                        ImVec2(row.Max.x - px(k_item_pad_x) - bw, centre.y - bf->LegacySize * 0.5f),
                        mo::with_alpha(c_muted_foreground, label_a), item.badge);
                }
            }

            y += px(k_item_h + k_item_gap);
        }

        s.rail_content = (y - nav_top_y) + px(k_group_pad_y);
        scrollbar(dl, nav_box, s.rail_content, nav_scroll, alpha);
        dl->PopClipRect();

        splitter.SetCurrentChannel(dl, 0);
        dl->PushClipRect(nav_box.Min, nav_box.Max, true);
        if (have_active)
        {
            if (!s.pill_seeded)
            {
                s.pill_x.snap(active_rect.Min.x);
                s.pill_y.snap(active_rect.Min.y);
                s.pill_w.snap(active_rect.GetWidth());
                s.pill_h.snap(active_rect.GetHeight());
                s.pill_seeded = true;
            }

            const float rx = s.pill_x.to(active_rect.Min.x, mo::SPRING_LAYOUT, dt);
            const float ry = s.pill_y.to(active_rect.Min.y, mo::SPRING_LAYOUT, dt);
            const float rw = s.pill_w.to(active_rect.GetWidth(), mo::SPRING_LAYOUT, dt);
            const float rh = s.pill_h.to(active_rect.GetHeight(), mo::SPRING_LAYOUT, dt);

            dl->AddRectFilled(ImVec2(rx, ry), ImVec2(rx + rw, ry + rh),
                              mo::with_alpha(c_card, alpha), px(k_item_round));
        }
        dl->PopClipRect();
        splitter.Merge(dl);

        {
            const float top = plate.Max.y - px(1.f + k_footer_h);
            dl->AddRectFilled(ImVec2(origin.x + px(1.f), top),
                              ImVec2(origin.x + bar_w, top + px(1.f)),
                              mo::with_alpha(c_border, alpha));

            const ImRect row(ImVec2(origin.x + px(9.f), top + px(10.f)),
                             ImVec2(origin.x + bar_w - px(9.f), top + px(58.f)));
            const bool row_hot = profile_trigger(row);

            if (row_hot || profile_menu_open())
                dl->AddRectFilled(row.Min, row.Max, mo::with_alpha(c_card, alpha),
                                  px(k_item_round));

            const ImVec2 av(origin.x + px(17.f), top + px(16.f));
            draw_brand_avatar(dl, av, px(36.f), alpha);

            if (label_a > 0.004f)
            {
                ImFont* nf = font_medium(text_sm);
                draw_text(dl, nf,
                          ImVec2(origin.x + px(65.f + label_dx),
                                 top + px(17.f) + line_top(nf, px(leading_sm))),
                          mo::with_alpha(c_foreground, label_a), brand::user_name);

                ImFont* ef = font_regular(text_xs);
                draw_text(dl, ef,
                          ImVec2(origin.x + px(65.f + label_dx),
                                 top + px(37.f) + line_top(ef, px(leading_xs))),
                          mo::with_alpha(c_muted_foreground, label_a), brand::user_github);

                const float turn =
                    s.profile_chevron.to(profile_menu_open() ? 1.f : 0.f, mo::SPRING_LAYOUT, dt);
                const ImVec2 at(origin.x + px(224.f), top + px(27.f));
                const ImVec2 centre(at.x + px(k_icon) * 0.5f, at.y + px(k_icon) * 0.5f);

                const int rotation_start = draw_utils::rotation_start(dl);
                icons::draw(icons::id::chevron_right, dl, at, px(k_icon),
                            mo::with_alpha(row_hot ? c_foreground : c_muted_foreground, label_a));
                draw_utils::rotate_vertices(dl, rotation_start, -turn * IM_PI * 0.5f, centre);
            }
        }

        {
            const float ix = origin.x + bar_w + px(1.f);
            ImFont* f14 = font_medium(text_sm);

            // The top chrome - breadcrumb, search, the two switches - and the
            // page heading and tab strip under it are written straight onto the
            // plate, with no card between them and the picture. They get a
            // ground of their own here rather than the whole window getting one:
            // a band at the measured card floor that fades out below the tabs,
            // so the picture stays strong everywhere it is not being read over.
            {
                const float top = origin.y;
                const float solid_to = origin.y + px(216.f);
                const float fade_to = origin.y + px(276.f);
                const ImU32 ground = mo::with_alpha(c_background, shell::card_scrim * alpha);

                dl->AddRectFilled(ImVec2(ix, top), ImVec2(plate.Max.x - px(1.f), solid_to), ground);
                dl->AddRectFilledMultiColor(
                    ImVec2(ix, solid_to), ImVec2(plate.Max.x - px(1.f), fade_to), ground, ground,
                    mo::with_alpha(c_background, 0.f), mo::with_alpha(c_background, 0.f));
            }

            const ImRect trig(ImVec2(ix + px(16.f), origin.y + px(13.f)),
                              ImVec2(ix + px(56.f), origin.y + px(53.f)));

            ImGui::PushID("trigger");
            const ImGuiID tid = window->GetID("t");
            ImGui::SetCursorScreenPos(trig.Min);
            ImGui::ItemSize(ImVec2(0, 0));
            ImGui::ItemAdd(trig, tid);
            bool th = false, thd = false;
            if (ImGui::ButtonBehavior(trig, tid, &th, &thd))
                s.collapsed = !s.collapsed;
            ImGui::PopID();

            const ImU32 tcol =
                s.trigger_col.update(th ? c_foreground : c_muted_foreground, dt, 0.15f);
            if (th)
                dl->AddRectFilled(trig.Min, trig.Max, mo::with_alpha(c_card, alpha),
                                  px(k_item_round));
            icons::draw(icons::id::panel_left, dl,
                        ImVec2(trig.GetCenter().x - px(k_icon) * 0.5f,
                               trig.GetCenter().y - px(k_icon) * 0.5f),
                        px(k_icon), mo::with_alpha(tcol, alpha));

            dl->AddRectFilled(ImVec2(ix + px(64.f), origin.y + px(23.f)),
                              ImVec2(ix + px(65.f), origin.y + px(43.f)),
                              mo::with_alpha(c_border, alpha));

            const int active_index = route_index(s.active);
            const char* crumb_text = i18n::tr(page_title(s.active, s.sub_index[active_index]));
            draw_text(dl, f14,
                      ImVec2(ix + px(81.f), origin.y + px(22.f) + line_top(f14, px(leading_sm))),
                      mo::with_alpha(c_foreground, alpha), crumb_text);

            const float bar_right = plate.Max.x - px(16.f);
            const float button = px(40.f);
            const float gap = px(sp_2);

            const ImRect toggle_rect(ImVec2(bar_right - button, origin.y + px(12.f)),
                                     ImVec2(bar_right, origin.y + px(52.f)));
            theme_toggle("theme", toggle_rect, 16.f, alpha);

            // Language switch, beside the theme switch because it is the same
            // kind of choice: a display preference, not a setting about the
            // machine.
            const float lang_w = lang_cell_width(font_medium(text_xs)) * 2.f;
            const float lang_top = toggle_rect.GetCenter().y - px(k_lang_switch_h) * 0.5f;
            const ImRect lang_rect(ImVec2(toggle_rect.Min.x - gap - lang_w, lang_top),
                                   ImVec2(toggle_rect.Min.x - gap, lang_top + px(k_lang_switch_h)));
            language_switch(lang_rect, alpha);

            // The notification bell used to sit between search and the theme
            // toggle; with notifications gone, search extends to the language
            // switch. It used to extend to the theme toggle, which put it over
            // the language control and let it take those clicks.
            const float crumb_end = ix + px(81.f) + text_width(f14, crumb_text) + px(24.f);
            const float search_right = lang_rect.Min.x - gap;
            const float search_left = ImMax(crumb_end, search_right - px(search_trigger_w));

            if (search_right - search_left >= px(160.f))
            {
                const ImRect search_rect(
                    ImVec2(search_left, origin.y + px(8.f)),
                    ImVec2(search_right, origin.y + px(8.f) + px(search_trigger_h)));
                morphing_search_trigger(search_rect, alpha);
            }

            dl->AddRectFilled(ImVec2(ix, origin.y + px(64.f)),
                              ImVec2(plate.Max.x - px(1.f), origin.y + px(65.f)),
                              mo::with_alpha(c_border, alpha));

            const float bx = ix + px(28.f);
            const bool on_profile = is_account_route(s.active);
            const nav_item& current = k_items[on_profile ? 0 : active_index];
            const ImRect body(ImVec2(bx, origin.y + px(88.f)),
                              ImVec2(plate.Max.x - px(29.f), origin.y + px(628.f)));

            const route requested =
                draw_page(s.active, page_title(s.active, s.sub_index[active_index]),
                          on_profile ? nullptr : current.sub, on_profile ? 0 : current.sub_count,
                          &s.sub_index[active_index], body, alpha);
            if (requested != route::count)
                s.active = requested;

            ImFont* f12 = font_medium(text_xs);
            const float rule_y = plate.Max.y - px(1.f + k_footer_h);
            dl->AddRectFilled(ImVec2(ix, rule_y), ImVec2(plate.Max.x - px(1.f), rule_y + px(1.f)),
                              mo::with_alpha(c_border, alpha));

            ImFont* f10 = font_regular(k_label_size);
            draw_text_tracked(dl, f10, ImVec2(bx, rule_y + px(12.f) + line_top(f10, px(15.f))),
                              mo::with_alpha(c_muted_foreground, alpha), i18n::tr("ACTIVE VIEW"),
                              px(1.6f));

            draw_text(dl, f14, ImVec2(bx, rule_y + px(31.f) + line_top(f14, px(leading_sm))),
                      mo::with_alpha(c_foreground, alpha), crumb_text);

            const char* hint = "Press Ctrl+B to toggle";
            const float hw = text_width(f12, hint);
            draw_text(dl, f12,
                      ImVec2(plate.Max.x - px(29.f) - hw,
                             rule_y + px(35.f) + line_top(f12, px(leading_xs))),
                      mo::with_alpha(c_muted_foreground, alpha), hint);
        }

        {
            const int hit = morphing_search_overlay(plate, k_search, IM_ARRAYSIZE(k_search), alpha);
            if (hit >= 0)
            {
                // A hit names a tab as well as a route, so landing from search
                // lights up the same rail row a click would have.
                s.active = k_search[hit].destination;
                s.sub_index[route_index(s.active)] = k_search[hit].sub;
            }
        }

        switch (profile_menu(plate, alpha))
        {
        case profile_sign_out:
            sign_out = true;
            break;
        case profile_open_page:
            s.active = route::profile;
            break;
        case profile_open_preferences:
            s.active = route::preferences;
            break;
        default:
            break;
        }

        flush_overlays();
        toasts_draw(plate);
    }
    ImGui::End();
    return sign_out;
}
} // namespace szk
