// PwFreeVerify.cpp

#include "PwFreeVerify.h"

#include "PwFree7z.h"
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

class CSevenZVerifier Z7_final: public IVerify
{
  FString _path;
  NPwSevenZ::CProbe _probe;
  bool _ok;

public:
  CSevenZVerifier(const FString &path): _path(path), _ok(false)
  {
    _ok = _probe.Open(_path);
  }

  bool Available() const { return _ok; }

  unsigned CostClass() const { return 1; }

  const char *Name() const { return _probe.ModeName(); }

  bool Check(const Byte *password, size_t size)
  {
    if (!_ok)
      return false;
    return _probe.Check(password, size);
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

    // 7z: the probe reports by itself whether the archive can be checked.
    if (NPwSevenZ::IsSevenZ(arcPath))
    {
      CSevenZVerifier *verifier = new CSevenZVerifier(arcPath);
      if (verifier->Available())
        return verifier;
      delete verifier;
    }
  }
  catch (...)
  {
  }
  return NULL;
}

}