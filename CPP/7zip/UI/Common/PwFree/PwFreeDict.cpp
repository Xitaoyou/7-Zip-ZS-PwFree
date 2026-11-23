// PwFreeDict.cpp

#include "PwFreeDict.h"

#include "PwFreeCrypto.h"
#include "PwFreeHttp.h"
#include "PwFreeZip.h"

#include "../../../../Common/UTFConvert.h"
#include "../../../../Windows/Synchronization.h"

namespace NPwFree {

struct CDictData
{
  volatile long RefCount;
  UString Url;
  CByteBuffer Text;

  CDictData(): RefCount(1) {}
  void AddRef() { ::InterlockedIncrement(&RefCount); }
  void Release()
  {
    if (::InterlockedDecrement(&RefCount) == 0)
      delete this;
  }
};

namespace {

const UInt64 kDictionarySizeLimit = (UInt64)32 << 20;
const Byte kDesKey[8] = { 'D', 'E', 'S', 'K', 'e', 'y', 'E', 'n' };

UInt64 NowMs()
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

/*
  The three quirks that made this worth writing down:
    - the ZIP payload of some dictionaries has a broken CRC, so the member is
      inflated directly and the CRC is ignored;
    - the line separator is CRLF in most files, plain CR in one of them and LF
      in the others; splitting on LF alone turns the CR-only file into one
      enormous line;
    - the DES payload is padded the PKCS#5 way, but a few blocks carry a pad
      value larger than 8, so the strip has to be defensive.
*/
bool DecodeDictionary(const Byte *blob, size_t size, CByteBuffer &text)
{
  text.Free();
  CByteBuffer inner;

  if (size >= 4 && blob[0] == 'P' && blob[1] == 'K')
  {
    if (!NPwZip::Unzip_FirstEntry(blob, size, inner))
      return false;
  }
  else
    inner.CopyFrom(blob, size);

  if (inner.Size() == 0)
    return false;

  NPwCrypto::CDesEcbDecryptor des;
  if (!des.Init(kDesKey))
    return false;

  const char *p = (const char *)(const Byte *)inner;
  const size_t total = inner.Size();

  // Pre-allocate with geometric growth to keep decoding O(N) instead of O(N^2).
  size_t cap = (total >= 1024) ? total : 1024;
  size_t used = 0;
  text.Alloc(cap);

  for (size_t i = 0; i < total; )
  {
    size_t lineEnd = i;
    while (lineEnd < total && p[lineEnd] != '\r' && p[lineEnd] != '\n')
      lineEnd++;

    const size_t lineSize = lineEnd - i;
    if (lineSize >= 12)
    {
      CByteBuffer block;
      if (NPwCrypto::Base64_Decode(p + i, lineSize, block)
          && block.Size() >= 8 && (block.Size() & 7) == 0
          && des.Decrypt(block, block.Size()))
      {
        size_t plainSize = block.Size();
        const Byte pad = (plainSize != 0) ? ((const Byte *)block)[plainSize - 1] : 0;
        if (pad >= 1 && pad <= 8 && plainSize >= pad)
        {
          bool padOk = true;
          for (size_t k = 0; k < pad; k++)
            if (((const Byte *)block)[plainSize - 1 - k] != pad)
            {
              padOk = false;
              break;
            }
          if (padOk)
            plainSize -= pad;
        }
        while (plainSize != 0)
        {
          const Byte last = ((const Byte *)block)[plainSize - 1];
          if (last == ' ' || last == '\t')
            plainSize--;
          else
            break;
        }
        if (plainSize != 0)
        {
          const size_t need = used + plainSize + 1;
          if (need > cap)
          {
            size_t newCap = cap;
            while (newCap < need)
              newCap *= 2;
            text.ChangeSize_KeepData(newCap, used);
            cap = newCap;
          }
          Byte *dst = text;
          memcpy(dst + used, (const Byte *)block, plainSize);
          dst[used + plainSize] = '\n';
          used = need;
        }
      }
    }

    i = lineEnd;
    while (i < total && (p[i] == '\r' || p[i] == '\n'))
      i++;
  }

  if (used == 0)
  {
    text.Free();
    return false;
  }
  text.ChangeSize_KeepData(used, used);
  return true;
}

class CDictStore
{
public:
  CRecordVector<CDictData *> Items;
  ~CDictStore()
  {
    FOR_VECTOR (i, Items)
      Items[i]->Release();
    Items.Clear();
  }
};

CDictStore g_Store;
NWindows::NSynchronization::CCriticalSection g_StoreCS;

class CStoreLock
{
  NWindows::NSynchronization::CCriticalSection &_cs;
  CStoreLock(const CStoreLock &) = delete;
  CStoreLock &operator=(const CStoreLock &) = delete;
public:
  explicit CStoreLock(NWindows::NSynchronization::CCriticalSection &cs): _cs(cs) { _cs.Enter(); }
  ~CStoreLock() { _cs.Leave(); }
};

}

CDictHandle::CDictHandle(const CDictHandle &other): _ptr(other._ptr)
{
  if (_ptr)
    _ptr->AddRef();
}

CDictHandle &CDictHandle::operator=(const CDictHandle &other)
{
  if (this != &other)
  {
    if (other._ptr)
      other._ptr->AddRef();
    Reset();
    _ptr = other._ptr;
  }
  return *this;
}

void CDictHandle::Attach(CDictData *p)
{
  Reset();
  _ptr = p;
}

void CDictHandle::Reset()
{
  if (_ptr)
  {
    _ptr->Release();
    _ptr = NULL;
  }
}

const CByteBuffer *CDictHandle::Buffer() const
{
  return _ptr ? &_ptr->Text : NULL;
}

void Dict_Clear()
{
  CStoreLock lock(g_StoreCS);
  FOR_VECTOR (i, g_Store.Items)
    g_Store.Items[i]->Release();
  g_Store.Items.Clear();
}

HRESULT LoadDict(const UString &url, UInt32 timeoutMs, UInt64 deadline,
    bool allowHttpFallback, volatile long *cancel, CDictHandle &out)
{
  out.Reset();
  if (cancel && *cancel != 0)
    return E_ABORT;

  {
    CStoreLock lock(g_StoreCS);
    FOR_VECTOR (i, g_Store.Items)
    {
      if (g_Store.Items[i]->Url == url)
      {
        g_Store.Items[i]->AddRef();
        out.Attach(g_Store.Items[i]);
        return S_OK;
      }
    }
  }

  if (deadline != 0)
  {
    const UInt64 now = NowMs();
    if (now >= deadline)
      return S_FALSE;
    const UInt64 left = deadline - now;
    if (left < timeoutMs)
      timeoutMs = (UInt32)left;
  }
  if (timeoutMs == 0)
    return S_FALSE;

  CByteBuffer body;
  UString errorText;
  if (NPwHttp::GetBytes(url, timeoutMs, (size_t)kDictionarySizeLimit,
      allowHttpFallback, body, errorText) != S_OK)
    return S_FALSE;
  if (body.Size() == 0 || (UInt64)body.Size() > kDictionarySizeLimit)
    return S_FALSE;
  if (cancel && *cancel != 0)
    return E_ABORT;

  CByteBuffer decoded;
  if (!DecodeDictionary((const Byte *)body, body.Size(), decoded))
    return S_FALSE;

  CStoreLock lock(g_StoreCS);
  FOR_VECTOR (i, g_Store.Items)
  {
    if (g_Store.Items[i]->Url == url)
    {
      g_Store.Items[i]->AddRef();
      out.Attach(g_Store.Items[i]);
      return S_OK;
    }
  }
  CDictData *entry = new CDictData();
  entry->Url = url;
  entry->Text = decoded;
  g_Store.Items.Add(entry);
  entry->AddRef();
  out.Attach(entry);
  return S_OK;
}

void CDictLines::Init(const CByteBuffer &text)
{
  _text = &text;
  _pos = 0;
}

HRESULT CDictLines::Next(UString &line)
{
  line.Empty();
  if (!_text)
    return S_FALSE;

  const Byte *p = *_text;
  const size_t total = _text->Size();

  for (;;)
  {
    if (_pos >= total)
      return S_FALSE;

    size_t end = _pos;
    while (end < total && p[end] != '\n' && p[end] != '\r')
      end++;

    const size_t len = end - _pos;
    const size_t lineStart = _pos;
    _pos = end;
    while (_pos < total && (p[_pos] == '\n' || p[_pos] == '\r'))
      _pos++;

    if (len == 0)
      continue;

    if (Convert_UTF8_Buf_To_Unicode((const char *)p + lineStart, len, line))
      return S_OK;
    // A line that is not valid UTF-8 cannot be a password we would type.
  }
}

}
