// PwFreeSettings.h

#ifndef ZIP7_INC_PW_FREE_SETTINGS_H
#define ZIP7_INC_PW_FREE_SETTINGS_H

#include "../../../../Common/MyTypes.h"

namespace NPwFree {

/*
  Persisted configuration, stored next to the other 7-Zip ZS options:
    HKCU\\Software\\7-Zip-Zstandard\\PwFree
*/
struct CSettings
{
  // Master switch. On by default: a user who never opens the options page
  // still gets the behaviour the build is shipped for.
  bool Enabled;
  // Allow the remote lookup provider (the only provider that sends the
  // archive hash off the machine).
  bool UseCloud;
  // Allow the local dictionary provider.
  bool UseLocalDict;
  // Tell the user where the password came from after a successful lookup.
  bool ShowSource;
  // Allow retrying HTTPS cloud requests over plain HTTP on TLS failure.
  // Off by default to avoid silent downgrade attacks.
  bool AllowHttpFallback;

  /*
    Per-provider budgets. A provider that runs out is skipped, not failed.
    MaskBudgetMs is zero by default, which keeps the digits walk off entirely.
  */
  UInt32 CloudBudgetMs;
  UInt32 DictBudgetMs;
  UInt32 MaskBudgetMs;

  CSettings();
  void Load();
  void Save() const;
};

CSettings GetSettings();
bool IsEnabled();
void SetEnabled(bool enable);

}

#endif
