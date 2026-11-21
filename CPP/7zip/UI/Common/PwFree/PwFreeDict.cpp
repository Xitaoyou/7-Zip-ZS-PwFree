// PwFreeDict.cpp

#include "PwFreeDict.h"

#include "PwFreeCrypto.h"
#include "PwFreeHttp.h"
#include "PwFreeZip.h"

#include "../../../../Common/UTFConvert.h"
#include "../../../../Windows/Synchronization.h"

namespace NPwFree {

namespace {

const UInt64 kDictionarySizeLimit = (UInt64)32 << 20;
const Byte kDesKey[8] = { 'D', 'E', 'S', 'K', 'e', 'y', 'E', 'n' };

struct CDictEntry
{
  UString Url;
  CByteBuffer Text;
};

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

  const char *p = (const char *)(const Byte *)inner;
  const size_t total = inner.Size();

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
          && NPwCrypto::Des_Ecb_Decrypt(kDesKey, block, block.Size()))
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
          const size_t oldSize = text.Size();
          text.ChangeSize_KeepData(oldSize + plainSize + 1, oldSize);
          Byte *dst = text;
          memcpy(dst + oldSize, (const Byte *)block, plainSize);
          dst[oldSize + plainSize] = '\n';
        }
      }
    }

    i = lineEnd;
    while (i < total && (p[i] == '\r' || p[i] == '\n'))
      i++;
  }

  return text.Size() != 0;
}

class CDictStore
{
public:
  CObjectVector<CDictEntry> Items;
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

void Dict_Clear()
{
  CStoreLock lock(g_StoreCS);
  g_Store.Items.Clear();
}

HRESULT LoadDict(const UString &url, UInt32 timeoutMs, UInt32 deadline,
    const CByteBuffer *&text)
{
  text = NULL;
  {
    CStoreLock lock(g_StoreCS);
    FOR_VECTOR (i, g_Store.Items)
    {
      if (g_Store.Items[i].Url == url)
      {
        text = &g_Store.Items[i].Text;
        return S_OK;
      }
    }
  }

  if (deadline != 0)
  {
    const UInt32 now = (UInt32)::GetTickCount();
    if (now >= deadline)
      return S_FALSE;
    const UInt32 left = deadline - now;
    if (left < timeoutMs)
      timeoutMs = left;
  }
  if (timeoutMs == 0)
    return S_FALSE;

  AString body;
  UString errorText;
  if (NPwHttp::Get(url, timeoutMs, body, errorText) != S_OK)
    return S_FALSE;
  if (body.Len() == 0 || (UInt64)body.Len() > kDictionarySizeLimit)
    return S_FALSE;

  CByteBuffer decoded;
  if (!DecodeDictionary((const Byte *)(const char *)body, body.Len(), decoded))
    return S_FALSE;

  CStoreLock lock(g_StoreCS);
  FOR_VECTOR (i, g_Store.Items)
  {
    if (g_Store.Items[i].Url == url)
    {
      text = &g_Store.Items[i].Text;
      return S_OK;
    }
  }
  CDictEntry &e = g_Store.Items.AddNew();
  e.Url = url;
  e.Text = decoded;
  text = &e.Text;
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
