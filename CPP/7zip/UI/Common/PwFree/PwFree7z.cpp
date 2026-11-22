// PwFree7z.cpp

#include "PwFree7z.h"

#include "PwFreeCrypto.h"

#include "../../../../Common/UTFConvert.h"
#include "../../../../../C/Alloc.h"
#include "../../../../../C/LzmaDec.h"
#include "../../../../../C/Lzma2Dec.h"

using namespace NWindows;

namespace NPwFree {
namespace NPwSevenZ {

namespace {

const UInt64 k_Aes = (UInt64)0x6F10701;
const UInt64 k_Lzma = (UInt64)0x30101;
const UInt64 k_Lzma2 = (UInt64)0x21;
const UInt64 k_Copy = 0;

const size_t kSignatureSize = 6;
const Byte kSignature[kSignatureSize] = { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };

const unsigned kId_End = 0;
const unsigned kId_Header = 1;
const unsigned kId_ArchiveProperties = 2;
const unsigned kId_AdditionalStreamsInfo = 3;
const unsigned kId_MainStreamsInfo = 4;
const unsigned kId_FilesInfo = 5;
const unsigned kId_PackInfo = 6;
const unsigned kId_UnpackInfo = 7;
const unsigned kId_SubStreamsInfo = 8;
const unsigned kId_Size = 9;
const unsigned kId_CRC = 10;
const unsigned kId_Folder = 11;
const unsigned kId_CodersUnpackSize = 12;
const unsigned kId_NumUnpackStream = 13;
const unsigned kId_EncodedHeader = 23;

// A real archive stays far below these; the caps keep a damaged or hostile
// file from asking for a huge read or allocation.
const size_t kMaxNextHeaderSize = (size_t)1 << 20;
const size_t kMaxProbeData = (size_t)32 << 20;
const size_t kMaxPayloadDecode = (size_t)1 << 20;
const unsigned kMaxCoders = 4;
const unsigned kMaxFolders = 16;
const unsigned kMaxPackStreams = 64;
const unsigned kMaxSubStreams = 4096;
const unsigned kMaxChunks = (unsigned)1 << 20;

/* Reads the 7-Zip variable length integers and the record framing. */
class CMemReader
{
  const Byte *_data;
  size_t _size;
  size_t _pos;
  bool _bad;

  CMemReader(const CMemReader &) = delete;
  CMemReader &operator=(const CMemReader &) = delete;

public:
  CMemReader(const Byte *data, size_t size):
      _data(data), _size(size), _pos(0), _bad(false) {}

  bool Bad() const { return _bad; }
  size_t Pos() const { return _pos; }
  size_t Remain() const { return (_pos <= _size) ? (_size - _pos) : 0; }
  const Byte *Ptr() const { return _data + _pos; }

  bool ReadByte(unsigned &value)
  {
    if (_bad || _pos >= _size)
    {
      _bad = true;
      return false;
    }
    value = _data[_pos++];
    return true;
  }

  bool ReadBytes(size_t count, Byte *dest)
  {
    if (_bad || count > Remain())
    {
      _bad = true;
      return false;
    }
    memcpy(dest, _data + _pos, count);
    _pos += count;
    return true;
  }

  // A byte 0xFF is followed by the value as eight little endian bytes,
  // otherwise the high bits of the first byte mark how many follow.
  bool Number(UInt64 &value)
  {
    unsigned first;
    if (!ReadByte(first))
      return false;
    value = 0;
    unsigned mask = 0x80;
    for (unsigned i = 0; i < 8; i++)
    {
      if ((first & mask) == 0)
      {
        value |= (UInt64)(first & (mask - 1)) << (i * 8);
        return true;
      }
      unsigned next;
      if (!ReadByte(next))
        return false;
      value |= (UInt64)next << (i * 8);
      mask >>= 1;
    }
    return true;
  }

  bool Skip(size_t count)
  {
    if (_bad || count > Remain())
    {
      _bad = true;
      return false;
    }
    _pos += count;
    return true;
  }

  // A property record is a length followed by that many bytes.
  bool SkipProperty()
  {
    UInt64 size;
    if (!Number(size))
      return false;
    if (size > Remain())
      return false;
    return Skip((size_t)size);
  }
};

bool ReadCrcs(CMemReader &r, unsigned numItems, UInt64 *crcs, bool *defined)
{
  unsigned allDefined;
  if (!r.ReadByte(allDefined))
    return false;
  if (allDefined > 1)
    return false;

  if (allDefined == 0)
  {
    if (r.Remain() < numItems)
      return false;
    const Byte *defs = r.Ptr();
    for (unsigned i = 0; i < numItems; i++)
    {
      if (defs[i] > 1)
        return false;
      defined[i] = (defs[i] != 0);
    }
    if (!r.Skip(numItems))
      return false;
  }
  else
  {
    for (unsigned i = 0; i < numItems; i++)
      defined[i] = true;
  }

  for (unsigned i = 0; i < numItems; i++)
  {
    if (!defined[i])
    {
      crcs[i] = 0;
      continue;
    }
    if (r.Remain() < 4)
      return false;
    const Byte *p = r.Ptr();
    crcs[i] = (UInt64)p[0] | ((UInt64)p[1] << 8)
        | ((UInt64)p[2] << 16) | ((UInt64)p[3] << 24);
    if (!r.Skip(4))
      return false;
  }
  return true;
}

bool ReadFolder(CMemReader &r, CStreams *streams, unsigned index)
{
  UInt64 numCoders;
  if (!r.Number(numCoders))
    return false;
  if (numCoders == 0 || numCoders > kMaxCoders)
    return false;

  CFolder &folder = streams->Folders[index];
  folder.NumCoders = (unsigned)numCoders;
  folder.NumOut = 0;
  UInt64 numInStreams = 0;
  UInt64 numOutStreams = 0;

  for (unsigned i = 0; i < folder.NumCoders; i++)
  {
    CCoder &coder = folder.Coders[i];
    unsigned flags;
    if (!r.ReadByte(flags))
      return false;
    const unsigned idSize = flags & 0x0F;
    if (idSize > 8 || (flags & 0xC0) != 0)
      return false;

    UInt64 id = 0;
    for (unsigned k = 0; k < idSize; k++)
    {
      unsigned b;
      if (!r.ReadByte(b))
        return false;
      id = (id << 8) | b;
    }
    coder.Id = id;

    UInt64 inCount = 1;
    UInt64 outCount = 1;
    if ((flags & 0x10) != 0)
    {
      if (!r.Number(inCount) || !r.Number(outCount))
        return false;
    }
    if (inCount == 0 || outCount == 0 || inCount > 8 || outCount > 8)
      return false;
    numInStreams += inCount;
    numOutStreams += outCount;
    if (numInStreams > 8 || numOutStreams > 8)
      return false;

    coder.PropsSize = 0;
    if ((flags & 0x20) != 0)
    {
      UInt64 propsSize;
      if (!r.Number(propsSize))
        return false;
      if (propsSize > sizeof(coder.Props))
        return false;
      coder.PropsSize = (unsigned)propsSize;
      if (!r.ReadBytes((size_t)propsSize, coder.Props))
        return false;
    }

  }

  if (numOutStreams == 0)
    return false;

  /*
    The streams of a folder are numbered outputs first, then inputs. Every
    output but the final one is bound to an input, and the inputs that remain
    unbound are the packed streams.
  */
  const unsigned bindPairs = (unsigned)numOutStreams - 1;
  for (unsigned i = 0; i < bindPairs; i++)
  {
    UInt64 inIndex;
    UInt64 outIndex;
    if (!r.Number(inIndex) || !r.Number(outIndex))
      return false;
    // The two indexes live in different domains: inputs and outputs are
    // numbered independently inside the folder.
    if (inIndex >= numInStreams || outIndex >= numOutStreams)
      return false;
  }

  const unsigned packedStreams = (unsigned)(numInStreams - bindPairs);
  if (packedStreams == 0)
    return false;
  if (packedStreams > 1)
  {
    for (unsigned i = 0; i < packedStreams; i++)
    {
      UInt64 streamIndex;
      if (!r.Number(streamIndex))
        return false;
      if (streamIndex >= numInStreams)
        return false;
    }
  }

  folder.NumOut = (unsigned)numOutStreams;
  folder.NumPackStreams = packedStreams;
  // A probe only follows a folder that is fed by a single packed stream.
  return packedStreams == 1;
}

/*
  The sub stream records describe the split of the folder output; the probe does
  not use them but has to walk past them without losing alignment.
*/
bool SkipSubStreams(CMemReader &r, unsigned numFolders)
{
  if (numFolders > kMaxFolders)
    return false;
  UInt64 counts[kMaxFolders];
  for (unsigned i = 0; i < numFolders; i++)
    counts[i] = 1;

  for (;;)
  {
    unsigned id;
    if (!r.ReadByte(id))
      return false;
    if (id == kId_End)
      return true;

    if (id == kId_NumUnpackStream)
    {
      for (unsigned i = 0; i < numFolders; i++)
        if (!r.Number(counts[i]))
          return false;
      continue;
    }

    if (id == kId_Size)
    {
      for (unsigned i = 0; i < numFolders; i++)
      {
        if (counts[i] > kMaxSubStreams)
          return false;
        for (UInt64 k = 1; k < counts[i]; k++)
        {
          UInt64 size;
          if (!r.Number(size))
            return false;
        }
      }
      continue;
    }

    if (id == kId_CRC)
    {
      UInt64 total = 0;
      for (unsigned i = 0; i < numFolders; i++)
      {
        if (counts[i] > kMaxSubStreams || total > kMaxSubStreams)
          return false;
        total += counts[i];
      }
      if (total > kMaxSubStreams)
        return false;
      UInt64 crcs[kMaxSubStreams];
      bool defined[kMaxSubStreams];
      if (!ReadCrcs(r, (unsigned)total, crcs, defined))
        return false;
      continue;
    }

    if (!r.SkipProperty())
      return false;
  }
}

bool ParseStreams(CMemReader &r, CStreams *streams)
{
  unsigned id;
  if (!r.ReadByte(id))
    return false;

  if (id == kId_PackInfo)
  {
    if (!r.Number(streams->PackPos))
      return false;
    UInt64 count;
    if (!r.Number(count))
      return false;
    if (count > kMaxPackStreams)
      return false;
    streams->NumPackStreams = (unsigned)count;

    for (;;)
    {
      unsigned pid;
      if (!r.ReadByte(pid))
        return false;
      if (pid == kId_End)
        break;
      if (pid == kId_Size)
      {
        for (unsigned i = 0; i < streams->NumPackStreams; i++)
          if (!r.Number(streams->PackSizes[i]))
            return false;
      }
      else if (pid == kId_CRC)
      {
        UInt64 crcs[kMaxPackStreams];
        bool defined[kMaxPackStreams];
        if (!ReadCrcs(r, streams->NumPackStreams, crcs, defined))
          return false;
      }
      else if (!r.SkipProperty())
        return false;
    }
    if (!r.ReadByte(id))
      return false;
  }

  if (id == kId_UnpackInfo)
  {
    unsigned pid;
    if (!r.ReadByte(pid))
      return false;
    if (pid != kId_Folder)
      return false;

    UInt64 count;
    if (!r.Number(count))
      return false;
    if (count > kMaxFolders)
      return false;
    streams->NumFolders = (unsigned)count;

    unsigned external;
    if (!r.ReadByte(external))
      return false;
    if (external != 0)
      return false;

    for (unsigned i = 0; i < streams->NumFolders; i++)
    {
      streams->Folders[i].CrcDefined = false;
      streams->Folders[i].Crc = 0;
      if (!ReadFolder(r, streams, i))
        return false;
    }

    unsigned idSize;
    if (!r.ReadByte(idSize))
      return false;
    if (idSize != kId_CodersUnpackSize)
      return false;

    for (unsigned i = 0; i < streams->NumFolders; i++)
    {
      CFolder &folder = streams->Folders[i];
      for (unsigned k = 0; k < folder.NumOut; k++)
        if (!r.Number(folder.UnpackSizes[k]))
          return false;
    }

    for (;;)
    {
      unsigned prop;
      if (!r.ReadByte(prop))
        return false;
      if (prop == kId_End)
        break;
      if (prop == kId_CRC)
      {
        UInt64 crcs[kMaxFolders];
        bool defined[kMaxFolders];
        if (!ReadCrcs(r, streams->NumFolders, crcs, defined))
          return false;
        for (unsigned i = 0; i < streams->NumFolders; i++)
        {
          streams->Folders[i].CrcDefined = defined[i];
          streams->Folders[i].Crc = crcs[i];
        }
      }
      else if (!r.SkipProperty())
        return false;
    }

    if (!r.ReadByte(id))
      return false;
  }

  if (id == kId_SubStreamsInfo)
  {
    if (!SkipSubStreams(r, streams->NumFolders))
      return false;
    if (!r.ReadByte(id))
      return false;
  }

  return id == kId_End;
}

/*
  A plain next header that was decrypted with the right key parses cleanly: the
  section identifiers follow the specification and every record stays inside the
  buffer. Random plain text fails within a few bytes.
*/
bool ValidateHeader(const Byte *data, size_t size)
{
  if (size < 2)
    return false;
  CMemReader r(data + 1, size - 1);
  bool sawStreams = false;

  for (;;)
  {
    unsigned id;
    if (!r.ReadByte(id))
      return false;
    if (id == kId_End)
      return sawStreams && r.Remain() == 0;
    if (id == kId_ArchiveProperties)
    {
      for (;;)
      {
        unsigned pid;
        if (!r.ReadByte(pid))
          return false;
        if (pid == kId_End)
          break;
        if (!r.SkipProperty())
          return false;
      }
      continue;
    }
    if (id == kId_AdditionalStreamsInfo || id == kId_MainStreamsInfo)
    {
      CStreams scratch;
      memset(&scratch, 0, sizeof(scratch));
      if (!ParseStreams(r, &scratch))
        return false;
      if (id == kId_MainStreamsInfo)
        sawStreams = true;
      continue;
    }
    // The file records that follow are checked by the extraction itself.
    if (id == kId_FilesInfo)
      return sawStreams;
    if (id == kId_EncodedHeader)
      return false;
    return false;
  }
}

bool DecodeLzma(const Byte *src, size_t srcSize, const Byte *props, unsigned propsSize,
    Byte *dest, size_t destSize)
{
  if (propsSize != 5)
    return false;
  SizeT destLen = destSize;
  SizeT srcLen = srcSize;
  ELzmaStatus status;
  const SRes res = LzmaDecode(dest, &destLen, src, &srcLen, props, propsSize,
      LZMA_FINISH_ANY, &status, &g_Alloc);
  return res == SZ_OK && destLen == destSize;
}

bool DecodeLzma2(const Byte *src, size_t srcSize, unsigned prop,
    Byte *dest, size_t destSize)
{
  if (prop > 40)
    return false;
  SizeT destLen = destSize;
  SizeT srcLen = srcSize;
  ELzmaStatus status;
  const SRes res = Lzma2Decode(dest, &destLen, src, &srcLen, (Byte)prop,
      LZMA_FINISH_ANY, &status, &g_Alloc);
  return res == SZ_OK && destLen == destSize;
}

} // namespace

CProbe::CProbe():
    _fileSize(0),
    _mode(0),
    _folderIndex(0),
    _saltSize(0),
    _numCyclesPower(0),
    _headerCrc(0),
    _blockPos(0),
    _blockValid(false)
{
  memset(_iv, 0, sizeof(_iv));
  memset(_salt, 0, sizeof(_salt));
  memset(_block, 0, sizeof(_block));
  memset(&_streams, 0, sizeof(_streams));
}

CProbe::~CProbe()
{
  _file.Close();
}

bool IsSevenZ(const FString &path)
{
  NWindows::NFile::NIO::CInFile file;
  if (!file.Open(path))
    return false;
  Byte buf[kSignatureSize];
  UInt32 processed = 0;
  if (!file.ReadPart(buf, kSignatureSize, processed))
    return false;
  if (processed != kSignatureSize)
    return false;
  for (unsigned i = 0; i < kSignatureSize; i++)
    if (buf[i] != kSignature[i])
      return false;
  return true;
}

bool CProbe::ReadAt(UInt64 offset, void *data, size_t size)
{
  if (size == 0)
    return true;
  if (offset > _fileSize || size > _fileSize - offset)
    return false;
  UInt64 newPos;
  if (!_file.Seek(offset, newPos))
    return false;
  Byte *p = (Byte *)data;
  size_t done = 0;
  while (done < size)
  {
    UInt32 processed = 0;
    const size_t left = size - done;
    const size_t todo = (left > ((size_t)1 << 20)) ? ((size_t)1 << 20) : left;
    if (!_file.ReadPart(p + done, (UInt32)todo, processed))
      return false;
    if (processed == 0)
      return false;
    done += processed;
  }
  return true;
}

bool CProbe::DeriveKey(const Byte *password16, size_t size, Byte *key) const
{
  /*
    7-Zip hashes the salt followed by the UTF-16LE password, then replaces the
    last eight bytes of that buffer with a counter and hashes 2^power such
    buffers into one single SHA-256.
  */
  const unsigned kKeySize = 32;
  if (_numCyclesPower == 0x3F)
  {
    // Extended mode: the key is the truncated salt/password buffer itself.
    memset(key, 0, kKeySize);
    const size_t saltPart = (_saltSize < kKeySize) ? _saltSize : kKeySize;
    memcpy(key, _salt, saltPart);
    const size_t rest = kKeySize - saltPart;
    const size_t copy = (size < rest) ? size : rest;
    memcpy(key + saltPart, password16, copy);
    return true;
  }

  CByteBuffer buf;
  buf.Alloc(_saltSize + size);
  Byte *p = buf;
  if (_saltSize != 0)
    memcpy(p, _salt, _saltSize);
  if (size != 0)
    memcpy(p + _saltSize, password16, size);
  return NPwCrypto::Sha256_Kdf7z(p, _saltSize + size, _numCyclesPower, key);
}

/*
  CBC decryption of an arbitrary block aligned range: the cipher block just
  before the range serves as the initialisation vector of this range, so a
  chunk header can be read without touching the rest of the stream.
*/
bool CProbe::DecryptRange(const Byte *key, UInt64 packOffset, UInt64 posOffset,
    Byte *dest, size_t size)
{
  if (size == 0)
    return true;
  if ((posOffset & 15) != 0 || (size & 15) != 0)
    return false;
  const UInt64 offset = packOffset + posOffset;
  if (offset < packOffset)
    return false;

  Byte iv[16];
  if (posOffset == 0)
    memcpy(iv, _iv, 16);
  else if (!ReadAt(offset - 16, iv, 16))
    return false;

  if (!ReadAt(offset, dest, size))
    return false;
  return NPwCrypto::Aes_Cbc_Decrypt(key, 32, iv, dest, size);
}

bool CProbe::PlainByte(const Byte *key, UInt64 packOffset, UInt64 pos, unsigned &value)
{
  const UInt64 blockPos = pos & ~(UInt64)15;
  if (!_blockValid || _blockPos != blockPos)
  {
    if (!DecryptRange(key, packOffset, blockPos, _block, 16))
      return false;
    _blockPos = blockPos;
    _blockValid = true;
  }
  value = _block[(unsigned)(pos & 15)];
  return true;
}

bool CProbe::PackRange(unsigned folderIndex, UInt64 &offset, UInt64 &size) const
{
  if (folderIndex >= _streams.NumFolders)
    return false;
  const CFolder &folder = _streams.Folders[folderIndex];
  if (folder.NumPackStreams != 1)
    return false;

  UInt64 packIndex = 0;
  for (unsigned i = 0; i < folderIndex; i++)
    packIndex += _streams.Folders[i].NumPackStreams;
  if (packIndex >= kMaxPackStreams || packIndex >= _streams.NumPackStreams)
    return false;

  UInt64 packOffset = 32 + _streams.PackPos;
  for (UInt64 i = 0; i < packIndex; i++)
    packOffset += _streams.PackSizes[i];
  offset = packOffset;
  size = _streams.PackSizes[packIndex];
  return size != 0;
}

bool CProbe::WalkChunks(const Byte *key, UInt64 packOffset,
    UInt64 inputSize, UInt64 outputSize)
{
  UInt64 pos = 0;
  UInt64 produced = 0;
  unsigned needInit = 0xE0;

  for (unsigned chunk = 0; chunk < kMaxChunks; chunk++)
  {
    if (pos >= inputSize)
      return false;

    unsigned control;
    if (!PlainByte(key, packOffset, pos, control))
      return false;
    pos++;

    if (control == 0)
    {
      // The end marker has to sit exactly on the AES output boundary.
      if (pos != inputSize || produced != outputSize)
        return false;
      return CheckPadding(key, packOffset, inputSize, inputSize);
    }

    if (control < 0x80)
    {
      if (control == 1)
        needInit = 0xC0;
      else if (control > 2 || needInit == 0xE0)
        return false;

      unsigned hi, lo;
      if (!PlainByte(key, packOffset, pos, hi)
          || !PlainByte(key, packOffset, pos + 1, lo))
        return false;
      const UInt64 len = (((UInt64)hi << 8) | lo) + 1;
      pos += 2;
      if (len > inputSize - pos)
        return false;
      pos += len;
      produced += len;
    }
    else
    {
      if (control < needInit)
        return false;
      needInit = 0;

      unsigned b[4];
      for (unsigned i = 0; i < 4; i++)
        if (!PlainByte(key, packOffset, pos + i, b[i]))
          return false;
      const UInt64 unpack = (((UInt64)(control & 0x1F) << 16)
          | ((UInt64)b[0] << 8) | b[1]) + 1;
      const UInt64 pack = (((UInt64)b[2] << 8) | b[3]) + 1;
      pos += 4;

      if ((control & 0x40) != 0)
      {
        unsigned prop;
        if (!PlainByte(key, packOffset, pos, prop))
          return false;
        pos++;
        if (prop >= 9 * 5 * 5)
          return false;
        if (prop % 9 + (prop / 9) % 5 > 4)
          return false;
      }

      if (pack > inputSize - pos)
        return false;
      pos += pack;
      produced += unpack;
    }

    if (produced > outputSize)
      return false;
  }
  return false;
}

/*
  The packed stream is padded to the AES block size. With the right password
  the padding bytes are zero, with a wrong one they are random.
*/
bool CProbe::CheckPadding(const Byte *key, UInt64 packOffset,
    UInt64 inputSize, UInt64 packSize)
{
  if (inputSize >= packSize)
    return inputSize == packSize;
  const UInt64 padding = packSize - inputSize;
  if (padding > 16)
    return false;

  Byte block[16];
  const UInt64 blockPos = inputSize & ~(UInt64)15;
  if (!DecryptRange(key, packOffset, blockPos, block, 16))
    return false;
  for (UInt64 i = inputSize; i < packSize; i++)
    if (block[(unsigned)(i - blockPos)] != 0)
      return false;
  return true;
}

/*
  Decrypts the packed stream and, when a compressor follows the AES coder,
  decodes the rest of the chain as well.
*/
bool CProbe::DecodeChain(const Byte *key, UInt64 packOffset, UInt64 packSize,
    CByteBuffer &dest, UInt64 &destSize)
{
  const CFolder &folder = _streams.Folders[_folderIndex];
  if (folder.NumCoders == 0 || packSize > kMaxProbeData)
    return false;

  // The AES coder stores the real plaintext size; the packed stream is padded
  // up to the block size, so the last block is decrypted as a whole.
  const UInt64 aesOut = folder.UnpackSizes[0];
  if (aesOut == 0 || aesOut > packSize)
    return false;
  if (!CheckPadding(key, packOffset, aesOut, packSize))
    return false;

  const UInt64 aesFull = (aesOut + 15) & ~(UInt64)15;
  if (aesFull > packSize)
    return false;

  CByteBuffer aesBuf;
  aesBuf.Alloc((size_t)aesFull);
  if (!DecryptRange(key, packOffset, 0, aesBuf, (size_t)aesFull))
    return false;

  if (folder.NumCoders == 1)
  {
    dest.Alloc((size_t)aesOut);
    memcpy(dest, aesBuf, (size_t)aesOut);
    destSize = aesOut;
    return true;
  }

  if (folder.NumCoders != 2)
    return false;

  const UInt64 out = folder.UnpackSizes[1];
  if (out == 0 || out > kMaxProbeData)
    return false;

  const CCoder &coder = folder.Coders[1];
  dest.Alloc((size_t)out);
  bool ok = false;
  if (coder.Id == k_Lzma)
    ok = DecodeLzma(aesBuf, (size_t)aesOut, coder.Props, coder.PropsSize,
        dest, (size_t)out);
  else if (coder.Id == k_Lzma2)
    ok = DecodeLzma2(aesBuf, (size_t)aesOut, coder.PropsSize ? coder.Props[0] : 0x40,
        dest, (size_t)out);
  else if (coder.Id == k_Copy)
  {
    ok = (out == aesOut);
    if (ok && out != 0)
      memcpy(dest, aesBuf, (size_t)out);
  }
  if (!ok)
    return false;
  destSize = out;
  return true;
}

// The encrypted next header: decode it and compare the stored CRC.
bool CProbe::CheckHeader(const Byte *key, UInt64 packOffset, UInt64 packSize)
{
  CByteBuffer plain;
  UInt64 plainSize = 0;
  if (!DecodeChain(key, packOffset, packSize, plain, plainSize))
    return false;

  if (plainSize < 2)
    return false;
  const Byte *p = plain;
  if (p[0] != kId_Header)
    return false;
  const UInt32 calc = NPwCrypto::Crc32_Calc(plain, (size_t)plainSize);
  const CFolder &folder = _streams.Folders[_folderIndex];
  if (folder.CrcDefined)
    return calc == (UInt32)folder.Crc;
  // Without a stored digest only the shape of the plain text can confirm it.
  return ValidateHeader(plain, (size_t)plainSize);
}

bool CProbe::CheckPayload(const Byte *key, UInt64 packOffset, UInt64 packSize)
{
  const CFolder &folder = _streams.Folders[_folderIndex];

  const UInt64 aesOut = folder.UnpackSizes[0];
  if (aesOut == 0 || aesOut > packSize)
    return false;

  // A small folder that stores its CRC is verified exactly.
  const UInt64 finalSize = folder.UnpackSizes[folder.NumOut - 1];
  if (folder.CrcDefined && finalSize != 0 && finalSize <= kMaxPayloadDecode)
  {
    CByteBuffer plain;
    UInt64 plainSize = 0;
    if (!DecodeChain(key, packOffset, packSize, plain, plainSize))
      return false;
    return NPwCrypto::Crc32_Calc(plain, (size_t)plainSize) == (UInt32)folder.Crc;
  }

  // Otherwise only a well formed LZMA2 chunk chain behind the AES coder is a
  // usable signal; a bare AES stream cannot be told apart from random bytes.
  if (folder.NumCoders != 2 || folder.Coders[1].Id != k_Lzma2)
    return false;
  const UInt64 out = folder.UnpackSizes[1];
  if (out == 0)
    return false;
  if (!WalkChunks(key, packOffset, aesOut, out))
    return false;
  return CheckPadding(key, packOffset, aesOut, packSize);
}

/*
  Picks the first folder that starts with an AES coder and reads its
  properties. Returns false when no folder can be probed.
*/
bool CProbe::SelectFolder()
{
  for (unsigned i = 0; i < _streams.NumFolders; i++)
  {
    const CFolder &folder = _streams.Folders[i];
    if (folder.NumCoders == 0)
      continue;
    const CCoder &coder = folder.Coders[0];
    if (coder.Id != k_Aes)
      continue;
    if (coder.PropsSize < 2 || coder.PropsSize > 2 + 16 + 16)
      continue;

    const unsigned b0 = coder.Props[0];
    const unsigned b1 = coder.Props[1];
    const unsigned power = b0 & 0x3F;
    const unsigned saltSize = ((b0 >> 7) & 1) + (b1 >> 4);
    const unsigned ivSize = ((b0 >> 6) & 1) + (b1 & 0x0F);
    if (saltSize > 16 || ivSize != 16)
      continue;
    if (coder.PropsSize != 2 + saltSize + ivSize)
      continue;

    _saltSize = saltSize;
    _numCyclesPower = power;
    if (saltSize != 0)
      memcpy(_salt, coder.Props + 2, saltSize);
    memcpy(_iv, coder.Props + 2 + saltSize, 16);
    _folderIndex = i;
    _blockValid = false;
    return true;
  }
  return false;
}

bool CProbe::Open_Header(const Byte *nextHeader, size_t nextHeaderSize)
{
  CMemReader r(nextHeader + 1, nextHeaderSize - 1);
  bool parsed = false;

  for (;;)
  {
    unsigned id;
    if (!r.ReadByte(id))
      return false;
    if (id == kId_End)
      break;
    if (id == kId_ArchiveProperties)
    {
      // The records of this section are length prefixed and unused here.
      for (;;)
      {
        unsigned pid;
        if (!r.ReadByte(pid))
          return false;
        if (pid == kId_End)
          break;
        if (!r.SkipProperty())
          return false;
      }
      continue;
    }
    if (id == kId_AdditionalStreamsInfo)
    {
      CStreams scratch;
      memset(&scratch, 0, sizeof(scratch));
      if (!ParseStreams(r, &scratch))
        return false;
      continue;
    }
    if (id == kId_MainStreamsInfo)
    {
      if (!ParseStreams(r, &_streams))
        return false;
      parsed = true;
      continue;
    }
    if (id == kId_FilesInfo)
      break;
    return false;
  }

  if (!parsed)
    return false;
  if (!SelectFolder())
    return false;
  _mode = 2;
  return true;
}

bool CProbe::Open_EncodedHeader(const Byte *nextHeader, size_t nextHeaderSize)
{
  CMemReader r(nextHeader + 1, nextHeaderSize - 1);
  if (!ParseStreams(r, &_streams))
    return false;
  if (!SelectFolder())
    return false;
  _mode = 1;
  return true;
}

bool CProbe::Open(const FString &path)
{
  if (!_file.Open(path))
    return false;
  if (!_file.GetLength(_fileSize))
    return false;
  if (_fileSize < 32)
    return false;

  Byte start[32];
  if (!ReadAt(0, start, 32))
    return false;
  for (unsigned i = 0; i < kSignatureSize; i++)
    if (start[i] != kSignature[i])
      return false;
  if (start[6] != 0)
    return false;

  const UInt64 nextOffset = (UInt64)start[12] | ((UInt64)start[13] << 8)
      | ((UInt64)start[14] << 16) | ((UInt64)start[15] << 24)
      | ((UInt64)start[16] << 32) | ((UInt64)start[17] << 40)
      | ((UInt64)start[18] << 48) | ((UInt64)start[19] << 56);
  const UInt64 nextSize = (UInt64)start[20] | ((UInt64)start[21] << 8)
      | ((UInt64)start[22] << 16) | ((UInt64)start[23] << 24)
      | ((UInt64)start[24] << 32) | ((UInt64)start[25] << 40)
      | ((UInt64)start[26] << 48) | ((UInt64)start[27] << 56);
  _headerCrc = (UInt32)start[28] | ((UInt32)start[29] << 8)
      | ((UInt32)start[30] << 16) | ((UInt32)start[31] << 24);

  /*
    The start header carries a CRC over its own twenty bytes, so a damaged or
    unrelated file is rejected before anything is decrypted.
  */
  const UInt32 startCrc = (UInt32)start[8] | ((UInt32)start[9] << 8)
      | ((UInt32)start[10] << 16) | ((UInt32)start[11] << 24);
  if (NPwCrypto::Crc32_Calc(start + 12, 20) != startCrc)
    return false;

  if (nextSize < 2 || nextSize > kMaxNextHeaderSize)
    return false;

  CByteBuffer buf;
  buf.Alloc((size_t)nextSize);
  if (!ReadAt(32 + nextOffset, buf, (size_t)nextSize))
    return false;

  const Byte *p = buf;
  if (p[0] == kId_Header)
    return Open_Header(p, (size_t)nextSize);
  if (p[0] == kId_EncodedHeader)
    return Open_EncodedHeader(p, (size_t)nextSize);
  return false;
}

bool CProbe::Check(const Byte *password, size_t size)
{
  if (_mode == 0)
    return false;

  /*
    The chain hands out UTF-8, but 7-Zip derives its key from the UTF-16LE form
    of the password, so the candidate is converted back here.
  */
  UString unicode;
  if (!Convert_UTF8_Buf_To_Unicode((const char *)password, size, unicode))
    return false;

  const unsigned len = unicode.Len();
  CByteBuffer pw;
  pw.Alloc((size_t)len * 2);
  Byte *p = pw;
  for (unsigned i = 0; i < len; i++)
  {
    const unsigned c = (unsigned)unicode[i];
    p[i * 2] = (Byte)(c & 0xFF);
    p[i * 2 + 1] = (Byte)((c >> 8) & 0xFF);
  }

  Byte key[32];
  const bool keyOk = DeriveKey(p, (size_t)len * 2, key);
  NPwCrypto::SecureZero(p, (size_t)len * 2);
  if (!keyOk)
    return false;

  UInt64 packOffset = 0;
  UInt64 packSize = 0;
  bool result = false;
  if (PackRange(_folderIndex, packOffset, packSize))
  {
    _blockValid = false;
    try
    {
      result = (_mode == 1)
          ? CheckHeader(key, packOffset, packSize)
          : CheckPayload(key, packOffset, packSize);
    }
    catch (...)
    {
      result = false;
    }
  }
  NPwCrypto::SecureZero(key, sizeof(key));
  return result;
}

const char *CProbe::ModeName() const
{
  if (_mode == 1)
    return "7z-header";
  if (_mode == 2)
    return "7z-payload";
  return "7z";
}

}}