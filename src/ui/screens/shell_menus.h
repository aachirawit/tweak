#pragma once
#include "imgui.h"
#include "imgui_internal.h"

namespace szk
{

enum profile_choice
{
    profile_none = 0,
    profile_open_page,
    profile_open_preferences,
    profile_sign_out,
};

bool profile_trigger(const ImRect& rect);
bool profile_menu_open();

profile_choice profile_menu(const ImRect& viewport, float alpha);
} // namespace szk
