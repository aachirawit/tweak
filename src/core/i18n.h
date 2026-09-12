#pragma once

namespace szk::i18n
{
enum class lang
{
    en,
    th,
};

lang language();
void set_language(lang value);
void toggle_language();

// Translate a UI string.
//
// The English text is the key. That keeps the call sites readable - tr("Power
// plan") rather than tr(STR_POWER_PLAN) - and means a string with no entry in
// the table falls back to the English the code already said, so a half-finished
// translation shows a mix of languages instead of blank labels or key names.
//
// The returned pointer is to a static string and is valid for the process
// lifetime, so it is safe to hand straight to the draw calls.
const char* tr(const char* english);
} // namespace szk::i18n
