// PwFreeCache.cpp

#include "PwFreeCache.h"

#include "../../../../Common/StringConvert.h"
#include "../../../../Windows/Synchronization.h"

namespace NPwFree {

namespace {

const unsigned kMaxEntries = 64;
const unsigned kTailSize = 96;

struct CEntry
{
  UInt64 Size;
  UInt64 MTime;
  UString PathTail;
  UString Password;

  CEntry(): Size(0), MTime(0) {}
};

class CCache
{
  CEntry _items[kMaxEntries];
  unsigned _count;
  unsigned _next;

public:
  CCache(): _count(0), _next(0) {}

  bool Get(const CRequest &req, UString &password) const
  {
    const UString tail = GetTail(req);
    for (unsigned i = 0; i < _count; i++)
    {
      const CEntry &e = _items[i];
      if (e.Size == req.Size && e.MTime == req.MTime && e.PathTail == tail)
      {
        password = e.Password;
        return true;
      }
    }
    return false;
  }

  void Put(const CRequest &req, const UString &password)
  {
    const UString tail = GetTail(req);
    for (unsigned i = 0; i < _count; i++)
    {
      CEntry &e = _items[i];
      if (e.Size == req.Size && e.MTime == req.MTime && e.PathTail == tail)
      {
        e.Password = password;
        return;
      }
    }
    CEntry &e = _items[_next];
    e.Size = req.Size;
    e.MTime = req.MTime;
    e.PathTail = tail;
    e.Password = password;
    _next = (_next + 1) % kMaxEntries;
    if (_count < kMaxEntries)
      _count++;
  }

  void Clear()
  {
    for (unsigned i = 0; i < _count; i++)
      _items[i].Password.Wipe_and_Empty();
    _count = 0;
    _next = 0;
  }

private:
  // The tail is enough to tell two archives apart without keeping the whole
  // directory structure in memory.
  static UString GetTail(const CRequest &req)
  {
    if (!req.ArcPath)
      return UString();
    const UString path = GetUnicodeString(*(req.ArcPath));
    if (path.Len() <= kTailSize)
      return path;
    UString tail;
    tail.SetFrom(path.Ptr() + (path.Len() - kTailSize), kTailSize);
    return tail;
  }
};

CCache g_Cache;
NWindows::NSynchronization::CCriticalSection g_CacheCS;

class CCacheLock
{
  NWindows::NSynchronization::CCriticalSection &_cs;
  CCacheLock(const CCacheLock &) = delete;
  CCacheLock &operator=(const CCacheLock &) = delete;
public:
  explicit CCacheLock(NWindows::NSynchronization::CCriticalSection &cs): _cs(cs) { _cs.Enter(); }
  ~CCacheLock() { _cs.Leave(); }
};

}

bool Cache_Get(const CRequest &req, UString &password)
{
  CCacheLock lock(g_CacheCS);
  return g_Cache.Get(req, password);
}

void Cache_Put(const CRequest &req, const UString &password)
{
  CCacheLock lock(g_CacheCS);
  g_Cache.Put(req, password);
}

void Cache_Clear()
{
  CCacheLock lock(g_CacheCS);
  g_Cache.Clear();
}

}
