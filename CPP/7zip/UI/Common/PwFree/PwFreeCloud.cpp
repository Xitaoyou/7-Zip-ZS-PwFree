// PwFreeCloud.cpp

#include "PwFreeCloud.h"

#include "PwFreeHttp.h"
#include "PwFreeZip.h"

#include "../../../../Common/StringConvert.h"
#include "../../../../Common/UTFConvert.h"

/*
  The service answered the same way for every build this module was tested
  against:

    yun.php?md5=<hex>        {"code":200,"msg":"...","message":"<password>"}
    checkFile.php?md5=<hex>  an older alias, tried as a fallback

  Only a "code" of 200, 1 or 0 counts as an answer, and every string value that
  does not read like a failure sentence becomes a candidate. Each candidate is
  then checked against the archive locally, so a wrong, stale or hostile answer
  can never make 7-Zip report a password that does not work.

  No JSON library is linked: the payload is one flat object, and a scanner that
  understands quoted strings is smaller than a dependency.
*/

namespace NPwFree {

namespace {

const char * const kYunUrl = "https://apps.cscjapp.cn/compressnew/yun.php?md5=";
const char * const kCheckUrl = "https://apps.cscjapp.cn/compressnew/checkFile.php?md5=";

// Values that would be reported as a password but are really status chatter.
const char * const kNoise[] = {
  "\u6ca1\u6709\u627e\u5230",  // 沒有找到 (UTF-8 encoded in the source below)
  "ok", "no", "none", "null", "true", "false",
  "error", "failed", "failure", "success", "not found", "no password"
};

void ToLowerAscii(AString &s)
{
  for (unsigned i = 0; i < s.Len(); i++)
  {
    char c = s[i];
    if (c >= 'A' && c <= 'Z')
      s.ReplaceOneCharAtPos(i, (char)(c - 'A' + 'a'));
  }
}

bool IsNoise(const AString &value)
{
  AString v = value;
  ToLowerAscii(v);
  const char *s = v;
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kNoise); i++)
    if (strcmp(s, kNoise[i]) == 0)
      return true;
  return false;
}

// Appends the 4 hex digits at 'p' as a Unicode escape; returns false on a
// malformed sequence.
bool ParseHex4(const char *p, size_t size, unsigned &value)
{
  if (size < 4)
    return false;
  value = 0;
  for (unsigned i = 0; i < 4; i++)
  {
    const char c = p[i];
    unsigned d;
    if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
    else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
    else return false;
    value = (value << 4) | d;
  }
  return true;
}

/*
  Pulls the string values of a flat JSON object and the value of "code".
  A JSON string is decoded to UTF-8 bytes; \uXXXX escapes are re-encoded.
*/
void ScanJson(const AString &body, int &code, UStringVector &values)
{
  code = -1;
  const char *p = body;
  const size_t size = body.Len();

  for (size_t i = 0; i < size; i++)
  {
    if (p[i] != '"')
      continue;

    // Key.
    AString key;
    size_t j = i + 1;
    bool closed = false;
    for (; j < size; j++)
    {
      const char c = p[j];
      if (c == '\\' && j + 1 < size)
      {
        key.Add_Char(p[++j]);
        continue;
      }
      if (c == '"')
      {
        closed = true;
        break;
      }
      key.Add_Char(c);
    }
    if (!closed)
      break;

    // Skip spaces, expect a colon.
    size_t k = j + 1;
    while (k < size && (p[k] == ' ' || p[k] == '\t' || p[k] == '\r' || p[k] == '\n'))
      k++;
    if (k >= size || p[k] != ':')
    {
      i = j;
      continue;
    }
    k++;
    while (k < size && (p[k] == ' ' || p[k] == '\t' || p[k] == '\r' || p[k] == '\n'))
      k++;
    if (k >= size)
      break;

    if (p[k] == '"')
    {
      AString value;
      size_t m = k + 1;
      bool valueClosed = false;
      for (; m < size; m++)
      {
        const char c = p[m];
        if (c == '\\' && m + 1 < size)
        {
          const char e = p[m + 1];
          if (e == 'u')
          {
            unsigned cp = 0;
            if (ParseHex4(p + m + 2, size - (m + 2), cp))
            {
              // Re-encode as UTF-8 so the rest of the module only sees UTF-8.
              Byte utf8[4];
              unsigned n = 0;
              if (cp < 0x80)
              {
                utf8[n++] = (Byte)cp;
              }
              else if (cp < 0x800)
              {
                utf8[n++] = (Byte)(0xC0 | (cp >> 6));
                utf8[n++] = (Byte)(0x80 | (cp & 0x3F));
              }
              else
              {
                utf8[n++] = (Byte)(0xE0 | (cp >> 12));
                utf8[n++] = (Byte)(0x80 | ((cp >> 6) & 0x3F));
                utf8[n++] = (Byte)(0x80 | (cp & 0x3F));
              }
              for (unsigned q = 0; q < n; q++)
                value.Add_Char((char)utf8[q]);
              m += 5;
              continue;
            }
          }
          value.Add_Char(e);
          m++;
          continue;
        }
        if (c == '"')
        {
          valueClosed = true;
          break;
        }
        value.Add_Char(c);
      }
      if (valueClosed)
      {
        if (key == "code")
        {
          code = 0;
          for (unsigned q = 0; q < value.Len(); q++)
          {
            const char c = value[q];
            if (c < '0' || c > '9')
            {
              code = -1;
              break;
            }
            code = code * 10 + (c - '0');
          }
        }
        UString u;
        if (Convert_UTF8_Buf_To_Unicode(value, value.Len(), u) && !u.IsEmpty())
          values.Add(u);
      }
      i = m;
      continue;
    }

    // A number: "code":200 comes without quotes.
    if ((p[k] >= '0' && p[k] <= '9') || p[k] == '-')
    {
      size_t m = k;
      int v = 0;
      bool negative = false;
      if (p[m] == '-')
      {
        negative = true;
        m++;
      }
      bool ok = false;
      for (; m < size && p[m] >= '0' && p[m] <= '9'; m++)
      {
        v = v * 10 + (p[m] - '0');
        ok = true;
      }
      if (ok && key == "code")
        code = negative ? -v : v;
      i = m;
      continue;
    }

    i = k;
  }
}

}

CCloudLookup::CCloudLookup(const FString &arcPath):
    _arcPath(arcPath), _tried(false), _ok(false), _pos(0)
{
}

bool CCloudLookup::Fetch(UInt32 timeoutMs)
{
  AString md5;
  if (!NPwZip::FileMd5_Hex(_arcPath, md5))
  {
    _lastError = L"fingerprint failed";
    return false;
  }

  const char *urls[2] = { kYunUrl, kCheckUrl };
  for (unsigned u = 0; u < 2 && !_ok; u++)
  {
    UString url = GetUnicodeString(urls[u]);
    url += GetUnicodeString(md5);

    AString body;
    UString errorText;
    if (NPwHttp::Get(url, timeoutMs, body, errorText) != S_OK)
    {
      _lastError = errorText;
      continue;
    }

    int code = -1;
    UStringVector values;
    ScanJson(body, code, values);

    // 200 / 1 / 0 are the "found" codes the service has used.
    if (code != 200 && code != 1 && code != 0)
      continue;

    FOR_VECTOR (i, values)
    {
      const UString &v = values[i];
      AString ansi;
      ConvertUnicodeToUTF8(v, ansi);
      if (IsNoise(ansi))
        continue;
      FOR_VECTOR (j, _candidates)
        if (_candidates[j] == v)
          goto nextValue;
      _candidates.Add(v);
      _ok = true;
    nextValue:;
    }
  }

  _providerName = L"cloud";
  return _ok;
}

HRESULT CCloudLookup::Query(UInt32 timeoutMs)
{
  if (!_tried)
  {
    _tried = true;
    Fetch(timeoutMs);
  }
  return _ok ? S_OK : S_FALSE;
}

HRESULT CCloudLookup::Next(UString &password)
{
  password.Empty();
  if (_pos >= _candidates.Size())
    return S_FALSE;
  password = _candidates[(unsigned)_pos];
  _pos++;
  return password.IsEmpty() ? S_FALSE : S_OK;
}

}
