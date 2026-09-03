#pragma once

#include "core/product_info.h"

namespace solace::brand
{
struct credit_profile
{
    const char* user_name;
    const char* user_github;
    const char* user_initials;
    const char* author;
    const char* repo;
};

namespace profiles
{
constexpr credit_profile szk{
    "SZK", "SZK", "S", "SZK", "SZK",
};
} // namespace profiles

// Select the visible creator profile.
constexpr const credit_profile& active = profiles::szk;

constexpr const char* product = solace::product_info::name;

constexpr const char* user_name = active.user_name;
constexpr const char* user_github = active.user_github;
constexpr const char* user_initials = active.user_initials;

constexpr const char* author = active.author;
constexpr const char* repo = active.repo;

constexpr const char* game = "SZK";
} // namespace solace::brand
