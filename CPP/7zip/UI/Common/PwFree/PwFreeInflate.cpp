// PwFreeInflate.cpp

#include "PwFreeInflate.h"

namespace NPwFree {
namespace NPwInflate {

namespace {

const unsigned kMaxBits = 15;
const unsigned kNumLitLenSymbols = 288;
const unsigned kNumDistSymbols = 32;
const unsigned kNumFixedLitLenSymbols = 288;

class CBitReader
{
  const Byte *_data;
  size_t _size;
  size_t _pos;
  UInt32 _bits;
  unsigned _numBits;

public:
  CBitReader(const Byte *data, size_t size):
      _data(data), _size(size), _pos(0), _bits(0), _numBits(0) {}

  bool NeedBits(unsigned num)
  {
    while (_numBits < num)
    {
      if (_pos >= _size)
        return false;
      _bits |= ((UInt32)_data[_pos++]) << _numBits;
      _numBits += 8;
    }
    return true;
  }

  UInt32 ReadBits(unsigned num)
  {
    if (num == 0)
      return 0;
    if (!NeedBits(num))
      return 0xFFFFFFFFu;
    const UInt32 v = _bits & ((1u << num) - 1u);
    _bits >>= num;
    _numBits -= num;
    return v;
  }

  void AlignToByte()
  {
    const unsigned drop = _numBits & 7;
    _bits >>= drop;
    _numBits -= drop;
  }

  // Bits consumed from the stream, rounded up to whole bytes.
  size_t GetPos() const { return _pos - (_numBits >> 3); }

  bool CopyBytes(Byte *dest, size_t size)
  {
    AlignToByte();
    // Any whole bytes still buffered come first.
    while (size != 0 && _numBits >= 8)
    {
      *dest++ = (Byte)(_bits & 0xFF);
      _bits >>= 8;
      _numBits -= 8;
      size--;
    }
    if (size > _size - _pos)
      return false;
    memcpy(dest, _data + _pos, size);
    _pos += size;
    return true;
  }
};

struct CHuffTable
{
  // count[len] = number of codes of that length, symbol[] = symbols sorted by code
  unsigned Count[kMaxBits + 1];
  unsigned Symbols[kNumLitLenSymbols];

  void Build(const Byte *lengths, unsigned numSymbols)
  {
    for (unsigned i = 0; i <= kMaxBits; i++)
      Count[i] = 0;
    for (unsigned i = 0; i < numSymbols; i++)
      Count[lengths[i]]++;
    Count[0] = 0;

    unsigned offsets[kMaxBits + 2];
    offsets[0] = 0;
    offsets[1] = 0;
    for (unsigned len = 1; len <= kMaxBits; len++)
      offsets[len + 1] = offsets[len] + Count[len];

    for (unsigned i = 0; i < numSymbols; i++)
      if (lengths[i] != 0)
        Symbols[offsets[lengths[i]]++] = i;
  }

  // Returns 0xFFFFFFFF on a code that is not in the table.
  unsigned Decode(CBitReader &br) const
  {
    unsigned code = 0;
    unsigned first = 0;
    unsigned index = 0;
    for (unsigned len = 1; len <= kMaxBits; len++)
    {
      const UInt32 bit = br.ReadBits(1);
      if (bit == 0xFFFFFFFFu)
        return 0xFFFFFFFFu;
      code |= (unsigned)bit;
      const unsigned count = Count[len];
      if (code - first < count)
        return Symbols[index + (code - first)];
      index += count;
      first = (first + count) << 1;
      code <<= 1;
    }
    return 0xFFFFFFFFu;
  }
};

const unsigned kLenBase[29] =
  { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const unsigned kLenExtra[29] =
  { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const unsigned kDistBase[30] =
  { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577 };
const unsigned kDistExtra[30] =
  { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

// Output window that writes into a caller buffer and grows it when needed.
class COutWindow
{
  Byte *_fixed;
  size_t _fixedSize;
  size_t _pos;

  CByteBuffer *_grow;
  CByteBuffer _own;

public:
  COutWindow(Byte *buf, size_t size): _fixed(buf), _fixedSize(size), _pos(0), _grow(NULL) {}

  explicit COutWindow(CByteBuffer &grow):
      _fixed(NULL), _fixedSize(0), _pos(0), _grow(&grow) {}

  bool PutByte(Byte b)
  {
    if (_grow)
    {
      if (_pos >= _grow->Size())
      {
        const size_t oldSize = _grow->Size();
        const size_t newSize = (oldSize == 0) ? (size_t)(1 << 16) : (oldSize * 2);
        _grow->ChangeSize_KeepData(newSize, oldSize);
      }
      Byte *p = *_grow;
      p[_pos++] = b;
      return true;
    }
    if (_pos >= _fixedSize)
      return false;
    _fixed[_pos++] = b;
    return true;
  }

  Byte GetByte(size_t back) const
  {
    if (_grow)
    {
      const Byte *p = *_grow;
      return p[_pos - back];
    }
    return _fixed[_pos - back];
  }

  bool CopyBack(size_t dist, size_t len)
  {
    if (dist == 0 || dist > _pos)
      return false;
    for (size_t i = 0; i < len; i++)
      if (!PutByte(GetByte(dist)))
        return false;
    return true;
  }

  size_t Pos() const { return _pos; }

  void ShrinkToPos()
  {
    if (_grow)
      _grow->ChangeSize_KeepData(_pos, _pos);
  }
};

bool InflateBlock(CBitReader &br, COutWindow &out,
    const CHuffTable &litLen, const CHuffTable &dist)
{
  for (;;)
  {
    const unsigned sym = litLen.Decode(br);
    if (sym == 0xFFFFFFFFu)
      return false;
    if (sym < 256)
    {
      if (!out.PutByte((Byte)sym))
        return false;
      continue;
    }
    if (sym == 256)
      return true;
    const unsigned lenIndex = sym - 257;
    if (lenIndex >= 29)
      return false;
    unsigned len = kLenBase[lenIndex];
    if (kLenExtra[lenIndex] != 0)
    {
      const UInt32 extra = br.ReadBits(kLenExtra[lenIndex]);
      if (extra == 0xFFFFFFFFu)
        return false;
      len += (unsigned)extra;
    }
    const unsigned distSym = dist.Decode(br);
    if (distSym >= 30)
      return false;
    size_t dist2 = kDistBase[distSym];
    if (kDistExtra[distSym] != 0)
    {
      const UInt32 extra = br.ReadBits(kDistExtra[distSym]);
      if (extra == 0xFFFFFFFFu)
        return false;
      dist2 += (size_t)extra;
    }
    if (!out.CopyBack(dist2, len))
      return false;
  }
}

const unsigned kCodeLengthOrder[19] =
  { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

bool ReadDynamicTables(CBitReader &br, CHuffTable &litLen, CHuffTable &dist)
{
  const UInt32 hlit = br.ReadBits(5);
  const UInt32 hdist = br.ReadBits(5);
  const UInt32 hclen = br.ReadBits(4);
  if (hlit == 0xFFFFFFFFu || hdist == 0xFFFFFFFFu || hclen == 0xFFFFFFFFu)
    return false;

  const unsigned numLit = (unsigned)hlit + 257;
  const unsigned numDist = (unsigned)hdist + 1;
  const unsigned numCl = (unsigned)hclen + 4;
  if (numLit > kNumLitLenSymbols || numDist > kNumDistSymbols)
    return false;

  Byte codeLengths[19];
  memset(codeLengths, 0, sizeof(codeLengths));
  for (unsigned i = 0; i < numCl; i++)
  {
    const UInt32 v = br.ReadBits(3);
    if (v == 0xFFFFFFFFu)
      return false;
    codeLengths[kCodeLengthOrder[i]] = (Byte)v;
  }

  CHuffTable codeLenTable;
  codeLenTable.Build(codeLengths, 19);

  Byte lengths[kNumLitLenSymbols + kNumDistSymbols];
  unsigned index = 0;
  while (index < numLit + numDist)
  {
    const unsigned sym = codeLenTable.Decode(br);
    if (sym == 0xFFFFFFFFu)
      return false;
    if (sym < 16)
    {
      lengths[index++] = (Byte)sym;
      continue;
    }
    unsigned repeat;
    Byte value = 0;
    if (sym == 16)
    {
      if (index == 0)
        return false;
      value = lengths[index - 1];
      const UInt32 v = br.ReadBits(2);
      if (v == 0xFFFFFFFFu)
        return false;
      repeat = 3 + (unsigned)v;
    }
    else if (sym == 17)
    {
      const UInt32 v = br.ReadBits(3);
      if (v == 0xFFFFFFFFu)
        return false;
      repeat = 3 + (unsigned)v;
    }
    else
    {
      const UInt32 v = br.ReadBits(7);
      if (v == 0xFFFFFFFFu)
        return false;
      repeat = 11 + (unsigned)v;
    }
    if (index + repeat > numLit + numDist)
      return false;
    while (repeat-- != 0)
      lengths[index++] = value;
  }

  litLen.Build(lengths, numLit);
  dist.Build(lengths + numLit, numDist);
  return true;
}

void BuildFixedTables(CHuffTable &litLen, CHuffTable &dist)
{
  Byte lengths[kNumLitLenSymbols];
  unsigned i;
  for (i = 0; i < 144; i++) lengths[i] = 8;
  for (; i < 256; i++) lengths[i] = 9;
  for (; i < 280; i++) lengths[i] = 7;
  for (; i < kNumFixedLitLenSymbols; i++) lengths[i] = 8;
  litLen.Build(lengths, kNumFixedLitLenSymbols);

  Byte distLengths[kNumDistSymbols];
  for (i = 0; i < kNumDistSymbols; i++) distLengths[i] = 5;
  dist.Build(distLengths, kNumDistSymbols);
}

bool InflateImpl(const Byte *in, size_t inSize, COutWindow &out)
{
  CBitReader br(in, inSize);
  CHuffTable fixedLitLen, fixedDist;
  bool fixedBuilt = false;

  for (;;)
  {
    const UInt32 final = br.ReadBits(1);
    const UInt32 type = br.ReadBits(2);
    if (final == 0xFFFFFFFFu || type == 0xFFFFFFFFu)
      return false;

    if (type == 0)
    {
      br.AlignToByte();
      // Block length, then its complement; both little endian.
      Byte hdr[4];
      if (!br.CopyBytes(hdr, 4))
        return false;
      const unsigned len = hdr[0] | ((unsigned)hdr[1] << 8);
      const unsigned nlen = hdr[2] | ((unsigned)hdr[3] << 8);
      if ((len ^ 0xFFFFu) != nlen)
        return false;
      for (unsigned i = 0; i < len; i++)
      {
        Byte b;
        if (!br.CopyBytes(&b, 1))
          return false;
        if (!out.PutByte(b))
          return false;
      }
    }
    else if (type == 1)
    {
      if (!fixedBuilt)
      {
        BuildFixedTables(fixedLitLen, fixedDist);
        fixedBuilt = true;
      }
      if (!InflateBlock(br, out, fixedLitLen, fixedDist))
        return false;
    }
    else if (type == 2)
    {
      CHuffTable litLen, dist;
      if (!ReadDynamicTables(br, litLen, dist))
        return false;
      if (!InflateBlock(br, out, litLen, dist))
        return false;
    }
    else
      return false;

    if (final == 1)
      return true;
  }
}

}

bool Inflate_Raw(const Byte *in, size_t inSize, Byte *out, size_t outSize)
{
  COutWindow w(out, outSize);
  if (!InflateImpl(in, inSize, w))
    return false;
  return w.Pos() == outSize;
}

bool Inflate_Raw_Grow(const Byte *in, size_t inSize, CByteBuffer &out)
{
  out.Free();
  COutWindow w(out);
  if (!InflateImpl(in, inSize, w))
    return false;
  w.ShrinkToPos();
  return true;
}

}}
