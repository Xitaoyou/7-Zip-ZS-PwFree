// PwFreeHttp.h

#ifndef ZIP7_INC_PW_FREE_HTTP_H
#define ZIP7_INC_PW_FREE_HTTP_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyString.h"

/*
  One-shot HTTP GET used by the cloud provider.

  WinHTTP is used directly: the module is linked into three different targets
  (7zFM, 7zG, 7z) and must not pull in a socket stack of its own. The call is
  fully bounded - connect, send, receive and total time are all limited, so a
  dead server can never hold the password callback longer than the budget.
*/

namespace NPwFree {
namespace NPwHttp {

/*
  Returns S_OK and fills 'body' on HTTP 200.
  Returns S_FALSE on transport failure, non-200 status or timeout.
  'timeoutMs' covers the whole request, not just the socket read.
*/
HRESULT Get(const UString &url, UInt32 timeoutMs, AString &body,
    UString &errorText);

}}

#endif
