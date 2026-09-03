#pragma once

namespace solace
{
// Storage order, not menu order. page_renderer indexes its blurb, column and
// scroll tables straight off route_index(), and shell_screen's k_items table
// is required to line up with the first route::profile entries, so nothing
// here may be reordered or removed. What the user actually sees is the rail
// table in shell_screen.cpp, which is free to name, group and order rows any
// way it likes and points back at these routes.
enum class route : int
{
    search = 0,
    assistant,
    messages,
    settings,
    presets,
    patches,
    tasks,
    notes,
    automation,
    dashboard,
    profile,
    notifications,
    preferences,
    count,
};

inline constexpr int route_count = static_cast<int>(route::count);

[[nodiscard]] constexpr int route_index(route value) noexcept
{
    return static_cast<int>(value);
}

[[nodiscard]] constexpr route route_from_index(int value) noexcept
{
    return value < 0              ? route::search
           : value >= route_count ? route::preferences
                                  : static_cast<route>(value);
}

[[nodiscard]] constexpr bool is_account_route(route value) noexcept
{
    return value == route::profile || value == route::notifications || value == route::preferences;
}

// route::settings is the one page with tabs. Naming them keeps the rail, the
// search index and the page tab strip talking about the same seven things.
enum class settings_tab : int
{
    all = 0,
    performance,
    network,
    power_plan,
    nvidia,
    amd,
    cleanup,
    count,
};

inline constexpr int settings_tab_count = static_cast<int>(settings_tab::count);

[[nodiscard]] constexpr int tab_index(settings_tab value) noexcept
{
    return static_cast<int>(value);
}

// Where a rail row or a search hit lands. tab is the index within a tabbed
// route, or keep_tab to leave whatever tab that route was last on.
inline constexpr int keep_tab = -1;

struct nav_target
{
    route dest;
    int tab;
};
} // namespace solace
