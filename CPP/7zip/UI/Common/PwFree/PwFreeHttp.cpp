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

static HRESULT FetchOnce(const CUrlParts &parts, UInt32 timeoutMs,
    AString &body, UString &errorText)
{
  HINTERNET session = ::WinHttpOpen(L"7-Zip-PwFree/1.0",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
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
  AString temp;
  ::WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &secureFailure, sizeof(secureFailure));
  ::WinHttpSetStatusCallback(request, StatusCallback, WINHTTP_CALLBACK_FLAG_SECURE_FAILURE, 0);

  ::SetLastError(0);
  bool sent = (::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
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
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX)
        && statusCode == 200)
    {
      CByteBuffer buf(4096);
      size_t used = 0;
      for (;;)
      {
        DWORD avail = 0;
        if (!::WinHttpQueryDataAvailable(request, &avail) || avail == 0)
          break;
        if (used + avail > ((size_t)1 << 20))
          break; // never read more than 1 MB from one endpoint
        buf.ChangeSize_KeepData(used + avail, used);
        DWORD read = 0;
        if (!::WinHttpReadData(request, (Byte *)buf + used, avail, &read) || read == 0)
          break;
        used += read;
      }
      body.SetFrom((const char *)(const Byte *)buf, (unsigned)used);
      result = S_OK;
    }
    else
      AddError(temp, "status", statusCode);
  }

  if (secureFailure != 0)
    AddError(temp, "tls", secureFailure);

  if (result != S_OK)
    errorText = GetUnicodeString(temp);

  ::WinHttpCloseHandle(request);
  ::WinHttpCloseHandle(connect);
  ::WinHttpCloseHandle(session);
  return result;
}

HRESULT Get(const UString &url, UInt32 timeoutMs, AString &body, UString &errorText)
{
  body.Empty();
  errorText.Empty();

  CUrlParts parts;
  if (!ParseUrl(url, parts))
  {
    errorText = L"bad url";
    return S_FALSE;
  }

  HRESULT result = FetchOnce(parts, timeoutMs, body, errorText);
  if (result == S_OK || !parts.Https)
    return result;

  // The vendor hosts serve the same JSON over plain HTTP, and some machines
  // do not have the current TLS intermediate installed. Retry over HTTP.
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
  AString httpBody;
  const HRESULT httpResult = FetchOnce(httpParts, timeoutMs, httpBody, httpError);
  if (httpResult == S_OK)
  {
    body = httpBody;
    errorText.Empty();
    return S_OK;
  }
  return result;
}

#else

HRESULT Get(const UString &url, UInt32 timeoutMs, AString &body, UString &errorText)
{
  UNUSED_VAR(url) UNUSED_VAR(timeoutMs)
  body.Empty();
  errorText = L"not supported";
  return S_FALSE;
}

#endif

}}
