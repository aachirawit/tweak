#include "ui/screens/page_renderer.h"

#include "application/brand.h"
#include "assets/avatars.h"
#include "backend/activity_log.h"
#include "backend/cleanup_tweaks.h"
#include "backend/debloat_tweaks.h"
#include "backend/gpu_tweaks.h"
#include "backend/hardware_info.h"
#include "backend/os_tweaks.h"
#include "backend/power_plan.h"
#include "backend/reshade_manager.h"
#include "backend/system_monitor.h"
#include "core/product_info.h"
#include "ui/controls/form_controls.h"
#include "ui/controls/scroll.h"
#include "ui/controls/widgets.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace solace
{
namespace
{

bool check(const char* id, const ImVec2& pos, bool* checked, const char* label)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::PushID(id);
    checkbox_state* st = ui_runtime::animation_state<checkbox_state>(window->GetID("cb"));
    checkbox_update(*st, *checked, ImGui::GetIO().DeltaTime);
    const bool hit = checkbox_draw("c", *st, checked, label, pos, false);
    ImGui::PopID();
    return hit;
}

bool action(const char* id, const ImVec2& pos, float width, button_state state, const char* label)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::PushID(id);
    stateful_button_state* st =
        ui_runtime::animation_state<stateful_button_state>(window->GetID("btn"));
    stateful_button_update(*st, state, label, ImGui::GetIO().DeltaTime);
    const bool hit = stateful_button_draw("b", *st, state, pos, px(width), false);
    ImGui::PopID();
    return hit;
}

float heading(ImDrawList* dl, const ImVec2& pos, const char* title, const char* sub, float width,
              float alpha)
{
    ImFont* h = font_semibold(20.f);
    draw_text_tracked(dl, h, ImVec2(pos.x, pos.y + line_top(h, px(28.f))),
                      mo::with_alpha(c_foreground, alpha), title, px(-0.4f));

    ImFont* d = font_regular(text_sm);
    const int lines = ImMax(1, wrapped_line_count(d, sub, width));
    draw_text_wrapped(dl, d, ImVec2(pos.x, pos.y + px(32.f)),
                      mo::with_alpha(c_muted_foreground, alpha), sub, width, px(leading_sm));

    return px(32.f) + px(leading_sm) * (float)lines + px(12.f);
}

void panel(ImDrawList* dl, const ImRect& r, float alpha)
{
    dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_card, 0.5f * alpha), px(16.f));
    dl->AddRect(ImVec2(r.Min.x + px(0.5f), r.Min.y + px(0.5f)),
                ImVec2(r.Max.x - px(0.5f), r.Max.y - px(0.5f)), mo::with_alpha(c_border, alpha),
                px(16.f), px(1.f), ImDrawFlags_None);
}

float pill(ImDrawList* dl, const ImVec2& at, const char* text, ImU32 col, float alpha)
{
    ImFont* f = font_medium(text_xs);
    const float w = text_width(f, text) + px(16.f);
    const float h = px(22.f);

    dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), mo::with_alpha(col, 0.13f * alpha), h * 0.5f);
    draw_text(dl, f, ImVec2(at.x + px(8.f), at.y + h * 0.5f - f->LegacySize * 0.5f),
              mo::with_alpha(col, alpha), text);
    return w;
}

void meter(ImDrawList* dl, const ImRect& r, float t, ImU32 col, float alpha)
{
    const float h = r.GetHeight();
    dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_muted_foreground, 0.16f * alpha), h * 0.5f);
    const float fill = r.GetWidth() * ImClamp(t, 0.f, 1.f);
    if (fill > 0.5f)
        dl->AddRectFilled(r.Min, ImVec2(r.Min.x + fill, r.Max.y), mo::with_alpha(col, alpha),
                          h * 0.5f);
}

float series_at(const float* v, int n, float u)
{
    if (n <= 0)
        return 0.f;
    if (n == 1)
        return v[0];
    const float f = ImClamp(u, 0.f, 1.f) * (float)(n - 1);
    const int i = ImMin((int)f, n - 2);
    const float k = f - (float)i;

    const float e = k * k * (3.f - 2.f * k);
    return v[i] + (v[i + 1] - v[i]) * e;
}

float edge_window(float x, float w, float fade)
{
    if (fade <= 0.f)
        return 1.f;
    const float a = ImClamp(x / fade, 0.f, 1.f);
    const float b = ImClamp((w - x) / fade, 0.f, 1.f);
    const float t = ImMin(a, b);
    return t * t * (3.f - 2.f * t);
}

void series_fill(ImDrawList* dl, const ImRect& r, const float* v, int n, float lo, float hi,
                 ImU32 col, float alpha, float reveal, float fade = 0.f)
{
    const float span = ImMax(hi - lo, 1e-4f);
    const float full = ImMax(r.GetWidth(), 1.f);
    const float w = full * ImClamp(reveal, 0.f, 1.f);
    const float step = px(2.f);

    for (float sx = 0.f; sx < w; sx += step)
    {
        const float x0 = r.Min.x + sx;
        const float x1 = ImMin(x0 + step, r.Min.x + w);
        const float u = (sx + step * 0.5f) / full;
        const float y = r.Max.y - (series_at(v, n, u) - lo) / span * r.GetHeight();

        const float a = alpha * edge_window(sx + step * 0.5f, full, fade);
        dl->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(x1, r.Max.y),
                                    mo::with_alpha(col, 0.26f * a), mo::with_alpha(col, 0.26f * a),
                                    mo::with_alpha(col, 0.01f * a), mo::with_alpha(col, 0.01f * a));
    }
}

void series_line(ImDrawList* dl, const ImRect& r, const float* v, int n, float lo, float hi,
                 ImU32 col, float alpha, float reveal, float thickness, float fade = 0.f)
{
    const float span = ImMax(hi - lo, 1e-4f);
    const float full = ImMax(r.GetWidth(), 1.f);
    const float w = full * ImClamp(reveal, 0.f, 1.f);
    const float step = px(2.f);

    auto at = [&](float sx)
    {
        return ImVec2(r.Min.x + sx,
                      r.Max.y - (series_at(v, n, sx / full) - lo) / span * r.GetHeight());
    };

    if (fade <= 0.f)
    {
        dl->PathClear();
        for (float sx = 0.f; sx <= w; sx += step)
            dl->PathLineTo(at(sx));
        if (dl->_Path.Size > 1)
            dl->PathStroke(mo::with_alpha(col, alpha), thickness, ImDrawFlags_None);
        return;
    }

    for (float sx = 0.f; sx < w; sx += step)
    {
        const float nx = ImMin(sx + step, w);
        const float a = alpha * edge_window(sx + (nx - sx) * 0.5f, full, fade);
        dl->AddLine(at(sx), at(nx), mo::with_alpha(col, a), thickness);
    }
}

void sparkline(ImDrawList* dl, const ImRect& r, const float* v, int n, ImU32 col, float alpha,
               float reveal)
{
    float lo = v[0], hi = v[0];
    for (int i = 1; i < n; i++)
    {
        lo = ImMin(lo, v[i]);
        hi = ImMax(hi, v[i]);
    }

    const float pad = ImMax((hi - lo) * 0.18f, 0.5f);
    lo -= pad;
    hi += pad;

    const float fade = px(22.f);
    series_fill(dl, r, v, n, lo, hi, col, alpha, reveal, fade);
    series_line(dl, r, v, n, lo, hi, col, alpha, reveal, px(1.5f), fade);
}

void row_label(ImDrawList* dl, const ImVec2& pos, const char* label, const char* hint, float alpha,
               float max_width = 0.f)
{
    ImFont* f = font_medium(text_sm);
    if (max_width > 0.f)
        draw_text_ellipsis(dl, f, ImVec2(pos.x, pos.y), mo::with_alpha(c_foreground, alpha), label,
                           max_width);
    else
        draw_text(dl, f, ImVec2(pos.x, pos.y), mo::with_alpha(c_foreground, alpha), label);

    if (hint)
    {

        ImFont* g = font_medium(text_xs);
        if (max_width > 0.f)
            draw_text_ellipsis(dl, g, ImVec2(pos.x, pos.y + px(18.f)),
                               mo::with_alpha(c_muted_foreground, alpha), hint, max_width);
        else
            draw_text(dl, g, ImVec2(pos.x, pos.y + px(18.f)),
                      mo::with_alpha(c_muted_foreground, alpha), hint);
    }
}

bool row_hit(ImDrawList* dl, const char* id, const ImRect& r, float alpha, bool draw_hover = true)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::PushID(id);
    const ImGuiID item = window->GetID("row");
    ImGui::PopID();

    ImGui::SetCursorScreenPos(r.Min);
    ImGui::ItemSize(ImVec2(0.f, 0.f));
    ImGui::ItemAdd(r, item);

    bool hovered = false, held = false;
    const bool pressed = !pointer_claimed() && ImGui::ButtonBehavior(r, item, &hovered, &held);

    if (draw_hover && (hovered || held))
        dl->AddRectFilled(r.Min, r.Max,
                          mo::with_alpha(c_foreground, (held ? 0.06f : 0.035f) * alpha), px(10.f));

    return pressed;
}

void empty_state(ImDrawList* dl, const ImRect& card, const char* msg, float alpha)
{
    ImFont* f = font_regular(text_sm);
    draw_text(dl, f,
              ImVec2(card.GetCenter().x - text_width(f, msg) * 0.5f,
                     card.GetCenter().y - f->LegacySize * 0.5f),
              mo::with_alpha(c_muted_foreground, alpha), msg);
}

void hairline(ImDrawList* dl, const ImRect& card, float y, float alpha)
{
    dl->AddRectFilled(ImVec2(card.Min.x + px(sp_4), y - px(0.5f)),
                      ImVec2(card.Max.x - px(sp_4), y + px(0.5f)), mo::with_alpha(c_border, alpha));
}

void chip(ImDrawList* dl, const ImVec2& at, float size, float round, const char* label, ImU32 bg,
          ImU32 fg, int person = -1)
{
    if (person >= 0 && avatars::draw(dl, avatars::other(person), at, size, 1.f))
        return;

    dl->AddRectFilled(at, ImVec2(at.x + size, at.y + size), bg, round);

    ImFont* f = font_semibold(text_xs);
    draw_text(dl, f,
              ImVec2(at.x + size * 0.5f - text_width(f, label) * 0.5f,
                     at.y + size * 0.5f - f->LegacySize * 0.5f),
              fg, label);
}

void initials_of(const char* name, char out[3])
{
    out[0] = name && *name ? name[0] : '?';
    const char* space = name ? strchr(name, ' ') : nullptr;
    out[1] = space ? space[1] : 0;
    out[2] = 0;
}

enum act_kind
{
    act_stage = 0,
    act_note,
    act_shipped,
    act_task,
    act_authors,
    act_quiet
};

const ImU32 k_act_tone[] = {
    IM_COL32(0x4C, 0x8D, 0xF6, 0xFF),
    IM_COL32(0x9A, 0x7C, 0xF7, 0xFF),
    c_success,
    c_amber_400,
    IM_COL32(0x36, 0xB8, 0xC0, 0xFF),
    c_muted_foreground,
};

struct activity_row
{
    const char* who;
    const char* what;
    const char* when;
    act_kind kind;
};

const activity_row k_activity[] = {
    {"Graphics", "set Shadows to High", "12m ago", act_stage},
    {"Camera", "turned Depth of field on", "48m ago", act_note},
    {"Display", "set Film grain to 25%", "2h ago", act_shipped},
    {"Audio", "muted the menu music", "5h ago", act_task},
    {"Profile", "saved the Competitive preset", "Yesterday", act_stage},
    {"Engine", "loaded SZK 1.4.2", "Yesterday", act_authors},
    {"Cache", "cleared 14 stale shaders", "Monday", act_quiet},
};

float aside_head(ImDrawList* dl, const ImVec2& pos, ImU32 col, float alpha, const char* label)
{
    ImFont* lf = font_regular(10.f);
    draw_text_tracked(dl, lf, ImVec2(pos.x, pos.y + line_top(lf, px(15.f))),
                      mo::with_alpha(col, alpha), label, px(1.6f));
    return px(24.f);
}

struct stat_line
{
    const char* label;
    const char* value;
};

float aside_stats(ImDrawList* dl, const ImVec2& pos, float width, float alpha, const char* title,
                  const stat_line* rows, int count, float card_gap = 0.f)
{
    float y = pos.y + aside_head(dl, pos, c_muted_foreground, alpha, title) + card_gap;

    const float row_h = px(38.f);
    const ImRect card(ImVec2(pos.x, y),
                      ImVec2(pos.x + width, y + px(sp_3) * 2.f + row_h * (float)count));
    panel(dl, card, alpha);

    ImFont* lf = font_regular(text_sm);
    ImFont* vf = font_medium(text_sm);

    for (int i = 0; i < count; i++)
    {
        const float ry = card.Min.y + px(sp_3) + row_h * (float)i;
        const float mid = ry + row_h * 0.5f;

        if (i > 0)
            hairline(dl, card, ry, alpha * 0.9f);

        const float vw = text_width(vf, rows[i].value);
        draw_text_ellipsis(dl, lf, ImVec2(card.Min.x + px(sp_4), mid - lf->LegacySize * 0.5f),
                           mo::with_alpha(c_muted_foreground, alpha), rows[i].label,
                           width - px(sp_4) * 2.f - vw - px(12.f));
        draw_text(dl, vf, ImVec2(card.Max.x - px(sp_4) - vw, mid - vf->LegacySize * 0.5f),
                  mo::with_alpha(c_foreground, alpha), rows[i].value);
    }

    return (card.Max.y - pos.y);
}

struct badge_line
{
    const char* label;
    const char* value;
    badge_status tone;
};

float aside_badges(ImDrawList* dl, const ImVec2& pos, float width, float alpha, const char* title,
                   const badge_line* rows, int count, float card_gap = 0.f)
{
    float y = pos.y + aside_head(dl, pos, c_muted_foreground, alpha, title) + card_gap;

    const float row_h = px(38.f);
    const ImRect card(ImVec2(pos.x, y),
                      ImVec2(pos.x + width, y + px(sp_3) * 2.f + row_h * (float)count));
    panel(dl, card, alpha);

    ImFont* lf = font_regular(text_sm);

    for (int i = 0; i < count; i++)
    {
        const float ry = card.Min.y + px(sp_3) + row_h * (float)i;
        const float mid = ry + row_h * 0.5f;

        if (i > 0)
            hairline(dl, card, ry, alpha * 0.9f);

        const float bw = badge_width(rows[i].value);
        draw_text_ellipsis(dl, lf, ImVec2(card.Min.x + px(sp_4), mid - lf->LegacySize * 0.5f),
                           mo::with_alpha(c_muted_foreground, alpha), rows[i].label,
                           width - px(sp_4) * 2.f - bw - px(12.f));

        char bid[24];
        ImFormatString(bid, IM_ARRAYSIZE(bid), "%s%d", title, i);
        badge(bid, dl, ImVec2(card.Max.x - px(sp_4) - bw, mid - px(12.f)), rows[i].value,
              rows[i].tone, false, alpha);
    }

    return (card.Max.y - pos.y);
}

float aside_lines(ImDrawList* dl, const ImVec2& pos, float width, float alpha, const char* title,
                  const char* const* lines, int count, float card_gap = 0.f)
{
    float y = pos.y + aside_head(dl, pos, c_muted_foreground, alpha, title) + card_gap;

    ImFont* f = font_regular(text_sm);
    const float inner = width - px(sp_4) * 2.f;

    float body = px(sp_3) * 2.f;
    for (int i = 0; i < count; i++)
        body += px(leading_sm) * (float)ImMax(1, wrapped_line_count(f, lines[i], inner)) + px(sp_3);
    body -= px(sp_3);

    const ImRect card(ImVec2(pos.x, y), ImVec2(pos.x + width, y + body));
    panel(dl, card, alpha);

    float ly = card.Min.y + px(sp_3);
    for (int i = 0; i < count; i++)
    {
        const int n = ImMax(1, wrapped_line_count(f, lines[i], inner));
        draw_text_wrapped(dl, f, ImVec2(card.Min.x + px(sp_4), ly),
                          mo::with_alpha(c_muted_foreground, alpha), lines[i], inner,
                          px(leading_sm));
        ly += px(leading_sm) * (float)n + px(sp_3);
    }

    return (card.Max.y - pos.y);
}

float activity(ImDrawList* dl, const ImVec2& pos, float width, float alpha)
{
    ImFont* lf = font_regular(10.f);
    draw_text_tracked(dl, lf, ImVec2(pos.x, pos.y + line_top(lf, px(15.f))),
                      mo::with_alpha(c_muted_foreground, alpha), "RECENT ACTIVITY", px(1.6f));

    const float top = pos.y + px(24.f);
    const int rows = IM_ARRAYSIZE(k_activity);
    const ImRect card(ImVec2(pos.x, top),
                      ImVec2(pos.x + width, top + px(sp_4) * 2.f + px(52.f) * (float)rows));
    panel(dl, card, alpha);

    float ry = card.Min.y + px(sp_4);
    for (int i = 0; i < rows; i++)
    {

        const ImVec2 av(card.Min.x + px(sp_4), ry + px(10.f));
        char initial[3];
        initials_of(k_activity[i].who, initial);
        chip(dl, av, px(28.f), px(14.f), initial, mo::with_alpha(c_foreground, 0.06f * alpha),
             mo::with_alpha(c_muted_foreground, alpha), i);

        {
            const ImVec2 at(av.x + px(22.f), av.y + px(22.f));
            const ImU32 tone =
                k_act_tone[ImClamp((int)k_activity[i].kind, 0, IM_ARRAYSIZE(k_act_tone) - 1)];
            dl->AddCircleFilled(at, px(6.f), mo::with_alpha(c_background, alpha));
            dl->AddCircleFilled(at, px(4.f), mo::with_alpha(tone, alpha));
        }

        row_label(dl, ImVec2(card.Min.x + px(56.f), ry + px(8.f)), k_activity[i].who,
                  k_activity[i].what, alpha);

        ImFont* wf = font_regular(text_xs);
        draw_text(dl, wf,
                  ImVec2(card.Max.x - px(sp_4) - text_width(wf, k_activity[i].when), ry + px(10.f)),
                  mo::with_alpha(c_muted_foreground, alpha), k_activity[i].when);

        if (i + 1 < rows)
            dl->AddRectFilled(ImVec2(card.Min.x + px(sp_4), ry + px(52.f) - px(0.5f)),
                              ImVec2(card.Max.x - px(sp_4), ry + px(52.f) + px(0.5f)),
                              mo::with_alpha(c_border, alpha));

        ry += px(52.f);
    }
    return card.Max.y - pos.y;
}

constexpr int k_module_count = 55;
constexpr int k_task_count = 8;
constexpr int k_message_count = 8;
constexpr int k_range_sample_count = 13;
constexpr int k_sys_history = 60; // rolling live-sample window for the Dashboard

struct page_state
{

    smooth_scroll body[route_count];
    float content[route_count] = {};

    button_state settings_apply_btn = btn_idle;
    float settings_apply_timer = 0.f;

    button_state szk_apply_btn = btn_idle;
    float szk_apply_timer = 0.f;

    bool mobo_queried = false;
    backend::motherboard_info mobo;

    bool gpu_vendor_queried = false;
    bool has_nvidia_gpu = false;
    bool has_amd_gpu = false;

    bool row_applied[k_module_count] = {};
    bool row_status_dirty = true;
    int row_status_checked_sub = -999;

    button_state restore_point_btn = btn_idle;
    float restore_point_timer = 0.f;

    bool reshade_road_mod = false;
    button_state reshade_install_btn = btn_idle;
    float reshade_install_timer = 0.f;
    button_state reshade_uninstall_btn = btn_idle;
    float reshade_uninstall_timer = 0.f;

    float dash_reveal = 0.f;

    // Dashboard's own scan, kept separate from the Settings page's
    // row_applied/row_status_dirty pair so neither can leave the other stale.
    // Registry reads, so this runs on page entry and on demand - never per
    // frame.
    bool dash_scanned = false;
    bool dash_applied[k_module_count] = {};
    int dash_findings[k_module_count] = {}; // module indices, worst first
    int dash_finding_count = 0;
    int dash_checkable = 0;
    int dash_ok = 0;
    button_state dash_optimize_btn = btn_idle;
    float dash_optimize_timer = 0.f;
    button_state dash_rescan_btn = btn_idle;
    float dash_rescan_timer = 0.f;
    backend::power_plan_info dash_power;
    int dash_fix_request = -1; // module index a row's Fix button asked for

    int last_nav = -1;
    float entered = 0.f;

    bool pref_on[7] = {true, true, false, true, false, true, false};
    bool pref_open[3] = {true, true, false};
    mo::spring pref_detail[7];
    int pref_theme = 1;
    int pref_digest_day = 0;
    float pref_quiet = 0.34f;
    float reset_cascade = 1e6f;

    int notif_tab = 0;
    bool notif_read[7] = {false, false, true, false, true, true, true};
    bool notif_gone[7] = {};
    mo::spring notif_height[7];
    float notif_slide[7] = {};
    float mark_cascade = 1e6f;
    int notif_undo = -1;
    float undo_timer = 0.f;

    int stage = 1;
    int author = 0;
    float projected_installs = 48000.f;
    button_state save = btn_idle;
    float save_timer = 0.f;

    bool automations[4] = {true, true, false, true};

    bool tasks[k_task_count] = {true, false, false, true, false, false, true, false};
    button_state sweep = btn_idle;
    float sweep_timer = 0.f;

    int range = 1;

    bool sys_mon_inited = false;
    float sys_poll_t = 1e6f;
    backend::system_snapshot sys_snap;
    backend::system_snapshot sys_prev;
    float sys_cpu_hist[k_sys_history] = {};
    float sys_ram_hist[k_sys_history] = {};
    float sys_disk_hist[k_sys_history] = {};
    float sys_ping_hist[k_sys_history] = {};
    int sys_hist_filled = 0;

    bool profile_prefs[3] = {true, false, true};
    button_state profile_save = btn_idle;
    float profile_timer = 0.f;

    button_state check_updates_btn = btn_idle;
    float check_updates_timer = 0.f;

    int search_tab = 0;
    bool search_bodies = true;
    button_state clear_history = btn_idle;
    float clear_timer = 0.f;
    bool history_cleared = false;

    int answer_style = 0;
    float creativity = 0.4f;
    button_state ask = btn_idle;
    float ask_timer = 0.f;

    int message_tab = 0;
    bool message_read[k_message_count] = {false, true, false, true, true, true, false, true};
    button_state mark_all = btn_idle;
    float mark_timer = 0.f;

    int notes_tab = 0;
};

page_state& state()
{
    static page_state s;
    return s;
}

const char* const k_stage_options[] = {"Draft", "Internal", "Playtest", "Candidate", "Live"};
const char* const k_author_options[] = {brand::user_name, "Corvid", "Kestrel", "Unassigned"};

// How loudly the Dashboard argues for a tweak that is currently off. This is
// about what the machine loses while it stays off, not about how dangerous
// applying it is - none of these are dangerous, or they would not be here.
enum finding_severity
{
    finding_none = 0, // never surfaced on the Dashboard
    finding_note,     // worth doing, costs little to leave alone
    finding_advised,  // a measurable cost every session
    finding_urgent,   // hurts frame pacing or aim directly
};

struct module_row
{
    const char* name;
    const char* role;
    int category;              // 0 = other (All Settings only), 1 = gaming, 2 = network
    bool (*check)() = nullptr; // live applied/not-applied status for the
                               // green/red dot; null = no cheap way to
                               // check (one-shot action, or applied via
                               // netsh/bcdedit/powercfg output/nvidia-smi)

    // The Dashboard's "Needs attention" list is built from these two. A row
    // earns them only if it has a check() (nothing else can tell whether it
    // is off) and a specific, honest answer to "what does leaving this cost
    // me". Rows without a cost worth a sentence stay off the Dashboard and
    // live in Settings, which is the point of the split.
    finding_severity severity = finding_none;
    const char* cost = nullptr;
};
// Curated from github.com/HickerDicker/SapphireOS's tweak categories
// (Others/, PostInstall/GPU, PostInstall/Others/Network). Each row is a
// one-shot "Apply" action, matching the source repo's own apply-scripts.
const module_row k_modules[] = {
    {"Disable Action Center", "Others", 0, backend::check_disable_action_center},
    {"Classic Alt-Tab", "Others", 0, backend::check_classic_alt_tab},
    {"Disable DMA Remapping", "Others", 0},
    {"Disable GPU MPO", "Gaming", 1, backend::check_disable_gpu_mpo, finding_advised,
     "Multiplane overlay flickers and drops frames on several driver builds."},
    {"USB Polling Rate Override", "Gaming", 1},
    {"Optimize Network Stack", "Network", 2},
    {"Disable Background Services", "Others", 0, backend::check_background_services_disabled,
     finding_advised, "SysMain and Windows Search index in the background while you play."},
    {"Low Latency TCP", "Network", 2},
    {"Disable USB Selective Suspend", "Gaming", 1, backend::check_usb_selective_suspend_disabled,
     finding_advised, "Windows may power down the mouse or keyboard mid-session."},
    {"BCD Timer Tweaks", "Gaming", 1},
    {"Szk Network Tweaks", "Network", 2},
    {"Network Driver Tweaks", "Network", 2},
    {"Process Priority Tweaks", "Gaming", 1},
    {"Network Hardening", "Network", 2},
    {"Privacy & Telemetry", "Others", 0},
    {"UI & Explorer Tweaks", "Others", 0},
    {"Debloat Services", "Others", 0},
    {"Disable HDCP", "NVIDIA", 4, backend::check_nvidia_disable_hdcp},
    {"Disable Telemetry", "NVIDIA", 4, backend::check_nvidia_disable_telemetry},
    {"Disable ECC", "NVIDIA", 4},
    {"Unrestricted P-State", "NVIDIA", 4, backend::check_nvidia_unrestricted_pstate},
    {"Unrestricted Clock Policy", "NVIDIA", 4},
    {"FiveM QoS Priority", "Network", 2, backend::check_fivem_qos_priority, finding_advised,
     "FiveM's packets are unmarked, so QoS-aware routers give them no priority."},
    {"FiveM Cache Auto-Clear", "Gaming", 1, backend::check_fivem_cache_autoclear},
    {"FiveM High CPU Priority", "Gaming", 1, backend::check_fivem_high_cpu_priority,
     finding_advised, "FiveM competes with background work for CPU time instead of winning it."},
    {"AMD Software Debloat", "AMD", 5},
    {"Disable AMD Chill", "AMD", 5, backend::check_amd_disable_chill},
    {"AMD Background Services", "AMD", 5, backend::check_amd_background_services},
    {"Disable PCIe ASPM", "Gaming", 1},
    {"Disable Game Bar & DVR", "Gaming", 1, backend::check_game_bar_dvr, finding_urgent,
     "Background recording holds GPU time and adds jitter to frame pacing."},
    {"Mouse 1:1 Raw Aim", "Gaming", 1, backend::check_mouse_raw_1to1, finding_urgent,
     "Pointer acceleration is on, so the same hand movement aims differently each time."},
    {"Keyboard Rapid Response", "Gaming", 1, backend::check_keyboard_rapid_response, finding_note,
     "Key repeat still runs at the desktop default delay."},
    {"Flush DNS Cache", "Network", 2},
    {"Renew IP Lease", "Network", 2},
    {"Clear ARP Cache", "Network", 2},
    {"Reset Winsock Catalog", "Network", 2},
    {"Register DNS Record", "Network", 2},
    {"Disable Network Throttling", "Network", 2, backend::check_network_throttling, finding_urgent,
     "Windows caps non-multimedia traffic at ten packets per millisecond."},
    {"Reset Filter Keys Timing", "Gaming", 1, backend::check_filter_keys_timing},
    {"Hardware GPU Scheduling", "Gaming", 1, backend::check_hardware_gpu_scheduling},
    {"Visual Effects: Performance", "Others", 0, backend::check_visual_effects_performance,
     finding_note, "Window animations and shadows cost frames on a busy GPU."},
    {"Power Plan: High Performance", "Gaming", 1},
    {"Remove Startup Delay", "Others", 0, backend::check_startup_delay, finding_note,
     "Explorer holds startup apps back for several seconds after logon."},
    {"Disable NTFS Last Access", "Others", 0, backend::check_ntfs_last_access, finding_note,
     "Every file read also writes a timestamp back to the disk."},
    {"Disable Advertising ID", "Others", 0, backend::check_advertising_id},
    {"Disable Tips & Suggested Apps", "Others", 0, backend::check_tips_and_suggestions},
    {"FiveM GPU: High Performance", "Gaming", 1, backend::check_fivem_gpu_high_performance,
     finding_advised, "On a laptop, FiveM may be running on the integrated GPU."},
    {"FiveM Defender Exclusion", "Gaming", 1},
    {"Clear Temp Files", "Cleanup", 6},
    {"Clear Prefetch", "Cleanup", 6},
    {"Clear Windows Update Cache", "Cleanup", 6},
    {"Clear Shader Cache", "Cleanup", 6},
    {"Clear Thumbnail Cache", "Cleanup", 6},
    {"Empty Recycle Bin", "Cleanup", 6},
    {"FiveM Exclusive Fullscreen", "Gaming", 1,
     backend::check_fivem_disable_fullscreen_optimizations},
};
static_assert(IM_ARRAYSIZE(k_modules) == k_module_count, "module_rows is per module");

// Applies one k_modules row by index. Split out of the Settings page's
// "Apply" button so the Dashboard's "Optimize now" runs the same code path
// rather than a second copy of it that drifts as rows are added.
enum apply_result
{
    apply_unwired = 0, // no backend behind this row yet
    apply_ok,
    apply_failed,
};

// A tweak that is off but reads back cleanly is worth arguing for; one whose
// state cannot be read is not, so the score counts only rows with a check().
// That keeps the number honest: it is "N of the M things we can actually
// verify", never a guess padded out to look thorough.
struct scan_result
{
    bool applied[k_module_count];
    int findings[k_module_count]; // module indices, worst severity first
    int finding_count;
    int checkable;
    int ok;
};

scan_result scan_modules()
{
    scan_result r{};

    for (int i = 0; i < k_module_count; i++)
    {
        if (!k_modules[i].check)
            continue;

        r.checkable++;
        r.applied[i] = k_modules[i].check();
        if (r.applied[i])
            r.ok++;
        else if (k_modules[i].severity != finding_none)
            r.findings[r.finding_count++] = i;
    }

    // Loudest first, and stable within a severity so the list does not
    // reshuffle under the cursor between scans.
    for (int a = 1; a < r.finding_count; a++)
    {
        const int key = r.findings[a];
        int b = a - 1;
        while (b >= 0 && k_modules[r.findings[b]].severity < k_modules[key].severity)
        {
            r.findings[b + 1] = r.findings[b];
            b--;
        }
        r.findings[b + 1] = key;
    }

    return r;
}

ImU32 severity_color(finding_severity s)
{
    return s == finding_urgent    ? c_destructive
           : s == finding_advised ? c_amber_500
                                  : c_muted_foreground;
}

const char* severity_label(finding_severity s)
{
    return s == finding_urgent ? "Urgent" : s == finding_advised ? "Advised" : "Optional";
}

apply_result apply_module(int index)
{
    bool ok = false;
    bool wired = true;
    switch (index)
    {
    case 0:
        ok = backend::disable_action_center();
        break;
    case 1:
        ok = backend::set_classic_alt_tab();
        break;
    case 2:
        wired = false; // DMA remapping not wired yet
        break;
    case 3:
        ok = backend::disable_gpu_mpo();
        break;
    case 4:
        wired = false; // needs the hidusbf driver installed
        break;
    case 5:
        ok = backend::set_network_autotuning(true);
        break;
    case 6:
        ok = backend::set_background_services_disabled(true);
        break;
    case 7:
        ok = backend::set_low_latency_tcp(true);
        break;
    case 8:
        ok = backend::set_usb_selective_suspend_disabled(true);
        break;
    case 9:
        ok = backend::set_bcd_tweaks(true);
        break;
    case 10:
        ok = backend::apply_sapphire_network_defaults();
        break;
    case 11:
        ok = backend::apply_network_driver_tweaks();
        break;
    case 12:
        ok = backend::apply_gaming_priority_tweaks();
        break;
    case 13:
        ok = backend::apply_network_hardening();
        break;
    case 14:
        ok = backend::apply_privacy_telemetry_tweaks();
        break;
    case 15:
        ok = backend::apply_ui_explorer_tweaks();
        break;
    case 16:
        ok = backend::apply_debloat_services();
        break;
    case 17:
        ok = backend::nvidia_disable_hdcp();
        break;
    case 18:
        ok = backend::nvidia_disable_telemetry();
        break;
    case 19:
        ok = backend::nvidia_disable_ecc();
        break;
    case 20:
        ok = backend::nvidia_unrestricted_pstate();
        break;
    case 21:
        ok = backend::nvidia_unrestricted_clocks();
        break;
    case 22:
        ok = backend::apply_fivem_qos_priority();
        break;
    case 23:
        ok = backend::set_fivem_cache_autoclear(true);
        break;
    case 24:
        ok = backend::set_fivem_high_cpu_priority(true);
        break;
    case 25:
        ok = backend::amd_disable_adrenalin_bloat();
        break;
    case 26:
        ok = backend::amd_disable_chill();
        break;
    case 27:
        ok = backend::amd_disable_background_services();
        break;
    case 28:
        ok = backend::disable_pcie_aspm();
        break;
    case 29:
        ok = backend::disable_game_bar_dvr();
        break;
    case 30:
        ok = backend::set_mouse_raw_1to1();
        break;
    case 31:
        ok = backend::set_keyboard_rapid_response();
        break;
    case 32:
        ok = backend::flush_dns_cache();
        break;
    case 33:
        ok = backend::renew_ip_lease();
        break;
    case 34:
        ok = backend::clear_arp_cache();
        break;
    case 35:
        ok = backend::reset_winsock_catalog();
        break;
    case 36:
        ok = backend::register_dns_record();
        break;
    case 37:
        ok = backend::disable_network_throttling();
        break;
    case 38:
        ok = backend::reset_filter_keys_timing();
        break;
    case 39:
        ok = backend::disable_hardware_gpu_scheduling(true);
        break;
    case 40:
        ok = backend::set_visual_effects_performance();
        break;
    case 41:
        ok = backend::switch_power_plan_high_performance(true);
        break;
    case 42:
        ok = backend::remove_startup_delay();
        break;
    case 43:
        ok = backend::disable_ntfs_last_access();
        break;
    case 44:
        ok = backend::disable_advertising_id();
        break;
    case 45:
        ok = backend::disable_tips_and_suggestions();
        break;
    case 46:
        ok = backend::fivem_gpu_high_performance();
        break;
    case 47:
        ok = backend::fivem_defender_exclusion(true);
        break;
    case 48:
        ok = backend::clear_temp_files();
        break;
    case 49:
        ok = backend::clear_prefetch();
        break;
    case 50:
        ok = backend::clear_windows_update_cache();
        break;
    case 51:
        ok = backend::clear_shader_cache();
        break;
    case 52:
        ok = backend::clear_thumbnail_cache();
        break;
    case 53:
        ok = backend::empty_recycle_bin();
        break;
    case 54:
        ok = backend::fivem_disable_fullscreen_optimizations();
        break;
    default:
        break;
    }
    if (!wired)
        return apply_unwired;
    return ok ? apply_ok : apply_failed;
}

// Network Optimization's rows aren't independent — a couple of them are
// actions with a real "before/after" (a Winsock reset should happen before
// anything else, a DNS re-registration needs the fresh IP a lease renewal
// just got, a cache flush should happen after that so it can't hold a
// stale entry). Apply/display order follows this instead of array index
// for those rows; everything else keeps its natural index so this doesn't
// have to be filled in for every row.
int apply_priority(int index)
{
    switch (index)
    {
    case 35:
        return 10; // Reset Winsock Catalog — clean slate first
    case 34:
        return 20; // Clear ARP Cache
    case 33:
        return 30; // Renew IP Lease — fresh IP/gateway/DNS from DHCP
    case 36:
        return 40; // Register DNS Record — needs the new IP above
    case 32:
        return 50; // Flush DNS Cache — after re-registering, not before
    case 11:
        return 60; // Network Driver Tweaks — adapter-level, foundational
    case 5:
        return 70; // Optimize Network Stack
    case 7:
        return 80; // Low Latency TCP
    case 10:
        return 90; // Szk Network Tweaks
    case 13:
        return 100; // Network Hardening
    case 22:
        return 110; // FiveM QoS Priority
    case 37:
        return 120; // Disable Network Throttling
    default:
        return index * 1000;
    }
}

struct automation
{
    const char* name;
    const char* detail;
};
const automation k_automations[] = {
    {"Apply on launch", "The Competitive preset the moment the game starts"},
    {"Panic key", "Reset everything to defaults on End"},
    {"Re-detect", "Probe the GPU again after a driver change"},
    {"Weekly digest", "Send Monday 09:00 in local time"},
};

struct search_hit
{
    const char* query;
    const char* count;
};
const search_hit k_recent[] = {
    {"frame pacing", "18 results"},  {"key:insert overlay", "7 results"},
    {"input latency", "24 results"}, {"stutter", "3 results"},
    {"shader cache", "11 results"},
};
const search_hit k_saved[] = {
    {"Everything I tune", "Updated 2h ago"},
    {"Unstable presets", "Updated Monday"},
    {"Rendering, all", "Updated last week"},
};

struct chat_turn
{
    bool from_user;
    const char* text;
};
const chat_turn k_thread[] = {
    {true, "Why does frame time spike above 16 ms?"},
    {false, "Two reasons. Shadow distance updates every frame instead of every third, and "
            "motion blur is still active underneath it. Cache the value and update on change."},
    {true, "Write me the change check."},
};
const char* const k_answer_styles[] = {"Fast", "Balanced", "Thorough"};

struct message
{
    const char* who;
    const char* subject;
    const char* when;
    bool mention;
};
const message k_messages[] = {
    {"Corvid", "Shadows flicker over water", "09:41", true},
    {"Kestrel", "Re: controller deadzone", "08:12", false},
    {"SZK Lab", "Photo mode 2.1 is ready", "Yesterday", false},
    {"Halcyon", "Notes from the playtest", "Yesterday", true},
    {"Orrin", "Signed off - clean build inside", "Monday", false},
    {"Pell", "Projection is ready for review", "Monday", false},
    {"Vermillion", "Two players joined the playtest", "Friday", true},
    {"Cinder", "Archived 14 old presets", "Friday", false},
};
static_assert(IM_ARRAYSIZE(k_messages) == k_message_count, "message_read is per message");

struct note
{
    const char* title;
    const char* body;
    const char* meta;
    int bucket;
};

const note k_notes[] = {
    {"Shadows - what actually flickers",
     "It is not the shadow system. Two reports pin it to a texture stream that only "
     "fires on ultra, and both had render scale above 100%.",
     "You - 12m ago", 1},
    {"Playtest notes",
     "The build went out fine. What hurt was the week after: nobody owned the report list, "
     "so three of the six sat untouched.",
     "Kestrel - Yesterday", 1},
    {"Keybinds, next patch",
     "Grouping by category rather than by hand. Rough shape only so far - the modifier "
     "assumption is the part I am least sure about.",
     "You - Monday", 2},
    {"Settings guide rewrite",
     "Five steps down to three. The middle one was doing nothing that the first did not "
     "already say.",
     "Halcyon - Last week", 0},
    {"What SZK Lab actually asked for",
     "Not a rewrite. They want the preset export to carry the build number, which we "
     "already store and simply do not print.",
     "Corvid - Last week", 1},
    {"Versioning, thinking aloud",
     "Per-preset versions stop making sense the moment a patch ships a migrator. Nobody "
     "has asked yet, but Vermillion will.",
     "You - Last week", 2},
};
const char* const k_note_tabs[] = {"All", "Shared", "Drafts"};

struct field
{
    const char* label;
    const char* value;
};
const field k_profile_fields[] = {
    {"Full name", brand::user_name},
    {"GitHub", brand::user_github},
    {"Role", "Graphics and camera"},
    {"Time zone", "Europe/London (GMT+1)"},
};

struct preference
{
    const char* name;
    const char* detail;
};

const stat_line k_credits[] = {
    {"Built by", brand::author},
    {"Source", brand::repo},
    {"Runtime", "Dear ImGui 1.92.9b - DirectX 11"},
    {"Type", "Geist, through FreeType"},
};

const preference k_profile_prefs[] = {
    {"Patch emails", "Every patch, the day it goes live"},
    {"Weekly digest", "Monday 09:00, in your time zone"},
    {"Mentions", "Notify me when someone writes my name"},
};

struct kpi
{
    const char* label;
    float value;
    const char* prefix;
    const char* suffix;
    int decimals;
    const char* delta;
    bool good;
};

struct range_data
{
    kpi stats[4];
    const float spark[4][k_range_sample_count];
    int spark_n;
    const float frames[k_range_sample_count];
    int frame_count;
    const char* x_labels[k_range_sample_count];
    const char* axis_hi;
    const char* axis_mid;
    const char* total;
    const char* total_note;
};

const range_data k_ranges[3] = {

    {
        {{"CPU Usage", 42.f, "", "%", 0, "+3.1%", true},
         {"GPU Usage", 68.f, "", "%", 0, "+5.2%", true},
         {"CPU Temp", 54.f, "",
          " \xC2\xB0"
          "C",
          0,
          "-2\xC2\xB0"
          "C",
          true},
         {"GPU Temp", 61.f, "",
          " \xC2\xB0"
          "C",
          0,
          "+1\xC2\xB0"
          "C",
          true}},
        {{36, 39, 37, 41, 40, 43, 42, 0, 0, 0, 0, 0, 0},
         {60, 63, 61, 66, 64, 69, 68, 0, 0, 0, 0, 0, 0},
         {57, 56, 55, 55, 54, 53, 54, 0, 0, 0, 0, 0, 0},
         {58, 59, 60, 60, 61, 62, 61, 0, 0, 0, 0, 0, 0}},
        7,
        {34, 40, 37, 45, 42, 48, 42, 0, 0, 0, 0, 0, 0},
        7,
        {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun", 0, 0, 0, 0, 0, 0},
        "100%",
        "50%",
        "Avg 42%",
        "+3.1% on last week",
    },

    {
        {{"CPU Usage", 47.f, "", "%", 0, "+2.4%", true},
         {"GPU Usage", 71.f, "", "%", 0, "+4.0%", true},
         {"CPU Temp", 56.f, "",
          " \xC2\xB0"
          "C",
          0,
          "-1\xC2\xB0"
          "C",
          true},
         {"GPU Temp", 64.f, "",
          " \xC2\xB0"
          "C",
          0,
          "+2\xC2\xB0"
          "C",
          true}},
        {{40, 43, 41, 46, 44, 48, 46, 49, 47, 50, 0, 0, 0},
         {63, 66, 64, 69, 67, 71, 69, 73, 71, 74, 0, 0, 0},
         {59, 58, 58, 57, 57, 56, 56, 55, 55, 56, 0, 0, 0},
         {61, 62, 62, 63, 63, 64, 64, 65, 64, 65, 0, 0, 0}},
        10,
        {38, 44, 41, 48, 45, 52, 49, 55, 51, 58, 0, 0, 0},
        10,
        {"1", "4", "7", "10", "13", "16", "19", "22", "25", "28", 0, 0, 0},
        "100%",
        "50%",
        "Avg 47%",
        "+2.4% on last month",
    },

    {
        {{"CPU Usage", 51.f, "", "%", 0, "+6.0%", true},
         {"GPU Usage", 74.f, "", "%", 0, "+3.5%", true},
         {"CPU Temp", 58.f, "",
          " \xC2\xB0"
          "C",
          0,
          "+3\xC2\xB0"
          "C",
          false},
         {"GPU Temp", 67.f, "",
          " \xC2\xB0"
          "C",
          0,
          "+4\xC2\xB0"
          "C",
          false}},
        {{42, 45, 43, 48, 46, 51, 49, 54, 51, 57, 55, 58, 51},
         {66, 68, 67, 70, 69, 72, 71, 74, 72, 76, 74, 77, 74},
         {53, 54, 54, 55, 55, 56, 56, 57, 57, 58, 58, 59, 58},
         {60, 61, 61, 62, 62, 63, 64, 65, 65, 66, 66, 67, 67}},
        13,
        {40, 46, 43, 50, 47, 54, 51, 58, 54, 61, 58, 64, 51},
        13,
        {"W1", "W2", "W3", "W4", "W5", "W6", "W7", "W8", "W9", "W10", "W11", "W12", "W13"},
        "100%",
        "50%",
        "Avg 51%",
        "+6.0% on last quarter",
    },
};

struct stage_row
{
    const char* name;
    const char* count;
    const char* value;
    float weight;
};
const stage_row k_stages[] = {
    {"Draft", "38 patches", "1.42M", 1.00f},   {"Internal", "24 patches", "1.08M", 0.76f},
    {"Playtest", "16 patches", "742k", 0.52f}, {"Candidate", "9 patches", "418k", 0.29f},
    {"Live", "4 patches", "196k", 0.14f},
};

struct build_row
{
    const char* mod;
    const char* author;
    const char* stage;
    const char* value;
    const char* close;
    int person;
    int tone;
};
const build_row k_builds[] = {
    {"1.4.2 Hotfix", brand::user_name, "Candidate", "248k", "12 Sep", 0, 0},
    {"1.5 Seasons", "Corvid", "Playtest", "188k", "26 Sep", 1, 1},
    {"Photo mode", brand::user_name, "Internal", "84k", "3 Oct", 0, 2},
    {"Controller fix", "Kestrel", "Live", "61k", "8 Sep", 2, 3},
    {"Ray tracing", "Halcyon", "Draft", "26k", "17 Oct", 3, 2},
};

struct author_row
{
    const char* name;
    const char* figure;
    float attainment;
    int person;
};
const author_row k_authors[] = {
    {brand::user_name, "412k", 1.03f, 0},
    {"Corvid", "368k", 0.92f, 1},
    {"Kestrel", "286k", 0.71f, 2},
    {"Halcyon", "194k", 0.48f, 3},
};

enum pref_extra
{
    extra_none = 0,
    extra_theme,
    extra_digest,
    extra_quiet
};

struct pref_row
{
    const char* label;
    const char* off_text;
    const char* on_text;
    pref_extra extra;
};

struct pref_group
{
    const char* title;
    int first, count;
};

const pref_row k_prefs[] = {
    {"Match the system theme", "Locked to whichever you last picked.",
     "Follows the machine from dusk until it changes back.", extra_theme},
    {"Reduce motion", "Transitions run at their usual length.",
     "Everything still moves, but only far enough to say where it went.", extra_none},

    {"Patch emails", "Nothing about new patches will reach you.",
     "Every patch, the day it goes live.", extra_none},
    {"Weekly digest", "No summary is sent.", "One summary a week, in your own time zone.",
     extra_digest},
    {"Mentions", "Only counted, never announced.", "Notify me the moment someone writes my name.",
     extra_none},
    {"Quiet hours", "Notifications arrive whenever they arrive.",
     "Held until morning, unless someone marks it urgent.", extra_quiet},

    {"Usage analytics", "Nothing leaves this machine.", "Anonymous counts only - no content, ever.",
     extra_none},
};

const pref_group k_pref_groups[] = {
    {"Appearance", 0, 2},
    {"Notifications", 2, 4},
    {"Privacy", 6, 1},
};

const char* const k_theme_names[] = {"Light", "Dark", "System"};
const char* const k_digest_days[] = {"Monday", "Wednesday", "Friday", "Sunday"};

struct notification
{
    const char* who;
    const char* did;
    const char* about;
    const char* when;
    int day;
    int person;
    bool mention;
};

const notification k_notifications[] = {
    {"Corvid", "moved", "1.4.2 Hotfix to Candidate", "09:41", 0, 0, false},
    {"Kestrel", "mentioned you in", "Re: controller deadzone", "08:12", 0, 1, true},
    {"Halcyon", "closed", "Playtest notes", "Yesterday", 1, 2, false},
    {"Vermillion", "assigned you", "Draft the keybind layout", "Yesterday", 1, 3, true},
    {"SZK Lab", "shipped", "Photo mode 2.1", "Yesterday", 1, 4, false},
    {brand::user_name, "shared", "Keybinds, next patch", "Monday", 2, 5, false},
    {"Orrin", "commented on", "Ray tracing", "Monday", 2, 6, false},
};

const char* const k_notif_days[] = {"Today", "Yesterday", "Earlier"};
const char* const k_notif_tabs[] = {"All", "Unread", "Mentions"};

const badge_line k_aside_reports[] = {
    {"Unread", "2", badge_info},
    {"Mentions", "2", badge_warn},
    {"On my presets", "3", badge_good},
    {"Archived", "41", badge_neutral},
};

const stat_line k_aside_pipeline[] = {
    {"Committed", "418k"},
    {"Best case", "742k"},
    {"In test", "1.42M"},
    {"Live", "286k"},
};

const stat_line k_aside_tasks[] = {
    {"Due today", "2"},
    {"Due this week", "3"},
    {"Overdue", "1"},
    {"Done this week", "7"},
};

const stat_line k_aside_notes[] = {
    {"Bugs", "6"},
    {"Keybinds", "4"},
    {"Playtests", "3"},
    {"Guides", "2"},
};

const stat_line k_aside_runs[] = {
    {"Apply on launch", "4m ago"},
    {"Panic key", "1h ago"},
    {"Weekly digest", "Monday"},
    {"Re-detect", "Paused"},
};

const stat_line k_aside_sessions[] = {
    {"This machine", "Now"},
    {"iPhone", "2h ago"},
    {"Studio iMac", "Yesterday"},
};

const badge_line k_aside_notif[] = {
    {"All", "7", badge_neutral},
    {"Unread", "2", badge_info},
    {"Mentions", "2", badge_warn},
};

const stat_line k_aside_about[] = {
    {"Game", brand::game},
    {"Plan", "Team"},
    {"Region", "eu-west"},
};

const stat_line k_aside_stagemix[] = {
    {"Draft", "38"},
    {"Internal", "24"},
    {"Playtest", "16"},
    {"Candidate", "9"},
};

const stat_line k_aside_assigned[] = {
    {brand::user_name, "4"},
    {"Corvid", "2"},
    {"Kestrel", "2"},
};

const stat_line k_aside_security[] = {
    {"Two-factor", "On"},
    {"Password", "41d ago"},
    {"Recovery", "Verified"},
};

const stat_line k_aside_storage[] = {
    {"Game", "84.2 GB"},
    {"Presets", "1.2 MB"},
    {"Shaders", "1.4 GB"},
};

const stat_line k_aside_threads[] = {
    {"Shadow flicker", "12m"},
    {"Controller drift", "2h"},
    {"Keybinds", "Mon"},
};

const char* const k_aside_search[] = {
    "F opens the palette from anywhere, and Escape closes it.",
    "Ctrl+B folds the rail away when you want the room.",
    "Results are ranked by what you opened last, not alphabetically.",
};

const char* const k_aside_assistant[] = {
    "Summarise the shadow reports since Tuesday.",
    "Read this crash dump and tell me which system owns it.",
    "What changed in the camera this week?",
};

const char* const k_tasks[] = {
    "Draft the keybind layout",           "Review the shadow flicker reports",
    "Refresh the settings guide",         "Close out the playtest notes",
    "Rebuild the shader cache",           "Send SZK Lab the signed build number",
    "Split the graphics presets by tier", "Chase the controller deadzone fix",
};
static_assert(IM_ARRAYSIZE(k_tasks) == k_task_count, "tasks is per task");
} // namespace

namespace
{

float page_aside(ImDrawList* dl, int nav, const ImVec2& pos, float width, float alpha,
                 float card_gap)
{
    switch (nav)
    {
    case 0:
        return aside_lines(dl, pos, width, alpha, "SHORTCUTS", k_aside_search,
                           IM_ARRAYSIZE(k_aside_search), card_gap);
    case 5:
        return aside_stats(dl, pos, width, alpha, "THIS DROP", k_aside_pipeline,
                           IM_ARRAYSIZE(k_aside_pipeline), card_gap);
    case 6:
        return aside_stats(dl, pos, width, alpha, "DUE", k_aside_tasks, IM_ARRAYSIZE(k_aside_tasks),
                           card_gap);
    case 7:
        return aside_stats(dl, pos, width, alpha, "TAGS", k_aside_notes,
                           IM_ARRAYSIZE(k_aside_notes), card_gap);
    case 10:
        return aside_stats(dl, pos, width, alpha, "SIGNED IN ON", k_aside_sessions,
                           IM_ARRAYSIZE(k_aside_sessions), card_gap);
    case 11:
        return aside_badges(dl, pos, width, alpha, "FILTERS", k_aside_notif,
                            IM_ARRAYSIZE(k_aside_notif), card_gap);
    case 12:
        return aside_stats(dl, pos, width, alpha, "ABOUT", k_aside_about,
                           IM_ARRAYSIZE(k_aside_about), card_gap);
    default:
        return 0.f;
    }
}

float page_aside_more(ImDrawList* dl, int nav, const ImVec2& pos, float width, float alpha)
{
    switch (nav)
    {
    case 5:
        return aside_stats(dl, pos, width, alpha, "STAGE MIX", k_aside_stagemix,
                           IM_ARRAYSIZE(k_aside_stagemix));
    case 6:
        return aside_stats(dl, pos, width, alpha, "ASSIGNED BY", k_aside_assigned,
                           IM_ARRAYSIZE(k_aside_assigned));
    case 10:
        return aside_stats(dl, pos, width, alpha, "SECURITY", k_aside_security,
                           IM_ARRAYSIZE(k_aside_security));
    case 12:
        return aside_stats(dl, pos, width, alpha, "STORAGE", k_aside_storage,
                           IM_ARRAYSIZE(k_aside_storage));
    default:
        return 0.f;
    }
}
} // namespace

route draw_page(route destination, const char* title, const char* const* subs, int sub_count,
                int* sub, const ImRect& area, float alpha)
{
    route nav_request = route::count;
    const int nav = route_index(destination);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    page_state& s = state();
    const float dt = ImGui::GetIO().DeltaTime;

    const float x = area.Min.x;
    const float w = area.GetWidth();

    if (s.last_nav != nav)
    {
        s.last_nav = nav;
        s.entered = 0.f;

        // Coming back to the Dashboard should show the machine as it is now,
        // not as it was when the page was last open.
        if (nav == route_index(route::dashboard))
            s.dash_scanned = false;
    }
    s.entered += dt;

    auto stagger = [&](int index) -> float
    { return mo::EASE_OUT(ImClamp((s.entered - (float)index * 0.045f) / 0.34f, 0.f, 1.f)); };

    const int slot = ImClamp(nav, 0, 12);
    const float measured = s.content[slot] > 0.f ? s.content[slot] : area.GetHeight();
    dl->PushClipRect(area.Min, area.Max, true);
    const float scroll = scroll_area(s.body[slot], area, measured);

    float y = area.Min.y - scroll;

    static const char* const k_account_blurbs[] = {
        "Your account, and what the game is allowed to send you.",
        "Everything the game wanted you to know about, newest first.",
        "How this game behaves, and how loudly.",
    };

    static const char* k_blurbs[] = {
        "Jump anywhere in the game without lifting your hands off the keyboard.",
        "Version, support, and a couple of useful Windows shortcuts.",
        "This machine, at a glance.",
        "Everything you can change, and what it is set to.",
        "Drop ReShade and the 2K Road Mod straight into FiveM's game folder.",
        "What is in test for the next patch, and how stable it looks.",
        "The short list. Anything older than a fortnight gets a nudge.",
        "Longer-form thinking that does not belong in a changelog.",
        "One click to the driver page for this exact board.",
        "What this machine is doing, and what would make it quicker.",
    };

    const char* blurb =
        (nav >= 10) ? k_account_blurbs[ImClamp(nav - 10, 0, 2)] : k_blurbs[ImClamp(nav, 0, 9)];
    y += heading(dl, ImVec2(x, y), title, blurb, w, alpha);

    if (sub_count > 0)
    {
        tabs("page-tabs", ImVec2(x, y), subs, sub_count, sub, tabs_underline);
        y += tabs_height(tabs_underline) + px(sp_6);
    }

    const float gutter = 0.f;
    const float aside_gap = px(sp_5);
    const bool two_col = (nav != route_index(route::dashboard)) && ((w - gutter) >= px(770.f));

    const float col =
        two_col ? ImMin((w - gutter - aside_gap) * 0.60f, px(520.f)) : ImMin(w, px(440.f));

    const float aside_x = x + col + aside_gap;
    const float aside_w = (w - gutter) - col - aside_gap;
    const float aside_y = y;

    static const char* const k_column_labels[] = {
        "RECENT",    "",      "", "SETTINGS",     "",           "THIS PATCH", "OPEN",
        "ALL NOTES", "BOARD", "", "YOUR DETAILS", "EVERYTHING", "SETTINGS",
    };

    if (two_col)
    {
        const char* label = k_column_labels[ImClamp(nav, 0, 12)];
        if (*label)
            y += aside_head(dl, ImVec2(x, y), c_muted_foreground, alpha, label);
    }

    float aside_offset = 0.f;
    switch (nav)
    {
    case 0:
        aside_offset = tabs_height(tabs_pill) + px(sp_4);
        break;
    case 7:
        aside_offset = tabs_height(tabs_segment) + px(sp_4);
        break;

    case 11:
        aside_offset = tabs_height(tabs_pill) + px(sp_4) + px(28.f);
        break;

    case 12:
        aside_offset = px(26.f);
        break;
    default:
        break;
    }

    switch (nav)
    {
    case 3:
    {
        // aside_y was captured before the "SETTINGS" eyebrow label above —
        // the list card (and anything stacked in the right-hand column
        // alongside it) actually starts here, after that label.
        const float content_top = y;

        if (*sub != 3)
        {
            if (!s.gpu_vendor_queried)
            {
                s.has_nvidia_gpu = backend::has_nvidia_gpu();
                s.has_amd_gpu = backend::has_amd_gpu();
                s.gpu_vendor_queried = true;
            }

            int shown[k_module_count];
            int count = 0;
            for (int i = 0; i < k_module_count; i++)
            {
                // On "All Settings", skip whichever GPU vendor's rows don't
                // match this machine — a green-camp user doesn't need to see
                // (or have counted as "failed") a stack of red-camp tweaks,
                // and vice versa. The NVIDIA/AMD tabs themselves still show
                // everything regardless, in case detection is wrong.
                if (*sub == 0 && k_modules[i].category == 4 && !s.has_nvidia_gpu)
                    continue;
                if (*sub == 0 && k_modules[i].category == 5 && !s.has_amd_gpu)
                    continue;
                if (*sub == 0 || k_modules[i].category == *sub)
                    shown[count++] = i;
            }
            std::sort(shown, shown + count,
                      [](int a, int b) { return apply_priority(a) < apply_priority(b); });

            // Re-reads every row's actual registry/service state once when
            // this sub-tab is (re)entered, and again after an Apply finishes —
            // not every frame, since several checks are registry/service reads
            // and a couple (NVIDIA/AMD) are WMI queries.
            if (s.row_status_dirty || s.row_status_checked_sub != *sub)
            {
                for (int i = 0; i < k_module_count; i++)
                    s.row_applied[i] = k_modules[i].check ? k_modules[i].check() : false;
                s.row_status_dirty = false;
                s.row_status_checked_sub = *sub;
            }

            const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(sp_4) * 2.f +
                                                                px(40.f) * (float)ImMax(count, 1)));
            panel(dl, card, alpha);

            if (count == 0)
                empty_state(dl, card, "Nothing in this category.", alpha);

            float ry = card.Min.y + px(sp_4);
            for (int k = 0; k < count; k++)
            {
                const int i = shown[k];
                const module_row& m = k_modules[i];

                const ImVec2 av(card.Min.x + px(sp_4), ry + px(2.f));
                char initials[3];
                initials_of(m.name, initials);
                chip(dl, av, px(24.f), px(12.f), initials, mo::with_alpha(c_card, alpha),
                     mo::with_alpha(c_muted_foreground, alpha), i);

                ImFont* nf = font_medium(text_sm);
                draw_text(dl, nf,
                          ImVec2(card.Min.x + px(46.f), ry - nf->LegacySize * 0.5f + px(12.f)),
                          mo::with_alpha(c_foreground, alpha), m.name);

                ImFont* rf = font_regular(text_xs);
                const float rw = text_width(rf, m.role);
                const float dot_reserve = m.check ? px(14.f) : 0.f;
                const float role_x = card.Max.x - px(sp_4) - dot_reserve - rw;

                if (m.check)
                {
                    const ImU32 dot_color =
                        mo::with_alpha(s.row_applied[i] ? c_success : c_destructive, alpha);
                    const ImVec2 dot_center(card.Max.x - px(sp_4) - px(4.f), ry + px(9.f));
                    dl->AddCircleFilled(dot_center, px(3.f), dot_color);
                }

                draw_text(dl, rf, ImVec2(role_x, ry + px(3.f)),
                          mo::with_alpha(c_muted_foreground, alpha), m.role);

                ry += px(40.f);
            }

            y = card.Max.y + px(sp_4);

            {
                const char* apply_label = *sub == 1   ? "Apply Gaming Tweaks"
                                          : *sub == 2 ? "Apply Network Optimization"
                                          : *sub == 4 ? "Apply NVIDIA"
                                          : *sub == 5 ? "Apply AMD"
                                          : *sub == 6 ? "Apply Cleanup"
                                                      : "Apply All Settings";

                // The list can run to 50+ rows now — pin Apply to the bottom of
                // the viewport instead of the end of the scrolled content, so
                // it doesn't take a long scroll to reach.
                const float bar_h = px(76.f);
                const ImVec2 apply_pos(x, area.Max.y - bar_h + px(14.f));
                dl->AddRectFilled(ImVec2(area.Min.x, area.Max.y - bar_h), area.Max,
                                  mo::with_alpha(c_background, alpha));
                dl->AddRectFilled(ImVec2(area.Min.x, area.Max.y - bar_h),
                                  ImVec2(area.Max.x, area.Max.y - bar_h + px(1.f)),
                                  mo::with_alpha(c_border, alpha));

                if (action("apply-category", apply_pos, col, s.settings_apply_btn, apply_label) &&
                    s.settings_apply_btn == btn_idle && count > 0)
                {
                    s.settings_apply_btn = btn_loading;
                    s.settings_apply_timer = 0.f;
                }
                if (s.settings_apply_btn == btn_loading)
                {
                    s.settings_apply_timer += dt;
                    if (s.settings_apply_timer > 0.6f)
                    {
                        int applied = 0, failed = 0, unwired = 0;
                        for (int k = 0; k < count; k++)
                        {
                            const int i = shown[k];
                            switch (apply_module(i))
                            {
                            case apply_unwired:
                                unwired++;
                                break;
                            case apply_ok:
                                applied++;
                                break;
                            default:
                                failed++;
                                break;
                            }
                        }

                        char summary[96];
                        ImFormatString(summary, IM_ARRAYSIZE(summary),
                                       "%d applied, %d failed, %d not wired yet", applied, failed,
                                       unwired);
                        s.settings_apply_btn = (failed == 0) ? btn_success : btn_error;
                        toast(apply_label, summary, (failed == 0) ? toast_success : toast_error);
                        s.row_status_dirty = true;
                    }
                }
                y += bar_h;
            }
        }

        float nip_bottom = 0.f;
        bool nip_shown = false;

        if (*sub == 0 || *sub == 4)
        {
            const bool nip_right_col = two_col;
            const float nip_x = nip_right_col ? aside_x : x;
            const float nip_w = nip_right_col ? aside_w : col;
            const float nip_h = nip_right_col ? px(108.f) : px(72.f);
            const float nip_y = nip_right_col ? content_top : y;

            const ImRect nip(ImVec2(nip_x, nip_y), ImVec2(nip_x + nip_w, nip_y + nip_h));
            panel(dl, nip, alpha);

            if (nip_right_col)
            {
                row_label(dl, ImVec2(nip.Min.x + px(sp_4), nip.Min.y + px(14.f)),
                          "NVIDIA Profile Inspector", nullptr, alpha,
                          nip.GetWidth() - px(sp_4) * 2.f);

                const float btn_w = nip.GetWidth() - px(sp_4) * 2.f;
                if (action("nip-launch", ImVec2(nip.Min.x + px(sp_4), nip.Min.y + px(46.f)),
                           btn_w / ui_runtime::scale, btn_idle, "Open"))
                    backend::launch_nvidia_profile_inspector();
            }
            else
            {
                row_label(dl, ImVec2(nip.Min.x + px(sp_4), nip.Min.y + px(14.f)),
                          "NVIDIA Profile Inspector", nullptr, alpha,
                          nip.GetWidth() - px(sp_4) * 2.f - px(100.f));

                if (action("nip-launch",
                           ImVec2(nip.Max.x - px(sp_4) - px(90.f), nip.Min.y + px(16.f)), 90.f,
                           btn_idle, "Open"))
                    backend::launch_nvidia_profile_inspector();
            }

            nip_bottom = nip.Max.y;
            nip_shown = true;

            if (nip_right_col)
                y = ImMax(y, nip.Max.y + px(sp_4));
            else
                y = nip.Max.y + px(sp_4);
        }

        if (*sub == 0 || *sub == 3)
        {
            const backend::power_plan_info info = backend::power_plan_active();
            const bool is_szk = info.available && std::strcmp(info.name, "SZK") == 0;
            const char* status_label = !info.available ? "Unknown"
                                       : is_szk        ? "Active"
                                                       : "Not applied";
            const badge_status status_kind = !info.available ? badge_neutral
                                             : is_szk        ? badge_good
                                                             : badge_bad;

            // On "All Settings" (with the NVIDIA Profile Inspector card
            // also on screen), stack this right underneath it in the same
            // right-hand column instead of the main column. On the
            // Powerplan tab alone there's nothing to stack under, so it
            // keeps the full-width main-column layout.
            const bool szk_right_col = two_col && *sub == 0 && nip_shown;
            const float szk_x = szk_right_col ? aside_x : x;
            const float szk_w = szk_right_col ? aside_w : col;
            const float szk_y = szk_right_col ? nip_bottom + px(sp_4) : y;

            const ImRect szk(ImVec2(szk_x, szk_y), ImVec2(szk_x + szk_w, szk_y + px(164.f)));
            panel(dl, szk, alpha);

            const float icon_box = px(40.f);
            const ImRect icon_rect(
                ImVec2(szk.Min.x + px(sp_5), szk.Min.y + px(20.f)),
                ImVec2(szk.Min.x + px(sp_5) + icon_box, szk.Min.y + px(20.f) + icon_box));
            dl->AddRectFilled(icon_rect.Min, icon_rect.Max, mo::with_alpha(c_background, alpha),
                              px(10.f));
            draw_border(dl, icon_rect, px(10.f), px(1.f), mo::with_alpha(c_border_strong, alpha),
                        0);
            icons::draw(
                icons::id::zap, dl,
                ImVec2(icon_rect.GetCenter().x - px(11.f), icon_rect.GetCenter().y - px(11.f)),
                px(22.f), mo::with_alpha(c_foreground, alpha));

            const float bw = badge_width(status_label, true);
            ImFont* tf = font_semibold(text_base);
            draw_text(dl, tf, ImVec2(icon_rect.Max.x + px(14.f), szk.Min.y + px(22.f)),
                      mo::with_alpha(c_foreground, alpha), "SZK Power Plan");
            ImFont* df = font_regular(text_xs);
            draw_text_ellipsis(dl, df, ImVec2(icon_rect.Max.x + px(14.f), szk.Min.y + px(44.f)),
                               mo::with_alpha(c_muted_foreground, alpha),
                               "Renames the active Windows scheme so it's easy to spot",
                               szk.Max.x - (icon_rect.Max.x + px(14.f)) - bw - px(sp_3));
            badge("szk-status", dl, ImVec2(szk.Max.x - px(sp_5) - bw, szk.Min.y + px(24.f)),
                  status_label, status_kind, true, alpha);

            hairline(dl, szk, szk.Min.y + px(80.f), alpha);

            ImFont* lf = font_regular(text_xs);
            draw_text(dl, lf, ImVec2(szk.Min.x + px(sp_5), szk.Min.y + px(96.f)),
                      mo::with_alpha(c_muted_foreground, alpha), "ACTIVE PLAN");
            ImFont* vf = font_semibold(text_base);
            const char* current = info.available ? info.name : "Unknown";
            draw_text(dl, vf, ImVec2(szk.Min.x + px(sp_5), szk.Min.y + px(112.f)),
                      mo::with_alpha(c_foreground, alpha), current);

            ImFont* bf = font_medium(text_base);
            const float apply_w = text_width(bf, "Apply") + px(40.f);
            if (action("szk-apply",
                       ImVec2(szk.Max.x - px(sp_5) - apply_w / ui_runtime::scale,
                              szk.Min.y + px(100.f)),
                       apply_w / ui_runtime::scale, s.szk_apply_btn, "Apply") &&
                s.szk_apply_btn == btn_idle)
            {
                s.szk_apply_btn = btn_loading;
                s.szk_apply_timer = 0.f;
            }
            if (s.szk_apply_btn == btn_loading)
            {
                s.szk_apply_timer += dt;
                if (s.szk_apply_timer > 0.6f)
                {
                    const bool ok = backend::power_plan_rename_active(L"SZK", L"szk powerplan");
                    s.szk_apply_btn = ok ? btn_success : btn_error;
                    toast(ok ? "Renamed" : "Failed", "szk powerplan",
                          ok ? toast_success : toast_error);
                }
            }

            if (szk_right_col)
                y = ImMax(y, szk.Max.y + px(sp_4));
            else
                y = szk.Max.y + px(sp_4);
        }
        break;
    }

    case 5:
    {
        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(268.f)));
        panel(dl, card, alpha);

        float ry = card.Min.y + px(sp_4);
        row_label(dl, ImVec2(card.Min.x + px(sp_4), ry), "Stage", nullptr, alpha);
        ry += px(24.f);
        select("stage", ImVec2(card.Min.x + px(sp_4), ry), col - px(sp_4) * 2.f, k_stage_options,
               IM_ARRAYSIZE(k_stage_options), &s.stage, "Pick a stage");
        ry += px(select_h) + px(sp_4);

        row_label(dl, ImVec2(card.Min.x + px(sp_4), ry), "Author", nullptr, alpha);
        ry += px(24.f);
        select("author", ImVec2(card.Min.x + px(sp_4), ry), col - px(sp_4) * 2.f, k_author_options,
               IM_ARRAYSIZE(k_author_options), &s.author, "Unassigned");
        ry += px(select_h) + px(sp_4);

        char amount[64];
        ImFormatString(amount, IM_ARRAYSIZE(amount), "Projected installs   %.0fk",
                       s.projected_installs / 1000.f);
        row_label(dl, ImVec2(card.Min.x + px(sp_4), ry), amount, nullptr, alpha);
        ry += px(24.f);
        range_slider("projected-installs", ImVec2(card.Min.x + px(sp_4), ry), col - px(sp_4) * 2.f,
                     &s.projected_installs, 0.f, 250000.f, 6);

        y = card.Max.y + px(sp_4);

        if (action("save", ImVec2(x, y), 200.f, s.save, "Save patch") && s.save == btn_idle)
        {
            s.save = btn_loading;
            s.save_timer = 0.f;
        }
        if (s.save == btn_loading)
        {
            s.save_timer += dt;
            if (s.save_timer > 1.2f)
            {
                s.save = btn_success;
                toast("Patch saved", k_stage_options[s.stage], toast_success);
            }
        }
        y += px(44.f) + px(sp_5);

        {
            const float head = px(50.f);
            const float row_h = px(46.f);
            const int count = IM_ARRAYSIZE(k_builds);
            const ImRect list(ImVec2(x, y),
                              ImVec2(x + col, y + head + row_h * (float)count + px(8.f)));
            panel(dl, list, alpha);

            row_label(dl, ImVec2(list.Min.x + px(sp_4), list.Min.y + px(14.f)),
                      "Open in this stage", nullptr, alpha);

            ImFont* mf = font_medium(text_xs);
            float pill_w = 0.f;
            for (int i = 0; i < count; i++)
                pill_w = ImMax(pill_w, text_width(mf, k_builds[i].stage) + px(16.f));

            const ImU32 tones[4] = {c_primary, c_amber_400, c_muted_foreground, c_success};
            const float value_r = list.Max.x - px(sp_4);
            const float stage_r = value_r - px(76.f);

            for (int i = 0; i < count; i++)
            {
                const float ry2 = list.Min.y + head + row_h * (float)i;
                const float mid = ry2 + row_h * 0.5f;

                if (i > 0)
                    hairline(dl, list, ry2, alpha * 0.9f);

                const float av = px(22.f);
                char ini[3];
                initials_of(k_builds[i].author, ini);
                chip(dl, ImVec2(list.Min.x + px(sp_4), mid - av * 0.5f), av, av * 0.5f, ini,
                     mo::with_alpha(c_muted_foreground, 0.16f * alpha),
                     mo::with_alpha(c_foreground, alpha), k_builds[i].person);

                ImFont* nf = font_medium(text_sm);
                const float nx = list.Min.x + px(sp_4) + av + px(10.f);
                draw_text_ellipsis(dl, nf, ImVec2(nx, mid - nf->LegacySize * 0.5f),
                                   mo::with_alpha(c_foreground, alpha), k_builds[i].mod,
                                   (stage_r - pill_w - px(12.f)) - nx);

                pill(dl, ImVec2(stage_r - pill_w, mid - px(11.f)), k_builds[i].stage,
                     tones[ImClamp(k_builds[i].tone, 0, 3)], alpha);

                ImFont* vf = font_semibold(text_sm);
                const float vw = text_width(vf, k_builds[i].value);
                draw_text(dl, vf, ImVec2(value_r - vw, mid - vf->LegacySize * 0.5f),
                          mo::with_alpha(c_foreground, alpha), k_builds[i].value);
            }

            y = list.Max.y;
        }
        break;
    }

    case 8:
    {
        if (!s.mobo_queried)
        {
            s.mobo = backend::motherboard_query();
            s.mobo_queried = true;
        }

        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(196.f)));
        panel(dl, card, alpha);
        row_label(dl, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(16.f)), "Board Lookup",
                  "Find the right drivers for this machine", alpha);

        const char* product = s.mobo.available ? s.mobo.product : "Not detected";
        ImFont* lf = font_medium(text_sm);
        draw_text(dl, lf, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(62.f)),
                  mo::with_alpha(c_muted_foreground, alpha), "Detected board");

        ImFont* vf = font_semibold(text_sm);
        const float vw = text_width(vf, product);
        draw_text(dl, vf, ImVec2(card.Max.x - px(sp_4) - vw, card.Min.y + px(62.f)),
                  mo::with_alpha(c_foreground, alpha), product);

        hairline(dl, card, card.Min.y + px(90.f), alpha);

        ImFont* nf = font_regular(text_xs);
        draw_text(dl, nf, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(104.f)),
                  mo::with_alpha(c_muted_foreground, alpha),
                  "Read straight from the board sensor.");

        if (action("mobo-search", ImVec2(card.Min.x + px(sp_4), card.Min.y + px(140.f)),
                   col - px(sp_4) * 2.f, btn_idle, "Find Drivers") &&
            s.mobo.available)
        {
            backend::open_google_search(s.mobo.product);
        }

        y = card.Max.y + px(sp_4);
        break;
    }

    case 6:
    {
        const ImRect card(ImVec2(x, y),
                          ImVec2(x + col, y + px(sp_4) * 2.f + px(34.f) * (float)k_task_count));
        panel(dl, card, alpha);

        float ry = card.Min.y + px(sp_4);
        for (int i = 0; i < k_task_count; i++)
        {
            char id[32];
            ImFormatString(id, IM_ARRAYSIZE(id), "task%d", i);
            check(id, ImVec2(card.Min.x + px(sp_4), ry), &s.tasks[i], k_tasks[i]);
            ry += px(34.f);
        }

        y = card.Max.y + px(sp_4);
        if (action("sweep", ImVec2(x, y), 200.f, s.sweep, "Clear completed") && s.sweep == btn_idle)
        {
            s.sweep = btn_loading;
            s.sweep_timer = 0.f;
        }
        if (s.sweep == btn_loading)
        {
            s.sweep_timer += dt;
            if (s.sweep_timer > 1.f)
            {
                int cleared = 0;
                for (bool& t : s.tasks)
                {
                    if (t)
                        cleared++;
                    t = false;
                }
                s.sweep = btn_success;

                char msg[64];
                ImFormatString(msg, IM_ARRAYSIZE(msg), "%d task%s cleared", cleared,
                               cleared == 1 ? "" : "s");
                toast("Done", msg, toast_success);
            }
        }
        y += px(44.f);
        break;
    }

    case 9:
    {

        const float dashboard_width = w;
        const float reveal = 1.f;

        if (!s.sys_mon_inited)
        {
            backend::system_monitor_init();
            s.sys_mon_inited = true;
            s.sys_poll_t = 1e6f; // force an immediate first sample
        }
        s.sys_poll_t += dt;
        if (s.sys_poll_t >= 0.75f)
        {
            s.sys_prev = s.sys_snap;
            s.sys_snap = backend::system_monitor_poll();
            s.sys_poll_t = 0.f;

            auto push = [](float* buf, float value)
            {
                for (int i = 0; i < k_sys_history - 1; i++)
                    buf[i] = buf[i + 1];
                buf[k_sys_history - 1] = value;
            };
            push(s.sys_cpu_hist, s.sys_snap.cpu_percent);
            push(s.sys_ram_hist, s.sys_snap.ram_percent);
            push(s.sys_disk_hist, s.sys_snap.disk_percent);
            push(s.sys_ping_hist, s.sys_snap.ping_ms >= 0 ? (float)s.sys_snap.ping_ms : 0.f);
            s.sys_hist_filled = ImMin(s.sys_hist_filled + 1, k_sys_history);
        }

        const int hist_n = s.sys_hist_filled;
        const float* cpu_series = &s.sys_cpu_hist[k_sys_history - hist_n];
        const float* ram_series = &s.sys_ram_hist[k_sys_history - hist_n];
        const float* disk_series = &s.sys_disk_hist[k_sys_history - hist_n];
        const float* ping_series = &s.sys_ping_hist[k_sys_history - hist_n];

        char delta_buf[4][16];
        const bool have_ping = s.sys_snap.ping_ms >= 0;
        std::snprintf(delta_buf[0], sizeof(delta_buf[0]), "%+.0f%%",
                      s.sys_snap.cpu_percent - s.sys_prev.cpu_percent);
        std::snprintf(delta_buf[1], sizeof(delta_buf[1]), "%+.0f%%",
                      s.sys_snap.ram_percent - s.sys_prev.ram_percent);
        std::snprintf(delta_buf[2], sizeof(delta_buf[2]), "%+.0f%%",
                      s.sys_snap.disk_percent - s.sys_prev.disk_percent);
        if (have_ping && s.sys_prev.ping_ms >= 0)
            std::snprintf(delta_buf[3], sizeof(delta_buf[3]), "%+dms",
                          s.sys_snap.ping_ms - s.sys_prev.ping_ms);
        else
            std::snprintf(delta_buf[3], sizeof(delta_buf[3]), "live");

        char value_buf[4][16];
        std::snprintf(value_buf[0], sizeof(value_buf[0]), "%.0f%%", s.sys_snap.cpu_percent);
        std::snprintf(value_buf[1], sizeof(value_buf[1]), "%.0f%%", s.sys_snap.ram_percent);
        std::snprintf(value_buf[2], sizeof(value_buf[2]), "%.0f%%", s.sys_snap.disk_percent);
        if (have_ping)
            std::snprintf(value_buf[3], sizeof(value_buf[3]), "%dms", s.sys_snap.ping_ms);
        else
            std::snprintf(value_buf[3], sizeof(value_buf[3]), "--");

        const char* tile_labels[4] = {"CPU Usage", "RAM Usage", "Disk Usage", "Ping"};
        const float tile_deltas[4] = {
            s.sys_snap.cpu_percent - s.sys_prev.cpu_percent,
            s.sys_snap.ram_percent - s.sys_prev.ram_percent,
            s.sys_snap.disk_percent - s.sys_prev.disk_percent,
            have_ping && s.sys_prev.ping_ms >= 0 ? (float)(s.sys_snap.ping_ms - s.sys_prev.ping_ms)
                                                 : 0.f,
        };
        const float tile_targets[4] = {s.sys_snap.cpu_percent, s.sys_snap.ram_percent,
                                       s.sys_snap.disk_percent,
                                       have_ping ? (float)s.sys_snap.ping_ms : 0.f};
        const float* tile_series[4] = {cpu_series, ram_series, disk_series, ping_series};

        // ── Live scan ──────────────────────────────────────────────────────
        // Registry reads, so this runs on page entry, after Optimize, and
        // when Scan again is pressed - never per frame.
        if (!s.dash_scanned)
        {
            const scan_result r = scan_modules();
            std::memcpy(s.dash_applied, r.applied, sizeof(s.dash_applied));
            std::memcpy(s.dash_findings, r.findings, sizeof(s.dash_findings));
            s.dash_finding_count = r.finding_count;
            s.dash_checkable = r.checkable;
            s.dash_ok = r.ok;
            s.dash_power = backend::power_plan_active();
            s.dash_scanned = true;
        }

        int sev_count[4] = {};
        for (int k = 0; k < s.dash_finding_count; k++)
            sev_count[k_modules[s.dash_findings[k]].severity]++;

        const float score_target =
            s.dash_checkable > 0 ? 100.f * (float)s.dash_ok / (float)s.dash_checkable : 100.f;

        // ── Hero: one number, one sentence, one button ─────────────────────
        {
            const float card_h = px(154.f);
            const ImRect card(ImVec2(x, y), ImVec2(x + dashboard_width, y + card_h));
            panel(dl, card, alpha);

            const float ring_r = px(44.f);
            const ImVec2 ring_c(card.Min.x + px(sp_5) + ring_r, card.GetCenter().y);

            const float shown_score = number_value("dash-score", score_target);
            const ImU32 score_col = score_target >= 85.f   ? c_accent
                                    : score_target >= 60.f ? c_amber_500
                                                           : c_destructive;

            const float thickness = px(9.f);
            const float a0 = -IM_PI * 0.5f;
            dl->PathArcTo(ring_c, ring_r, a0, a0 + IM_PI * 2.f, 64);
            dl->PathStroke(mo::with_alpha(c_muted_foreground, 0.16f * alpha), thickness,
                           ImDrawFlags_None);

            const float sweep = ImClamp(shown_score / 100.f, 0.f, 1.f);
            if (sweep > 0.004f)
            {
                const float a1 = a0 + IM_PI * 2.f * sweep;
                dl->PathArcTo(ring_c, ring_r, a0, a1, 64);
                dl->PathStroke(mo::with_alpha(score_col, alpha), thickness, ImDrawFlags_None);

                // Round both ends by hand - PathStroke has no cap mode here.
                dl->AddCircleFilled(ImVec2(ring_c.x, ring_c.y - ring_r), thickness * 0.5f,
                                    mo::with_alpha(score_col, alpha), 16);
                dl->AddCircleFilled(
                    ImVec2(ring_c.x + ImCos(a1) * ring_r, ring_c.y + ImSin(a1) * ring_r),
                    thickness * 0.5f, mo::with_alpha(score_col, alpha), 16);
            }

            char score_buf[8];
            ImFormatString(score_buf, IM_ARRAYSIZE(score_buf), "%.0f", shown_score);
            ImFont* sf = font_semibold(28.f);
            draw_text_tracked(
                dl, sf, ImVec2(ring_c.x - text_width(sf, score_buf) * 0.5f, ring_c.y - px(20.f)),
                mo::with_alpha(c_foreground, alpha), score_buf, px(-1.f));

            ImFont* cf = font_medium(10.f);
            const char* score_cap = "SCORE";
            draw_text_tracked(
                dl, cf,
                ImVec2(ring_c.x - text_width(cf, score_cap) * 0.5f - px(0.7f), ring_c.y + px(9.f)),
                mo::with_alpha(c_dim_foreground, alpha), score_cap, px(1.4f));

            const float btn_w = 178.f;
            const float btn_x = card.Max.x - px(sp_5) - px(btn_w);
            const float text_x = ring_c.x + ring_r + px(sp_5);
            const float text_w = btn_x - text_x - px(sp_5);

            char headline[96];
            if (s.dash_finding_count == 0)
                ImFormatString(headline, IM_ARRAYSIZE(headline), "Nothing left to fix here");
            else
                ImFormatString(headline, IM_ARRAYSIZE(headline),
                               "%d tweak%s would help this machine", s.dash_finding_count,
                               s.dash_finding_count == 1 ? "" : "s");

            ImFont* tf = font_semibold(18.f);
            draw_text_tracked(dl, tf, ImVec2(text_x, card.Min.y + px(30.f)),
                              mo::with_alpha(c_foreground, alpha), headline, px(-0.4f));

            char rationale[192];
            ImFormatString(rationale, IM_ARRAYSIZE(rationale),
                           "%d of the %d settings this app can read back are already applied. The "
                           "rest are listed below, loudest first.",
                           s.dash_ok, s.dash_checkable);

            ImFont* bf = font_regular(text_sm);
            draw_text_wrapped(dl, bf, ImVec2(text_x, card.Min.y + px(58.f)),
                              mo::with_alpha(c_muted_foreground, alpha), rationale, text_w,
                              px(leading_sm));

            float pill_x = text_x;
            const float pill_y = card.Min.y + px(108.f);

            struct sev_pill
            {
                finding_severity severity;
                const char* suffix;
            };
            static const sev_pill k_sev_pills[] = {
                {finding_urgent, " urgent"},
                {finding_advised, " advised"},
                {finding_note, " optional"},
            };

            for (const sev_pill& entry : k_sev_pills)
            {
                const int n = sev_count[entry.severity];
                if (n <= 0)
                    continue;

                char label[24];
                ImFormatString(label, IM_ARRAYSIZE(label), "%d%s", n, entry.suffix);
                pill_x +=
                    pill(dl, ImVec2(pill_x, pill_y), label, severity_color(entry.severity), alpha) +
                    px(sp_2);
            }
            if (s.dash_finding_count == 0)
                pill(dl, ImVec2(pill_x, pill_y), "All verified", c_accent, alpha);

            const float btn_y = card.Min.y + px(44.f);
            if (s.dash_finding_count > 0)
            {
                if (action("dash-optimize", ImVec2(btn_x, btn_y), btn_w, s.dash_optimize_btn,
                           "Optimize now") &&
                    s.dash_optimize_btn == btn_idle)
                {
                    s.dash_optimize_btn = btn_loading;
                    s.dash_optimize_timer = 0.f;
                }

                if (s.dash_optimize_btn == btn_loading)
                {
                    s.dash_optimize_timer += dt;
                    if (s.dash_optimize_timer > 0.6f)
                    {
                        // The button promises a restore point, so failing to
                        // take one stops the run instead of quietly skipping
                        // it. That promise is the whole reason one click here
                        // is reasonable at all.
                        if (!backend::create_system_restore_point())
                        {
                            s.dash_optimize_btn = btn_error;
                            s.dash_optimize_timer = 0.f;
                            toast("Nothing was changed",
                                  "Windows would not create a restore point. Turn System "
                                  "Protection on for C: and try again.",
                                  toast_error);
                        }
                        else
                        {
                            int applied = 0, failed = 0;
                            for (int k = 0; k < s.dash_finding_count; k++)
                            {
                                switch (apply_module(s.dash_findings[k]))
                                {
                                case apply_ok:
                                    applied++;
                                    break;
                                case apply_failed:
                                    failed++;
                                    break;
                                default:
                                    break;
                                }
                            }

                            char summary[80];
                            ImFormatString(summary, IM_ARRAYSIZE(summary),
                                           "%d applied, %d failed. Restore point taken first.",
                                           applied, failed);
                            s.dash_optimize_btn = failed == 0 ? btn_success : btn_error;
                            s.dash_optimize_timer = 0.f;
                            toast("Optimize now", summary,
                                  failed == 0 ? toast_success : toast_error);

                            s.dash_scanned = false;    // the score is stale now
                            s.row_status_dirty = true; // so are Settings' dots
                        }
                    }
                }
                else if (s.dash_optimize_btn != btn_idle)
                {
                    // Back to idle so the run can be repeated - the toast is
                    // what carries the result once the button has said it.
                    s.dash_optimize_timer += dt;
                    if (s.dash_optimize_timer > 2.5f)
                        s.dash_optimize_btn = btn_idle;
                }
            }
            else if (action("dash-rescan-hero", ImVec2(btn_x, btn_y), btn_w, s.dash_rescan_btn,
                            "Scan again") &&
                     s.dash_rescan_btn == btn_idle)
            {
                s.dash_scanned = false;
            }

            const float trust_y = card.Min.y + px(102.f);
            icons::draw(icons::id::check, dl, ImVec2(btn_x, trust_y), px(13.f),
                        mo::with_alpha(c_dim_foreground, alpha));

            ImFont* trf = font_regular(text_xs);
            draw_text(dl, trf, ImVec2(btn_x + px(18.f), trust_y + px(1.f)),
                      mo::with_alpha(c_dim_foreground, alpha),
                      s.dash_finding_count > 0 ? "Restore point created first"
                                               : "Reads the registry, changes nothing");

            y = card.Max.y + px(sp_3);
        }

        // ── Live telemetry ─────────────────────────────────────────────────
        {
            const float gap = px(sp_3);
            const float tile_w = (dashboard_width - gap * 3.f) / 4.f;
            const float tile_h = px(118.f);

            char detail_buf[4][24];
            if (s.sys_snap.cpu_temp_available)
                std::snprintf(detail_buf[0], sizeof(detail_buf[0]), "%.0f C",
                              s.sys_snap.cpu_temp_c);
            else
                std::snprintf(detail_buf[0], sizeof(detail_buf[0]), "%s", delta_buf[0]);
            std::snprintf(detail_buf[1], sizeof(detail_buf[1]), "%.1f / %.0f GB",
                          s.sys_snap.ram_used_gb, s.sys_snap.ram_total_gb);
            std::snprintf(detail_buf[2], sizeof(detail_buf[2]), "%.0f / %.0f GB",
                          s.sys_snap.disk_used_gb, s.sys_snap.disk_total_gb);
            std::snprintf(detail_buf[3], sizeof(detail_buf[3]), "%s",
                          have_ping ? delta_buf[3] : "no route");

            for (int i = 0; i < 4; i++)
            {
                const ImRect t(ImVec2(x + (tile_w + gap) * (float)i, y),
                               ImVec2(x + (tile_w + gap) * (float)i + tile_w, y + tile_h));
                panel(dl, t, alpha);

                ImFont* lf = font_medium(text_xs);
                draw_text(dl, lf, ImVec2(t.Min.x + px(sp_4), t.Min.y + px(14.f)),
                          mo::with_alpha(c_muted_foreground, alpha), tile_labels[i]);

                char nid[16];
                ImFormatString(nid, IM_ARRAYSIZE(nid), "kpi%d", i);
                number_value(nid, tile_targets[i]); // keeps the easing state warm

                ImFont* vf = font_semibold(22.f);
                draw_text_tracked(dl, vf, ImVec2(t.Min.x + px(sp_4), t.Min.y + px(36.f)),
                                  mo::with_alpha(c_foreground, alpha), value_buf[i], px(-0.4f));

                // The second line is what the percentage is a percentage of.
                // A bare 61% is a number; 19.4 / 32 GB is an answer.
                ImFont* df = font_medium(text_xs);
                draw_text_ellipsis(dl, df, ImVec2(t.Min.x + px(sp_4), t.Min.y + px(70.f)),
                                   mo::with_alpha(c_dim_foreground, alpha), detail_buf[i],
                                   tile_w - px(sp_4) * 2.f);

                const bool good = (i == 3 && !have_ping) ? true : tile_deltas[i] <= 0.f;
                const ImU32 tone =
                    (i == 3 && !have_ping) ? c_muted_foreground : (good ? c_accent : c_amber_500);

                if (hist_n > 0)
                {
                    const ImRect spark(ImVec2(t.Min.x + px(1.f), t.Min.y + px(86.f)),
                                       ImVec2(t.Max.x - px(1.f), t.Max.y - px(1.f)));
                    dl->PushClipRect(spark.Min, spark.Max, true);
                    sparkline(dl, spark, tile_series[i], hist_n, tone, alpha, reveal);
                    dl->PopClipRect();
                }
            }
            y += tile_h + px(sp_3);
        }

        // ── Needs attention ────────────────────────────────────────────────
        {
            const float head_h = px(48.f);
            const float row_h = px(66.f);
            const int shown = s.dash_finding_count;
            const float body_h = shown > 0 ? row_h * (float)shown : px(96.f);
            const ImRect card(ImVec2(x, y), ImVec2(x + dashboard_width, y + head_h + body_h));
            panel(dl, card, alpha);

            row_label(dl, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(16.f)), "Needs attention",
                      nullptr, alpha);

            {
                const float rw = px(104.f);
                const ImRect rescan(ImVec2(card.Max.x - px(sp_4) - rw, card.Min.y + px(10.f)),
                                    ImVec2(card.Max.x - px(sp_4), card.Min.y + px(38.f)));
                if (row_hit(dl, "dash-rescan", rescan, alpha))
                    s.dash_scanned = false;

                ImFont* rf = font_medium(text_xs);
                const char* rl = "Scan again";
                draw_text(dl, rf,
                          ImVec2(rescan.GetCenter().x - text_width(rf, rl) * 0.5f,
                                 rescan.GetCenter().y - rf->LegacySize * 0.5f),
                          mo::with_alpha(c_muted_foreground, alpha), rl);
            }

            hairline(dl, card, card.Min.y + head_h, alpha);

            if (shown == 0)
                empty_state(dl, ImRect(ImVec2(card.Min.x, card.Min.y + head_h), card.Max),
                            "Everything this app can verify is already applied.", alpha);

            for (int k = 0; k < shown; k++)
            {
                const int m = s.dash_findings[k];
                const module_row& row = k_modules[m];
                const float ry = card.Min.y + head_h + row_h * (float)k;

                if (k > 0)
                    hairline(dl, card, ry, alpha);

                // Severity reads three ways here - stripe, pill, and the
                // button's own verb - so the row still works without colour.
                const ImU32 sev_col = severity_color(row.severity);
                dl->AddRectFilled(ImVec2(card.Min.x + px(1.f), ry + px(15.f)),
                                  ImVec2(card.Min.x + px(4.f), ry + row_h - px(15.f)),
                                  mo::with_alpha(sev_col, alpha), px(1.5f));

                const float fix_w = 84.f;
                const float fix_x = card.Max.x - px(sp_4) - px(fix_w);
                const float tx = card.Min.x + px(sp_4);

                ImFont* nf = font_medium(text_sm);
                draw_text(dl, nf, ImVec2(tx, ry + px(16.f)), mo::with_alpha(c_foreground, alpha),
                          row.name);

                pill(dl, ImVec2(tx + text_width(nf, row.name) + px(sp_2), ry + px(14.f)),
                     severity_label(row.severity), sev_col, alpha);

                ImFont* costf = font_regular(text_xs);
                draw_text_ellipsis(dl, costf, ImVec2(tx, ry + px(38.f)),
                                   mo::with_alpha(c_muted_foreground, alpha), row.cost,
                                   fix_x - tx - px(sp_4));

                // Outlined, not filled. Eight of these sit under one white
                // "Optimize now", and only one control on a screen gets to be
                // the loudest.
                char fid[24];
                ImFormatString(fid, IM_ARRAYSIZE(fid), "dash-fix%d", m);

                const ImRect fix(ImVec2(fix_x, ry + px(17.f)),
                                 ImVec2(fix_x + px(fix_w), ry + row_h - px(17.f)));
                if (row_hit(dl, fid, fix, alpha) && s.dash_fix_request < 0)
                    s.dash_fix_request = m;

                // A real 1px stroke. draw_border() fills the whole box with the
                // border colour and then paints `inner` inside it, so passing
                // a transparent inner leaves a filled plate, not an outline.
                dl->AddRect(ImVec2(fix.Min.x + px(0.5f), fix.Min.y + px(0.5f)),
                            ImVec2(fix.Max.x - px(0.5f), fix.Max.y - px(0.5f)),
                            mo::with_alpha(c_border_strong, alpha), px(8.f), px(1.f),
                            ImDrawFlags_None);

                ImFont* ff = font_medium(text_sm);
                draw_text(dl, ff,
                          ImVec2(fix.GetCenter().x - text_width(ff, "Fix") * 0.5f,
                                 fix.GetCenter().y - ff->LegacySize * 0.5f),
                          mo::with_alpha(c_foreground, alpha), "Fix");
            }

            y = card.Max.y + px(sp_3);
        }

        // Applied after the list is drawn, so the rows this frame still match
        // the indices their buttons were hit on.
        if (s.dash_fix_request >= 0)
        {
            const int m = s.dash_fix_request;
            s.dash_fix_request = -1;

            const apply_result res = apply_module(m);
            toast(k_modules[m].name,
                  res == apply_ok       ? "Applied"
                  : res == apply_failed ? "Windows refused the change"
                                        : "Not wired to a backend yet",
                  res == apply_ok ? toast_success : toast_error);

            s.dash_scanned = false;
            s.row_status_dirty = true;
        }

        // ── What this machine is running ───────────────────────────────────
        {
            const backend::machine_info mi = backend::system_monitor_machine_info();
            const unsigned long long up_min = mi.uptime_seconds / 60ull;

            char stat_value[3][64];
            ImFormatString(stat_value[0], IM_ARRAYSIZE(stat_value[0]), "%d of %d applied",
                           s.dash_ok, s.dash_checkable);
            ImFormatString(stat_value[1], IM_ARRAYSIZE(stat_value[1]), "%s",
                           s.dash_power.available ? s.dash_power.name : "Unknown");
            ImFormatString(stat_value[2], IM_ARRAYSIZE(stat_value[2]), "%lluh %llum",
                           up_min / 60ull, up_min % 60ull);

            static const char* const k_stat_label[3] = {"Verified tweaks", "Power plan", "Uptime"};

            const float card_h = px(76.f);
            const ImRect card(ImVec2(x, y), ImVec2(x + dashboard_width, y + card_h));
            panel(dl, card, alpha);

            const float cell = (dashboard_width - px(sp_4) * 2.f) / 3.f;
            for (int i = 0; i < 3; i++)
            {
                const float cx = card.Min.x + px(sp_4) + cell * (float)i;

                if (i > 0)
                    dl->AddRectFilled(ImVec2(cx - px(sp_4) * 0.5f, card.Min.y + px(18.f)),
                                      ImVec2(cx - px(sp_4) * 0.5f + px(1.f), card.Max.y - px(18.f)),
                                      mo::with_alpha(c_border, alpha));

                ImFont* lf = font_medium(text_xs);
                draw_text(dl, lf, ImVec2(cx, card.Min.y + px(20.f)),
                          mo::with_alpha(c_dim_foreground, alpha), k_stat_label[i]);

                ImFont* vf = font_medium(text_sm);
                draw_text_ellipsis(dl, vf, ImVec2(cx, card.Min.y + px(42.f)),
                                   mo::with_alpha(c_foreground, alpha), stat_value[i],
                                   cell - px(sp_4));
            }

            y = card.Max.y;
        }
        break;
    }

    case 0:
    {
        static const char* const scopes[] = {"Recent", "Saved"};
        tabs("search-tabs", ImVec2(x, y), scopes, IM_ARRAYSIZE(scopes), &s.search_tab, tabs_pill);
        y += tabs_height(tabs_pill) + px(sp_4);

        const bool recent = (s.search_tab == 0);
        const int count =
            recent ? (s.history_cleared ? 0 : IM_ARRAYSIZE(k_recent)) : IM_ARRAYSIZE(k_saved);
        const float row = px(44.f);

        const ImRect card(ImVec2(x, y),
                          ImVec2(x + col, y + px(sp_4) * 2.f + row * (float)ImMax(count, 1)));
        panel(dl, card, alpha);

        if (count == 0)
        {
            ImFont* f = font_regular(text_sm);
            const char* msg = "Nothing here yet.";
            draw_text(dl, f,
                      ImVec2(card.GetCenter().x - text_width(f, msg) * 0.5f,
                             card.GetCenter().y - f->LegacySize * 0.5f),
                      mo::with_alpha(c_muted_foreground, alpha), msg);
        }

        float ry = card.Min.y + px(sp_4);
        for (int i = 0; i < count; i++)
        {
            const search_hit& hit = recent ? k_recent[i] : k_saved[i];

            icons::draw(recent ? icons::id::clock : icons::id::star, dl,
                        ImVec2(card.Min.x + px(sp_4), ry + px(14.f)), px(16.f),
                        mo::with_alpha(c_muted_foreground, alpha));

            ImFont* qf = font_medium(text_sm);
            draw_text(dl, qf,
                      ImVec2(card.Min.x + px(44.f), ry + px(12.f) + line_top(qf, px(leading_sm))),
                      mo::with_alpha(c_foreground, alpha), hit.query);

            ImFont* cf = font_regular(text_xs);
            draw_text(dl, cf,
                      ImVec2(card.Max.x - px(sp_4) - text_width(cf, hit.count), ry + px(14.f)),
                      mo::with_alpha(c_muted_foreground, alpha), hit.count);

            if (i + 1 < count)
                hairline(dl, card, ry + row, alpha);
            ry += row;
        }
        y = card.Max.y + px(sp_4);

        check("search-bodies", ImVec2(x, y), &s.search_bodies, "Search inside note bodies");
        y += px(38.f);

        if (recent &&
            action("clear-history", ImVec2(x, y), 200.f, s.clear_history, "Clear history") &&
            s.clear_history == btn_idle)
        {
            s.clear_history = btn_loading;
            s.clear_timer = 0.f;
        }
        if (s.clear_history == btn_loading)
        {
            s.clear_timer += dt;
            if (s.clear_timer > 0.9f)
            {
                s.clear_history = btn_success;
                s.history_cleared = true;
                toast("History cleared", "Five recent searches removed", toast_success);
            }
        }
        if (recent)
            y += px(44.f);
        break;
    }

    case 1:
    {
        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(94.f)));
        panel(dl, card, alpha);
        row_label(dl, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(16.f)), "SZK",
                  "Build information", alpha);

        {
            ImFont* lf = font_medium(text_sm);
            draw_text(dl, lf, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(58.f)),
                      mo::with_alpha(c_muted_foreground, alpha), "Version");
            ImFont* vf = font_semibold(text_sm);
            char version_line[64];
            ImFormatString(version_line, IM_ARRAYSIZE(version_line), "%s", product_info::version);
            const float vw = text_width(vf, version_line);
            draw_text(dl, vf, ImVec2(card.Max.x - px(sp_4) - vw, card.Min.y + px(58.f)),
                      mo::with_alpha(c_foreground, alpha), version_line);
        }

        y = card.Max.y + px(sp_4);

        {
            const ImRect community(ImVec2(x, y), ImVec2(x + col, y + px(130.f)));
            panel(dl, community, alpha);

            const float icon_box = px(28.f);
            const ImRect icon_rect(ImVec2(community.Min.x + px(sp_4), community.Min.y + px(14.f)),
                                   ImVec2(community.Min.x + px(sp_4) + icon_box,
                                          community.Min.y + px(14.f) + icon_box));
            dl->AddRectFilled(icon_rect.Min, icon_rect.Max, mo::with_alpha(c_background, alpha),
                              px(8.f));
            draw_border(dl, icon_rect, px(8.f), px(1.f), mo::with_alpha(c_border, alpha), 0);
            icons::draw(
                icons::id::inbox, dl,
                ImVec2(icon_rect.GetCenter().x - px(8.f), icon_rect.GetCenter().y - px(8.f)),
                px(16.f), mo::with_alpha(c_foreground, alpha));

            row_label(dl, ImVec2(icon_rect.Max.x + px(10.f), community.Min.y + px(14.f)),
                      "Community", "Support, updates, and the rest of the SZK crew", alpha,
                      community.Max.x - (icon_rect.Max.x + px(10.f)) - px(sp_4));

            hairline(dl, community, community.Min.y + px(56.f), alpha);

            ImFont* jbf = font_medium(text_base);
            const float join_w = text_width(jbf, "Join Discord") + px(32.f);
            if (action("community-join",
                       ImVec2(community.Max.x - px(sp_4) - join_w / ui_runtime::scale,
                              community.Min.y + px(68.f)),
                       join_w / ui_runtime::scale, btn_idle, "Join Discord"))
                backend::open_discord();

            y = community.Max.y + px(sp_4);
        }

        const bool recovery_right_col = two_col;
        const float recovery_x = recovery_right_col ? aside_x : x;
        const float recovery_y = recovery_right_col ? aside_y : y;
        const float recovery_w = recovery_right_col ? aside_w : col;
        const float recovery_h = recovery_right_col ? px(182.f) : px(122.f);

        {
            const ImRect recovery(ImVec2(recovery_x, recovery_y),
                                  ImVec2(recovery_x + recovery_w, recovery_y + recovery_h));
            panel(dl, recovery, alpha);

            const float icon_box = px(28.f);
            const ImRect icon_rect(
                ImVec2(recovery.Min.x + px(sp_4), recovery.Min.y + px(14.f)),
                ImVec2(recovery.Min.x + px(sp_4) + icon_box, recovery.Min.y + px(14.f) + icon_box));
            dl->AddRectFilled(icon_rect.Min, icon_rect.Max, mo::with_alpha(c_background, alpha),
                              px(8.f));
            draw_border(dl, icon_rect, px(8.f), px(1.f), mo::with_alpha(c_border, alpha), 0);
            icons::draw(
                icons::id::settings, dl,
                ImVec2(icon_rect.GetCenter().x - px(8.f), icon_rect.GetCenter().y - px(8.f)),
                px(16.f), mo::with_alpha(c_foreground, alpha));

            row_label(dl, ImVec2(icon_rect.Max.x + px(10.f), recovery.Min.y + px(14.f)),
                      "System Recovery", "A safety net before you change anything risky", alpha,
                      recovery.Max.x - (icon_rect.Max.x + px(10.f)) - px(sp_4));

            if (recovery_right_col)
            {
                const float btn_w = recovery.GetWidth() - px(sp_4) * 2.f;
                const float btn_w_logical = btn_w / ui_runtime::scale;
                const float row1_y = recovery.Min.y + px(60.f);
                const float row2_y = row1_y + px(sp_12) + px(sp_2);
                const float btn_x = recovery.Min.x + px(sp_4);

                if (action("create-restore-point", ImVec2(btn_x, row1_y), btn_w_logical,
                           s.restore_point_btn, "Create Restore Point") &&
                    s.restore_point_btn == btn_idle)
                {
                    s.restore_point_btn = btn_loading;
                    s.restore_point_timer = 0.f;
                }
                if (s.restore_point_btn == btn_loading)
                {
                    s.restore_point_timer += dt;
                    if (s.restore_point_timer > 1.5f)
                    {
                        const bool ok = backend::create_system_restore_point();
                        s.restore_point_btn = ok ? btn_success : btn_error;
                        toast("System Restore", ok ? "Checkpoint created" : "Failed to create one",
                              ok ? toast_success : toast_error);
                    }
                }

                if (action("open-restore-wizard", ImVec2(btn_x, row2_y), btn_w_logical, btn_idle,
                           "Open System Restore"))
                    backend::open_system_restore_wizard();
            }
            else
            {
                const float button_y = recovery.Min.y + px(60.f);
                ImFont* bf = font_medium(text_base);
                const float pad_x = px(32.f);
                const float w1 = text_width(bf, "Create Restore Point") + pad_x;
                const float w2 = text_width(bf, "Open System Restore") + pad_x;
                const float row_right = recovery.Max.x - px(sp_4);
                const float x2 = row_right - w2;
                const float x1 = x2 - px(sp_3) - w1;
                const float w1_logical = w1 / ui_runtime::scale;
                const float w2_logical = w2 / ui_runtime::scale;

                if (action("create-restore-point", ImVec2(x1, button_y), w1_logical,
                           s.restore_point_btn, "Create Restore Point") &&
                    s.restore_point_btn == btn_idle)
                {
                    s.restore_point_btn = btn_loading;
                    s.restore_point_timer = 0.f;
                }
                if (s.restore_point_btn == btn_loading)
                {
                    s.restore_point_timer += dt;
                    if (s.restore_point_timer > 1.5f)
                    {
                        const bool ok = backend::create_system_restore_point();
                        s.restore_point_btn = ok ? btn_success : btn_error;
                        toast("System Restore", ok ? "Checkpoint created" : "Failed to create one",
                              ok ? toast_success : toast_error);
                    }
                }

                if (action("open-restore-wizard", ImVec2(x2, button_y), w2_logical, btn_idle,
                           "Open System Restore"))
                    backend::open_system_restore_wizard();
            }

            if (recovery_right_col)
                y = ImMax(y, recovery.Max.y + px(sp_4));
            else
                y = recovery.Max.y + px(sp_4);
        }

        if (action("open-recovery", ImVec2(x, y), col, btn_idle, "Open Windows Recovery Settings"))
            backend::open_windows_recovery_settings();
        y += px(44.f);
        break;
    }

    case 2:
    {
        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(216.f)));
        panel(dl, card, alpha);
        row_label(dl, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(16.f)), "System",
                  "This machine", alpha);

        const backend::machine_info info = backend::system_monitor_machine_info();

        const unsigned long long hh = info.uptime_seconds / 3600ULL;
        const unsigned long long mm = (info.uptime_seconds % 3600ULL) / 60ULL;
        char uptime_buf[24];
        std::snprintf(uptime_buf, sizeof(uptime_buf), "%lluh %llum", hh, mm);

        char os_buf[48];
        std::snprintf(os_buf, sizeof(os_buf), "Windows %s (Build %d)",
                      info.os_build >= 22000 ? "11" : "10", info.os_build);

        char cores_buf[16];
        std::snprintf(cores_buf, sizeof(cores_buf), "%d", info.logical_processors);

        const char* rows_label[4] = {"Hostname", "OS", "Logical CPUs", "Uptime"};
        const char* rows_value[4] = {info.hostname, os_buf, cores_buf, uptime_buf};

        float ry = card.Min.y + px(58.f);
        for (int i = 0; i < 4; i++)
        {
            ImFont* lf = font_medium(text_sm);
            draw_text(dl, lf, ImVec2(card.Min.x + px(sp_4), ry),
                      mo::with_alpha(c_muted_foreground, alpha), rows_label[i]);

            ImFont* vf = font_medium(text_sm);
            const float vw = text_width(vf, rows_value[i]);
            draw_text(dl, vf, ImVec2(card.Max.x - px(sp_4) - vw, ry),
                      mo::with_alpha(c_foreground, alpha), rows_value[i]);

            if (i + 1 < 4)
                hairline(dl, card, ry + px(30.f), alpha * 0.9f);
            ry += px(38.f);
        }

        y = card.Max.y + px(sp_4);

        if (action("check-updates", ImVec2(x, y), 200.f, s.check_updates_btn, "Check for Update") &&
            s.check_updates_btn == btn_idle)
        {
            s.check_updates_btn = btn_loading;
            s.check_updates_timer = 0.f;
        }
        if (s.check_updates_btn == btn_loading)
        {
            s.check_updates_timer += dt;
            if (s.check_updates_timer > 0.9f)
            {
                s.check_updates_btn = btn_success;
                toast("Up to date", "You're on the latest version", toast_success);
            }
        }
        y += px(44.f);
        break;
    }

    case 4:
    {
        const backend::reshade_status status = backend::reshade_check();
        const char* status_label = !status.game_found          ? "FiveM not found"
                                   : !status.installed         ? "Not installed"
                                   : !status.crash_ack_present ? "Needs Citizen.ini fix"
                                   : status.road_mod_present   ? "Installed + Road Mod"
                                                               : "Installed";
        const badge_status status_kind = !status.game_found          ? badge_neutral
                                         : !status.installed         ? badge_bad
                                         : !status.crash_ack_present ? badge_warn
                                                                     : badge_good;

        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(118.f)));
        panel(dl, card, alpha);

        const float icon_box = px(28.f);
        const ImRect icon_rect(
            ImVec2(card.Min.x + px(sp_4), card.Min.y + px(14.f)),
            ImVec2(card.Min.x + px(sp_4) + icon_box, card.Min.y + px(14.f) + icon_box));
        dl->AddRectFilled(icon_rect.Min, icon_rect.Max, mo::with_alpha(c_background, alpha),
                          px(8.f));
        draw_border(dl, icon_rect, px(8.f), px(1.f), mo::with_alpha(c_border, alpha), 0);
        icons::draw(icons::id::building_2, dl,
                    ImVec2(icon_rect.GetCenter().x - px(8.f), icon_rect.GetCenter().y - px(8.f)),
                    px(16.f), mo::with_alpha(c_foreground, alpha));

        const float bw = badge_width(status_label);
        row_label(dl, ImVec2(icon_rect.Max.x + px(10.f), card.Min.y + px(14.f)), "Auto ReShade",
                  "Drops ReShade + the 2K Road Mod into FiveM's plugin folder", alpha,
                  card.Max.x - (icon_rect.Max.x + px(10.f)) - bw - px(sp_3));
        badge("reshade-status", dl, ImVec2(card.Max.x - px(sp_4) - bw, card.Min.y + px(16.f)),
              status_label, status_kind);

        hairline(dl, card, card.Min.y + px(56.f), alpha);

        row_label(dl, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(68.f)), "Include 2K Road Mod",
                  "QuantV add-on for higher-resolution road textures", alpha,
                  col - px(sp_4) * 2.f - px(switch_w) - px(sp_3));
        switch_toggle("reshade-road-mod",
                      ImVec2(card.Max.x - px(sp_4) - px(switch_w), card.Min.y + px(78.5f)),
                      &s.reshade_road_mod);

        y = card.Max.y + px(sp_4);

        ImFont* bf = font_medium(text_base);
        const float pad_x = px(32.f);
        const float w1 = text_width(bf, "Install") + pad_x;
        const float w2 = text_width(bf, "Uninstall") + pad_x;
        const float w3 = text_width(bf, "Open Folder") + pad_x;
        const float row_right = x + col;
        const float x3 = row_right - w3;
        const float x2 = x3 - px(sp_3) - w2;
        const float x1 = x2 - px(sp_3) - w1;
        const float w1_logical = w1 / ui_runtime::scale;
        const float w2_logical = w2 / ui_runtime::scale;
        const float w3_logical = w3 / ui_runtime::scale;
        const float button_y = y;

        if (action("reshade-install", ImVec2(x1, button_y), w1_logical, s.reshade_install_btn,
                   "Install") &&
            s.reshade_install_btn == btn_idle)
        {
            s.reshade_install_btn = btn_loading;
            s.reshade_install_timer = 0.f;
        }
        if (s.reshade_install_btn == btn_loading)
        {
            s.reshade_install_timer += dt;
            if (s.reshade_install_timer > 0.8f)
            {
                const bool ok = backend::reshade_install(s.reshade_road_mod);
                s.reshade_install_btn = ok ? btn_success : btn_error;
                s.reshade_install_timer = 0.f;
                toast("ReShade",
                      ok ? (s.reshade_road_mod ? "Installed with the 2K Road Mod" : "Installed")
                         : "Some files failed to copy",
                      ok ? toast_success : toast_error);
            }
        }
        else if (s.reshade_install_btn == btn_success || s.reshade_install_btn == btn_error)
        {
            s.reshade_install_timer += dt;
            if (s.reshade_install_timer > 1.5f)
                s.reshade_install_btn = btn_idle;
        }

        if (action("reshade-uninstall", ImVec2(x2, button_y), w2_logical, s.reshade_uninstall_btn,
                   "Uninstall") &&
            s.reshade_uninstall_btn == btn_idle)
        {
            s.reshade_uninstall_btn = btn_loading;
            s.reshade_uninstall_timer = 0.f;
        }
        if (s.reshade_uninstall_btn == btn_loading)
        {
            s.reshade_uninstall_timer += dt;
            if (s.reshade_uninstall_timer > 0.6f)
            {
                const bool ok = backend::reshade_uninstall();
                s.reshade_uninstall_btn = ok ? btn_success : btn_error;
                s.reshade_uninstall_timer = 0.f;
                toast("ReShade", ok ? "Removed from the plugins folder" : "Nothing to remove",
                      ok ? toast_success : toast_error);
            }
        }
        else if (s.reshade_uninstall_btn == btn_success || s.reshade_uninstall_btn == btn_error)
        {
            s.reshade_uninstall_timer += dt;
            if (s.reshade_uninstall_timer > 1.5f)
                s.reshade_uninstall_btn = btn_idle;
        }

        if (action("reshade-plugins", ImVec2(x3, button_y), w3_logical, btn_idle, "Open Folder"))
            backend::reshade_open_plugins_folder();

        y = button_y + px(sp_12) + px(sp_4);
        break;
    }

    case 7:
    {
        tabs("notes-tabs", ImVec2(x, y), k_note_tabs, IM_ARRAYSIZE(k_note_tabs), &s.notes_tab,
             tabs_segment);
        y += tabs_height(tabs_segment) + px(sp_4);

        for (int i = 0; i < IM_ARRAYSIZE(k_notes); i++)
        {
            const note& n = k_notes[i];
            if (s.notes_tab == 1 && n.bucket != 1)
                continue;
            if (s.notes_tab == 2 && n.bucket != 2)
                continue;

            ImFont* bf = font_regular(text_xs);
            const float inner = col - px(sp_4) * 2.f;
            const int lines = ImMin(wrapped_line_count(bf, n.body, inner), 2);
            const float h = px(sp_4) * 2.f + px(leading_sm) + px(4.f) + px(18.f) * (float)lines +
                            px(6.f) + px(leading_xs);

            const ImRect card(ImVec2(x, y), ImVec2(x + col, y + h));
            panel(dl, card, alpha);

            ImFont* tf = font_medium(text_sm);
            draw_text(
                dl, tf,
                ImVec2(card.Min.x + px(sp_4), card.Min.y + px(sp_4) + line_top(tf, px(leading_sm))),
                mo::with_alpha(c_foreground, alpha), n.title);

            dl->PushClipRect(card.Min,
                             ImVec2(card.Max.x, card.Min.y + px(sp_4) + px(leading_sm) + px(4.f) +
                                                    px(18.f) * (float)lines),
                             true);
            draw_text_wrapped(
                dl, bf,
                ImVec2(card.Min.x + px(sp_4), card.Min.y + px(sp_4) + px(leading_sm) + px(4.f)),
                mo::with_alpha(c_muted_foreground, alpha), n.body, inner, px(18.f));
            dl->PopClipRect();

            ImFont* mf = font_regular(text_xs);
            draw_text(dl, mf,
                      ImVec2(card.Min.x + px(sp_4),
                             card.Max.y - px(sp_4) - px(leading_xs) + line_top(mf, px(leading_xs))),
                      mo::with_alpha(c_muted_foreground, 0.75f * alpha), n.meta);

            y = card.Max.y + px(sp_3);
        }
        break;
    }

    case 10:
    {

        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(96.f)));
        panel(dl, card, alpha);

        const float avatar = px(56.f);
        const ImVec2 at(card.Min.x + px(sp_5), card.GetCenter().y - avatar * 0.5f);
        if (!avatars::draw(dl, avatars::me(), at, avatar, alpha))
            chip(dl, at, avatar, avatar * 0.5f, brand::user_initials,
                 mo::with_alpha(c_foreground, 0.06f * alpha),
                 mo::with_alpha(c_muted_foreground, alpha));

        ImFont* nf = font_semibold(text_base);
        draw_text(dl, nf, ImVec2(at.x + avatar + px(sp_4), card.GetCenter().y - px(19.f)),
                  mo::with_alpha(c_foreground, alpha), brand::user_name);

        ImFont* ef = font_regular(text_sm);
        draw_text(dl, ef, ImVec2(at.x + avatar + px(sp_4), card.GetCenter().y + px(3.f)),
                  mo::with_alpha(c_muted_foreground, alpha), brand::user_github);

        y = card.Max.y + px(sp_5);

        const float row = px(46.f);
        const ImRect fields(
            ImVec2(x, y),
            ImVec2(x + col, y + px(sp_4) * 2.f + row * (float)IM_ARRAYSIZE(k_profile_fields)));
        panel(dl, fields, alpha);

        float fy = fields.Min.y + px(sp_4);
        for (int i = 0; i < IM_ARRAYSIZE(k_profile_fields); i++)
        {
            ImFont* lf = font_regular(text_xs);
            draw_text(dl, lf, ImVec2(fields.Min.x + px(sp_4), fy + px(6.f)),
                      mo::with_alpha(c_muted_foreground, alpha), k_profile_fields[i].label);

            ImFont* vf = font_medium(text_sm);
            draw_text(dl, vf,
                      ImVec2(fields.Max.x - px(sp_4) - text_width(vf, k_profile_fields[i].value),
                             fy + px(4.f)),
                      mo::with_alpha(c_foreground, alpha), k_profile_fields[i].value);

            if (i + 1 < IM_ARRAYSIZE(k_profile_fields))
                hairline(dl, fields, fy + row, alpha);
            fy += row;
        }
        y = fields.Max.y + px(sp_5);

        ImFont* sf = font_regular(10.f);
        draw_text_tracked(dl, sf, ImVec2(x, y + line_top(sf, px(15.f))),
                          mo::with_alpha(c_muted_foreground, alpha), "NOTIFICATIONS", px(1.6f));
        y += px(24.f);

        const ImRect prefs(
            ImVec2(x, y),
            ImVec2(x + col, y + px(sp_4) * 2.f + px(60.f) * (float)IM_ARRAYSIZE(k_profile_prefs)));
        panel(dl, prefs, alpha);

        float py = prefs.Min.y + px(sp_4);
        for (int i = 0; i < IM_ARRAYSIZE(k_profile_prefs); i++)
        {
            row_label(dl, ImVec2(prefs.Min.x + px(sp_4), py + px(6.f)), k_profile_prefs[i].name,
                      k_profile_prefs[i].detail, alpha);

            char id[32];
            ImFormatString(id, IM_ARRAYSIZE(id), "pref%d", i);
            if (switch_toggle(id, ImVec2(prefs.Max.x - px(sp_4) - px(switch_w), py + px(16.5f)),
                              &s.profile_prefs[i]))
                toast(k_profile_prefs[i].name, s.profile_prefs[i] ? "On" : "Off",
                      s.profile_prefs[i] ? toast_success : toast_neutral);

            py += px(60.f);
        }
        y = prefs.Max.y + px(sp_5);

        if (action("profile-save", ImVec2(x, y), 200.f, s.profile_save, "Save changes") &&
            s.profile_save == btn_idle)
        {
            s.profile_save = btn_loading;
            s.profile_timer = 0.f;
        }
        if (s.profile_save == btn_loading)
        {
            s.profile_timer += dt;
            if (s.profile_timer > 1.1f)
            {
                s.profile_save = btn_success;
                toast("Profile saved", brand::user_name, toast_success);
            }
        }
        y += px(44.f);

        ImFont* cl = font_regular(10.f);
        draw_text_tracked(dl, cl, ImVec2(x, y + line_top(cl, px(15.f))),
                          mo::with_alpha(c_muted_foreground, alpha), "CREDITS", px(1.6f));
        y += px(24.f);

        {
            const float crow = px(36.f);
            const ImRect box(
                ImVec2(x, y),
                ImVec2(x + col, y + px(sp_4) * 2.f + crow * (float)IM_ARRAYSIZE(k_credits)));
            panel(dl, box, alpha);

            ImFont* kf = font_regular(text_sm);
            ImFont* vf = font_medium(text_sm);

            float cy = box.Min.y + px(sp_4);
            for (int i = 0; i < IM_ARRAYSIZE(k_credits); i++)
            {
                draw_text(dl, kf, ImVec2(box.Min.x + px(sp_4), cy + px(4.f)),
                          mo::with_alpha(c_muted_foreground, alpha), k_credits[i].label);

                const float lw = text_width(kf, k_credits[i].label);
                const float room = col - px(sp_4) * 2.f - lw - px(16.f);
                const float vw = ImMin(text_width(vf, k_credits[i].value), room);
                draw_text_ellipsis(dl, vf, ImVec2(box.Max.x - px(sp_4) - vw, cy + px(4.f)),
                                   mo::with_alpha(c_foreground, alpha), k_credits[i].value, room);

                if (i + 1 < IM_ARRAYSIZE(k_credits))
                    hairline(dl, box, cy + crow - px(6.f), alpha);

                cy += crow;
            }
            y = box.Max.y + px(sp_5);
        }
        break;
    }

    case 11:
    {
        tabs("notif-tabs", ImVec2(x, y), k_notif_tabs, IM_ARRAYSIZE(k_notif_tabs), &s.notif_tab,
             tabs_pill);

        {
            const float bw = px(150.f);
            const float tabs_w = tabs_width(k_notif_tabs, IM_ARRAYSIZE(k_notif_tabs), tabs_pill);
            const float bx = ImMax(x + tabs_w + px(sp_3), x + col - bw);
            if (action("mark-notifs", ImVec2(bx, y - px(3.f)), 150.f, btn_idle, "Mark all read"))
                s.mark_cascade = 0.f;
        }
        if (s.mark_cascade < 1e5f)
        {
            s.mark_cascade += dt;
            for (int i = 0; i < IM_ARRAYSIZE(k_notifications); i++)
                if (s.mark_cascade > (float)i * 0.055f)
                    s.notif_read[i] = true;
            if (s.mark_cascade > 1.4f)
                s.mark_cascade = 1e6f;
        }

        y += tabs_height(tabs_pill) + px(sp_4);

        const float row_h = px(64.f);
        int visible[IM_ARRAYSIZE(k_notifications)];
        int count = 0;
        for (int i = 0; i < IM_ARRAYSIZE(k_notifications); i++)
        {
            if (s.notif_gone[i])
                continue;
            if (s.notif_tab == 1 && s.notif_read[i])
                continue;
            if (s.notif_tab == 2 && !k_notifications[i].mention)
                continue;
            visible[count++] = i;
        }

        int drawn = 0;
        int last_day = -1;
        float total = 0.f;
        const float start_y = y;

        for (int k = 0; k < count; k++)
        {
            const int i = visible[k];
            const notification& n = k_notifications[i];

            const float open = s.notif_height[i].to(row_h, mo::SPRING_LAYOUT, dt);
            if (open < 1.f)
                continue;

            if (n.day != last_day)
            {
                last_day = n.day;
                ImFont* df = font_medium(text_xs);
                const float t = stagger(drawn);
                draw_text(dl, df, ImVec2(x + px(2.f), y + px(6.f) + px(8.f) * (1.f - t)),
                          mo::with_alpha(c_muted_foreground, 0.9f * t * alpha),
                          k_notif_days[ImClamp(n.day, 0, 2)]);
                y += px(28.f);
                total += px(28.f);
            }

            const float t = stagger(drawn++);
            const float slide = s.notif_slide[i];
            const float rx = x + px(16.f) * (1.f - t) + slide;
            const float row_a = alpha * t * (1.f - ImClamp(slide / px(120.f), 0.f, 1.f));

            const ImRect card(ImVec2(rx, y), ImVec2(rx + col, y + open - px(6.f)));

            dl->PushClipRect(ImVec2(x, y), ImVec2(x + col, y + open), true);
            panel(dl, card, row_a);

            const bool unread = !s.notif_read[i];

            {
                ImGui::PushID(1000 + i);
                mo::spring* bar = ui_runtime::animation_state<mo::spring>(
                    ImGui::GetCurrentWindow()->GetID("bar"));
                ImGui::PopID();
                const float h = bar->to(unread ? px(20.f) : 0.f, mo::SPRING_LAYOUT, dt);
                if (h > 0.5f)
                    dl->AddRectFilled(ImVec2(card.Min.x + px(1.f), card.GetCenter().y - h * 0.5f),
                                      ImVec2(card.Min.x + px(4.f), card.GetCenter().y + h * 0.5f),
                                      mo::with_alpha(c_primary, row_a), px(2.f));
            }

            char row_id[24];
            ImFormatString(row_id, IM_ARRAYSIZE(row_id), "notif%d", i);
            const ImRect hit(ImVec2(card.Min.x + px(4.f), card.Min.y + px(2.f)),
                             ImVec2(card.Max.x - px(40.f), card.Max.y - px(2.f)));
            if (row_hit(dl, row_id, hit, row_a, false) && unread)
                s.notif_read[i] = true;

            const float av = px(30.f);
            char ini[3];
            initials_of(n.who, ini);
            chip(dl, ImVec2(card.Min.x + px(14.f), card.GetCenter().y - av * 0.5f), av, av * 0.5f,
                 ini, mo::with_alpha(c_foreground, 0.06f * row_a),
                 mo::with_alpha(c_muted_foreground, row_a), n.person);

            const float tx = card.Min.x + px(14.f) + av + px(12.f);

            ImFont* wf = unread ? font_semibold(text_sm) : font_medium(text_sm);
            ImFont* af = font_regular(text_sm);
            ImFont* tf = font_regular(text_xs);

            const float when_w = text_width(tf, n.when);
            const float when_r = card.Max.x - px(44.f);
            const float line_w = (when_r - px(10.f) - when_w) - tx;

            float cx = tx;
            cx += draw_text_ellipsis(dl, wf, ImVec2(cx, card.GetCenter().y - px(15.f)),
                                     mo::with_alpha(c_foreground, row_a), n.who, line_w);
            cx += px(4.f);
            draw_text_ellipsis(dl, af, ImVec2(cx, card.GetCenter().y - px(15.f)),
                               mo::with_alpha(c_muted_foreground, row_a), n.did,
                               line_w - (cx - tx));

            draw_text_ellipsis(dl, af, ImVec2(tx, card.GetCenter().y + px(3.f)),
                               mo::with_alpha(c_foreground, 0.88f * row_a), n.about, line_w);

            draw_text(dl, tf, ImVec2(when_r - when_w, card.GetCenter().y - tf->LegacySize * 0.5f),
                      mo::with_alpha(c_muted_foreground, row_a), n.when);

            {
                char cid[24];
                ImFormatString(cid, IM_ARRAYSIZE(cid), "x%d", i);
                const ImRect xb(ImVec2(card.Max.x - px(36.f), card.GetCenter().y - px(13.f)),
                                ImVec2(card.Max.x - px(10.f), card.GetCenter().y + px(13.f)));

                ImGui::PushID(cid);
                const ImGuiID xid = ImGui::GetCurrentWindow()->GetID("x");
                ImGui::PopID();
                ImGui::SetCursorScreenPos(xb.Min);
                ImGui::ItemSize(ImVec2(0, 0));
                ImGui::ItemAdd(xb, xid);

                bool xh = false, xheld = false;
                if (ImGui::ButtonBehavior(xb, xid, &xh, &xheld))
                {
                    s.notif_undo = i;
                    s.undo_timer = 0.f;
                }
                if (xh)
                    dl->AddCircleFilled(xb.GetCenter(), px(13.f),
                                        mo::with_alpha(c_foreground, 0.07f * row_a));
                icons::draw(icons::id::cross, dl,
                            ImVec2(xb.GetCenter().x - px(7.f), xb.GetCenter().y - px(7.f)),
                            px(14.f),
                            mo::with_alpha(c_muted_foreground, (xh ? 1.f : 0.7f) * row_a));
            }

            dl->PopClipRect();

            y += open;
            total += open;
        }

        if (s.notif_undo >= 0)
        {
            const int i = s.notif_undo;
            s.undo_timer += dt;
            s.notif_slide[i] = ImMin(px(140.f), s.notif_slide[i] + dt * px(900.f));
            if (s.notif_slide[i] >= px(120.f))
                s.notif_height[i].to(0.f, mo::SPRING_LAYOUT, dt);

            if (s.undo_timer > 0.32f)
            {
                s.notif_gone[i] = true;
                s.notif_height[i] = mo::spring();
                s.notif_slide[i] = 0.f;
                s.notif_undo = -1;
                toast("Dismissed", k_notifications[i].about, toast_neutral);
            }
        }

        if (drawn == 0)
        {
            const ImRect card(ImVec2(x, start_y), ImVec2(x + col, start_y + px(120.f)));
            const float t = mo::EASE_OUT(ImClamp(s.entered / 0.4f, 0.f, 1.f));
            panel(dl, card, alpha * t);
            empty_state(dl, card, s.notif_tab == 2 ? "Nobody has said your name." : "Nothing new.",
                        alpha * t);
            y = card.Max.y;
        }
        break;
    }

    case 12:
    {

        if (s.reset_cascade < 1e5f)
        {
            const bool defaults[] = {true, false, true, true, true, false, true};
            s.reset_cascade += dt;
            for (int i = 0; i < IM_ARRAYSIZE(k_prefs); i++)
                if (s.reset_cascade > (float)i * 0.07f)
                    s.pref_on[i] = defaults[i];
            if (s.reset_cascade > 1.4f)
            {
                s.reset_cascade = 1e6f;
                toast("Preferences reset", "Back to how they shipped", toast_info);
            }
        }

        int index = 0;

        for (int g = 0; g < IM_ARRAYSIZE(k_pref_groups); g++)
        {
            const pref_group& grp = k_pref_groups[g];

            float body_h = 0.f;
            for (int r = 0; r < grp.count; r++)
            {
                const pref_row& pr = k_prefs[grp.first + r];
                const float extra_h = (pr.extra == extra_none)
                                          ? 0.f
                                          : (pr.extra == extra_quiet ? px(slider_h) + px(8.f)
                                                                     : px(select_h) + px(10.f));
                body_h += px(70.f) + px(sp_2) +
                          ((s.pref_on[grp.first + r] && pr.extra != extra_none) ? extra_h : 0.f);
            }

            char aid[16];
            ImFormatString(aid, IM_ARRAYSIZE(aid), "prefgrp%d", g);

            float body_alpha = 1.f;
            const float open_h = accordion(aid, dl, ImVec2(x, y), col, grp.title, &s.pref_open[g],
                                           body_h, alpha, &body_alpha);

            y += px(accordion_trigger_h);

            if (open_h <= 1.f)
            {

                index += grp.count;
                y += px(sp_3);
                continue;
            }

            const float body_top = y;
            dl->PushClipRect(ImVec2(x, body_top), ImVec2(x + col, body_top + open_h), true);
            y = body_top + open_h - body_h;

            for (int r = 0; r < grp.count; r++)
            {
                const int i = grp.first + r;
                const pref_row& pr = k_prefs[i];
                const float t = stagger(index++);

                const float extra_h = (pr.extra == extra_none)
                                          ? 0.f
                                          : (pr.extra == extra_quiet ? px(slider_h) + px(8.f)
                                                                     : px(select_h) + px(10.f));
                const float open =
                    s.pref_detail[i].to((s.pref_on[i] && pr.extra != extra_none) ? extra_h : 0.f,
                                        mo::SPRING_LAYOUT, dt);

                const float base_h = px(70.f);
                const ImRect card(ImVec2(x + px(14.f) * (1.f - t), y),
                                  ImVec2(x + col + px(14.f) * (1.f - t), y + base_h + open));
                const float row_a = alpha * t;

                dl->PushClipRect(ImVec2(x, y), ImVec2(x + col, y + base_h + open), true);
                panel(dl, card, row_a);

                ImFont* lf = font_medium(text_sm);
                draw_text_ellipsis(dl, lf, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(16.f)),
                                   mo::with_alpha(c_foreground, row_a), pr.label,
                                   col - px(sp_4) * 2.f - px(switch_w) - px(16.f));

                {
                    ImGui::PushID(2000 + i);
                    mo::spring* mixv = ui_runtime::animation_state<mo::spring>(
                        ImGui::GetCurrentWindow()->GetID("mix"));
                    ImGui::PopID();
                    const float m = mixv->to(s.pref_on[i] ? 1.f : 0.f, mo::SPRING_LAYOUT, dt);

                    ImFont* df = font_regular(text_xs);
                    const float dy = card.Min.y + px(38.f);
                    const float dw = col - px(sp_4) * 2.f - px(switch_w) - px(16.f);

                    if (m < 0.995f)
                        draw_text_ellipsis(dl, df, ImVec2(card.Min.x + px(sp_4), dy + px(6.f) * m),
                                           mo::with_alpha(c_muted_foreground, (1.f - m) * row_a),
                                           pr.off_text, dw);
                    if (m > 0.005f)
                        draw_text_ellipsis(
                            dl, df, ImVec2(card.Min.x + px(sp_4), dy - px(6.f) * (1.f - m)),
                            mo::with_alpha(c_muted_foreground, m * row_a), pr.on_text, dw);
                }

                {
                    char sid[24];
                    ImFormatString(sid, IM_ARRAYSIZE(sid), "pref%d", i);
                    switch_toggle(
                        sid, ImVec2(card.Max.x - px(sp_4) - px(switch_w), card.Min.y + px(23.5f)),
                        &s.pref_on[i]);
                }

                if (open > 2.f)
                {
                    const float iy = card.Min.y + base_h + open - extra_h;
                    const float iw = ImMin(col - px(sp_4) * 2.f, px(220.f));
                    const ImVec2 at(card.Min.x + px(sp_4), iy);

                    if (pr.extra == extra_theme)
                        select("pref-theme", at, iw, k_theme_names, IM_ARRAYSIZE(k_theme_names),
                               &s.pref_theme, "System");
                    else if (pr.extra == extra_digest)
                        select("pref-day", at, iw, k_digest_days, IM_ARRAYSIZE(k_digest_days),
                               &s.pref_digest_day, "Monday");
                    else
                        range_slider("pref-quiet", ImVec2(at.x, iy - px(6.f)), col - px(sp_4) * 2.f,
                                     &s.pref_quiet, 0.f, 1.f, 6);
                }

                dl->PopClipRect();
                y += base_h + open + px(sp_2);
            }

            dl->PopClipRect();
            y = body_top + open_h;
            y += px(sp_3);
        }

        y -= px(sp_3);
        y += px(sp_2);

        if (action("reset-prefs", ImVec2(x, y), 170.f, btn_idle, "Reset to defaults"))
            s.reset_cascade = 0.f;
        y += px(44.f);
        break;
    }

    default:
    {
        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(120.f)));
        panel(dl, card, alpha);

        ImFont* f = font_regular(text_sm);
        draw_text_wrapped(
            dl, f, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(sp_4)),
            mo::with_alpha(c_muted_foreground, alpha),
            "Nothing needs your attention here right now. Pick another view from the rail, "
            "or fold it away with Ctrl+B.",
            col - px(sp_4) * 2.f, px(22.f));
        y = card.Max.y;
        break;
    }
    }

    if (two_col)
    {

        float aside_h = page_aside(dl, nav, ImVec2(aside_x, aside_y), aside_w, alpha, aside_offset);

        const float more =
            page_aside_more(dl, nav, ImVec2(aside_x, aside_y + aside_h + px(sp_5)), aside_w, alpha);
        if (more > 0.f)
            aside_h += px(sp_5) + more;

        y = ImMax(y, aside_y + aside_h) + px(sp_6);
    }
    else if (nav != route_index(route::dashboard) && nav != route_index(route::assistant))
    {
        y += activity(dl, ImVec2(x, y + px(sp_6)), col, alpha) + px(sp_6);
    }

    s.content[slot] = (y + px(sp_6)) - (area.Min.y - scroll);

    dl->PopClipRect();
    scrollbar(dl, area, measured, scroll, alpha);
    return nav_request;
}
} // namespace solace
