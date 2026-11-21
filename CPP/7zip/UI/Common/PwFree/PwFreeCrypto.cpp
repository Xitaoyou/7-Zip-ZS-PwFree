// PwFreeCrypto.cpp

#include "PwFreeCrypto.h"

#ifdef _WIN32
#include <bcrypt.h>
#endif

namespace NPwFree {
namespace NPwCrypto {

/* ---------------- CRC-32 ---------------- */

static UInt32 g_CrcTable[256];

static struct CCrcTableInit
{
  CCrcTableInit()
  {
    for (UInt32 i = 0; i < 256; i++)
    {
      UInt32 r = i;
      for (unsigned j = 0; j < 8; j++)
        r = (r >> 1) ^ (0xEDB88320u & (0u - (r & 1u)));
      g_CrcTable[i] = r;
    }
  }
} g_CrcTableInit;

UInt32 Crc32_Update(UInt32 crc, const void *data, size_t size)
{
  const Byte *p = (const Byte *)data;
  while (size-- != 0)
    crc = g_CrcTable[((unsigned)crc ^ (unsigned)(*p++)) & 0xFF] ^ (crc >> 8);
  return crc;
}

UInt32 Crc32_Calc(const void *data, size_t size)
{
  return Crc32_Update(0xFFFFFFFFu, data, size) ^ 0xFFFFFFFFu;
}

#ifdef _WIN32

/* ---------------- bcrypt helpers ---------------- */

class CAlgProvider
{
  BCRYPT_ALG_HANDLE _handle;

  CAlgProvider(const CAlgProvider &) = delete;
  CAlgProvider &operator=(const CAlgProvider &) = delete;

public:
  CAlgProvider(): _handle(NULL) {}
  ~CAlgProvider() { if (_handle) BCryptCloseAlgorithmProvider(_handle, 0); }

  bool Open(LPCWSTR algId, ULONG flags)
  {
    return BCryptOpenAlgorithmProvider(&_handle, algId, NULL, flags) >= 0;
  }
  BCRYPT_ALG_HANDLE Handle() const { return _handle; }
};

class CHashHandle
{
  BCRYPT_HASH_HANDLE _handle;

  CHashHandle(const CHashHandle &) = delete;
  CHashHandle &operator=(const CHashHandle &) = delete;

public:
  CHashHandle(): _handle(NULL) {}
  ~CHashHandle() { if (_handle) BCryptDestroyHash(_handle); }

  bool Create(BCRYPT_ALG_HANDLE alg, const Byte *secret, size_t secretSize)
  {
    return BCryptCreateHash(alg, &_handle, NULL, 0,
        (PUCHAR)secret, (ULONG)secretSize, 0) >= 0;
  }
  bool Add(const void *data, size_t size)
  {
    if (size == 0)
      return true;
    return BCryptHashData(_handle, (PUCHAR)data, (ULONG)size, 0) >= 0;
  }
  bool Finish(Byte *digest, size_t digestSize)
  {
    return BCryptFinishHash(_handle, digest, (ULONG)digestSize, 0) >= 0;
  }
};

static bool Hash_Calc(LPCWSTR algId, ULONG flags,
    const Byte *secret, size_t secretSize,
    const void *data, size_t size, Byte *digest, size_t digestSize)
{
  CAlgProvider prov;
  if (!prov.Open(algId, flags))
    return false;
  CHashHandle hash;
  if (!hash.Create(prov.Handle(), secret, secretSize))
    return false;
  if (!hash.Add(data, size))
    return false;
  return hash.Finish(digest, digestSize);
}

#endif // _WIN32

/* ---------------- MD5 ---------------- */

bool Md5_Calc(const void *data, size_t size, Byte *digest)
{
#ifdef _WIN32
  return Hash_Calc(BCRYPT_MD5_ALGORITHM, 0, NULL, 0, data, size, digest, 16);
#else
  UNUSED_VAR(data)
  UNUSED_VAR(size)
  UNUSED_VAR(digest)
  return false;
#endif
}

CMd5Calc::CMd5Calc(): _ok(false)
{
#ifdef _WIN32
  _alg = NULL;
  _hash = NULL;
  BCRYPT_ALG_HANDLE alg = NULL;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, NULL, 0) < 0)
    return;
  BCRYPT_HASH_HANDLE hash = NULL;
  if (BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) < 0)
  {
    BCryptCloseAlgorithmProvider(alg, 0);
    return;
  }
  _alg = alg;
  _hash = hash;
  _ok = true;
#endif
}

CMd5Calc::~CMd5Calc()
{
#ifdef _WIN32
  if (_hash)
    BCryptDestroyHash((BCRYPT_HASH_HANDLE)_hash);
  if (_alg)
    BCryptCloseAlgorithmProvider((BCRYPT_ALG_HANDLE)_alg, 0);
#endif
}

bool CMd5Calc::Update(const void *data, size_t size)
{
#ifdef _WIN32
  if (!_ok || size == 0)
    return _ok;
  return BCryptHashData((BCRYPT_HASH_HANDLE)_hash, (PUCHAR)data, (ULONG)size, 0) >= 0;
#else
  UNUSED_VAR(data) UNUSED_VAR(size)
  return false;
#endif
}

bool CMd5Calc::Final(Byte *digest)
{
#ifdef _WIN32
  if (!_ok)
    return false;
  _ok = false;
  return BCryptFinishHash((BCRYPT_HASH_HANDLE)_hash, digest, 16, 0) >= 0;
#else
  UNUSED_VAR(digest)
  return false;
#endif
}

/* ---------------- HMAC-SHA1 / PBKDF2 ---------------- */

bool HmacSha1_Calc(const Byte *key, size_t keySize,
    const void *data, size_t size, Byte *digest)
{
#ifdef _WIN32
  return Hash_Calc(BCRYPT_SHA1_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG,
      key, keySize, data, size, digest, 20);
#else
  UNUSED_VAR(key)
  UNUSED_VAR(keySize)
  UNUSED_VAR(data)
  UNUSED_VAR(size)
  UNUSED_VAR(digest)
  return false;
#endif
}

bool Pbkdf2_HmacSha1(const Byte *password, size_t passwordSize,
    const Byte *salt, size_t saltSize,
    UInt32 numIterations, Byte *key, size_t keySize)
{
  if (numIterations == 0 || keySize == 0)
    return false;

  Byte u[20];
  Byte t[20];

  Byte saltBuf[64];
  if (saltSize > sizeof(saltBuf) - 4)
    return false;

  UInt32 blockIndex = 1;
  size_t offset = 0;

  while (offset < keySize)
  {
    for (size_t i = 0; i < saltSize; i++)
      saltBuf[i] = salt[i];
    saltBuf[saltSize + 0] = (Byte)(blockIndex >> 24);
    saltBuf[saltSize + 1] = (Byte)(blockIndex >> 16);
    saltBuf[saltSize + 2] = (Byte)(blockIndex >> 8);
    saltBuf[saltSize + 3] = (Byte)(blockIndex);

    if (!HmacSha1_Calc(password, passwordSize, saltBuf, saltSize + 4, u))
      return false;

    for (unsigned i = 0; i < 20; i++)
      t[i] = u[i];

    for (UInt32 iter = 1; iter < numIterations; iter++)
    {
      if (!HmacSha1_Calc(password, passwordSize, u, 20, u))
        return false;
      for (unsigned i = 0; i < 20; i++)
        t[i] = (Byte)(t[i] ^ u[i]);
    }

    size_t todo = keySize - offset;
    if (todo > 20)
      todo = 20;
    for (size_t i = 0; i < todo; i++)
      key[offset + i] = t[i];

    offset += todo;
    blockIndex++;
  }

  Z7_memset_0_ARRAY(u);
  Z7_memset_0_ARRAY(t);
  Z7_memset_0_ARRAY(saltBuf);
  return true;
}

/* ---------------- DES-ECB ---------------- */

bool Des_Ecb_Decrypt(const Byte *key8, Byte *data, size_t size)
{
  if ((size & 7) != 0)
    return false;
  if (size == 0)
    return true;

#ifdef _WIN32
  CAlgProvider prov;
  if (!prov.Open(BCRYPT_DES_ALGORITHM, 0))
    return false;

  BCRYPT_KEY_HANDLE key = NULL;
  if (BCryptGenerateSymmetricKey(prov.Handle(), &key, NULL, 0,
      (PUCHAR)key8, 8, 0) < 0)
    return false;

  // CNG defaults to CBC for block ciphers, and a zero IV would silently turn
  // this into "first block decrypts, the rest is noise". The vendor payload is
  // plain ECB, which has to be selected explicitly.
  if (BCryptSetProperty(key, BCRYPT_CHAINING_MODE,
      (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
      (ULONG)((wcslen(BCRYPT_CHAIN_MODE_ECB) + 1) * sizeof(wchar_t)), 0) < 0)
  {
    BCryptDestroyKey(key);
    return false;
  }

  ULONG done = 0;
  const NTSTATUS res = BCryptDecrypt(key, data, (ULONG)size, NULL, NULL, 0,
      data, (ULONG)size, &done, 0);
  BCryptDestroyKey(key);
  return res >= 0 && done == (ULONG)size;
#else
  UNUSED_VAR(key8)
  UNUSED_VAR(data)
  UNUSED_VAR(size)
  return false;
#endif
}

/* ---------------- AES ---------------- */

#ifdef _WIN32

class CAesKey
{
  BCRYPT_KEY_HANDLE _handle;

  CAesKey(const CAesKey &) = delete;
  CAesKey &operator=(const CAesKey &) = delete;

public:
  CAesKey(): _handle(NULL) {}
  ~CAesKey() { if (_handle) BCryptDestroyKey(_handle); }
  BCRYPT_KEY_HANDLE Handle() const { return _handle; }

  bool Create(BCRYPT_ALG_HANDLE alg, const Byte *key, unsigned keySize, LPCWSTR mode)
  {
    if (BCryptGenerateSymmetricKey(alg, &_handle, NULL, 0,
        (PUCHAR)key, keySize, 0) < 0)
      return false;
    if (mode)
    {
      if (BCryptSetProperty(_handle, BCRYPT_CHAINING_MODE,
          (PUCHAR)mode, (ULONG)((wcslen(mode) + 1) * sizeof(wchar_t)), 0) < 0)
        return false;
    }
    return true;
  }
};

static bool Aes_Crypt(const Byte *key, unsigned keySize, LPCWSTR mode,
    const Byte *iv, Byte *data, size_t size, bool encrypt)
{
  if (size == 0)
    return true;
  CAlgProvider prov;
  if (!prov.Open(BCRYPT_AES_ALGORITHM, 0))
    return false;
  CAesKey aesKey;
  if (!aesKey.Create(prov.Handle(), key, keySize, mode))
    return false;

  Byte ivBuf[16];
  if (iv)
    memcpy(ivBuf, iv, 16);
  else
    memset(ivBuf, 0, 16);

  ULONG done = 0;
  NTSTATUS res;
  if (encrypt)
    res = BCryptEncrypt(aesKey.Handle(), data, (ULONG)size, NULL, ivBuf, 16,
        data, (ULONG)size, &done, 0);
  else
    res = BCryptDecrypt(aesKey.Handle(), data, (ULONG)size, NULL, ivBuf, 16,
        data, (ULONG)size, &done, 0);
  return res >= 0 && done == (ULONG)size;
}

#endif

bool Aes_Cbc_Decrypt(const Byte *key, unsigned keySize,
    const Byte *iv, Byte *data, size_t size)
{
  if ((size & 15) != 0)
    return false;
#ifdef _WIN32
  return Aes_Crypt(key, keySize, BCRYPT_CHAIN_MODE_CBC, iv, data, size, false);
#else
  UNUSED_VAR(key) UNUSED_VAR(keySize) UNUSED_VAR(iv) UNUSED_VAR(data) UNUSED_VAR(size)
  return false;
#endif
}

bool Aes_Ecb_Encrypt(const Byte *key, unsigned keySize, Byte *data, size_t size)
{
  if ((size & 15) != 0)
    return false;
#ifdef _WIN32
  return Aes_Crypt(key, keySize, BCRYPT_CHAIN_MODE_ECB, NULL, data, size, true);
#else
  UNUSED_VAR(key) UNUSED_VAR(keySize) UNUSED_VAR(data) UNUSED_VAR(size)
  return false;
#endif
}

void Aes_Ctr_Crypt(const Byte *key, unsigned keySize,
    const Byte *nonce16, Byte *data, size_t size)
{
  Byte counter[16];
  memcpy(counter, nonce16, 16);
  size_t offset = 0;
  while (offset < size)
  {
    Byte block[16];
    memcpy(block, counter, 16);
    if (!Aes_Ecb_Encrypt(key, keySize, block, 16))
      break;
    const size_t todo = (size - offset < 16) ? (size - offset) : 16;
    for (size_t i = 0; i < todo; i++)
      data[offset + i] = (Byte)(data[offset + i] ^ block[i]);
    offset += todo;
    for (int i = 15; i >= 0; i--)
      if (++counter[i] != 0)
        break;
  }
  Z7_memset_0_ARRAY(counter);
}

static int Base64Value(char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

bool Base64_Decode(const char *src, size_t srcSize, CByteBuffer &dest)
{
  CByteBuffer buf(srcSize / 4 * 3 + 3);
  size_t outPos = 0;
  UInt32 acc = 0;
  unsigned bits = 0;

  for (size_t i = 0; i < srcSize; i++)
  {
    const char c = src[i];
    if (c == '=' || c == '\r' || c == '\n' || c == ' ' || c == '\t')
      continue;
    const int v = Base64Value(c);
    if (v < 0)
      return false;
    acc = (acc << 6) | (UInt32)v;
    bits += 6;
    if (bits >= 8)
    {
      bits -= 8;
      buf[outPos++] = (Byte)((acc >> bits) & 0xFF);
    }
  }

  dest.Free();
  dest.Alloc(outPos);
  if (outPos != 0)
    memcpy(dest, buf, outPos);
  return true;
}

void SecureZero(void *data, size_t size)
{
  volatile Byte *p = (volatile Byte *)data;
  while (size-- != 0)
    *p++ = 0;
}

/* ---------------- ZipCrypto ---------------- */

CZipCipher::CZipCipher():
    _key0(0x12345678),
    _key1(0x23456789),
    _key2(0x34567890)
{
}

void CZipCipher::UpdateKeys(Byte b)
{
  _key0 = g_CrcTable[(_key0 ^ b) & 0xFF] ^ (_key0 >> 8);
  _key1 = (_key1 + (_key0 & 0xFF)) * 0x8088405u + 1;
  _key2 = g_CrcTable[((_key2 ^ (_key1 >> 24)) & 0xFF)] ^ (_key2 >> 8);
}

void CZipCipher::SetPassword(const Byte *password, size_t size)
{
  _key0 = 0x12345678;
  _key1 = 0x23456789;
  _key2 = 0x34567890;
  for (size_t i = 0; i < size; i++)
    UpdateKeys(password[i]);
}

Byte CZipCipher::DecryptByte()
{
  const UInt32 temp = _key2 | 2;
  const Byte b = (Byte)((temp * (temp ^ 1)) >> 8);
  return b;
}

void CZipCipher::Decrypt(Byte *data, size_t size)
{
  for (size_t i = 0; i < size; i++)
  {
    const Byte b = (Byte)(data[i] ^ DecryptByte());
    data[i] = b;
    UpdateKeys(b);
  }
}

}}
