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

const unsigned kCancelCheckMask = 0xFFF;  // poll the stop flag every 4096 candidates

UInt32 Now()
{
  return (UInt32)::GetTickCount();
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
  const CByteBuffer *_text;
  const CByteBuffer *_loaded;
  UInt32 _budgetMs;
  UInt32 _deadline;
  bool _noMore;

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
  explicit CDictProvider(const CRequest &req):
      _urlIndex(0), _text(NULL), _loaded(NULL), _noMore(false)
  {
    UNUSED_VAR(req)
    const CSettings st = GetSettings();
    _budgetMs = st.DictBudgetMs;
    _deadline = (st.DictBudgetMs == 0) ? 0 : (Now() + st.DictBudgetMs);
    BuildUrlList();
  }

  const char *Name() const { return "dict"; }
  UInt32 BudgetMs() const { return _budgetMs; }

  HRESULT Next(UString &password)
  {
    for (;;)
    {
      password.Empty();
      if (IsCancelled(NULL))
        return S_FALSE;
      if (_deadline != 0 && Now() >= _deadline)
        return S_FALSE;

      if (_text)
      {
        if (_lines.Next(password) == S_OK && !password.IsEmpty())
          return S_OK;
        _text = NULL;
        _urlIndex++;
        continue;
      }

      if (_urlIndex >= _urls.Size())
        return S_FALSE;

      const UString &url = _urls[_urlIndex];
      if (LoadDict(url, 120000, _deadline, _loaded) != S_OK)
      {
        _urlIndex++;
        continue;
      }
      _text = _loaded;
      _lines.Init(*_text);
    }
  }
};

/* ---------------- cloud ---------------- */

class CCloudProvider Z7_final: public IProvider
{
  CCloudLookup _lookup;
  UInt32 _budgetMs;
  bool _queried;

public:
  explicit CCloudProvider(const CRequest &req):
      _lookup(*req.ArcPath), _queried(false)
  {
    _budgetMs = GetSettings().CloudBudgetMs;
  }

  const char *Name() const { return "cloud"; }
  UInt32 BudgetMs() const { return _budgetMs; }

  HRESULT Next(UString &password)
  {
    if (!_queried)
    {
      _queried = true;
      _lookup.Query(_budgetMs);
    }
    return _lookup.Next(password);
  }
};

}

IProvider *CreateSeedProvider(const CRequest &req) { return new CSeedProvider(req); }
IProvider *CreateCacheProvider(const CRequest &req) { return new CCacheProvider(req); }
IProvider *CreateDictProvider(const CRequest &req) { return new CDictProvider(req); }
IProvider *CreateCloudProvider(const CRequest &req) { return new CCloudProvider(req); }

HRESULT RunChain(const CRequest &req, IVerify &verify, UString &password,
    AString &providerName, unsigned &elapsedMs, volatile long *cancel)
{
  password.Empty();
  providerName.Empty();
  elapsedMs = 0;

  const UInt32 start = Now();
  const CSettings st = GetSettings();

  CObjectVector<IProvider *> providers;
  providers.Add(CreateSeedProvider(req));
  providers.Add(CreateCacheProvider(req));
  if (st.UseLocalDict)
    providers.Add(CreateDictProvider(req));
  if (st.UseCloud && req.ArcPath)
    providers.Add(CreateCloudProvider(req));

  HRESULT result = S_FALSE;
  unsigned counter = 0;

  for (unsigned i = 0; i < providers.Size() && result != S_OK; i++)
  {
    IProvider *const provider = providers[i];
    const UInt32 providerStart = Now();
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

  elapsedMs = Now() - start;
  return result;
}

}
