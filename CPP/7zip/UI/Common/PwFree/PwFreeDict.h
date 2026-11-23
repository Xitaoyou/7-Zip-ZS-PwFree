// PwFreeDict.h

#ifndef ZIP7_INC_PW_FREE_DICT_H
#define ZIP7_INC_PW_FREE_DICT_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyBuffer.h"
#include "../../../../Common/MyString.h"

/*
  Vendor password dictionaries.

  The format was taken apart once and is fully known:

    data<N>.dt
      outer layer : ZIP with a single member (its CRC is broken)
      member      : base64 text, one block per line
      block       : DES-ECB, 8 byte key "DESKeyEn", PKCS#5 padding
      plaintext   : one UTF-8 password per line

  About 148.000 unique passwords ship that way.

  The decoded text stays in memory for the lifetime of the process: a
  dictionary is a few megabytes of text, writing it next to the executable
  would need write access to the install directory and would leave a copy of
  the password list on disk.
*/

namespace NPwFree {

/*
  Ref-counted handle to an immutable decoded dictionary buffer so Dict_Clear()
  cannot invalidate a buffer while a provider is still iterating it.
*/
struct CDictData;

class CDictHandle
{
  CDictData *_ptr;
public:
  CDictHandle(): _ptr(NULL) {}
  ~CDictHandle() { Reset(); }
  CDictHandle(const CDictHandle &other);
  CDictHandle &operator=(const CDictHandle &other);
  void Attach(CDictData *p);
  void Reset();
  bool IsValid() const { return _ptr != NULL; }
  const CByteBuffer *Buffer() const;
};

/*
  Returns the decoded dictionary text for 'url', fetching and decoding it on
  the first request. 'deadline' is an absolute GetTickCount64() value, 0 for
  "no deadline". Returns S_FALSE when the dictionary is unavailable.
*/
HRESULT LoadDict(const UString &url, UInt32 timeoutMs, UInt64 deadline,
    bool allowHttpFallback, volatile long *cancel, CDictHandle &out);

// Forgets every decoded dictionary (used by "reload" and by the tests).
void Dict_Clear();

/*
  Splits a decoded dictionary buffer into lines.
*/
class CDictLines
{
  const CByteBuffer *_text;
  size_t _pos;
  AString _line;

public:
  CDictLines(): _text(NULL), _pos(0) {}
  void Init(const CByteBuffer &text);
  // S_FALSE at the end.
  HRESULT Next(UString &line);
};

}

#endif
