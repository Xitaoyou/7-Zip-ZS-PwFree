// PwFreeHttp.cpp

#include "PwFreeHttp.h"

#include "../../../../Common/MyBuffer.h"
#include "../../../../Common/StringConvert.h"
#include <stdio.h>

#ifdef _WIN32
#include <winhttp.h>
#endif

namespace NPwFree {
namespace NPwHttp {

#ifdef _WIN32

static void WINAPI StatusCallback(HINTERNET, DWORD_PTR context,
    DWORD status, LPVOID info, DWORD)
{
  if (status != WINHTTP_CALLBACK_STATUS_SECURE_FAILURE)
    return;
  if (!context || !info)
    return;
  // Any TLS failure (expired, revoked, unknown issuer, protocol mismatch)
  // makes the request fail instead of silently downgrading the connection.
  *((DWORD *)context) = *((const DWORD *)info);
}

struct CUrlParts
{
  UString Host;
  UString Path;
  INTERNET_PORT Port;
  bool Https;

  CUrlParts(): Port(0), Https(false) {}
};

static bool ParseUrl(const UString &url, CUrlParts &parts)
{
  const wchar_t *s = url;
  if (wcsncmp(s, L"http://", 7) == 0)
  {
    parts.Https = false;
    parts.Port = 80;
    s += 7;
  }
  else if (wcsncmp(s, L"https://", 8) == 0)
  {
    parts.Https = true;
    parts.Port = 443;
    s += 8;
  }
  else
    return false;

  const wchar_t *slash = wcschr(s, L'/');
  UString hostPart;
  if (slash)
  {
    hostPart.SetFrom(s, (unsigned)(slash - s));
    parts.Path = slash;
  }
  else
  {
    hostPart = s;
    parts.Path = L"/";
  }

  const wchar_t *colon = wcschr(hostPart, L':');
  if (colon)
  {
    parts.Host.SetFrom(hostPart, (unsigned)(colon - hostPart));
    parts.Port = (INTERNET_PORT)wcstoul(colon + 1, NULL, 10);
  }
  else
    parts.Host = hostPart;

  return !parts.Host.IsEmpty();
}

static void AddError(AString &errorText, const char *tag, DWORD code)
{
  if (errorText.Len() > 0)
    errorText.Add_Space();
  errorText += tag;
  errorText += ':';
  char temp[16];
  for (int i = 12; i >= 0; i -= 4)
  {
    const unsigned v = (unsigned)((code >> i) & 0xF);
    temp[(12 - i) / 4] = (char)((v < 10) ? ('0' + v) : ('a' + v - 10));
  }
  temp[4] = 0;
  errorText += temp;
}

static void QueryHeaderString(HINTERNET request, DWORD infoLevel, UString &out)
{
  out.Empty();
  DWORD size = 0;
  ::WinHttpQueryHeaders(request, infoLevel, WINHTTP_HEADER_NAME_BY_INDEX,
      WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
  if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || size < sizeof(wchar_t) || size > 4096)
    return;
  const unsigned chars = (unsigned)(size / sizeof(wchar_t));
  wchar_t *buf = out.GetBuf(chars);
  if (::WinHttpQueryHeaders(request, infoLevel, WINHTTP_HEADER_NAME_BY_INDEX,
      buf, &size, WINHTTP_NO_HEADER_INDEX))
  {
    buf[chars - 1] = 0;
    out.ReleaseBuf_CalcLen(chars);
  }
  else
    out.ReleaseBuf_SetEnd(0);
}

static HRESULT FetchOnce(const CUrlParts &parts, UInt32 timeoutMs,
    size_t maxBytes, const UString &ifNoneMatch, const UString &ifModifiedSince,
    CByteBuffer &body, CResponseMeta &meta, UString &errorText)
{
  body.Free();
  meta = CResponseMeta();

  HINTERNET session = ::WinHttpOpen(L"7-Zip-PwFree/1.0",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session)
  {
    // Windows 7 fallback: WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY requires Win8.1+.
    session = ::WinHttpOpen(L"7-Zip-PwFree/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!session)
  {
    AString temp;
    AddError(temp, "open", ::GetLastError());
    errorText = GetUnicodeString(temp);
    return S_FALSE;
  }

  ::WinHttpSetTimeouts(session, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs);

  HRESULT result = S_FALSE;
  HINTERNET connect = ::WinHttpConnect(session, parts.Host, parts.Port, 0);
  if (!connect)
  {
    AString temp;
    AddError(temp, "connect", ::GetLastError());
    errorText = GetUnicodeString(temp);
    ::WinHttpCloseHandle(session);
    return S_FALSE;
  }

  HINTERNET request = ::WinHttpOpenRequest(connect, L"GET", parts.Path,
      NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      parts.Https ? WINHTTP_FLAG_SECURE : 0);
  if (!request)
  {
    AString temp;
    AddError(temp, "open-request", ::GetLastError());
    errorText = GetUnicodeString(temp);
    ::WinHttpCloseHandle(connect);
    ::WinHttpCloseHandle(session);
    return S_FALSE;
  }

  DWORD secureFailure = 0;
  DWORD_PTR contextVal = (DWORD_PTR)&secureFailure;
  AString temp;
  ::WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &contextVal, sizeof(contextVal));
  ::WinHttpSetStatusCallback(request, StatusCallback, WINHTTP_CALLBACK_FLAG_SECURE_FAILURE, 0);

  UString extraHeaders;
  if (!ifNoneMatch.IsEmpty())
  {
    extraHeaders += L"If-None-Match: ";
    extraHeaders += ifNoneMatch;
    extraHeaders += L"\r\n";
  }
  if (!ifModifiedSince.IsEmpty())
  {
    extraHeaders += L"If-Modified-Since: ";
    extraHeaders += ifModifiedSince;
    extraHeaders += L"\r\n";
  }

  ::SetLastError(0);
  bool sent = (::WinHttpSendRequest(request,
      extraHeaders.IsEmpty() ? WINHTTP_NO_ADDITIONAL_HEADERS : (LPCWSTR)extraHeaders,
      extraHeaders.IsEmpty() ? 0 : (DWORD)-1L,
      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE);
  if (sent)
  {
    ::SetLastError(0);
    if (!::WinHttpReceiveResponse(request, NULL))
    {
      AddError(temp, "receive", ::GetLastError());
      sent = false;
    }
  }
  else
    AddError(temp, "send", ::GetLastError());

  if (sent)
  {
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    if (::WinHttpQueryHeaders(request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX))
    {
      meta.StatusCode = (UInt32)statusCode;
      QueryHeaderString(request, WINHTTP_QUERY_ETAG, meta.ETag);
      QueryHeaderString(request, WINHTTP_QUERY_LAST_MODIFIED, meta.LastModified);

      if (statusCode == 304)
      {
        meta.NotModified = true;
        result = S_OK;
      }
      else if (statusCode == 200)
      {
        size_t cap = 4096;
        if (maxBytes < cap)
          cap = maxBytes;
        CByteBuffer buf(cap);
        size_t used = 0;
        bool readOk = true;
        for (;;)
        {
          DWORD avail = 0;
          if (!::WinHttpQueryDataAvailable(request, &avail))
          {
            AddError(temp, "avail", ::GetLastError());
            readOk = false;
            break;
          }
          if (avail == 0)
            break;
          if ((size_t)avail > maxBytes - used)
          {
            AddError(temp, "limit", (DWORD)avail);
            readOk = false;
            break;
          }
          if (used + avail > cap)
          {
            size_t newCap = cap;
            while (newCap < used + avail)
            {
              if (newCap >= maxBytes / 2)
              {
                newCap = maxBytes;
                break;
              }
              newCap *= 2;
            }
            if (newCap < used + avail)
              newCap = used + avail;
            buf.ChangeSize_KeepData(newCap, used);
            cap = newCap;
          }
          DWORD read = 0;
          if (!::WinHttpReadData(request, (Byte *)buf + used, avail, &read))
          {
            AddError(temp, "read", ::GetLastError());
            readOk = false;
            break;
          }
          if (read == 0)
            break;
          used += read;
        }
        if (readOk)
        {
          buf.ChangeSize_KeepData(used, used);
          body = buf;
          result = S_OK;
        }
      }
      else
        AddError(temp, "status", statusCode);
    }
    else
      AddError(temp, "status", statusCode);
  }

  if (secureFailure != 0)
    AddError(temp, "tls", secureFailure);

  if (result != S_OK)
    errorText = GetUnicodeString(temp);

  ::WinHttpSetStatusCallback(request, NULL, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0);
  ::WinHttpCloseHandle(request);
  ::WinHttpCloseHandle(connect);
  ::WinHttpCloseHandle(session);
  return result;
}

HRESULT GetBytesEx(const UString &url, UInt32 timeoutMs, size_t maxBytes,
    bool allowHttpFallback, const UString &ifNoneMatch, const UString &ifModifiedSince,
    CByteBuffer &body, CResponseMeta &meta, UString &errorText)
{
  body.Free();
  meta = CResponseMeta();
  errorText.Empty();

  if (maxBytes == 0)
  {
    errorText = L"zero limit";
    return S_FALSE;
  }

  CUrlParts parts;
  if (!ParseUrl(url, parts))
  {
    errorText = L"bad url";
    return S_FALSE;
  }

  HRESULT result = FetchOnce(parts, timeoutMs, maxBytes, ifNoneMatch, ifModifiedSince,
      body, meta, errorText);
  if (result == S_OK || !parts.Https || !allowHttpFallback)
    return result;

  // Optional fallback to plain HTTP when explicitly enabled in settings.
  UString fallback = L"http://";
  fallback += parts.Host;
  if (parts.Port != 80 && parts.Port != 443)
  {
    fallback += L':';
    wchar_t portText[16];
    swprintf_s(portText, 16, L"%u", (unsigned)parts.Port);
    fallback += portText;
  }
  fallback += parts.Path;

  CUrlParts httpParts;
  if (!ParseUrl(fallback, httpParts))
    return result;

  UString httpError;
  CByteBuffer httpBody;
  CResponseMeta httpMeta;
  const HRESULT httpResult = FetchOnce(httpParts, timeoutMs, maxBytes,
      ifNoneMatch, ifModifiedSince, httpBody, httpMeta, httpError);
  if (httpResult == S_OK)
  {
    body = httpBody;
    meta = httpMeta;
    errorText.Empty();
    return S_OK;
  }
  return result;
}

HRESULT GetBytes(const UString &url, UInt32 timeoutMs, size_t maxBytes,
    bool allowHttpFallback, CByteBuffer &body, UString &errorText)
{
  CResponseMeta meta;
  UString empty;
  return GetBytesEx(url, timeoutMs, maxBytes, allowHttpFallback, empty, empty,
      body, meta, errorText);
}

HRESULT Get(const UString &url, UInt32 timeoutMs, AString &body,
    UString &errorText, bool allowHttpFallback)
{
  body.Empty();
  CByteBuffer raw;
  const HRESULT res = GetBytes(url, timeoutMs, (size_t)1 << 20, allowHttpFallback, raw, errorText);
  if (res != S_OK)
    return res;
  if (raw.Size() != 0)
    body.SetFrom((const char *)(const Byte *)raw, (unsigned)raw.Size());
  return S_OK;
}

#else

HRESULT GetBytesEx(const UString &url, UInt32 timeoutMs, size_t maxBytes,
    bool allowHttpFallback, const UString &ifNoneMatch, const UString &ifModifiedSince,
    CByteBuffer &body, CResponseMeta &meta, UString &errorText)
{
  UNUSED_VAR(url) UNUSED_VAR(timeoutMs) UNUSED_VAR(maxBytes) UNUSED_VAR(allowHttpFallback)
  UNUSED_VAR(ifNoneMatch) UNUSED_VAR(ifModifiedSince)
  body.Free();
  meta = CResponseMeta();
  errorText = L"not supported";
  return S_FALSE;
}

HRESULT GetBytes(const UString &url, UInt32 timeoutMs, size_t maxBytes,
    bool allowHttpFallback, CByteBuffer &body, UString &errorText)
{
  UNUSED_VAR(url) UNUSED_VAR(timeoutMs) UNUSED_VAR(maxBytes) UNUSED_VAR(allowHttpFallback)
  body.Free();
  errorText = L"not supported";
  return S_FALSE;
}

HRESULT Get(const UString &url, UInt32 timeoutMs, AString &body,
    UString &errorText, bool allowHttpFallback)
{
  UNUSED_VAR(url) UNUSED_VAR(timeoutMs) UNUSED_VAR(allowHttpFallback)
  body.Empty();
  errorText = L"not supported";
  return S_FALSE;
}

#endif

}}
