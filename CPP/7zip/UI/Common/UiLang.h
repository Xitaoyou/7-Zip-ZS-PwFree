// UiLang.h - Built-in auto-switching zh-CN / en-US UI localization (ASCII-safe)

#ifndef ZIP7_INC_UI_LANG_H
#define ZIP7_INC_UI_LANG_H

#include "../../../Common/MyTypes.h"

namespace NUiLang {

// Returns true when Simplified Chinese UI should be active (follows Windows UI language
// unless user explicitly selected English "-" in settings).
bool IsZhCn();

// Inline bilingual helper.
inline const wchar_t *Tr(const wchar_t *en, const wchar_t *zh)
{
  return IsZhCn() ? zh : en;
}

// Lookup built-in Simplified Chinese translation for standard 7-Zip resource IDs.
// Returns NULL if not in table.
const wchar_t *LookupBuiltinZhCn(UInt32 id);

} // namespace NUiLang

#endif
