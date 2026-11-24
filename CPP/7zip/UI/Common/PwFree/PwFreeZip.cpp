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
// entry cannot stall the callback.
static const UInt64 kMaxVerifySize = (UInt64)1 << 26; // 64 MB
// Small entry ciphertexts are cached in memory inside the probe to avoid
// re-reading from disk on every candidate password.
static const UInt64 kMaxCacheCipher = (UInt64)1 << 20; // 1 MB
static const UInt64 kMaxUnzipSize = (UInt64)64 << 20;  // 64 MB

static bool ReadFileAt(NFile::NIO::CInFile &file, UInt64 offset, void *data, size_t size)
{
  if (size == 0)
    return true;
  UInt64 newPos;
  if (!file.Seek(offset, newPos))
    return false;
  Byte *p = (Byte *)data;
  size_t done = 0;
  while (done < size)
  {
    UInt32 processed = 0;
    const size_t todo = (size - done > (1 << 20)) ? (size_t)(1 << 20) : (size - done);
    if (!file.ReadPart(p + done, (UInt32)todo, processed))
      return false;
    if (processed == 0)
      return false;
    done += processed;
  }
  return true;
}

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

  bool Open(const FString &path, UInt64 &fileSize)
  {
    fileSize = 0;
    if (!_file.Open(path))
      return false;
    return _file.GetLength(fileSize);
  }

  bool ReadAt(UInt64 offset, void *data, size_t size)
  {
    return ReadFileAt(_file, offset, data, size);
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

/*
  A self extracting ZIP stores the central directory behind a stub, and the
  offsets written inside it are relative to the archive body, not to the file.
  The base is recovered the way the ZIP handler does it: the position where the
  end record really sits minus the size and offset it records for the central
  directory. A plain archive yields zero.
*/
static bool FindEocd(CReader &r, UInt64 fileSize, UInt64 &cdOffset, UInt64 &cdSize,
    UInt64 &numEntries, UInt64 &base)
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

    const UInt64 eocdPos = fileSize - tailSize + i;
    base = 0;
    if (cdOffset == 0xFFFFFFFF || cdSize == 0xFFFFFFFF || numEntries == 0xFFFF)
    {
      // ZIP64: the real values live in the ZIP64 EOCD record.
      if (eocdPos >= 20)
      {
        Byte loc[20];
        if (r.ReadAt(eocdPos - 20, loc, 20) && Get32(loc) == kZip64LocatorSig)
        {
          /*
            The locator sits directly behind the ZIP64 end record, so its own
            position gives the base without trusting the offset it stores.
          */
          const UInt64 absEcd64 = eocdPos - 20 - 56;
          Byte rec[56];
          if (r.ReadAt(absEcd64, rec, 56) && Get32(rec) == kZip64EocdSig)
          {
            const UInt64 storedOffset = Get64(loc + 8);
            numEntries = Get64(rec + 32);
            cdSize = Get64(rec + 40);
            cdOffset = Get64(rec + 48);
            if (absEcd64 > storedOffset)
              base = absEcd64 - storedOffset;
            return true;
          }
        }
      }
    }
    // Non ZIP64: the end record is the last record of the archive body.
    const UInt64 cdEnd = cdOffset + cdSize;
    if (eocdPos > cdEnd)
      base = eocdPos - cdEnd;
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
  UInt64 fileSize = 0;
  if (!r.Open(path, fileSize))
    return false;

  info.ArcSize = fileSize;

  UInt64 cdOffset, cdSize, numEntries, base;
  if (!FindEocd(r, fileSize, cdOffset, cdSize, numEntries, base))
    return false;
  if (cdSize > ((UInt64)1 << 24))
    return false;
  if (cdOffset > fileSize || base > fileSize - cdOffset)
    return false;
  if (cdSize > fileSize - base - cdOffset)
    return false;

  // Offsets inside the archive body are relative to the base the stub adds.
  CByteBuffer cd((size_t)cdSize);
  if (!r.ReadAt(base + cdOffset, cd, (size_t)cdSize))
    return false;

  bool foundBest = false;
  CEntryInfo best;
  best.ArcSize = fileSize;

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

    const bool supportedZipCrypto = (!isAes && (realMethod == 0 || realMethod == 8));
    if ((flags & 1) != 0 && csize != 0 && (isAes || supportedZipCrypto))
    {
      if (!foundBest || csize < best.CSize)
      {
        Byte local[30];
        if (localOffset <= fileSize && base <= fileSize - localOffset
            && r.ReadAt(base + localOffset, local, 30)
            && Get32(local) == kLocalSig)
        {
          const size_t lNameLen = Get16(local + 26);
          const size_t lExtraLen = Get16(local + 28);
          const UInt64 dataOffset = base + localOffset + 30 + lNameLen + lExtraLen;
          if (dataOffset <= fileSize && csize <= fileSize - dataOffset)
          {
            best.Encrypted = true;
            best.IsWinZipAes = isAes;
            best.Method = realMethod;
            best.Flags = flags;
            best.AesStrength = aesStrength;
            best.Crc = crc;
            best.CSize = csize;
            best.USize = usize;
            best.DataOffset = dataOffset;
            best.CheckByte = ((flags & 8) != 0)
                ? (Byte)(Get16(local + 10) >> 8)
                : (Byte)(crc >> 24);
            foundBest = true;
            // A tiny entry (< 4 KB) is already fast enough; no need to scan further.
            if (csize <= 4096)
              break;
          }
        }
      }
    }

    pos += entrySize;
  }

  if (!foundBest)
    return false;
  info = best;
  return true;
}

bool FileMd5_Hex(const FString &path, AString &hex, volatile long *cancel)
{
  hex.Empty();
  NFile::NIO::CInFile file;
  if (!file.Open(path))
    return false;

  CMd5Calc md5;
  CByteBuffer buf(1 << 16);
  for (;;)
  {
    if (cancel && *cancel != 0)
    {
      file.Close();
      return false;
    }
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
  // The rest of the entry has to fit the buffer, so the arithmetic stays in
  // UInt64 and only the final size is narrowed (this file also builds as x86).
  const UInt64 avail = (UInt64)zipSize - offset;
  if (method == 8 && (csize == 0 || usize == 0))
    return Inflate_Raw_Grow(src, (size_t)avail, out, (size_t)kMaxUnzipSize);

  if (csize > avail)
    return false;

  if (method == 0)
  {
    if (csize > kMaxUnzipSize)
      return false;
    out.CopyFrom(src, (size_t)csize);
    return true;
  }
  if (method != 8)
    return false;

  // The vendor dictionaries have broken CRCs, so the declared output size is
  // trusted (bounded by kMaxUnzipSize) and the CRC is not checked at all.
  if (usize == 0 || usize > kMaxUnzipSize)
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

static unsigned AesSaltSize(unsigned strength)
{
  return (strength == 1) ? 8 : ((strength == 2) ? 12 : 16);
}

static unsigned AesKeySize(unsigned strength)
{
  return (strength == 1) ? 16 : ((strength == 2) ? 24 : 32);
}

CZipCryptoProbe::CZipCryptoProbe(): _hasCachedData(false), _ok(false)
{
  memset(_header, 0, sizeof(_header));
}

CZipCryptoProbe::~CZipCryptoProbe()
{
  _file.Close();
}

bool CZipCryptoProbe::Init(const FString &path, const CEntryInfo &info)
{
  _ok = false;
  _hasCachedData = false;
  _cachedData.Free();
  _file.Close();

  if (!info.Encrypted || info.IsWinZipAes || info.CSize < 12)
    return false;
  if (info.Method != 0 && info.Method != 8)
    return false;
  if (info.CSize - 12 > kMaxVerifySize)
    return false;
  if (info.Method == 8 && info.USize > kMaxVerifySize)
    return false;

  if (!_file.Open(path))
    return false;
  if (!ReadFileAt(_file, info.DataOffset, _header, 12))
  {
    _file.Close();
    return false;
  }

  _info = info;
  const UInt64 dataSize = info.CSize - 12;
  if (dataSize <= kMaxCacheCipher)
  {
    if (dataSize != 0)
    {
      _cachedData.Alloc((size_t)dataSize);
      if (!ReadFileAt(_file, info.DataOffset + 12, _cachedData, (size_t)dataSize))
      {
        _cachedData.Free();
        _file.Close();
        return false;
      }
    }
    _hasCachedData = true;
    _file.Close(); // no longer need the disk handle
  }

  _ok = true;
  return true;
}

bool CZipCryptoProbe::Check(const Byte *password, size_t passwordSize) const
{
  if (!_ok)
    return false;

  Byte header[12];
  memcpy(header, _header, 12);

  CZipCipher cipher;
  cipher.SetPassword(password, passwordSize);
  cipher.Decrypt(header, 12);

  if (header[11] != _info.CheckByte)
    return false;

  const size_t dataSize = (size_t)(_info.CSize - 12);
  CByteBuffer data(dataSize);
  if (dataSize != 0)
  {
    if (_hasCachedData)
      memcpy(data, _cachedData, dataSize);
    else
    {
      _fileCS.Enter();
      const bool readOk = ReadFileAt(_file, _info.DataOffset + 12, data, dataSize);
      _fileCS.Leave();
      if (!readOk)
        return false;
    }
    cipher.Decrypt(data, dataSize);
  }

  UInt32 crc = 0;
  if (_info.Method == 0)
  {
    crc = Crc32_Calc(data, dataSize);
  }
  else if (_info.Method == 8)
  {
    CByteBuffer out((size_t)_info.USize);
    if (!Inflate_Raw(data, dataSize, out, (size_t)_info.USize))
      return false;
    crc = Crc32_Calc(out, (size_t)_info.USize);
  }
  else
    return false;

  return crc == _info.Crc;
}

CWinZipAesProbe::CWinZipAesProbe():
    _saltSize(0), _keySize(0), _cipherOffset(0), _cipherSize(0),
    _hasCachedCipher(false), _ok(false)
{
  memset(_salt, 0, sizeof(_salt));
  memset(_verif, 0, sizeof(_verif));
  memset(_storedMac, 0, sizeof(_storedMac));
}

CWinZipAesProbe::~CWinZipAesProbe()
{
  _file.Close();
}

bool CWinZipAesProbe::Init(const FString &path, const CEntryInfo &info)
{
  _ok = false;
  _hasCachedCipher = false;
  _cachedCipher.Free();
  _file.Close();

  if (!info.Encrypted || !info.IsWinZipAes)
    return false;
  if (info.AesStrength < 1 || info.AesStrength > 3)
    return false;

  _saltSize = AesSaltSize(info.AesStrength);
  _keySize = AesKeySize(info.AesStrength);
  const UInt64 overhead = _saltSize + 2 + 10;
  if (info.CSize < overhead)
    return false;

  if (!_file.Open(path))
    return false;
  if (!ReadFileAt(_file, info.DataOffset, _salt, _saltSize)
      || !ReadFileAt(_file, info.DataOffset + _saltSize, _verif, 2)
      || !ReadFileAt(_file, info.DataOffset + info.CSize - 10, _storedMac, 10))
  {
    _file.Close();
    return false;
  }

  _info = info;
  _cipherOffset = info.DataOffset + _saltSize + 2;
  _cipherSize = info.CSize - overhead;

  if (_cipherSize <= kMaxCacheCipher)
  {
    if (_cipherSize != 0)
    {
      _cachedCipher.Alloc((size_t)_cipherSize);
      if (!ReadFileAt(_file, _cipherOffset, _cachedCipher, (size_t)_cipherSize))
      {
        _cachedCipher.Free();
        _file.Close();
        return false;
      }
    }
    _hasCachedCipher = true;
    _file.Close();
  }

  _ok = true;
  return true;
}

bool CWinZipAesProbe::Check(const Byte *password, size_t passwordSize) const
{
  if (!_ok)
    return false;

  Byte derived[2 * 32 + 2];
  const size_t derivedSize = 2 * _keySize + 2;
  if (!Pbkdf2_HmacSha1(password, passwordSize, _salt, _saltSize, 1000, derived, derivedSize))
    return false;

  const bool verifOk = (derived[2 * _keySize] == _verif[0] && derived[2 * _keySize + 1] == _verif[1]);
  if (!verifOk)
  {
    SecureZero(derived, sizeof(derived));
    return false;
  }

  Byte mac[20];
  bool macOk = false;
  if (_hasCachedCipher)
  {
    macOk = HmacSha1_Calc(derived + _keySize, _keySize,
        (_cipherSize != 0) ? (const void *)(const Byte *)_cachedCipher : (const void *)"",
        (size_t)_cipherSize, mac);
  }
  else
  {
    CHmacSha1Calc hmac;
    if (hmac.Init(derived + _keySize, _keySize))
    {
      CByteBuffer chunk(1 << 16);
      UInt64 remaining = _cipherSize;
      UInt64 pos = _cipherOffset;
      macOk = true;
      while (remaining != 0)
      {
        const size_t todo = (remaining > chunk.Size()) ? chunk.Size() : (size_t)remaining;
        _fileCS.Enter();
        const bool readOk = ReadFileAt(_file, pos, chunk, todo);
        _fileCS.Leave();
        if (!readOk || !hmac.Update(chunk, todo))
        {
          macOk = false;
          break;
        }
        pos += todo;
        remaining -= todo;
      }
      if (macOk)
        macOk = hmac.Final(mac);
    }
  }

  SecureZero(derived, sizeof(derived));
  return macOk && memcmp(mac, _storedMac, 10) == 0;
}

bool Verify_ZipCrypto(const FString &path, const CEntryInfo &info,
    const Byte *password, size_t passwordSize)
{
  CZipCryptoProbe probe;
  if (!probe.Init(path, info))
    return false;
  return probe.Check(password, passwordSize);
}

bool Verify_WinZipAes(const FString &path, const CEntryInfo &info,
    const Byte *password, size_t passwordSize)
{
  CWinZipAesProbe probe;
  if (!probe.Init(path, info))
    return false;
  return probe.Check(password, passwordSize);
}

}}
