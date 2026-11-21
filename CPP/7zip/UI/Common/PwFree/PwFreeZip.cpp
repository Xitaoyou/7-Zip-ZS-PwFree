// PwFreeZip.cpp

#include "PwFreeZip.h"

#include "PwFreeCrypto.h"
#include "PwFreeInflate.h"

#include "../../../../Windows/FileIO.h"

using namespace NWindows;

namespace NPwFree {
namespace NPwZip {

using namespace NPwCrypto;
using namespace NPwInflate;

static const UInt32 kEocdSig = 0x06054B50;
static const UInt32 kCentralSig = 0x02014B50;
static const UInt32 kLocalSig = 0x04034B50;
static const UInt32 kZip64EocdSig = 0x06064B50;
static const UInt32 kZip64LocatorSig = 0x07064B50;

// The CRC pass decrypts and inflates the entry; keep it bounded so a huge
// entry cannot stall the callback. Bigger entries still get the cheap check.
static const UInt64 kMaxVerifySize = (UInt64)1 << 26; // 64 MB

CEntryInfo::CEntryInfo():
    Encrypted(false),
    IsWinZipAes(false),
    Method(0),
    Flags(0),
    AesStrength(0),
    Crc(0),
    CSize(0),
    USize(0),
    DataOffset(0),
    ArcSize(0),
    CheckByte(0)
{
}

class CReader
{
  NFile::NIO::CInFile _file;

  CReader(const CReader &) = delete;
  CReader &operator=(const CReader &) = delete;

public:
  CReader() {}
  ~CReader() { _file.Close(); }

  bool Open(const FString &path)
  {
    if (!_file.Open(path))
      return false;
    UInt64 len;
    return _file.GetLength(len);
  }

  bool ReadAt(UInt64 offset, void *data, size_t size)
  {
    if (size == 0)
      return true;
    UInt64 newPos;
    if (!_file.Seek(offset, newPos))
      return false;
    Byte *p = (Byte *)data;
    size_t done = 0;
    while (done < size)
    {
      UInt32 processed = 0;
      const size_t todo = (size - done > (1 << 20)) ? (size_t)(1 << 20) : (size - done);
      if (!_file.ReadPart(p + done, (UInt32)todo, processed))
        return false;
      if (processed == 0)
        return false;
      done += processed;
    }
    return true;
  }
};

static UInt16 Get16(const Byte *p) { return (UInt16)(p[0] | ((UInt16)p[1] << 8)); }
static UInt32 Get32(const Byte *p)
{
  return (UInt32)p[0] | ((UInt32)p[1] << 8) | ((UInt32)p[2] << 16) | ((UInt32)p[3] << 24);
}
static UInt64 Get64(const Byte *p)
{
  return (UInt64)Get32(p) | ((UInt64)Get32(p + 4) << 32);
}

static bool FindEocd(CReader &r, UInt64 fileSize, UInt64 &cdOffset, UInt64 &cdSize, UInt64 &numEntries)
{
  const UInt64 tailMax = 65557; // 22 byte record + 65535 byte comment
  const UInt64 tailSize = (fileSize < tailMax) ? fileSize : tailMax;
  if (tailSize < 22)
    return false;

  CByteBuffer tail((size_t)tailSize);
  if (!r.ReadAt(fileSize - tailSize, tail, (size_t)tailSize))
    return false;

  for (size_t i = (size_t)tailSize - 22 + 1; i-- > 0;)
  {
    if (Get32(tail + i) != kEocdSig)
      continue;
    const Byte *p = tail + i;
    numEntries = Get16(p + 10);
    cdSize = Get32(p + 12);
    cdOffset = Get32(p + 16);

    if (cdOffset == 0xFFFFFFFF || cdSize == 0xFFFFFFFF || numEntries == 0xFFFF)
    {
      // ZIP64: the real values live in the ZIP64 EOCD record.
      const UInt64 eocdPos = fileSize - tailSize + i;
      if (eocdPos >= 20)
      {
        Byte loc[20];
        if (r.ReadAt(eocdPos - 20, loc, 20) && Get32(loc) == kZip64LocatorSig)
        {
          const UInt64 zip64Pos = Get64(loc + 8);
          Byte rec[56];
          if (r.ReadAt(zip64Pos, rec, 56) && Get32(rec) == kZip64EocdSig)
          {
            numEntries = Get64(rec + 32);
            cdSize = Get64(rec + 40);
            cdOffset = Get64(rec + 48);
          }
        }
      }
    }
    return true;
  }
  return false;
}

static bool ParseAesExtra(const Byte *extra, size_t extraSize, unsigned &strength, unsigned &method)
{
  size_t pos = 0;
  while (pos + 4 <= extraSize)
  {
    const UInt16 id = Get16(extra + pos);
    const size_t size = Get16(extra + pos + 2);
    if (pos + 4 + size > extraSize)
      return false;
    if (id == 0x9901 && size >= 7)
    {
      // version(2) vendor(2) strength(1) method(2)
      strength = extra[pos + 8];
      method = Get16(extra + pos + 9);
      return (strength >= 1 && strength <= 3);
    }
    pos += 4 + size;
  }
  return false;
}

bool Read_Info(const FString &path, CEntryInfo &info)
{
  info = CEntryInfo();

  CReader r;
  if (!r.Open(path))
    return false;

  NFile::NIO::CInFile probe;
  UInt64 fileSize = 0;
  if (!probe.Open(path) || !probe.GetLength(fileSize))
    return false;
  probe.Close();

  info.ArcSize = fileSize;

  UInt64 cdOffset, cdSize, numEntries;
  if (!FindEocd(r, fileSize, cdOffset, cdSize, numEntries))
    return false;
  if (cdSize > ((UInt64)1 << 24) || cdOffset + cdSize > fileSize)
    return false;

  CByteBuffer cd((size_t)cdSize);
  if (!r.ReadAt(cdOffset, cd, (size_t)cdSize))
    return false;

  size_t pos = 0;
  const size_t cdLen = (size_t)cdSize;
  for (UInt64 n = 0; n < numEntries && pos + 46 <= cdLen; n++)
  {
    const Byte *p = cd + pos;
    if (Get32(p) != kCentralSig)
      break;
    const unsigned flags = Get16(p + 8);
    const unsigned method = Get16(p + 10);
    const UInt32 crc = Get32(p + 16);
    UInt64 csize = Get32(p + 20);
    UInt64 usize = Get32(p + 24);
    const size_t nameLen = Get16(p + 28);
    const size_t extraLen = Get16(p + 30);
    const size_t commentLen = Get16(p + 32);
    UInt64 localOffset = Get32(p + 42);
    const size_t entrySize = 46 + nameLen + extraLen + commentLen;
    if (pos + entrySize > cdLen)
      break;

    const Byte *extra = p + 46 + nameLen;
    unsigned aesStrength = 0;
    unsigned realMethod = method;
    const bool isAes = (method == 99) && ParseAesExtra(extra, extraLen, aesStrength, realMethod);

    // ZIP64 extended information: replace the 0xFFFFFFFF placeholders.
    if (csize == 0xFFFFFFFF || usize == 0xFFFFFFFF || localOffset == 0xFFFFFFFF)
    {
      size_t e = 0;
      while (e + 4 <= extraLen)
      {
        const UInt16 id = Get16(extra + e);
        const size_t size = Get16(extra + e + 2);
        if (e + 4 + size > extraLen)
          break;
        if (id == 0x0001)
        {
          size_t q = e + 4;
          if (usize == 0xFFFFFFFF && q + 8 <= e + 4 + size) { usize = Get64(extra + q); q += 8; }
          if (csize == 0xFFFFFFFF && q + 8 <= e + 4 + size) { csize = Get64(extra + q); q += 8; }
          if (localOffset == 0xFFFFFFFF && q + 8 <= e + 4 + size) { localOffset = Get64(extra + q); }
          break;
        }
        e += 4 + size;
      }
    }

    if ((flags & 1) != 0 && csize != 0)
    {
      info.Encrypted = true;
      info.IsWinZipAes = isAes;
      info.Method = realMethod;
      info.Flags = flags;
      info.AesStrength = aesStrength;
      info.Crc = crc;
      info.CSize = csize;
      info.USize = usize;

      Byte local[30];
      if (!r.ReadAt(localOffset, local, 30))
        return false;
      if (Get32(local) != kLocalSig)
        return false;
      const size_t lNameLen = Get16(local + 26);
      const size_t lExtraLen = Get16(local + 28);
      info.DataOffset = localOffset + 30 + lNameLen + lExtraLen;

      // Streamed entries (flag 8) have no CRC in the local header, the DOS
      // time is used instead. Both are 8 bit checks, the CRC pass decides.
      info.CheckByte = ((flags & 8) != 0)
          ? (Byte)(Get16(local + 10) >> 8)
          : (Byte)(crc >> 24);
      return true;
    }

    pos += entrySize;
  }
  return false;
}

bool FileMd5_Hex(const FString &path, AString &hex)
{
  hex.Empty();
  NFile::NIO::CInFile file;
  if (!file.Open(path))
    return false;

  CMd5Calc md5;
  CByteBuffer buf(1 << 16);
  for (;;)
  {
    UInt32 processed = 0;
    if (!file.ReadPart((Byte *)buf, (UInt32)buf.Size(), processed))
    {
      file.Close();
      return false;
    }
    if (processed == 0)
      break;
    if (!md5.Update((const Byte *)buf, processed))
    {
      file.Close();
      return false;
    }
  }
  file.Close();

  Byte digest[16];
  if (!md5.Final(digest))
    return false;

  static const char kHex[] = "0123456789abcdef";
  for (unsigned i = 0; i < 16; i++)
  {
    hex += kHex[digest[i] >> 4];
    hex += kHex[digest[i] & 0xF];
  }
  return true;
}

bool Unzip_FirstEntry(const Byte *zipData, size_t zipSize, CByteBuffer &out)
{
  out.Free();
  if (zipSize < 30 || Get32(zipData) != kLocalSig)
    return false;

  const unsigned method = Get16(zipData + 8);
  UInt64 csize = Get32(zipData + 18);
  UInt64 usize = Get32(zipData + 22);
  const size_t nameLen = Get16(zipData + 26);
  const size_t extraLen = Get16(zipData + 28);
  const UInt64 offset = 30 + (UInt64)nameLen + extraLen;
  if (offset > zipSize)
    return false;

  // ZIP64 placeholders in a local header: look for the extra field.
  if (csize == 0xFFFFFFFF || usize == 0xFFFFFFFF)
  {
    size_t e = 0;
    const Byte *extra = zipData + 30 + nameLen;
    while (e + 4 <= extraLen)
    {
      const UInt16 id = Get16(extra + e);
      const size_t size = Get16(extra + e + 2);
      if (e + 4 + size > extraLen)
        break;
      if (id == 0x0001)
      {
        size_t q = e + 4;
        if (usize == 0xFFFFFFFF && q + 8 <= e + 4 + size) { usize = Get64(extra + q); q += 8; }
        if (csize == 0xFFFFFFFF && q + 8 <= e + 4 + size) { csize = Get64(extra + q); }
        break;
      }
      e += 4 + size;
    }
  }

  const Byte *src = zipData + offset;

  // Streamed entries (flag bit 3) carry neither size in the local header; the
  // vendor dictionaries are written that way, so the sizes are unknown and the
  // decoder has to grow its output buffer.
  if (method == 8 && (csize == 0 || usize == 0))
    return Inflate_Raw_Grow(src, zipSize - offset, out);

  if (csize > zipSize - offset)
    return false;

  if (method == 0)
  {
    out.CopyFrom(src, (size_t)csize);
    return true;
  }
  if (method != 8)
    return false;

  // The vendor dictionaries have broken CRCs, so the declared output size is
  // trusted and the CRC is not checked at all.
  if (usize == 0)
    return false;
  out.Alloc((size_t)usize);
  return Inflate_Raw(src, (size_t)csize, out, (size_t)usize);
}

bool Read_FirstEntry(const FString &path, CByteBuffer &data)
{
  NFile::NIO::CInFile file;
  if (!file.Open(path))
    return false;
  UInt64 len = 0;
  if (!file.GetLength(len) || len == 0 || len > ((UInt64)1 << 28))
  {
    file.Close();
    return false;
  }
  CByteBuffer buf((size_t)len);
  UInt64 pos = 0;
  while (pos < len)
  {
    UInt32 processed = 0;
    const size_t todo = (size_t)((len - pos > (1 << 20)) ? (1 << 20) : (len - pos));
    if (!file.ReadPart((Byte *)buf + (size_t)pos, (UInt32)todo, processed) || processed == 0)
    {
      file.Close();
      return false;
    }
    pos += processed;
  }
  file.Close();
  return Unzip_FirstEntry(buf, (size_t)len, data);
}

bool Verify_ZipCrypto(const FString &path, const CEntryInfo &info,
    const Byte *password, size_t passwordSize)
{
  if (!info.Encrypted || info.IsWinZipAes)
    return false;
  if (info.CSize < 12)
    return false;

  CReader r;
  if (!r.Open(path))
    return false;

  Byte header[12];
  if (!r.ReadAt(info.DataOffset, header, 12))
    return false;

  CZipCipher cipher;
  cipher.SetPassword(password, passwordSize);
  cipher.Decrypt(header, 12);

  // The last decrypted byte must match the high byte of the CRC, or the high
  // byte of the DOS time when a data descriptor is used. One wrong password
  // in 256 gets past this line, which is why the CRC pass below is mandatory.
  if (header[11] != info.CheckByte)
    return false;

  if (info.CSize - 12 > kMaxVerifySize)
    return false; // too large to confirm, let a slower verifier decide

  const size_t dataSize = (size_t)(info.CSize - 12);
  CByteBuffer data(dataSize);
  if (dataSize != 0)
  {
    if (!r.ReadAt(info.DataOffset + 12, data, dataSize))
      return false;
    cipher.Decrypt(data, dataSize);
  }

  UInt32 crc = 0;
  if (info.Method == 0)
  {
    crc = Crc32_Calc(data, dataSize);
  }
  else if (info.Method == 8)
  {
    if (info.USize > kMaxVerifySize)
      return false;
    CByteBuffer out((size_t)info.USize);
    if (!Inflate_Raw(data, dataSize, out, (size_t)info.USize))
      return false;
    crc = Crc32_Calc(out, (size_t)info.USize);
  }
  else
    return false;

  return crc == info.Crc;
}

static unsigned AesSaltSize(unsigned strength)
{
  return (strength == 1) ? 8 : ((strength == 2) ? 12 : 16);
}

static unsigned AesKeySize(unsigned strength)
{
  return (strength == 1) ? 16 : ((strength == 2) ? 24 : 32);
}

bool Verify_WinZipAes(const FString &path, const CEntryInfo &info,
    const Byte *password, size_t passwordSize)
{
  if (!info.Encrypted || !info.IsWinZipAes)
    return false;

  const unsigned saltSize = AesSaltSize(info.AesStrength);
  const unsigned keySize = AesKeySize(info.AesStrength);
  const unsigned verifSize = 2;
  const unsigned macSize = 10;
  const UInt64 overhead = saltSize + verifSize + macSize;
  if (info.CSize <= overhead)
    return false;

  CReader r;
  if (!r.Open(path))
    return false;

  Byte salt[16];
  Byte verif[2];
  if (!r.ReadAt(info.DataOffset, salt, saltSize))
    return false;
  if (!r.ReadAt(info.DataOffset + saltSize, verif, verifSize))
    return false;

  Byte derived[2 * 32 + 2];
  const size_t derivedSize = 2 * keySize + verifSize;
  if (!Pbkdf2_HmacSha1(password, passwordSize, salt, saltSize, 1000, derived, derivedSize))
    return false;

  const bool verifOk = (derived[2 * keySize] == verif[0] && derived[2 * keySize + 1] == verif[1]);
  if (!verifOk)
    return false;

  // 16 bit is not enough on its own: authenticate the ciphertext with the MAC.
  const UInt64 cipherSize = info.CSize - overhead;
  if (cipherSize > ((UInt64)1 << 24))
    return false; // too large for the MAC pass; the 16 bit check stands alone

  const size_t bufSize = (size_t)cipherSize;
  CByteBuffer buf(bufSize);
  if (bufSize != 0 && !r.ReadAt(info.DataOffset + saltSize + verifSize, buf, bufSize))
    return false;

  Byte mac[20];
  if (!HmacSha1_Calc(derived + keySize, keySize, buf, bufSize, mac))
    return false;

  Byte stored[10];
  if (!r.ReadAt(info.DataOffset + info.CSize - macSize, stored, macSize))
    return false;

  return memcmp(mac, stored, macSize) == 0;
}

}}
