// PwFree7z.h

#ifndef ZIP7_INC_PW_FREE_7Z_H
#define ZIP7_INC_PW_FREE_7Z_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyBuffer.h"
#include "../../../../Common/MyString.h"
#include "../../../../Windows/FileIO.h"

/*
  Password probe for the 7z container.

  Two layouts have to be told apart:

    header in clear, payload encrypted
      The next header is readable, so the coding chain of every folder is
      known. The probe picks the first folder that starts with an AES coder and
      verifies the candidate against it. When the folder stores a CRC and its
      stream is small enough, the stream is decrypted and decoded completely
      and the CRC decides. Otherwise the LZMA2 chunk chain is walked without
      decoding: the chunk sizes have to fit, the end marker has to land exactly
      on the AES output boundary, the chunks have to add up to the unpacked size
      from the header and the AES padding behind the output has to be zero. The
      walk decrypts one AES block per chunk header, never the payload.

    header encrypted (-mhe=on)
      The next header is itself a packed stream: AES, optionally followed by
      LZMA. The archive stores the CRC of the decoded header, so the candidate
      is verified exactly: decrypt, decode, compare the CRC.

  Both paths only read what they need, so the cost per candidate stays in the
  low milliseconds even for a multi gigabyte archive.
*/

namespace NPwFree {
namespace NPwSevenZ {

struct CCoder
{
  UInt64 Id;
  unsigned PropsSize;
  Byte Props[64];
};

struct CFolder
{
  unsigned NumCoders;
  CCoder Coders[4];
  unsigned NumOut;
  unsigned NumPackStreams;
  UInt64 UnpackSizes[4];
  bool CrcDefined;
  UInt64 Crc;
};

struct CStreams
{
  UInt64 PackPos;
  unsigned NumPackStreams;
  UInt64 PackSizes[64];
  unsigned NumFolders;
  CFolder Folders[16];
};

// True when the file starts with the 7z signature.
bool IsSevenZ(const FString &path);

// Created once per archive; Check() may be called for every candidate.
class CProbe
{
  NWindows::NFile::NIO::CInFile _file;
  UInt64 _fileSize;

  // 0 - nothing to probe, 1 - encrypted header, 2 - encrypted payload
  unsigned _mode;

  CStreams _streams;
  unsigned _folderIndex;

  Byte _iv[16];
  Byte _salt[16];
  unsigned _saltSize;
  unsigned _numCyclesPower;

  // CRC the archive stores for its decoded next header.
  UInt32 _headerCrc;

  // Cache of the last decrypted AES block, addressed by its offset inside the
  // packed stream, so walking a chunk chain costs one block per chunk header.
  UInt64 _blockPos;
  bool _blockValid;
  Byte _block[16];

  bool ReadAt(UInt64 offset, void *data, size_t size);
  bool DeriveKey(const Byte *password, size_t size, Byte *key) const;

  bool DecryptRange(const Byte *key, UInt64 packOffset, UInt64 posOffset, Byte *dest, size_t size);
  bool PlainByte(const Byte *key, UInt64 packOffset, UInt64 pos, unsigned &value);

  bool PackRange(unsigned folderIndex, UInt64 &offset, UInt64 &size) const;
  bool WalkChunks(const Byte *key, UInt64 packOffset,
      UInt64 inputSize, UInt64 outputSize);
  bool CheckPadding(const Byte *key, UInt64 packOffset,
      UInt64 inputSize, UInt64 packSize);
  bool DecodeChain(const Byte *key, UInt64 packOffset, UInt64 packSize,
      CByteBuffer &dest, UInt64 &destSize);

  bool CheckHeader(const Byte *key, UInt64 packOffset, UInt64 packSize);
  bool CheckPayload(const Byte *key, UInt64 packOffset, UInt64 packSize);

  bool SelectFolder();
  bool Open_Header(const Byte *nextHeader, size_t nextHeaderSize);
  bool Open_EncodedHeader(const Byte *nextHeader, size_t nextHeaderSize);

  CProbe(const CProbe &) = delete;
  CProbe &operator=(const CProbe &) = delete;

public:
  CProbe();
  ~CProbe();

  // Parses the structural information of the archive. Returns false when the
  // archive cannot be probed (not 7z, corrupt header, coder chain without a
  // check that tells the right password from a wrong one).
  bool Open(const FString &path);

  // True when the password decrypts the probed stream.
  bool Check(const Byte *password, size_t size);

  const char *ModeName() const;
};

}}

#endif