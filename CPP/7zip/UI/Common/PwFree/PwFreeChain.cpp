// PwFreeChain.cpp

#include "PwFreeChain.h"

#include "PwFreeCache.h"
#include "PwFreeCloud.h"
#include "PwFreeDict.h"
#include "PwFreeSettings.h"

#include "../../../../Common/StringConvert.h"
#include "../../../../Common/UTFConvert.h"

namespace NPwFree {

namespace {

const unsigned kCancelCheckMask = 0xFF;  // poll the stop flag every 256 candidates

UInt64 Now()
{
#ifdef _WIN32
  typedef ULONGLONG (WINAPI *PFN_GetTickCount64)(void);
  static PFN_GetTickCount64 pfn = (PFN_GetTickCount64)(void *)::GetProcAddress(
      ::GetModuleHandleW(L"kernel32.dll"), "GetTickCount64");
  if (pfn)
    return (UInt64)pfn();
  return (UInt64)::GetTickCount();
#else
  return 0;
#endif
}

bool IsCancelled(volatile long *cancel)
{
  return cancel && *cancel != 0;
}

/* ---------------- seed ---------------- */

class CSeedProvider Z7_final: public IProvider
{
  UString _password;
  bool _done;

public:
  explicit CSeedProvider(const CRequest &req): _done(true)
  {
    if (req.Seed && !req.Seed->IsEmpty())
    {
      _password = *req.Seed;
      _done = false;
    }
  }

  const char *Name() const { return "seed"; }
  UInt32 BudgetMs() const { return 0; }

  HRESULT Next(UString &password)
  {
    if (_done)
      return S_FALSE;
    _done = true;
    password = _password;
    return S_OK;
  }

  void Finish() { _password.Wipe_and_Empty(); }
};

/* ---------------- cache ---------------- */

class CCacheProvider Z7_final: public IProvider
{
  UString _password;
  bool _done;

public:
  explicit CCacheProvider(const CRequest &req): _done(true)
  {
    if (Cache_Get(req, _password))
      _done = false;
  }

  const char *Name() const { return "cache"; }
  UInt32 BudgetMs() const { return 0; }

  HRESULT Next(UString &password)
  {
    if (_done)
      return S_FALSE;
    _done = true;
    password = _password;
    return S_OK;
  }
};

/* ---------------- dictionary ---------------- */

/*
  The dictionary server publishes four files through its listing endpoint and
  carries a few more that the listing does not mention; the extra ones hold
  the newest passwords, so they are probed once and then remembered for the
  life of the process.
*/
class CDictProvider Z7_final: public IProvider
{
  CObjectVector<UString> _urls;
  unsigned _urlIndex;
  CDictLines _lines;
  CDictHandle _loaded;
  bool _hasLoaded;
  UInt32 _budgetMs;
  UInt64 _deadline;
  bool _allowHttpFallback;
  volatile long *_cancel;

  unsigned _dynamicNext;
  unsigned _dynamicMisses;

  void BuildUrlList()
  {
    _urls.Add(GetUnicodeString("http://file.cscjapp.cn/dic/server/data6.dt"));
    _urls.Add(GetUnicodeString("http://file.cscjapp.cn/dic/server/data4.dt"));
    _urls.Add(GetUnicodeString("http://file.cscjapp.cn/dic/server/data7.dt"));
    _urls.Add(GetUnicodeString("http://file.cscjapp.cn/dic/server/data5.dt"));
    _urls.Add(GetUnicodeString("http://file.cscjapp.cn/dic/server/data2.dt"));
    _urls.Add(GetUnicodeString("http://file.cscjapp.cn/dic/server/data3.dt"));
  }

public:
  CDictProvider(const CRequest &req, const CSettings &st, volatile long *cancel):
      _urlIndex(0), _hasLoaded(false), _budgetMs(st.DictBudgetMs),
      _deadline(0), _allowHttpFallback(st.AllowHttpFallback), _cancel(cancel),
      _dynamicNext(8), _dynamicMisses(0)
  {
    UNUSED_VAR(req)
    BuildUrlList();
  }

  const char *Name() const { return "dict"; }
  UInt32 BudgetMs() const { return _budgetMs; }

  HRESULT Next(UString &password)
  {
    if (_deadline == 0 && _budgetMs != 0)
      _deadline = Now() + _budgetMs;

    for (;;)
    {
      password.Empty();
      if (IsCancelled(_cancel))
        return E_ABORT;
      if (_deadline != 0 && Now() >= _deadline)
        return S_FALSE;

      if (_hasLoaded)
      {
        if (_lines.Next(password) == S_OK && !password.IsEmpty())
          return S_OK;
        _hasLoaded = false;
        _loaded.Reset();
        _urlIndex++;
        continue;
      }

      UString url;
      bool isDynamic = false;
      if (_urlIndex < _urls.Size())
      {
        url = _urls[_urlIndex];
      }
      else
      {
        // Dynamically discover new server shards (data8.dt, data9.dt, ...)
        // stopping after 2 consecutive missing shards.
        if (_dynamicMisses >= 2 || _dynamicNext > 32)
          return S_FALSE;
        isDynamic = true;
        url = L"http://file.cscjapp.cn/dic/server/data";
        url.Add_UInt32(_dynamicNext);
        url += L".dt";
        _dynamicNext++;
      }

      const UInt32 reqTimeout = isDynamic ? 3000 : (_budgetMs ? _budgetMs : 120000);
      const HRESULT hr = LoadDict(url, reqTimeout,
          _deadline, _allowHttpFallback, _cancel, _loaded);
      if (hr == E_ABORT)
        return E_ABORT;
      if (hr != S_OK || !_loaded.IsValid())
      {
        if (isDynamic)
          _dynamicMisses++;
        else
          _urlIndex++;
        continue;
      }
      if (isDynamic)
        _dynamicMisses = 0;
      _hasLoaded = true;
      _lines.Init(*_loaded.Buffer());
    }
  }

  void Finish()
  {
    _hasLoaded = false;
    _loaded.Reset();
  }
};

/* ---------------- mask ---------------- */

/*
  Digits only, four to eight characters. Longer candidates are dominated by the
  dictionary: a six digit password appears in a leaked list far more often than
  a random eight digit one, so short lengths are exhausted first and the budget
  decides how far the walk gets. The provider holds no state beyond an odometer,
  so it stops exactly where the budget cuts it off.
*/
class CMaskProvider Z7_final: public IProvider
{
  static const unsigned kMinLen = 4;
  static const unsigned kMaxLen = 8;
  static const unsigned kBase = 10;

  Byte _digits[kMaxLen];
  unsigned _len;
  UInt32 _budgetMs;
  bool _done;

  void Reset(unsigned len)
  {
    _len = len;
    memset(_digits, 0, sizeof(_digits));
  }

  // Advances the odometer, growing the length once the last one overflows.
  void Advance()
  {
    for (int i = (int)_len - 1; i >= 0; i--)
    {
      if (++_digits[i] < kBase)
        return;
      _digits[i] = 0;
    }
    if (_len >= kMaxLen)
      _done = true;
    else
      Reset(_len + 1);
  }

public:
  CMaskProvider(const CRequest &, const CSettings &st):
      _len(0), _budgetMs(st.MaskBudgetMs), _done(false)
  {
    Reset(kMinLen);
  }

  const char *Name() const { return "mask"; }

  UInt32 BudgetMs() const { return _budgetMs; }

  HRESULT Next(UString &password)
  {
    if (_done)
      return S_FALSE;
    password.Empty();
    for (unsigned i = 0; i < _len; i++)
      password += (wchar_t)(L'0' + _digits[i]);
    Advance();
    return S_OK;
  }
};

/* ---------------- cloud ---------------- */

class CCloudProvider Z7_final: public IProvider
{
  CCloudLookup _lookup;
  UInt32 _budgetMs;
  bool _allowHttpFallback;
  volatile long *_cancel;
  bool _queried;

public:
  CCloudProvider(const CRequest &req, const CSettings &st, volatile long *cancel):
      _lookup(*req.ArcPath),
      _budgetMs(st.CloudBudgetMs),
      _allowHttpFallback(st.AllowHttpFallback),
      _cancel(cancel),
      _queried(false)
  {
  }

  const char *Name() const { return "cloud"; }
  UInt32 BudgetMs() const { return _budgetMs; }

  HRESULT Next(UString &password)
  {
    if (IsCancelled(_cancel))
      return E_ABORT;
    if (!_queried)
    {
      _queried = true;
      _lookup.Query(_budgetMs, _allowHttpFallback, _cancel);
      if (IsCancelled(_cancel))
        return E_ABORT;
    }
    return _lookup.Next(password);
  }
};

}

IProvider *CreateSeedProvider(const CRequest &req) { return new CSeedProvider(req); }
IProvider *CreateCacheProvider(const CRequest &req) { return new CCacheProvider(req); }
IProvider *CreateDictProvider(const CRequest &req, const CSettings &st, volatile long *cancel)
{
  return new CDictProvider(req, st, cancel);
}
IProvider *CreateCloudProvider(const CRequest &req, const CSettings &st, volatile long *cancel)
{
  return new CCloudProvider(req, st, cancel);
}
IProvider *CreateMaskProvider(const CRequest &req, const CSettings &st)
{
  return new CMaskProvider(req, st);
}

HRESULT RunChain(const CRequest &req, const CSettings &st, IVerify &verify,
    UString &password, AString &providerName, unsigned &elapsedMs, volatile long *cancel)
{
  password.Empty();
  providerName.Empty();
  elapsedMs = 0;

  const UInt64 start = Now();

  CObjectVector<IProvider *> providers;
  providers.Add(CreateSeedProvider(req));
  providers.Add(CreateCacheProvider(req));
  // Cloud lookup takes milliseconds-to-seconds and returns exact archive hits,
  // so it runs BEFORE the 120s dictionary scan.
  if (st.UseCloud && req.ArcPath)
    providers.Add(CreateCloudProvider(req, st, cancel));
  if (st.UseLocalDict)
    providers.Add(CreateDictProvider(req, st, cancel));
  if (st.MaskBudgetMs != 0)
    providers.Add(CreateMaskProvider(req, st));

  HRESULT result = S_FALSE;
  unsigned counter = 0;

  for (unsigned i = 0; i < providers.Size() && result != S_OK && result != E_ABORT; i++)
  {
    IProvider *const provider = providers[i];
    const UInt64 providerStart = Now();
    const UInt32 budget = provider->BudgetMs();

    for (;;)
    {
      if ((counter++ & kCancelCheckMask) == 0 && IsCancelled(cancel))
      {
        result = E_ABORT;
        break;
      }
      if (budget != 0 && Now() - providerStart > budget)
        break;

      UString candidate;
      const HRESULT hr = provider->Next(candidate);
      if (hr == E_ABORT)
      {
        result = E_ABORT;
        break;
      }
      if (hr != S_OK)
        break;
      if (candidate.IsEmpty())
        continue;

      AString utf8;
      ConvertUnicodeToUTF8(candidate, utf8);
      if (verify.Check((const Byte *)(const char *)utf8, utf8.Len()))
      {
        password = candidate;
        providerName = provider->Name();
        result = S_OK;
        break;
      }
    }

    provider->Finish();
  }

  for (unsigned i = 0; i < providers.Size(); i++)
    delete providers[i];

  elapsedMs = (unsigned)(Now() - start);
  return result;
}

}
