#pragma once

#include "core/product_info.h"

namespace szk::brand
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
    "numbanine", "numbanine", "N", "numbanine", "numbanine",
};
} // namespace profiles

// Select the visible creator profile.
constexpr const credit_profile& active = profiles::szk;

constexpr const char* product = szk::product_info::name;

constexpr const char* user_name = active.user_name;
constexpr const char* user_github = active.user_github;
constexpr const char* user_initials = active.user_initials;

constexpr const char* author = active.author;
constexpr const char* repo = active.repo;

constexpr const char* game = "numbanine";
} // namespace szk::brand
