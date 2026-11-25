// AutoUpdate.cpp - Lightweight async GitHub Release auto-updater (ASCII-safe)

#include "StdAfx.h"

#include <ShellAPI.h>
#include <WinHttp.h>

#include "AutoUpdate.h"
#include "UiLang.h"

namespace NAutoUpdate {

namespace {

const wchar_t * const kRegKeyPath = L"Software\\7-Zip-Zstandard\\UnlockZip";
const unsigned kCurMajor = 2;
const unsigned kCurMinor = 1;
const unsigned kCurPatch = 1;
const wchar_t * const kCurVersionStr = L"v2.1.1";

const wchar_t * const kApiHost = L"api.github.com";
const wchar_t * const kApiPath = L"/repos/Xitaoyou/7-Zip-ZS-PwFree/releases/latest";

typedef HINTERNET (WINAPI *PFN_WinHttpOpen)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef HINTERNET (WINAPI *PFN_WinHttpConnect)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
typedef HINTERNET (WINAPI *PFN_WinHttpOpenRequest)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
typedef BOOL (WINAPI *PFN_WinHttpSendRequest)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL (WINAPI *PFN_WinHttpReceiveResponse)(HINTERNET, LPVOID);
typedef BOOL (WINAPI *PFN_WinHttpQueryDataAvailable)(HINTERNET, LPDWORD);
typedef BOOL (WINAPI *PFN_WinHttpReadData)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL (WINAPI *PFN_WinHttpCloseHandle)(HINTERNET);
typedef BOOL (WINAPI *PFN_WinHttpSetTimeouts)(HINTERNET, int, int, int, int);
typedef BOOL (WINAPI *PFN_WinHttpCrackUrl)(LPCWSTR, DWORD, DWORD, LPURL_COMPONENTS);

struct CWinHttpApi
{
  HMODULE Dll;
  PFN_WinHttpOpen pOpen;
  PFN_WinHttpConnect pConnect;
  PFN_WinHttpOpenRequest pOpenRequest;
  PFN_WinHttpSendRequest pSendRequest;
  PFN_WinHttpReceiveResponse pReceiveResponse;
  PFN_WinHttpQueryDataAvailable pQueryData;
  PFN_WinHttpReadData pReadData;
  PFN_WinHttpCloseHandle pClose;
  PFN_WinHttpSetTimeouts pSetTimeouts;
  PFN_WinHttpCrackUrl pCrackUrl;

  bool Load()
  {
    Dll = ::LoadLibraryW(L"winhttp.dll");
    if (!Dll)
      return false;
    pOpen = (PFN_WinHttpOpen)(void *)::GetProcAddress(Dll, "WinHttpOpen");
    pConnect = (PFN_WinHttpConnect)(void *)::GetProcAddress(Dll, "WinHttpConnect");
    pOpenRequest = (PFN_WinHttpOpenRequest)(void *)::GetProcAddress(Dll, "WinHttpOpenRequest");
    pSendRequest = (PFN_WinHttpSendRequest)(void *)::GetProcAddress(Dll, "WinHttpSendRequest");
    pReceiveResponse = (PFN_WinHttpReceiveResponse)(void *)::GetProcAddress(Dll, "WinHttpReceiveResponse");
    pQueryData = (PFN_WinHttpQueryDataAvailable)(void *)::GetProcAddress(Dll, "WinHttpQueryDataAvailable");
    pReadData = (PFN_WinHttpReadData)(void *)::GetProcAddress(Dll, "WinHttpReadData");
    pClose = (PFN_WinHttpCloseHandle)(void *)::GetProcAddress(Dll, "WinHttpCloseHandle");
    pSetTimeouts = (PFN_WinHttpSetTimeouts)(void *)::GetProcAddress(Dll, "WinHttpSetTimeouts");
    pCrackUrl = (PFN_WinHttpCrackUrl)(void *)::GetProcAddress(Dll, "WinHttpCrackUrl");
    return pOpen && pConnect && pOpenRequest && pSendRequest
        && pReceiveResponse && pQueryData && pReadData && pClose;
  }

  ~CWinHttpApi()
  {
    if (Dll)
      ::FreeLibrary(Dll);
  }
};

struct CUpdateResult
{
  bool ManualCheck;
  bool FetchOk;
  bool HasUpdate;
  UString LatestTag;
  UString DownloadUrl;

  CUpdateResult(): ManualCheck(false), FetchOk(false), HasUpdate(false) {}
};

struct CThreadArg
{
  HWND HwndNotify;
  bool ManualCheck;
};

bool HttpGetToBuffer(CWinHttpApi &api, const wchar_t *host, INTERNET_PORT port,
    const wchar_t *path, bool https, AString &outBuf, size_t maxBytes)
{
  outBuf.Empty();
  HINTERNET hSession = api.pOpen(L"UnlockZip/2.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession)
    return false;
  if (api.pSetTimeouts)
    api.pSetTimeouts(hSession, 6000, 6000, 8000, 15000);

  HINTERNET hConnect = api.pConnect(hSession, host, port, 0);
  HINTERNET hRequest = NULL;
  bool ok = false;
  if (hConnect)
  {
    hRequest = api.pOpenRequest(hConnect, L"GET", path, NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (hRequest)
    {
      if (api.pSendRequest(hRequest, L"Accept: application/vnd.github+json\r\n",
          (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
          && api.pReceiveResponse(hRequest, NULL))
      {
        ok = true;
        for (;;)
        {
          DWORD avail = 0;
          if (!api.pQueryData(hRequest, &avail) || avail == 0)
            break;
          if ((size_t)outBuf.Len() + avail > maxBytes)
          {
            ok = false;
            break;
          }
          char *buf = new char[avail + 1];
          DWORD readBytes = 0;
          if (api.pReadData(hRequest, buf, avail, &readBytes) && readBytes > 0)
          {
            buf[readBytes] = 0;
            outBuf.AddFrom(buf, readBytes);
          }
          delete []buf;
          if (readBytes == 0)
            break;
        }
      }
      api.pClose(hRequest);
    }
    api.pClose(hConnect);
  }
  api.pClose(hSession);
  return ok && !outBuf.IsEmpty();
}

bool HttpDownloadFile(CWinHttpApi &api, const wchar_t *url, const wchar_t *destPath)
{
  if (!api.pCrackUrl)
    return false;
  wchar_t host[256];
  wchar_t urlPath[2048];
  URL_COMPONENTS uc;
  memset(&uc, 0, sizeof(uc));
  uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host;
  uc.dwHostNameLength = sizeof(host) / sizeof(host[0]);
  uc.lpszUrlPath = urlPath;
  uc.dwUrlPathLength = sizeof(urlPath) / sizeof(urlPath[0]);
  if (!api.pCrackUrl(url, 0, 0, &uc))
    return false;

  HINTERNET hSession = api.pOpen(L"UnlockZip/2.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession)
    return false;
  if (api.pSetTimeouts)
    api.pSetTimeouts(hSession, 8000, 8000, 10000, 60000);

  const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
  HINTERNET hConnect = api.pConnect(hSession, host, uc.nPort, 0);
  HINTERNET hRequest = NULL;
  bool ok = false;
  if (hConnect)
  {
    hRequest = api.pOpenRequest(hConnect, L"GET", urlPath, NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (hRequest)
    {
      if (api.pSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
          && api.pReceiveResponse(hRequest, NULL))
      {
        HANDLE hFile = ::CreateFileW(destPath, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
          DWORD totalWritten = 0;
          ok = true;
          for (;;)
          {
            DWORD avail = 0;
            if (!api.pQueryData(hRequest, &avail) || avail == 0)
              break;
            if (avail > 65536)
              avail = 65536;
            BYTE *buf = new BYTE[avail];
            DWORD readBytes = 0;
            if (api.pReadData(hRequest, buf, avail, &readBytes) && readBytes > 0)
            {
              DWORD written = 0;
              if (!::WriteFile(hFile, buf, readBytes, &written, NULL) || written != readBytes)
                ok = false;
              else
                totalWritten += written;
            }
            delete []buf;
            if (!ok || readBytes == 0)
              break;
          }
          ::CloseHandle(hFile);
          if (totalWritten < 100 * 1024)
            ok = false;
        }
      }
      api.pClose(hRequest);
    }
    api.pClose(hConnect);
  }
  api.pClose(hSession);
  return ok;
}

bool ExtractJsonStringField(const AString &json, const char *key, AString &outVal, int startPos = 0)
{
  AString pat("\"");
  pat += key;
  pat += "\"";
  int pos = json.Find(pat, (unsigned)startPos);
  if (pos < 0)
    return false;
  pos = json.Find(':', (unsigned)pos + pat.Len());
  if (pos < 0)
    return false;
  pos = json.Find('"', (unsigned)pos + 1);
  if (pos < 0)
    return false;
  int end = json.Find('"', (unsigned)pos + 1);
  if (end < 0)
    return false;
  outVal.SetFrom(json.Ptr((unsigned)pos + 1), (unsigned)(end - pos - 1));
  return true;
}

bool ParseVersionTuple(const char *tag, unsigned &maj, unsigned &min, unsigned &pat)
{
  if (!tag)
    return false;
  if (*tag == 'v' || *tag == 'V')
    tag++;
  maj = min = pat = 0;
  unsigned parts[3] = { 0, 0, 0 };
  unsigned idx = 0;
  bool anyDigit = false;
  for (const char *p = tag; *p && idx < 3; p++)
  {
    if (*p >= '0' && *p <= '9')
    {
      parts[idx] = parts[idx] * 10 + (unsigned)(*p - '0');
      anyDigit = true;
    }
    else if (*p == '.')
    {
      idx++;
    }
    else
    {
      break;
    }
  }
  if (!anyDigit)
    return false;
  maj = parts[0];
  min = parts[1];
  pat = parts[2];
  return true;
}

bool IsNewerVersion(unsigned maj, unsigned min, unsigned pat)
{
  if (maj != kCurMajor) return maj > kCurMajor;
  if (min != kCurMinor) return min > kCurMinor;
  return pat > kCurPatch;
}

DWORD WINAPI UpdateCheckThread(LPVOID param)
{
  CThreadArg *arg = (CThreadArg *)param;
  const HWND hwndNotify = arg->HwndNotify;
  const bool manualCheck = arg->ManualCheck;
  delete arg;

  CUpdateResult *res = new CUpdateResult();
  res->ManualCheck = manualCheck;

  CWinHttpApi api;
  AString json;
  if (api.Load() && HttpGetToBuffer(api, kApiHost, INTERNET_DEFAULT_HTTPS_PORT, kApiPath, true, json, 512 * 1024))
  {
    AString tag;
    if (ExtractJsonStringField(json, "tag_name", tag))
    {
      res->FetchOk = true;
      for (unsigned i = 0; i < tag.Len(); i++)
        res->LatestTag += (wchar_t)(Byte)tag[i];

      unsigned maj = 0, min = 0, pat = 0;
      if (ParseVersionTuple(tag, maj, min, pat) && IsNewerVersion(maj, min, pat))
      {
        res->HasUpdate = true;
        int searchPos = 0;
        for (;;)
        {
          AString url;
          int foundPos = json.Find("\"browser_download_url\"", (unsigned)searchPos);
          if (foundPos < 0)
            break;
          if (ExtractJsonStringField(json, "browser_download_url", url, foundPos))
          {
            if (url.Find("setup.exe") >= 0)
            {
              for (unsigned i = 0; i < url.Len(); i++)
                res->DownloadUrl += (wchar_t)(Byte)url[i];
              break;
            }
          }
          searchPos = foundPos + 20;
        }
      }
    }
  }

  // Record check timestamp
  {
    HKEY hKey = NULL;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegKeyPath, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
      FILETIME ft;
      ::GetSystemTimeAsFileTime(&ft);
      ULARGE_INTEGER uli;
      uli.LowPart = ft.dwLowDateTime;
      uli.HighPart = ft.dwHighDateTime;
      const DWORD nowHours = (DWORD)(uli.QuadPart / 36000000000ULL);
      ::RegSetValueExW(hKey, L"LastUpdateCheckHours", 0, REG_DWORD,
          (const BYTE *)&nowHours, sizeof(nowHours));
      ::RegCloseKey(hKey);
    }
  }

  if (::IsWindow(hwndNotify) && (manualCheck || res->HasUpdate))
  {
    if (!::PostMessageW(hwndNotify, kMsgUpdateCheckResult, 0, (LPARAM)res))
      delete res;
  }
  else
  {
    delete res;
  }
  return 0;
}

} // namespace

bool IsAutoUpdateEnabled()
{
  HKEY hKey = NULL;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegKeyPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return true; // Enabled by default
  DWORD val = 1;
  DWORD sz = sizeof(val);
  const LONG r = ::RegQueryValueExW(hKey, L"AutoCheckUpdate", NULL, NULL, (LPBYTE)&val, &sz);
  ::RegCloseKey(hKey);
  if (r != ERROR_SUCCESS)
    return true;
  return val != 0;
}

void SetAutoUpdateEnabled(bool enabled)
{
  HKEY hKey = NULL;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegKeyPath, 0, NULL,
      REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
  {
    const DWORD val = enabled ? 1u : 0u;
    ::RegSetValueExW(hKey, L"AutoCheckUpdate", 0, REG_DWORD, (const BYTE *)&val, sizeof(val));
    ::RegCloseKey(hKey);
  }
}

void StartUpdateCheckAsync(HWND hwndNotify, bool manualCheck)
{
  if (!hwndNotify)
    return;

  if (!manualCheck)
  {
    if (!IsAutoUpdateEnabled())
      return;

    // Throttle automatic checks to once every 24 hours
    HKEY hKey = NULL;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegKeyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
      DWORD lastHours = 0;
      DWORD sz = sizeof(lastHours);
      const LONG r = ::RegQueryValueExW(hKey, L"LastUpdateCheckHours", NULL, NULL, (LPBYTE)&lastHours, &sz);
      ::RegCloseKey(hKey);
      if (r == ERROR_SUCCESS)
      {
        FILETIME ft;
        ::GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        const DWORD nowHours = (DWORD)(uli.QuadPart / 36000000000ULL);
        if (nowHours >= lastHours && (nowHours - lastHours) < 24u)
          return;
      }
    }
  }

  CThreadArg *arg = new CThreadArg();
  arg->HwndNotify = hwndNotify;
  arg->ManualCheck = manualCheck;
  HANDLE hThread = ::CreateThread(NULL, 0, UpdateCheckThread, arg, 0, NULL);
  if (hThread)
    ::CloseHandle(hThread);
  else
    delete arg;
}

void HandleUpdateCheckResult(HWND hwndParent, LPARAM lParam)
{
  CUpdateResult *res = (CUpdateResult *)lParam;
  if (!res)
    return;

  const wchar_t *title = NUiLang::Tr(L"UnlockZip Update", L"UnlockZip \x66F4\x65B0");

  if (!res->FetchOk)
  {
    if (res->ManualCheck)
    {
      ::MessageBoxW(hwndParent,
          NUiLang::Tr(
              L"Failed to check for updates. Please check your network connection.",
              L"\x68C0\x67E5\x66F4\x65B0\x5931\x8D25\xFF0C\x8BF7\x68C0\x67E5\x7F51\x7EDC\x8FDE\x63A5\x540E\x91CD\x8BD5\x3002"),
          title, MB_ICONWARNING | MB_OK);
    }
    delete res;
    return;
  }

  if (!res->HasUpdate)
  {
    if (res->ManualCheck)
    {
      wchar_t msg[256];
      wsprintfW(msg,
          NUiLang::Tr(
              L"UnlockZip is already up to date (%s).",
              L"\x5F53\x524D\x5DF2\x662F\x6700\x65B0\x7248\x672C (%s)\x3002"),
          kCurVersionStr);
      ::MessageBoxW(hwndParent, msg, title, MB_ICONINFORMATION | MB_OK);
    }
    delete res;
    return;
  }

  wchar_t prompt[512];
  wsprintfW(prompt,
      NUiLang::Tr(
          L"A new version of UnlockZip (%s) is available (current: %s).\n\nDownload and install now?",
          L"\x53D1\x73B0 UnlockZip \x65B0\x7248\x672C %s\xFF08\x5F53\x524D\x7248\x672C %s\xFF09\x3002\n\n\x662F\x5426\x7ACB\x5373\x4E0B\x8F7D\x5E76\x5B89\x88C5\x66F4\x65B0\xFF1F"),
      (const wchar_t *)res->LatestTag, kCurVersionStr);

  if (::MessageBoxW(hwndParent, prompt, title, MB_ICONQUESTION | MB_YESNO) == IDYES)
  {
    bool launched = false;
    if (!res->DownloadUrl.IsEmpty())
    {
      wchar_t tempDir[MAX_PATH];
      if (::GetTempPathW(MAX_PATH, tempDir) > 0)
      {
        UString setupPath(tempDir);
        setupPath += L"UnlockZip-setup-update.exe";
        CWinHttpApi api;
        if (api.Load() && HttpDownloadFile(api, res->DownloadUrl, setupPath))
        {
          if ((INT_PTR)::ShellExecuteW(NULL, L"open", setupPath, NULL, NULL, SW_SHOWNORMAL) > 32)
          {
            launched = true;
            if (hwndParent)
              ::PostMessageW(hwndParent, WM_CLOSE, 0, 0);
          }
        }
      }
    }
    if (!launched)
    {
      ::ShellExecuteW(NULL, L"open",
          L"https://github.com/Xitaoyou/7-Zip-ZS-PwFree/releases/latest",
          NULL, NULL, SW_SHOWNORMAL);
    }
  }

  delete res;
}

} // namespace NAutoUpdate
