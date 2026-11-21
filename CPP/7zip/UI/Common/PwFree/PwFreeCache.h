// PwFreeCache.h

#ifndef ZIP7_INC_PW_FREE_CACHE_H
#define ZIP7_INC_PW_FREE_CACHE_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyString.h"

#include "PwFree.h"

/*
  Remembers passwords that already worked, keyed by the archive signature
  (size, modification time, path tail). The file manager asks twice for a
  normal operation - once when the archive is opened and once when it is
  extracted - and the second answer must not cost another chain run.

  Nothing is written to disk: the cache lives as long as the process, so the
  passwords stay in memory only while the user works.
*/

namespace NPwFree {

bool Cache_Get(const CRequest &req, UString &password);
void Cache_Put(const CRequest &req, const UString &password);
void Cache_Clear();

}

#endif
