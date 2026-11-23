// PwFreeInflate.h

#ifndef ZIP7_INC_PW_FREE_INFLATE_H
#define ZIP7_INC_PW_FREE_INFLATE_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyBuffer.h"

/*
  Minimal raw DEFLATE (RFC 1951) decoder.

  The recovery module needs to decompress two things: entry data of a ZIP
  archive (to check the CRC of a candidate password) and the payload of the
  vendor password dictionaries, which are ZIP files with a single deflated
  member. Both use raw DEFLATE, so a 300 line decoder is enough and keeps
  the module independent from the codec objects of the build it is linked
  into (those are built with a per-target precompiled header).
*/

namespace NPwFree {
namespace NPwInflate {

// outSize must be the exact expected size; returns false on any error.
bool Inflate_Raw(const Byte *in, size_t inSize, Byte *out, size_t outSize);

// Unknown output size: 'out' grows as needed up to 'maxOutSize'.
// Returns false on corruption or if output exceeds 'maxOutSize'.
bool Inflate_Raw_Grow(const Byte *in, size_t inSize, CByteBuffer &out,
    size_t maxOutSize = (size_t)64 << 20);

}}

#endif
