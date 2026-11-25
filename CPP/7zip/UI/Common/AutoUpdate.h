// AutoUpdate.h - Lightweight async GitHub Release auto-updater (ASCII-safe)

#ifndef ZIP7_INC_AUTO_UPDATE_H
#define ZIP7_INC_AUTO_UPDATE_H

#include "../../../Common/MyWindows.h"
#include "../../../Common/MyString.h"

namespace NAutoUpdate {

static const UINT kMsgUpdateCheckResult = WM_APP + 42;

bool IsAutoUpdateEnabled();
void SetAutoUpdateEnabled(bool enabled);

// Starts an asynchronous background check against GitHub Releases.
// Automatic checks are throttled to once per 24 hours and only notify when a newer version exists.
void StartUpdateCheckAsync(HWND hwndNotify, bool manualCheck);

// Handles kMsgUpdateCheckResult on the UI thread (frees lParam).
void HandleUpdateCheckResult(HWND hwndParent, LPARAM lParam);

} // namespace NAutoUpdate

#endif
