#pragma once
#include "imgui.h"
#include "imgui_internal.h"

namespace szk
{

bool notifications_trigger(const ImRect& rect);
bool notifications_open();
int notifications_unread();

void notifications_panel(const ImRect& viewport, float alpha);

enum profile_choice
{
    profile_none = 0,
    profile_open_page,
    profile_open_notifications,
    profile_open_preferences,
    profile_sign_out,
};

bool profile_trigger(const ImRect& rect);
bool profile_menu_open();

profile_choice profile_menu(const ImRect& viewport, float alpha);
} // namespace szk
