// PwFree.h

#ifndef ZIP7_INC_PW_FREE_H
#define ZIP7_INC_PW_FREE_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyString.h"

/*
  PwFree - password recovery facade.

  PwFree itself knows nothing about any archive format, dictionary format or
  network service. It owns only:
    - the request context (which archive, which size/time, which format),
    - the ordered provider chain,
    - the per-format verifier that turns a candidate password into
      "this really opens the archive" (true / false).

  Everything else is plugged in: providers are pull based (PwFreeChain.h),
  verifiers are created per archive (PwFreeVerify.h).

  Return values of Resolve():
    S_OK              - ans holds a verified password
    S_FALSE           - no password found (nothing to report to the user)
    E_ABORT           - the user cancelled
    E_PWFREE_NOPROBE  - no verifier for this format, the chain was not run
*/

namespace NPwFree {

/*
  Returned by Resolve() when the archive format has no cheap local verifier.
  It is a plain HRESULT value outside the system range, so it can never be
  confused with a real Windows error.
*/
#define E_PWFREE_NOPROBE  ((HRESULT)0x80040001L)

enum ESource
{
  kSrcNone = 0,
  kSrcSeed,      // password the caller already had (retry, second volume)
  kSrcCache,     // remembered password for the same archive signature
  kSrcBook,      // user password book
  kSrcDict,      // bundled or downloaded dictionary
  kSrcCloud,     // remote "hash -> password" lookup service
  kSrcMask       // generated mask / brute force
};

const char *GetSourceName(ESource src);

struct CRequest
{
  // Path of the archive being opened; NULL when unknown (stream / stdin).
  const FString *ArcPath;
  UInt64 Size;
  UInt64 MTime;
  // Assigned by the caller (CArc / CArcInfoEx index). May be
  // (UInt32)(Int32)-1 when the format is not known yet.
  UInt32 FormatIndex;
  // True when the archive really needs a password (headers encrypted, or the
  // caller already got a password request). Hint only: the verifier decides.
  bool HeadersEncrypted;
  // Password the caller already knows (retry, second volume, ...). Optional.
  const UString *Seed;

  CRequest();
};

struct CAnswer
{
  ESource Source;
  UString Password;
  unsigned ElapsedMs;
  // Human readable provider id ("cloud", "dict:data6", "cache", ...), for the
  // "where did the password come from" line in the UI.
  AString ProviderName;

  CAnswer();
  void Clear();
};

HRESULT Resolve(const CRequest &req, CAnswer &ans);

/*
  Fills Size / MTime / ArcPath from the file itself. Returns false when the
  file cannot be stat'ed (stream mode, missing file); the chain then runs
  without a signature and only providers that do not need one are useful.
*/
bool FillRequest_FromPath(const UString &arcPath, CRequest &req);

/*
  Single entry used by the four IOpenCallbackUI implementations. Returns
  S_OK and fills 'password' only when the chain produced a *verified*
  password; every other outcome (disabled, nothing found, no verifier,
  internal error) returns S_FALSE and the caller shows the normal dialog.
*/
HRESULT FindPassword_ForArc(const UString &arcPath, bool headersEncrypted, UString &password);

}

#endif
