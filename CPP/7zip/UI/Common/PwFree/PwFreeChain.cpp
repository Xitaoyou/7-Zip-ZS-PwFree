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

#ifdef _WIN32

/* ---------------- multi-threaded verification pool ---------------- */

class CVerifyPool
{
  static const unsigned kMaxWorkers = 16;

  const IVerify *_verify;
  volatile long *_cancel;
  UInt64 _deadline;

  const CObjectVector<AString> *_batch;
  volatile long _nextIdx;
  volatile long _found;
  long _matchIndex;
  volatile long _stopPool;
  volatile long _batchSeq;
  volatile long _activeWorkers;

  HANDLE _threads[kMaxWorkers];
  HANDLE _wakeEvent;
  HANDLE _doneEvent;
  unsigned _numWorkers;

  void RunWorkerLoop(long &seenSeq)
  {
    for (;;)
    {
      while (_batchSeq == seenSeq && _stopPool == 0)
        ::WaitForSingleObject(_wakeEvent, INFINITE);
      if (_stopPool != 0)
        break;
      seenSeq = _batchSeq;

      const long total = (long)_batch->Size();
      for (;;)
      {
        if (_found != 0 || IsCancelled(_cancel))
          break;
        if (_deadline != 0 && Now() >= _deadline)
          break;
        const long idx = ::InterlockedIncrement(&_nextIdx) - 1;
        if (idx >= total)
          break;
        const AString &u8 = (*_batch)[(unsigned)idx];
        if (_verify->Check((const Byte *)(const char *)u8, u8.Len()))
        {
          if (::InterlockedCompareExchange(&_found, 1, 0) == 0)
            _matchIndex = idx;
          break;
        }
      }

      if (::InterlockedDecrement(&_activeWorkers) == 0)
        ::SetEvent(_doneEvent);
    }
  }

  static DWORD WINAPI WorkerThreadProc(LPVOID param)
  {
    CVerifyPool *self = (CVerifyPool *)param;
    long seenSeq = 0;
    self->RunWorkerLoop(seenSeq);
    return 0;
  }

  CVerifyPool(const CVerifyPool &) = delete;
  CVerifyPool &operator=(const CVerifyPool &) = delete;

public:
  CVerifyPool():
      _verify(NULL), _cancel(NULL), _deadline(0), _batch(NULL),
      _nextIdx(0), _found(0), _matchIndex(-1), _stopPool(0),
      _batchSeq(0), _activeWorkers(0),
      _wakeEvent(NULL), _doneEvent(NULL), _numWorkers(0)
  {
    memset(_threads, 0, sizeof(_threads));
  }

  ~CVerifyPool()
  {
    Stop();
  }

  unsigned Start(const IVerify &verify, volatile long *cancel, UInt64 deadline)
  {
    Stop();
    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    unsigned cores = (unsigned)si.dwNumberOfProcessors;
    if (cores < 2)
      return 1;
    if (cores > kMaxWorkers)
      cores = kMaxWorkers;

    _verify = &verify;
    _cancel = cancel;
    _deadline = deadline;
    _stopPool = 0;
    _batchSeq = 0;
    _found = 0;
    _matchIndex = -1;

    _wakeEvent = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    _doneEvent = ::CreateEventW(NULL, TRUE, TRUE, NULL);
    if (!_wakeEvent || !_doneEvent)
    {
      Stop();
      return 1;
    }

    // Spawn (cores - 1) helper threads; the caller thread also participates.
    const unsigned helpers = cores - 1;
    for (unsigned i = 0; i < helpers; i++)
    {
      HANDLE h = ::CreateThread(NULL, 0, WorkerThreadProc, this, 0, NULL);
      if (!h)
        break;
      _threads[_numWorkers++] = h;
    }
    return _numWorkers + 1;
  }

  void Stop()
  {
    if (_numWorkers != 0)
    {
      ::InterlockedExchange(&_stopPool, 1);
      if (_wakeEvent)
        ::SetEvent(_wakeEvent);
      ::WaitForMultipleObjects(_numWorkers, _threads, TRUE, INFINITE);
      for (unsigned i = 0; i < _numWorkers; i++)
      {
        ::CloseHandle(_threads[i]);
        _threads[i] = NULL;
      }
      _numWorkers = 0;
    }
    if (_wakeEvent) { ::CloseHandle(_wakeEvent); _wakeEvent = NULL; }
    if (_doneEvent) { ::CloseHandle(_doneEvent); _doneEvent = NULL; }
  }

  // Returns index of matching candidate in 'batch', or -1 if none matched.
  int RunBatch(const CObjectVector<AString> &batch)
  {
    if (batch.IsEmpty())
      return -1;
    _batch = &batch;
    _nextIdx = 0;
    _found = 0;
    _matchIndex = -1;
    _activeWorkers = (long)_numWorkers;
    ::ResetEvent(_doneEvent);
    ::InterlockedIncrement(&_batchSeq);
    ::SetEvent(_wakeEvent);

    // Caller thread works alongside helper threads.
    const long total = (long)batch.Size();
    for (;;)
    {
      if (_found != 0 || IsCancelled(_cancel))
        break;
      if (_deadline != 0 && Now() >= _deadline)
        break;
      const long idx = ::InterlockedIncrement(&_nextIdx) - 1;
      if (idx >= total)
        break;
      const AString &u8 = batch[(unsigned)idx];
      if (_verify->Check((const Byte *)(const char *)u8, u8.Len()))
      {
        if (::InterlockedCompareExchange(&_found, 1, 0) == 0)
          _matchIndex = idx;
        break;
      }
    }

    if (_numWorkers != 0)
      ::WaitForSingleObject(_doneEvent, INFINITE);
    ::ResetEvent(_wakeEvent);
    return (_found != 0) ? (int)_matchIndex : -1;
  }
};

#endif

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
    const UInt64 providerDeadline = (budget != 0) ? (providerStart + budget) : 0;

    const char *const provName = provider->Name();
    const bool usePool =
        #ifdef _WIN32
        (verify.CostClass() >= 1) && (strcmp(provName, "dict") == 0 || strcmp(provName, "mask") == 0);
        #else
        false;
        #endif

    #ifdef _WIN32
    if (usePool)
    {
      CVerifyPool pool;
      const unsigned totalThreads = pool.Start(verify, cancel, providerDeadline);
      if (totalThreads > 1)
      {
        const unsigned batchLimit = totalThreads * 8;
        CObjectVector<UString> batchRaw;
        CObjectVector<AString> batchUtf8;

        for (;;)
        {
          if (IsCancelled(cancel))
          {
            result = E_ABORT;
            break;
          }
          if (providerDeadline != 0 && Now() >= providerDeadline)
            break;

          batchRaw.Clear();
          batchUtf8.Clear();
          bool providerDone = false;
          for (unsigned b = 0; b < batchLimit; b++)
          {
            UString candidate;
            const HRESULT hr = provider->Next(candidate);
            if (hr == E_ABORT)
            {
              result = E_ABORT;
              providerDone = true;
              break;
            }
            if (hr != S_OK)
            {
              providerDone = true;
              break;
            }
            if (candidate.IsEmpty())
              continue;
            AString utf8;
            ConvertUnicodeToUTF8(candidate, utf8);
            batchRaw.Add(candidate);
            batchUtf8.Add(utf8);
          }

          if (!batchUtf8.IsEmpty() && result != E_ABORT)
          {
            const int hit = pool.RunBatch(batchUtf8);
            if (hit >= 0)
            {
              password = batchRaw[(unsigned)hit];
              providerName = provName;
              result = S_OK;
              break;
            }
          }

          if (providerDone || result == E_ABORT)
            break;
        }
        pool.Stop();
        provider->Finish();
        continue;
      }
    }
    #endif

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
        providerName = provName;
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
