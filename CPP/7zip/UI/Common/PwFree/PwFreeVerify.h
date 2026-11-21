// PwFreeVerify.h

#ifndef ZIP7_INC_PW_FREE_VERIFY_H
#define ZIP7_INC_PW_FREE_VERIFY_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyString.h"

#include "PwFree.h"

/*
  A verifier answers one question: does this candidate password really open
  the archive? It is the only piece of PwFree that knows about formats, and
  every provider result goes through it, so a provider can be as noisy as it
  likes.

  Verifiers are created once per archive and reused for every candidate, so
  they may cache parsed headers, file handles and derived keys.
*/

namespace NPwFree {

class IVerify
{
public:
  virtual ~IVerify() {}

  /*
    0 - the check reads a few bytes of the archive (fastest)
    1 - the check decrypts and validates one entry
    2 - the check re-opens the archive through the 7-Zip handlers
    The chain picks the cheapest verifier the format allows.
  */
  virtual unsigned CostClass() const = 0;

  // Human readable id, used in the "where did the password come from" line.
  virtual const char *Name() const = 0;

  virtual bool Check(const Byte *password, size_t size) = 0;
};

/*
  Creates the verifier for 'arcPath'. 'formatIndex' is the CCodecs format
  index when the caller knows it ((UInt32)(Int32)-1 otherwise). Returns NULL
  when the format has no cheap probe; the chain then reports E_PWFREE_NOPROBE
  instead of guessing.
*/
IVerify *CreateVerifier(const FString &arcPath, UInt32 formatIndex);

/*
  Opens the archive through 7-Zip and reports whether the password works.
  Used when the archive is opened by a caller that already has a working
  codecs object; the verifier takes a borrowed pointer.
*/
typedef bool (*FOpenWithPassword)(void *context, const UString &password);

}

#endif
