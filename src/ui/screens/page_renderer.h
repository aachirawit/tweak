#pragma once

#include "ui/screens/navigation.h"

struct ImRect;

namespace szk
{
// Returns route::count when the page doesn't want to navigate anywhere this
// frame, or the requested destination if e.g. a quick-link tile was clicked.
route draw_page(route destination, const char* title, const char* const* subs, int sub_count,
                int* sub, const ImRect& area, float alpha);
} // namespace szk
