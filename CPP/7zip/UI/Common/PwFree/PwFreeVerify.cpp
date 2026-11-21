// PwFreeVerify.cpp

#include "PwFreeVerify.h"

#include "PwFreeZip.h"

namespace NPwFree {

namespace {

class CZipCryptoVerifier Z7_final: public IVerify
{
  FString _path;
  NPwZip::CEntryInfo _info;
  bool _ok;

public:
  CZipCryptoVerifier(const FString &path): _path(path), _ok(false)
  {
    _ok = NPwZip::Read_Info(_path, _info) && _info.Encrypted && !_info.IsWinZipAes;
  }

  unsigned CostClass() const { return 1; }
  const char *Name() const { return "zipcrypto"; }

  bool Check(const Byte *password, size_t size)
  {
    if (!_ok)
      return false;
    try
    {
      return NPwZip::Verify_ZipCrypto(_path, _info, password, size);
    }
    catch (...)
    {
      return false;
    }
  }
};

class CWzAesVerifier Z7_final: public IVerify
{
  FString _path;
  NPwZip::CEntryInfo _info;
  bool _ok;

public:
  CWzAesVerifier(const FString &path): _path(path), _ok(false)
  {
    _ok = NPwZip::Read_Info(_path, _info) && _info.Encrypted && _info.IsWinZipAes;
  }

  unsigned CostClass() const { return 1; }
  const char *Name() const { return "winzipaes"; }

  bool Check(const Byte *password, size_t size)
  {
    if (!_ok)
      return false;
    try
    {
      return NPwZip::Verify_WinZipAes(_path, _info, password, size);
    }
    catch (...)
    {
      return false;
    }
  }
};

}

IVerify *CreateVerifier(const FString &arcPath, UInt32 formatIndex)
{
  UNUSED_VAR(formatIndex)
  try
  {
    NPwZip::CEntryInfo info;
    if (NPwZip::Read_Info(arcPath, info) && info.Encrypted)
    {
      if (info.IsWinZipAes)
        return new CWzAesVerifier(arcPath);
      return new CZipCryptoVerifier(arcPath);
    }
  }
  catch (...)
  {
  }
  return NULL;
}

}
