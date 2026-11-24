// PwFreeZip.h

#ifndef ZIP7_INC_PW_FREE_ZIP_H
#define ZIP7_INC_PW_FREE_ZIP_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyBuffer.h"
#include "../../../../Common/MyString.h"
#include "../../../../Windows/FileIO.h"
#include "../../../../Windows/Synchronization.h"

/*
  Minimal ZIP reader used to verify a candidate password without opening the
  archive through the 7-Zip handlers.

  Why a separate reader: the password callback happens while the archive is
  being opened, so there is no IInArchive to ask. Re-opening the archive for
  every candidate costs 20..300 checks per second; reading the 12 byte
  ZipCrypto header costs 45.000 per second, and the CRC pass over the entry
  is still an order of magnitude cheaper than a full re-open.
*/

namespace NPwFree {
namespace NPwZip {

struct CEntryInfo
{
  bool Encrypted;
  bool IsWinZipAes;
  unsigned Method;
  unsigned Flags;
  unsigned AesStrength;   // 1 = AES-128, 2 = AES-192, 3 = AES-256, 0 = not AES
  UInt32 Crc;
  UInt64 CSize;
  UInt64 USize;
  UInt64 DataOffset;      // absolute offset of the first byte of entry data
  UInt64 ArcSize;
  Byte CheckByte;         // expected value of the last decrypted header byte

  CEntryInfo();
};

// Lower case hex MD5 of the whole archive, the fingerprint the cloud lookup uses.
bool FileMd5_Hex(const FString &path, AString &hex, volatile long *cancel = NULL);

/*
  Reads and inflates the first entry of a ZIP archive (stored or deflated,
  not encrypted). Used for the vendor password dictionaries, which ship as a
  ZIP with a single member.
*/
bool Read_FirstEntry(const FString &path, CByteBuffer &data);

// Same, from a memory buffer (the vendor dictionaries are fetched, not saved).
bool Unzip_FirstEntry(const Byte *zipData, size_t zipSize, CByteBuffer &data);

// Locates the smallest encrypted entry of a ZIP archive.
// Returns false when the file is not a ZIP or has no encrypted entry.
bool Read_Info(const FString &path, CEntryInfo &info);

/*
  Cached ZipCrypto probe: pre-reads the 12-byte encryption header (and small
  entry ciphertext) once so candidate checks avoid per-candidate file opens.
*/
class CZipCryptoProbe
{
  CEntryInfo _info;
  mutable NWindows::NFile::NIO::CInFile _file;
  mutable NWindows::NSynchronization::CCriticalSection _fileCS;
  Byte _header[12];
  CByteBuffer _cachedData;
  bool _hasCachedData;
  bool _ok;

  CZipCryptoProbe(const CZipCryptoProbe &) = delete;
  CZipCryptoProbe &operator=(const CZipCryptoProbe &) = delete;

public:
  CZipCryptoProbe();
  ~CZipCryptoProbe();
  bool Init(const FString &path, const CEntryInfo &info);
  bool Check(const Byte *password, size_t passwordSize) const;
};

/*
  Cached WinZip AES probe: pre-reads salt, 2-byte verifier, stored 10-byte MAC,
  and small ciphertext once; streams larger entries through HMAC-SHA1 without
  a 16 MB cap.
*/
class CWinZipAesProbe
{
  CEntryInfo _info;
  mutable NWindows::NFile::NIO::CInFile _file;
  mutable NWindows::NSynchronization::CCriticalSection _fileCS;
  unsigned _saltSize;
  unsigned _keySize;
  Byte _salt[16];
  Byte _verif[2];
  Byte _storedMac[10];
  UInt64 _cipherOffset;
  UInt64 _cipherSize;
  CByteBuffer _cachedCipher;
  bool _hasCachedCipher;
  bool _ok;

  CWinZipAesProbe(const CWinZipAesProbe &) = delete;
  CWinZipAesProbe &operator=(const CWinZipAesProbe &) = delete;

public:
  CWinZipAesProbe();
  ~CWinZipAesProbe();
  bool Init(const FString &path, const CEntryInfo &info);
  bool Check(const Byte *password, size_t passwordSize) const;
};

/*
  ZipCrypto: decrypt the 12 byte header, compare the last byte with the check
  byte, then decrypt the whole entry and compare its CRC-32. The header check
  alone accepts roughly one wrong password in 256, so the CRC pass is not
  optional.
*/
bool Verify_ZipCrypto(const FString &path, const CEntryInfo &info,
    const Byte *password, size_t passwordSize);

/*
  WinZip AES: PBKDF2-HMAC-SHA1 (1000 rounds) gives the key, the 2 password
  verification bytes and the MAC key; the entry is authentic when the stored
  10 byte HMAC-SHA1 matches.
*/
bool Verify_WinZipAes(const FString &path, const CEntryInfo &info,
    const Byte *password, size_t passwordSize);

}}

#endif
