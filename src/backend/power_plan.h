#pragma once

namespace solace::backend
{
struct power_plan_info
{
    char name[128] = {};
    bool available = false;
};

// Reads the name of the currently active Windows power plan.
power_plan_info power_plan_active();

// Renames the currently active power plan (does not change any of its
// settings, just its display name/description in Windows' own power plan
// list — verifiable with `powercfg /list`).
bool power_plan_rename_active(const wchar_t* name, const wchar_t* description);
} // namespace solace::backend
