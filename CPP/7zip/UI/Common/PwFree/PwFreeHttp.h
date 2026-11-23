// PwFreeHttp.h

#ifndef ZIP7_INC_PW_FREE_HTTP_H
#define ZIP7_INC_PW_FREE_HTTP_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyBuffer.h"
#include "../../../../Common/MyString.h"

/*
  One-shot HTTP GET used by the cloud and dictionary providers.

  WinHTTP is used directly: the module is linked into three different targets
  (7zFM, 7zG, 7z) and must not pull in a socket stack of its own. The call is
  fully bounded - connect, send, receive, total size and total time are all
  limited, so a dead server can never hold the password callback longer than
  the budget.
*/

namespace NPwFree {
namespace NPwHttp {

/*
  Returns S_OK and fills 'body' on HTTP 200 within 'maxBytes'.
  Returns S_FALSE on transport failure, non-200 status, size overflow or timeout.
  'allowHttpFallback' controls whether an HTTPS failure retries over plain HTTP.
*/
HRESULT GetBytes(const UString &url, UInt32 timeoutMs, size_t maxBytes,
    bool allowHttpFallback, CByteBuffer &body, UString &errorText);

// Text convenience wrapper (1 MB cap by default).
HRESULT Get(const UString &url, UInt32 timeoutMs, AString &body,
    UString &errorText, bool allowHttpFallback = false);

}}

#endif
