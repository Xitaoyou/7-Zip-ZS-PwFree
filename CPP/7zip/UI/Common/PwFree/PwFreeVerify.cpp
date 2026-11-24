// PwFreeVerify.cpp

#include "PwFreeVerify.h"

#include "PwFree7z.h"
#include "PwFreeZip.h"

namespace NPwFree {

namespace {

class CZipCryptoVerifier Z7_final: public IVerify
{
  NPwZip::CZipCryptoProbe _probe;
  bool _ok;

public:
  CZipCryptoVerifier(const FString &path, const NPwZip::CEntryInfo &info): _ok(false)
  {
    _ok = _probe.Init(path, info);
  }

  bool Available() const { return _ok; }
  unsigned CostClass() const { return 0; }
  const char *Name() const { return "zipcrypto"; }

  bool Check(const Byte *password, size_t size) const
  {
    if (!_ok)
      return false;
    try
    {
      return _probe.Check(password, size);
    }
    catch (...)
    {
      return false;
    }
  }
};

class CWzAesVerifier Z7_final: public IVerify
{
  NPwZip::CWinZipAesProbe _probe;
  bool _ok;

public:
  CWzAesVerifier(const FString &path, const NPwZip::CEntryInfo &info): _ok(false)
  {
    _ok = _probe.Init(path, info);
  }

  bool Available() const { return _ok; }
  unsigned CostClass() const { return 1; }
  const char *Name() const { return "winzipaes"; }

  bool Check(const Byte *password, size_t size) const
  {
    if (!_ok)
      return false;
    try
    {
      return _probe.Check(password, size);
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

  bool Check(const Byte *password, size_t size) const
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
      {
        CWzAesVerifier *v = new CWzAesVerifier(arcPath, info);
        if (v->Available())
          return v;
        delete v;
      }
      else
      {
        CZipCryptoVerifier *v = new CZipCryptoVerifier(arcPath, info);
        if (v->Available())
          return v;
        delete v;
      }
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