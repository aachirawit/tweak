#pragma once

// Which product this build is.
//
// Two products ship from this tree. They are the same application - the same
// tweaks, the same backend, the same licence - wearing a different name, icon,
// palette and set of screens. PRODUCT_BRAND is set by the build from the Brand
// property (see SZK.vcxproj and scripts/build.ps1); 0 is the default so a plain
// build, or an editor that compiles a file on its own, still gets a real value.
//
// Everything a brand changes is gathered here and in the small number of places
// that read brand_id, so adding a third is a table entry and a folder of assets
// rather than a search through the tree.
#ifndef PRODUCT_BRAND
#define PRODUCT_BRAND 0
#endif

#define PRODUCT_BRAND_NUMBANINE 0
#define PRODUCT_BRAND_LESS 1

namespace szk::product_info
{
enum class brand
{
    numbanine = PRODUCT_BRAND_NUMBANINE,
    less = PRODUCT_BRAND_LESS,
};

inline constexpr brand brand_id = static_cast<brand>(PRODUCT_BRAND);

#if PRODUCT_BRAND == PRODUCT_BRAND_LESS
inline constexpr char name[] = "Less";
inline constexpr wchar_t name_wide[] = L"Less";
inline constexpr wchar_t window_title[] = L"Less";

// The folder under assets/brands this build takes its logo and background
// from. asset_io falls back to the shared assets/<name> when a brand has no
// folder of its own, so only what actually differs has to exist.
inline constexpr wchar_t asset_brand[] = L"less";
#else
inline constexpr char name[] = "numbanine";
inline constexpr wchar_t name_wide[] = L"numbanine";
inline constexpr wchar_t window_title[] = L"numbanine";
inline constexpr wchar_t asset_brand[] = L"numbanine";
#endif

inline constexpr char version[] = "0.1.0";

// Deliberately not per-brand and deliberately still SZK. The window class is
// only ever compared against itself, and the environment prefix names the
// SZK_* variables a user may already have set; changing either would strand
// existing settings to rename something nobody reads.
inline constexpr wchar_t window_class[] = L"SZKDesktopWindow";

inline constexpr char environment_prefix[] = "SZK_";
inline constexpr wchar_t environment_prefix_wide[] = L"SZK_";
} // namespace szk::product_info
