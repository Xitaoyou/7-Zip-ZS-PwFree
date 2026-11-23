// PwFree.cpp

#include "PwFree.h"

#include "PwFreeCache.h"
#include "PwFreeChain.h"
#include "PwFreeSettings.h"
#include "PwFreeVerify.h"

#include "../../../../Windows/FileFind.h"
#include "../../../../Windows/TimeUtils.h"

using namespace NWindows;

namespace NPwFree {

CRequest::CRequest():
    ArcPath(NULL),
    Size(0),
    MTime(0),
    FormatIndex((UInt32)(Int32)-1),
    HeadersEncrypted(false),
    Seed(NULL)
{
}

CAnswer::CAnswer():
    Source(kSrcNone),
    ElapsedMs(0)
{
}

void CAnswer::Clear()
{
  Source = kSrcNone;
  Password.Wipe_and_Empty();
  ElapsedMs = 0;
  ProviderName.Empty();
}

const char *GetSourceName(ESource src)
{
  switch (src)
  {
    case kSrcSeed:  return "seed";
    case kSrcCache: return "cache";
    case kSrcBook:  return "book";
    case kSrcDict:  return "dict";
    case kSrcCloud: return "cloud";
    case kSrcMask:  return "mask";
    case kSrcNone:
    default:        return "none";
  }
}

const char *GetSourceText(ESource src)
{
  switch (src)
  {
    case kSrcSeed:  return "known password";
    case kSrcCache: return "earlier lookup";
    case kSrcBook:  return "built-in list";
    case kSrcDict:  return "downloaded dictionary";
    case kSrcCloud: return "password database";
    case kSrcMask:  return "mask search";
    case kSrcNone:
    default:        return "unknown source";
  }
}

bool FillRequest_FromPath(const UString &arcPath, CRequest &req)
{
  req.Size = 0;
  req.MTime = 0;

  if (arcPath.IsEmpty())
    return false;

  NFile::NFind::CFileInfo fi;
  if (!fi.Find_FollowLink(us2fs(arcPath)))
    return false;
  if (fi.IsDir())
    return false;

  req.Size = fi.Size;
  #ifdef _WIN32
  {
    const Int64 t = NTime::FileTime_To_UnixTime64(fi.MTime);
    req.MTime = (t > 0) ? (UInt64)t : 0;
  }
  #else
  req.MTime = (UInt64)fi.MTime.tv_sec;
  #endif
  return true;
}

namespace {

ESource SourceFromProviderName(const AString &name)
{
  const char *s = name;
  if (strcmp(s, "seed") == 0) return kSrcSeed;
  if (strcmp(s, "cache") == 0) return kSrcCache;
  if (strcmp(s, "dict") == 0) return kSrcDict;
  if (strcmp(s, "cloud") == 0) return kSrcCloud;
  if (strcmp(s, "mask") == 0) return kSrcMask;
  return kSrcNone;
}

}

static HRESULT ResolveWithSettings(const CRequest &req, const CSettings &st,
    CAnswer &ans, volatile long *cancel)
{
  ans.Clear();

  if (!st.Enabled)
    return S_FALSE;
  if (!req.ArcPath || req.ArcPath->IsEmpty())
    return S_FALSE;

  IVerify *verify = CreateVerifier(*req.ArcPath, req.FormatIndex);
  if (!verify)
    return E_PWFREE_NOPROBE;

  UString password;
  AString providerName;
  unsigned elapsedMs = 0;

  HRESULT result;
  try
  {
    result = RunChain(req, st, *verify, password, providerName, elapsedMs, cancel);
  }
  catch (...)
  {
    delete verify;
    throw;
  }
  delete verify;

  if (result != S_OK || password.IsEmpty())
  {
    password.Wipe_and_Empty();
    return result;
  }

  ans.Source = SourceFromProviderName(providerName);
  ans.Password = password;
  ans.ProviderName = providerName;
  ans.ElapsedMs = elapsedMs;

  if (ans.Source != kSrcCache)
    Cache_Put(req, password);

  return S_OK;
}

HRESULT Resolve(const CRequest &req, CAnswer &ans, volatile long *cancel)
{
  const CSettings st = GetSettings();
  return ResolveWithSettings(req, st, ans, cancel);
}

HRESULT FindPassword_ForArc(const UString &arcPath, bool headersEncrypted, UString &password)
{
  UString note;
  return FindPassword_ForArc2(arcPath, headersEncrypted, password, note);
}

HRESULT FindPassword_ForArc2(const UString &arcPath, bool headersEncrypted,
    UString &password, UString &note)
{
  return FindPassword_ForArc2(arcPath, headersEncrypted, password, note, NULL);
}

HRESULT FindPassword_ForArc2(const UString &arcPath, bool headersEncrypted,
    UString &password, UString &note, volatile long *cancel)
{
  note.Empty();
  const CSettings st = GetSettings();
  if (!st.Enabled)
    return S_FALSE;

  try
  {
    FString fsPath;
    CRequest req;
    if (!arcPath.IsEmpty())
    {
      fsPath = us2fs(arcPath);
      req.ArcPath = &fsPath;
    }
    FillRequest_FromPath(arcPath, req);
    req.HeadersEncrypted = headersEncrypted;

    CAnswer ans;
    const HRESULT res = ResolveWithSettings(req, st, ans, cancel);
    if (res == S_OK && !ans.Password.IsEmpty())
    {
      password = ans.Password;
      if (st.ShowSource)
      {
        note = L"PwFree: password from ";
        const char *text = GetSourceText(ans.Source);
        for (; *text != 0; text++)
          note += (wchar_t)(unsigned char)*text;
        const char *raw = GetSourceName(ans.Source);
        const char *prov = ans.ProviderName.Ptr();
        bool same = true;
        for (unsigned i = 0;; i++)
        {
          if (raw[i] != prov[i])
          {
            same = false;
            break;
          }
          if (raw[i] == 0)
            break;
        }
        if (!same && prov[0] != 0)
        {
          note += L" (";
          for (; *prov != 0; prov++)
            note += (wchar_t)(unsigned char)*prov;
          note += L")";
        }
      }
      ans.Clear();
      return S_OK;
    }
    ans.Clear();
    if (res == E_ABORT)
      return E_ABORT;
  }
  catch (...)
  {
    // A recovery failure must never break archive opening.
  }
  return S_FALSE;
}

}
