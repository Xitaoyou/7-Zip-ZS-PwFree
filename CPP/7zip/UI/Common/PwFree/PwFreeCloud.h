// PwFreeCloud.h

#ifndef ZIP7_INC_PW_FREE_CLOUD_H
#define ZIP7_INC_PW_FREE_CLOUD_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyString.h"

#include "PwFree.h"

/*
  Remote "archive fingerprint -> password" lookup.

  This is the part of the upstream application that makes it feel magical: the
  client sends nothing but the MD5 of the archive, and the service answers with
  a password when somebody else has already cracked the same file. The
  archives never leave the machine.

  Two endpoints of the original service are used, and the answers of both go
  through the local verifier before they are accepted, so a wrong or hostile
  answer cannot make 7-Zip report a password that does not work.
*/

namespace NPwFree {

/*
  Collects candidate passwords for 'arcPath' from the remote service.
  The fingerprint is computed lazily, on the first call to Fetch().
*/
class CCloudLookup
{
  FString _arcPath;
  bool _tried;
  bool _ok;
  UStringVector _candidates;
  size_t _pos;
  UString _providerName;
  UString _lastError;

  bool Fetch(UInt32 timeoutMs, bool allowHttpFallback, volatile long *cancel);

public:
  explicit CCloudLookup(const FString &arcPath);
  // S_OK when at least one candidate was collected.
  HRESULT Query(UInt32 timeoutMs, bool allowHttpFallback = false, volatile long *cancel = NULL);
  // S_FALSE when exhausted.
  HRESULT Next(UString &password);
  const UString &GetProviderName() const { return _providerName; }
  const UString &GetLastError() const { return _lastError; }
};

}

#endif
