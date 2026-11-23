// PwFreeCrypto.h

#ifndef ZIP7_INC_PW_FREE_CRYPTO_H
#define ZIP7_INC_PW_FREE_CRYPTO_H

#include "../../../../Common/Common.h"
#include "../../../../Common/MyBuffer.h"

/*
  Small, self contained crypto set used by the password recovery module.

  PwFree deliberately does not reuse the 7-Zip crypto objects: those are
  compiled per target with different precompiled headers, and pulling them
  into a fourth object group would either duplicate symbols or force a
  project specific PCH. The primitives here are provided by the operating
  system (bcrypt) plus two tiny local helpers (CRC-32 and ZipCrypto), so the
  module links nothing beyond bcrypt.lib / winhttp.lib.
*/

namespace NPwFree {
namespace NPwCrypto {

/* ---- CRC-32 (IEEE 802.3, the one ZIP uses) ---- */

UInt32 Crc32_Update(UInt32 crc, const void *data, size_t size);
UInt32 Crc32_Calc(const void *data, size_t size);

/* ---- MD5 (archive fingerprint for the cloud lookup) ---- */

bool Md5_Calc(const void *data, size_t size, Byte *digest);

// Streaming MD5: archives can be hundreds of megabytes, so the fingerprint is
// computed while reading the file in chunks.
class CMd5Calc
{
  bool _ok;
#ifdef _WIN32
  void *_alg;
  void *_hash;
#endif
  CMd5Calc(const CMd5Calc &) = delete;
  CMd5Calc &operator=(const CMd5Calc &) = delete;

public:
  CMd5Calc();
  ~CMd5Calc();
  bool Update(const void *data, size_t size);
  bool Final(Byte *digest);
};

/* ---- HMAC-SHA1 / PBKDF2-HMAC-SHA1 (WinZip AES password check) ---- */

bool HmacSha1_Calc(const Byte *key, size_t keySize,
    const void *data, size_t size, Byte *digest);

// Streaming HMAC-SHA1 for large WinZip AES entries without loading all data in RAM.
class CHmacSha1Calc
{
  bool _ok;
#ifdef _WIN32
  void *_hash;
#endif
  CHmacSha1Calc(const CHmacSha1Calc &) = delete;
  CHmacSha1Calc &operator=(const CHmacSha1Calc &) = delete;

public:
  CHmacSha1Calc();
  ~CHmacSha1Calc();
  bool Init(const Byte *key, size_t keySize);
  bool Update(const void *data, size_t size);
  bool Final(Byte *digest);
};

bool Pbkdf2_HmacSha1(const Byte *password, size_t passwordSize,
    const Byte *salt, size_t saltSize,
    UInt32 numIterations, Byte *key, size_t keySize);

/* ---- DES-ECB (the vendor password dictionary is DES-ECB "DESKeyEn") ---- */

bool Des_Ecb_Decrypt(const Byte *key8, Byte *data, size_t size);

// Reusable DES-ECB key context to avoid re-opening CNG handles per dictionary line.
class CDesEcbDecryptor
{
  bool _ok;
#ifdef _WIN32
  void *_key;
#endif
  CDesEcbDecryptor(const CDesEcbDecryptor &) = delete;
  CDesEcbDecryptor &operator=(const CDesEcbDecryptor &) = delete;

public:
  CDesEcbDecryptor();
  ~CDesEcbDecryptor();
  bool Init(const Byte *key8);
  bool Decrypt(Byte *data, size_t size) const;
};

/* ---- AES (WinZip AES / 7z AES) ---- */

bool Aes_Cbc_Decrypt(const Byte *key, unsigned keySize,
    const Byte *iv, Byte *data, size_t size);

bool Aes_Ecb_Encrypt(const Byte *key, unsigned keySize, Byte *data, size_t size);

// AES-CTR keystream, counter block is incremented as a 128 bit big endian value.
void Aes_Ctr_Crypt(const Byte *key, unsigned keySize,
    const Byte *nonce16, Byte *data, size_t size);

// Overwrites a buffer in a way the compiler cannot drop.
void SecureZero(void *data, size_t size);

/* ---- Base64 (password dictionaries are base64 text) ---- */

// Ignores CR, LF, spaces. Returns false on an invalid character.
bool Base64_Decode(const char *src, size_t srcSize, CByteBuffer &dest);

/* ---- SHA-256 (7z AES key derivation) ---- */

bool Sha256_Calc(const void *data, size_t size, Byte *digest);

// 7-Zip's AES key derivation: one SHA-256 over 2^numCyclesPower copies of
// (salt || password || counter), the counter being written into the last eight
// bytes of each copy in little endian order.
bool Sha256_Kdf7z(const Byte *saltPassword, size_t size,
    unsigned numCyclesPower, Byte *key);

/* ---- ZipCrypto stream cipher ---- */

class CZipCipher
{
  UInt32 _key0;
  UInt32 _key1;
  UInt32 _key2;

  void UpdateKeys(Byte b);

public:
  CZipCipher();
  void SetPassword(const Byte *password, size_t size);
  Byte DecryptByte();
  void Decrypt(Byte *data, size_t size);
};

}}

#endif