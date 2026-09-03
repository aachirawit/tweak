#include "auth/auth.h"

#include "application/brand.h"
#include "backend/keyauth.h"
#include "ui/controls/form_controls.h"
#include "ui/foundation/primitives.h"
#include "ui/screens/shell.h"

#include <string>

namespace szk
{
namespace
{
constexpr float k_message_duration = 0.2f;

const char* k_title = "Sign in to SZK";
const char* k_description =
    "Enter the licence key from your purchase email. It binds to this machine the first time "
    "you use it.";

struct form_state
{
    char key[96] = "";

    button_state status = btn_idle;
    bool waiting = false; // a KeyAuth request is in flight
    bool touched = false;

    input_state in_key;
    stateful_button_state submit;

    mo::presence banner_presence;
    mo::spring h_banner;
    float banner_h = 0.f;
    std::string banner_text;
    bool banner_is_error = true;

    bool hwid_copied = false;
    float hwid_copied_timer = 0.f;
};

form_state& state()
{
    static form_state s;
    return s;
}

// The full machine GUID is long and the user only ever reads it aloud or
// pastes it, so the screen shows a shortened form and the Copy link puts the
// whole thing on the clipboard.
std::string hwid_display()
{
    const std::string full = backend::auth_hwid();
    if (full.empty())
        return "unavailable";
    if (full.size() <= 18)
        return full;
    return full.substr(0, 8) + "\xE2\x80\xA6" + full.substr(full.size() - 6);
}
} // namespace

void license_reset()
{
    state() = form_state();
    backend::auth_reset();
}

auth_action license_screen()
{
    form_state& s = state();
    const auth_view_effect& fx = auth_effect();
    const float dt = fx.interactive ? ImGui::GetIO().DeltaTime : 0.f;
    auth_action action = auth_action::none;

    // ── Poll the background licence check ───────────────────────────────────
    if (s.waiting)
    {
        const backend::auth_result result = backend::auth_status_now();

        if (result.status == backend::auth_status::authorised)
        {
            s.waiting = false;
            s.status = btn_success;
            s.banner_text = result.message;
            s.banner_is_error = false;
            action = auth_action::done;
        }
        else if (result.status == backend::auth_status::failed)
        {
            s.waiting = false;
            s.status = btn_error;
            s.banner_text = result.message;
            s.banner_is_error = true;
        }
    }

    const bool submitting = (s.status == btn_loading);

    if (s.hwid_copied)
    {
        s.hwid_copied_timer += dt;
        if (s.hwid_copied_timer > 1.8f)
        {
            s.hwid_copied = false;
            s.hwid_copied_timer = 0.f;
        }
    }

    const bool key_empty = (s.key[0] == 0);
    const char* key_error = (s.touched && key_empty) ? "Enter your licence key." : nullptr;

    input_desc d_key;
    d_key.label = "Licence key";
    d_key.placeholder = "XXXXX-XXXXX-XXXXX-XXXXX";
    d_key.buf = s.key;
    d_key.buf_size = IM_ARRAYSIZE(s.key);
    d_key.left = icon_lock;
    d_key.error = key_error;
    d_key.disabled = submitting;

    const char* submit_label = s.status == btn_loading   ? "Checking key"
                               : s.status == btn_success ? "Unlocked"
                               : s.status == btn_error   ? "Try again"
                                                         : "Unlock SZK";

    input_update(s.in_key, d_key, dt);
    stateful_button_update(s.submit, s.status, submit_label, dt);

    s.banner_presence.update(!s.banner_text.empty(), dt, k_message_duration);

    const float content_w = px(max_w_sm - 2.f - sp_6 * 2.f);

    const int description_lines =
        wrapped_line_count(font_regular(text_sm), k_description, content_w);
    const float description_h = px(leading_sm) * (float)description_lines;

    // The banner wraps, so its open height depends on what it is saying.
    const int banner_lines =
        s.banner_text.empty()
            ? 1
            : ImMax(1, wrapped_line_count(font_regular(text_xs), s.banner_text.c_str(),
                                          content_w - px(sp_3) * 2.f));
    const float k_banner_full =
        px(sp_5) + px(1.f + 10.f) + px(leading_xs) * (float)banner_lines + px(10.f + 1.f);

    s.banner_h =
        s.h_banner.to(s.banner_presence.mounted && !s.banner_presence.exiting ? k_banner_full : 0.f,
                      mo::SPRING_LAYOUT, dt);

    float height = px(1.f + sp_6);
    height += px(leading_xl + sp_1) + description_h;
    height += px(sp_5);
    height += input_height(s.in_key);
    height += s.banner_h;
    height += px(sp_5);
    height += px(sp_12);           // submit button
    height += px(sp_5) + px(44.f); // hardware id row
    height += px(sp_5) + px(leading_sm);
    height += px(sp_6 + 1.f);

    const ImVec2 card_size =
        shell::animate_size(ImVec2(px(max_w_sm + auth_layout::stage_width), height));
    const auth_layout::frame frame = auth_layout::begin("LicenseForm", card_size, fx.interactive);
    {
        ImDrawList* dl = frame.draw_list;
        const ImRect& card = frame.card;

        const int content_vtx_begin = dl->VtxBuffer.Size;

        const float x = card.Min.x + px(1.f + sp_6) + fx.offset.x;
        float y = card.Min.y + px(1.f + sp_6) + fx.offset.y;

        ImFont* title_font = font_semibold(text_xl);
        draw_text_tracked(dl, title_font, ImVec2(x, y + line_top(title_font, px(leading_xl))),
                          c_foreground, k_title, px(text_xl * tracking_tight));
        y += px(leading_xl + sp_1);

        draw_text_wrapped(dl, font_regular(text_sm), ImVec2(x, y), c_muted_foreground,
                          k_description, content_w, px(leading_sm));
        y += description_h;

        y += px(sp_5);

        bool blurred = false;
        const bool changed = input_draw("key", s.in_key, d_key, ImVec2(x, y), content_w, &blurred);
        if (blurred)
            s.touched = true;
        y += input_height(s.in_key);

        // ── Result banner ───────────────────────────────────────────────────
        // Success is green and failure is red, but both also change the button
        // label, so the outcome survives a screenshot and a colourblind reader.
        if (s.banner_h > 0.5f && !s.banner_text.empty())
        {
            const float open = s.banner_h;
            dl->PushClipRect(ImVec2(x, y), ImVec2(x + content_w, y + open), true);

            const float block_top = y;
            const float draw_y = block_top + open - k_banner_full + px(sp_5);

            const bool exiting = s.banner_presence.exiting;
            const float p = mo::EASE_OUT_NAMED(ImClamp(
                (exiting ? s.banner_presence.out : s.banner_presence.in) / k_message_duration, 0.f,
                1.f));
            const float opacity = exiting ? 1.f - p : p;
            const float shift = -4.f * (exiting ? p : 1.f - p);

            const float block_h =
                px(1.f + 10.f) + px(leading_xs) * (float)banner_lines + px(10.f + 1.f);
            const ImVec2 bmin(x, draw_y + px(shift));
            const ImVec2 bmax(x + content_w, bmin.y + block_h);

            const ImU32 tone = s.banner_is_error ? c_destructive : c_accent;

            dl->AddRectFilled(bmin, bmax, mo::with_alpha(tone, 0.1f * opacity), px(16.f));
            dl->AddRect(ImVec2(bmin.x + px(0.5f), bmin.y + px(0.5f)),
                        ImVec2(bmax.x - px(0.5f), bmax.y - px(0.5f)),
                        mo::with_alpha(tone, 0.3f * opacity), px(16.f), px(1.f), ImDrawFlags_None);

            draw_text_wrapped(dl, font_regular(text_xs),
                              ImVec2(bmin.x + px(sp_3), bmin.y + px(1.f + 10.f)),
                              mo::with_alpha(tone, opacity), s.banner_text.c_str(),
                              content_w - px(sp_3) * 2.f, px(leading_xs));

            dl->PopClipRect();
            y = block_top + open;
        }

        y += px(sp_5);

        // ── Submit ──────────────────────────────────────────────────────────
        const bool clicked =
            stateful_button_draw("submit", s.submit, s.status, ImVec2(x, y), content_w, false);
        const bool enter = fx.interactive && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                              ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));

        if ((clicked || enter) && !submitting && s.status != btn_success)
        {
            s.touched = true;

            if (!key_empty)
            {
                s.banner_text.clear();
                if (backend::auth_begin(s.key))
                {
                    s.status = btn_loading;
                    s.waiting = true;
                }
                else
                {
                    // auth_begin only refuses when it already published a
                    // reason, so show that rather than inventing one.
                    const backend::auth_result why = backend::auth_status_now();
                    s.status = btn_error;
                    s.banner_text = why.message;
                    s.banner_is_error = true;
                }
            }
        }

        y += px(sp_12);
        y += px(sp_5);

        // ── Hardware ID ─────────────────────────────────────────────────────
        // Support's first question on a rejected key is always "what is your
        // HWID", so the answer lives on the screen that shows the rejection.
        {
            const float row_h = px(44.f);
            const ImVec2 rmin(x, y);
            const ImVec2 rmax(x + content_w, y + row_h);

            dl->AddRectFilled(rmin, rmax, mo::with_alpha(c_card, 0.6f), px(12.f));
            dl->AddRect(ImVec2(rmin.x + px(0.5f), rmin.y + px(0.5f)),
                        ImVec2(rmax.x - px(0.5f), rmax.y - px(0.5f)), c_border, px(12.f), px(1.f),
                        ImDrawFlags_None);

            ImFont* label_font = font_regular(text_xs);
            draw_text(dl, label_font, ImVec2(rmin.x + px(sp_3), rmin.y + px(7.f)), c_dim_foreground,
                      "Hardware ID");

            ImFont* value_font = font_medium(text_xs);
            const std::string shown = hwid_display();
            draw_text(dl, value_font, ImVec2(rmin.x + px(sp_3), rmin.y + px(23.f)), c_foreground,
                      shown.c_str());

            ImFont* link_font = font_medium(text_xs);
            const char* copy_label = s.hwid_copied ? "Copied" : "Copy";
            const float link_w = text_width(link_font, copy_label);
            const ImVec2 link_at(rmax.x - px(sp_3) - link_w,
                                 rmin.y + row_h * 0.5f - link_font->LegacySize * 0.5f);

            if (s.hwid_copied)
            {
                draw_text(dl, link_font, link_at, c_accent, copy_label);
            }
            else if (link("copy-hwid", dl, link_font, link_at, copy_label, c_muted_foreground, 1.f))
            {
                const std::string full = backend::auth_hwid();
                if (!full.empty())
                {
                    ImGui::SetClipboardText(full.c_str());
                    s.hwid_copied = true;
                    s.hwid_copied_timer = 0.f;
                }
            }

            y += row_h;
        }

        y += px(sp_5);

        // ── Legal ───────────────────────────────────────────────────────────
        {
            ImFont* lead_font = font_regular(text_sm);
            ImFont* link_font = font_medium(text_sm);

            const char* lead = "You accept the ";
            const char* terms = "Terms";
            const char* mid = " and ";
            const char* privacy = "Privacy";

            const float lead_w = text_width(lead_font, lead);
            const float terms_w = text_width(link_font, terms);
            const float mid_w = text_width(lead_font, mid);
            const float privacy_w = text_width(link_font, privacy);

            const float total = lead_w + terms_w + mid_w + privacy_w;
            float fx_x = x + (content_w - total) * 0.5f;
            const float fy = y + line_top(lead_font, px(leading_sm));

            draw_text(dl, lead_font, ImVec2(fx_x, fy), c_muted_foreground, lead);
            fx_x += lead_w;

            if (link("terms", dl, link_font, ImVec2(fx_x, fy), terms, c_foreground, 1.f))
                action = auth_action::terms;
            fx_x += terms_w;

            draw_text(dl, lead_font, ImVec2(fx_x, fy), c_muted_foreground, mid);
            fx_x += mid_w;

            if (link("privacy", dl, link_font, ImVec2(fx_x, fy), privacy, c_foreground, 1.f))
                action = auth_action::privacy;
        }

        // Editing the key after a rejection clears the banner, so the message
        // never outlives the input it was about.
        if (changed && s.status == btn_error)
        {
            s.status = btn_idle;
            s.banner_text.clear();
            backend::auth_reset();
        }

        auth_apply_effect(dl, content_vtx_begin);
    }
    auth_layout::end();

    return action;
}
} // namespace szk
