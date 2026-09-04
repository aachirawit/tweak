#pragma once

namespace szk
{
// Storage order, not menu order. page_renderer indexes its blurb, column and
// scroll tables straight off route_index(), and shell_screen's k_items table
// must line up with the entries before route::profile, so the order here is
// load-bearing: the non-account routes come first (their blurbs are one array),
// then the account routes (profile, preferences) sit together at the end so a
// single "nav >= profile" test tells them apart. The route-content switches key
// off route_index(route::X) named labels, so they follow this order on their
// own. What the user actually sees is the rail table in shell_screen.cpp.
enum class route : int
{
    // Non-account routes. k_items in shell_screen.cpp mirrors these, in order.
    assistant, // About
    messages,  // This machine
    settings,
    presets,    // Auto ReShade
    automation, // Drivers
    dashboard,
    // Account routes, grouped at the end.
    profile,
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
    return value < 0              ? route::dashboard
           : value >= route_count ? route::preferences
                                  : static_cast<route>(value);
}

[[nodiscard]] constexpr bool is_account_route(route value) noexcept
{
    return value == route::profile || value == route::preferences;
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
} // namespace szk
