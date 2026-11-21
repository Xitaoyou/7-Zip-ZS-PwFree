// PwFreeChain.h

#ifndef ZIP7_INC_PW_FREE_CHAIN_H
#define ZIP7_INC_PW_FREE_CHAIN_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyString.h"

#include "PwFree.h"
#include "PwFreeVerify.h"

/*
  Providers are pull based on purpose.

  A provider implements Next() and returns one candidate at a time; the chain
  owns the loop, so the budget and the cancellation check cannot be forgotten
  by a provider, the order of the providers stays in one place, and a provider
  can be tested with a synthetic candidate list without a thread or network.

  The chain never returns a password: it feeds candidates to a verifier and
  stops at the first one the verifier accepts. An unverified hit is a bug, not
  a result.
*/

namespace NPwFree {

class IProvider
{
public:
  virtual ~IProvider() {}

  // Stable id used in the "password came from ..." line.
  virtual const char *Name() const = 0;

  // Milliseconds this provider may take. 0 means "no own limit".
  virtual UInt32 BudgetMs() const = 0;

  /*
    Produces the next candidate.
      S_OK    - 'password' holds a candidate, ask again for the next one
      S_FALSE - exhausted, the chain moves on
    Any error counts as "exhausted"; a provider never throws.
  */
  virtual HRESULT Next(UString &password) = 0;

  // Lets a provider release what it holds as soon as the chain is done.
  virtual void Finish() {}
};

IProvider *CreateSeedProvider(const CRequest &req);
IProvider *CreateCacheProvider(const CRequest &req);
IProvider *CreateDictProvider(const CRequest &req);
IProvider *CreateCloudProvider(const CRequest &req);

/*
  Runs the providers in cost order against 'verify'.

  Returns S_OK with 'password' filled, S_FALSE when nothing matched, E_ABORT
  when *cancel became non-zero. 'providerName' receives the provider id of the
  hit. 'elapsedMs' receives the wall time of the whole run.
*/
HRESULT RunChain(const CRequest &req, IVerify &verify, UString &password,
    AString &providerName, unsigned &elapsedMs, volatile long *cancel);

}

#endif
