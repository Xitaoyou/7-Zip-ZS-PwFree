// PwFreeSettings.cpp

#include "PwFreeSettings.h"

#include "../../../../Common/Common.h"

#include "../../../../Windows/Registry.h"

using namespace NWindows;

static LPCTSTR const kKeyPath = TEXT("Software\\7-Zip-Zstandard\\PwFree");

static LPCTSTR const kVal_Enabled     = TEXT("Enabled");
static LPCTSTR const kVal_UseCloud    = TEXT("UseCloud");
static LPCTSTR const kVal_UseLocalDic = TEXT("UseLocalDict");
static LPCTSTR const kVal_ShowSource  = TEXT("ShowSource");
static LPCTSTR const kVal_CloudBudget = TEXT("CloudBudgetMs");
static LPCTSTR const kVal_DictBudget  = TEXT("DictBudgetMs");
static LPCTSTR const kVal_MaskBudget  = TEXT("MaskBudgetMs");

namespace NPwFree {

CSettings::CSettings():
    Enabled(true),
    UseCloud(true),
    UseLocalDict(true),
    ShowSource(true),
    CloudBudgetMs(3000),
    DictBudgetMs(120000),
    MaskBudgetMs(60000)
{
}

void CSettings::Load()
{
  NRegistry::CKey key;
  if (key.Open(HKEY_CURRENT_USER, kKeyPath, KEY_READ) != ERROR_SUCCESS)
    return;

  key.GetValue_bool_IfOk(kVal_Enabled, Enabled);
  key.GetValue_bool_IfOk(kVal_UseCloud, UseCloud);
  key.GetValue_bool_IfOk(kVal_UseLocalDic, UseLocalDict);
  key.GetValue_bool_IfOk(kVal_ShowSource, ShowSource);
  key.GetValue_UInt32_IfOk(kVal_CloudBudget, CloudBudgetMs);
  key.GetValue_UInt32_IfOk(kVal_DictBudget, DictBudgetMs);
  key.GetValue_UInt32_IfOk(kVal_MaskBudget, MaskBudgetMs);
}

void CSettings::Save() const
{
  NRegistry::CKey key;
  if (key.Create(HKEY_CURRENT_USER, kKeyPath) != ERROR_SUCCESS)
    return;

  key.SetValue(kVal_Enabled, Enabled);
  key.SetValue(kVal_UseCloud, UseCloud);
  key.SetValue(kVal_UseLocalDic, UseLocalDict);
  key.SetValue(kVal_ShowSource, ShowSource);
  key.SetValue(kVal_CloudBudget, CloudBudgetMs);
  key.SetValue(kVal_DictBudget, DictBudgetMs);
  key.SetValue(kVal_MaskBudget, MaskBudgetMs);
}

CSettings GetSettings()
{
  CSettings s;
  s.Load();
  return s;
}

bool IsEnabled()
{
  CSettings s;
  s.Load();
  return s.Enabled;
}

void SetEnabled(bool enable)
{
  CSettings s;
  s.Load();
  s.Enabled = enable;
  s.Save();
}

}
