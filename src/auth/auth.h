#pragma once

#include "imgui.h"
#include "imgui_internal.h"

namespace szk
{
enum class auth_action
{
    none = 0,
    done,
    terms,
    privacy,
};

struct auth_view_effect
{
    float opacity = 1.f;
    ImVec2 offset{};
    bool interactive = true;
};

const auth_view_effect& auth_effect();
void auth_apply_effect(ImDrawList* draw_list, int first_vertex);

namespace auth_layout
{
inline constexpr float stage_width = 416.f;

struct frame
{
    ImGuiWindow* window = nullptr;
    ImDrawList* draw_list = nullptr;
    ImRect card;
};

frame begin(const char* name, const ImVec2& card_size, bool interactive = true);
void end();
} // namespace auth_layout

// The one gate into the app: a KeyAuth licence key. There is no account to
// create here - keys are issued by the seller dashboard, not by this client.
auth_action license_screen();
void license_reset();

void auth_stage(ImDrawList* draw_list, const ImRect& stage, const ImRect& card, float rounding);

enum class legal_document
{
    terms = 0,
    privacy,
};

bool legal_screen(legal_document document);
} // namespace szk
