// ModernTheme.cpp

#include "StdAfx.h"

#include <CommCtrl.h>

#include "ModernTheme.h"

#ifdef ZIP7_DARKMODE
#include "../../../../DarkMode/lib/include/Darkmodelib.h"
#endif

#ifndef CLEARTYPE_QUALITY
#define CLEARTYPE_QUALITY 5
#endif
#ifndef LVS_EX_DOUBLEBUFFER
#define LVS_EX_DOUBLEBUFFER 0x00010000
#endif

namespace NModernUI {

namespace {

// Win10+ DWM attributes defined here for older SDK headers.
const DWORD kDwmwaUseImmersiveDarkMode = 20;
const DWORD kDwmwaWindowCornerPreference = 33;
const DWORD kDwmwcpRound = 2;

const wchar_t * const kPropOrigHeaderProc = L"7zFM_OrigHeaderProc";
const wchar_t * const kPropOrigStatusProc = L"7zFM_OrigStatusProc";

struct CFontCacheEntry
{
  UINT Dpi;
  int PointSize10;
  int Weight;
  HFONT Handle;
};

const unsigned kMaxCachedFonts = 16;
CFontCacheEntry g_FontCache[kMaxCachedFonts];
unsigned g_FontCacheCount = 0;

void ApplyUxTheme(HWND hWnd, LPCWSTR subApp)
{
  typedef HRESULT (WINAPI *PFN_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
  static HMODULE hUx = ::LoadLibraryW(L"uxtheme.dll");
  static PFN_SetWindowTheme pfn = hUx
      ? (PFN_SetWindowTheme)(void *)::GetProcAddress(hUx, "SetWindowTheme")
      : NULL;
  if (pfn && hWnd)
    pfn(hWnd, subApp, NULL);
}

bool QueryWindowsAppsDarkMode()
{
  HKEY hKey = NULL;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return false;
  DWORD value = 1;
  DWORD size = sizeof(value);
  const LONG res = ::RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL,
      (LPBYTE)&value, &size);
  ::RegCloseKey(hKey);
  return (res == ERROR_SUCCESS && value == 0);
}

void DrawRoundedRect(HDC hdc, const RECT &rc, int radius, COLORREF fill, COLORREF border, bool drawBorder)
{
  HBRUSH br = ::CreateSolidBrush(fill);
  HPEN pen = drawBorder ? ::CreatePen(PS_SOLID, 1, border) : (HPEN)::GetStockObject(NULL_PEN);
  HGDIOBJ oldBr = ::SelectObject(hdc, br);
  HGDIOBJ oldPen = ::SelectObject(hdc, pen);
  ::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
  ::SelectObject(hdc, oldBr);
  ::SelectObject(hdc, oldPen);
  ::DeleteObject(br);
  if (drawBorder)
    ::DeleteObject(pen);
}

// Flat ListView Header subclass: cleaner 30dip height, subtle bottom divider, no 3D bevels.
LRESULT CALLBACK ModernHeaderProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC origProc = (WNDPROC)::GetPropW(hWnd, kPropOrigHeaderProc);
  if (!origProc)
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);

  if (msg == HDM_LAYOUT)
  {
    const LRESULT res = ::CallWindowProcW(origProc, hWnd, msg, wParam, lParam);
    HDLAYOUT *layout = (HDLAYOUT *)lParam;
    if (layout && layout->pwpos && layout->prc)
    {
      const UINT dpi = GetDpiForHwnd(hWnd);
      const int targetH = ScaleDip(30, dpi);
      if (layout->pwpos->cy < targetH)
      {
        const int delta = targetH - layout->pwpos->cy;
        layout->pwpos->cy = targetH;
        layout->prc->top += delta;
      }
    }
    return res;
  }

  if (msg == WM_PAINT)
  {
    PAINTSTRUCT ps;
    HDC hdc = ::BeginPaint(hWnd, &ps);
    RECT rc;
    ::GetClientRect(hWnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0)
    {
      ::EndPaint(hWnd, &ps);
      return 0;
    }

    HDC memDC = ::CreateCompatibleDC(hdc);
    HBITMAP memBmp = ::CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = ::SelectObject(memDC, memBmp);

    const CPalette pal = GetPalette();
    const UINT dpi = GetDpiForHwnd(hWnd);

    HBRUSH bgBr = ::CreateSolidBrush(pal.BgHeader);
    ::FillRect(memDC, &rc, bgBr);
    ::DeleteObject(bgBr);

    HFONT font = GetCachedUiFont(dpi, 90, FW_SEMIBOLD);
    HGDIOBJ oldFont = font ? ::SelectObject(memDC, font) : NULL;
    ::SetBkMode(memDC, TRANSPARENT);
    ::SetTextColor(memDC, pal.TextSecondary);

    const int count = Header_GetItemCount(hWnd);
    const int padX = ScaleDip(8, dpi);

    HPEN divPen = ::CreatePen(PS_SOLID, 1, pal.BorderSubtle);
    HGDIOBJ oldPen = ::SelectObject(memDC, divPen);

    for (int i = 0; i < count; i++)
    {
      RECT itemRc;
      if (!Header_GetItemRect(hWnd, i, &itemRc))
        continue;

      wchar_t text[256];
      text[0] = 0;
      HDITEMW hdi;
      memset(&hdi, 0, sizeof(hdi));
      hdi.mask = HDI_TEXT | HDI_FORMAT;
      hdi.pszText = text;
      hdi.cchTextMax = 255;
      Header_GetItem(hWnd, i, &hdi);

      RECT textRc = itemRc;
      textRc.left += padX;
      textRc.right -= padX;
      UINT fmt = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
      if ((hdi.fmt & HDF_RIGHT) != 0)
        fmt |= DT_RIGHT;
      else if ((hdi.fmt & HDF_CENTER) != 0)
        fmt |= DT_CENTER;
      else
        fmt |= DT_LEFT;

      ::DrawTextW(memDC, text, -1, &textRc, fmt);

      if (i + 1 < count)
      {
        const int sepPad = ScaleDip(7, dpi);
        ::MoveToEx(memDC, itemRc.right - 1, itemRc.top + sepPad, NULL);
        ::LineTo(memDC, itemRc.right - 1, itemRc.bottom - sepPad);
      }
    }

    ::MoveToEx(memDC, rc.left, rc.bottom - 1, NULL);
    ::LineTo(memDC, rc.right, rc.bottom - 1);

    ::SelectObject(memDC, oldPen);
    ::DeleteObject(divPen);
    if (oldFont)
      ::SelectObject(memDC, oldFont);

    ::BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(memBmp);
    ::DeleteDC(memDC);

    ::EndPaint(hWnd, &ps);
    return 0;
  }

  if (msg == WM_ERASEBKGND)
    return 1;

  if (msg == WM_NCDESTROY)
  {
    ::RemovePropW(hWnd, kPropOrigHeaderProc);
    ::SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)origProc);
  }

  return ::CallWindowProcW(origProc, hWnd, msg, wParam, lParam);
}

// Flat modern StatusBar subclass: 26dip height, 1px top border, PwFree accent pill.
LRESULT CALLBACK ModernStatusBarProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC origProc = (WNDPROC)::GetPropW(hWnd, kPropOrigStatusProc);
  if (!origProc)
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);

  if (msg == WM_ERASEBKGND)
    return 1;

  if (msg == WM_PAINT)
  {
    PAINTSTRUCT ps;
    HDC hdc = ::BeginPaint(hWnd, &ps);
    RECT rc;
    ::GetClientRect(hWnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0)
    {
      ::EndPaint(hWnd, &ps);
      return 0;
    }

    HDC memDC = ::CreateCompatibleDC(hdc);
    HBITMAP memBmp = ::CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = ::SelectObject(memDC, memBmp);

    const CPalette pal = GetPalette();
    const UINT dpi = GetDpiForHwnd(hWnd);

    HBRUSH bgBr = ::CreateSolidBrush(pal.BgSurface);
    ::FillRect(memDC, &rc, bgBr);
    ::DeleteObject(bgBr);

    HPEN topPen = ::CreatePen(PS_SOLID, 1, pal.BorderSubtle);
    HGDIOBJ oldPen = ::SelectObject(memDC, topPen);
    ::MoveToEx(memDC, rc.left, rc.top, NULL);
    ::LineTo(memDC, rc.right, rc.top);
    ::SelectObject(memDC, oldPen);
    ::DeleteObject(topPen);

    HFONT font = GetCachedUiFont(dpi, 88, FW_NORMAL);
    HGDIOBJ oldFont = font ? ::SelectObject(memDC, font) : NULL;
    ::SetBkMode(memDC, TRANSPARENT);

    int parts[16];
    int numParts = (int)::SendMessageW(hWnd, SB_GETPARTS, 16, (LPARAM)parts);
    if (numParts <= 0)
      numParts = 1;

    const int padX = ScaleDip(10, dpi);
    int leftX = padX;
    int rightX = rc.right - padX;

    for (int i = 0; i < numParts; i++)
    {
      wchar_t buf[512];
      buf[0] = 0;
      ::SendMessageW(hWnd, SB_GETTEXTW, (WPARAM)i, (LPARAM)buf);
      if (buf[0] == 0)
        continue;

      const bool isPwFree = (wcsncmp(buf, L"PwFree:", 7) == 0);
      SIZE sz = { 0, 0 };
      ::GetTextExtentPoint32W(memDC, buf, (int)wcslen(buf), &sz);

      if (isPwFree)
      {
        const int pillPadX = ScaleDip(8, dpi);
        const int pillH = ScaleDip(18, dpi);
        const int pillW = sz.cx + pillPadX * 2;
        RECT pillRc;
        pillRc.right = rightX;
        pillRc.left = pillRc.right - pillW;
        if (pillRc.left > leftX + padX)
        {
          pillRc.top = rc.top + (rc.bottom - rc.top - pillH) / 2;
          pillRc.bottom = pillRc.top + pillH;
          DrawRoundedRect(memDC, pillRc, ScaleDip(9, dpi), pal.PillBg, pal.Accent, false);
          ::SetTextColor(memDC, pal.PillText);
          ::DrawTextW(memDC, buf, -1, &pillRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
          rightX = pillRc.left - ScaleDip(8, dpi);
        }
      }
      else
      {
        RECT textRc = { leftX, rc.top + 1, rightX, rc.bottom };
        if (textRc.left < textRc.right)
        {
          ::SetTextColor(memDC, (i == 0) ? pal.TextPrimary : pal.TextSecondary);
          ::DrawTextW(memDC, buf, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
          leftX += sz.cx + ScaleDip(18, dpi);
        }
      }
    }

    if (oldFont)
      ::SelectObject(memDC, oldFont);

    ::BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(memBmp);
    ::DeleteDC(memDC);

    ::EndPaint(hWnd, &ps);
    return 0;
  }

  if (msg == WM_NCDESTROY)
  {
    ::RemovePropW(hWnd, kPropOrigStatusProc);
    ::SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)origProc);
  }

  return ::CallWindowProcW(origProc, hWnd, msg, wParam, lParam);
}

} // namespace

void FillRoundRectGdi(HDC hdc, const RECT &rc, int radius, COLORREF fill, COLORREF border, bool drawBorder)
{
  DrawRoundedRect(hdc, rc, radius, fill, border, drawBorder);
}

bool IsDarkThemeActive()
{
#ifdef ZIP7_DARKMODE
  return dmlib::isEnabled();
#else
  return QueryWindowsAppsDarkMode();
#endif
}

CPalette GetPalette()
{
  CPalette p;
  p.IsDark = IsDarkThemeActive();
  if (p.IsDark)
  {
    p.BgSurface     = RGB(32, 32, 32);
    p.BgContent     = RGB(25, 25, 25);
    p.BgHeader      = RGB(36, 36, 36);
    p.BgHover       = RGB(48, 48, 48);
    p.BgPressed     = RGB(58, 58, 58);
    p.BgSelected    = RGB(43, 61, 84);
    p.BorderSubtle  = RGB(54, 54, 54);
    p.TextPrimary   = RGB(243, 243, 243);
    p.TextSecondary = RGB(166, 166, 166);
    p.Accent        = RGB(96, 205, 255);
    p.AccentBg      = RGB(30, 58, 76);
    p.PillBg        = RGB(30, 62, 82);
    p.PillText      = RGB(130, 218, 255);
    p.Danger        = RGB(248, 113, 113);
    p.Success       = RGB(74, 222, 128);
  }
  else
  {
    p.BgSurface     = RGB(249, 249, 249);
    p.BgContent     = RGB(255, 255, 255);
    p.BgHeader      = RGB(246, 247, 249);
    p.BgHover       = RGB(236, 238, 241);
    p.BgPressed     = RGB(224, 228, 233);
    p.BgSelected    = RGB(229, 243, 255);
    p.BorderSubtle  = RGB(229, 231, 235);
    p.TextPrimary   = RGB(26, 26, 26);
    p.TextSecondary = RGB(102, 108, 117);
    p.Accent        = RGB(0, 103, 192);
    p.AccentBg      = RGB(229, 243, 255);
    p.PillBg        = RGB(227, 242, 253);
    p.PillText      = RGB(0, 90, 170);
    p.Danger        = RGB(209, 52, 56);
    p.Success       = RGB(16, 124, 65);
  }
  return p;
}

UINT GetDpiForHwnd(HWND hWnd)
{
  typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
  static PFN_GetDpiForWindow pfn = (PFN_GetDpiForWindow)(void *)::GetProcAddress(
      ::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
  if (pfn && hWnd)
  {
    const UINT dpi = pfn(hWnd);
    if (dpi >= 96)
      return dpi;
  }
  HDC hdc = ::GetDC(hWnd);
  int dpi = hdc ? ::GetDeviceCaps(hdc, LOGPIXELSX) : 96;
  if (hdc)
    ::ReleaseDC(hWnd, hdc);
  return (dpi >= 96) ? (UINT)dpi : 96u;
}

int ScaleDip(int dip, UINT dpi)
{
  return ::MulDiv(dip, (int)dpi, 96);
}

HFONT GetCachedUiFont(UINT dpi, int pointSize10, int weight)
{
  for (unsigned i = 0; i < g_FontCacheCount; i++)
  {
    if (g_FontCache[i].Dpi == dpi
        && g_FontCache[i].PointSize10 == pointSize10
        && g_FontCache[i].Weight == weight)
      return g_FontCache[i].Handle;
  }

  LOGFONTW lf;
  memset(&lf, 0, sizeof(lf));
  lf.lfHeight = -::MulDiv(pointSize10, (int)dpi, 720);
  lf.lfWeight = weight;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Segoe UI");
  HFONT hFont = ::CreateFontIndirectW(&lf);
  if (hFont && g_FontCacheCount < kMaxCachedFonts)
  {
    g_FontCache[g_FontCacheCount].Dpi = dpi;
    g_FontCache[g_FontCacheCount].PointSize10 = pointSize10;
    g_FontCache[g_FontCacheCount].Weight = weight;
    g_FontCache[g_FontCacheCount].Handle = hFont;
    g_FontCacheCount++;
  }
  return hFont;
}

void ApplyWindowChrome(HWND hWnd)
{
  if (!hWnd)
    return;
  HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
  if (!dwm)
    return;
  typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
  PFN_DwmSetWindowAttribute pfn = (PFN_DwmSetWindowAttribute)(void *)::GetProcAddress(
      dwm, "DwmSetWindowAttribute");
  if (pfn)
  {
    const BOOL dark = IsDarkThemeActive() ? TRUE : FALSE;
    pfn(hWnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));
    const DWORD corner = kDwmwcpRound;
    pfn(hWnd, kDwmwaWindowCornerPreference, &corner, sizeof(corner));
  }
  ::FreeLibrary(dwm);
}

void StyleListView(HWND hListView)
{
  if (!hListView)
    return;

  LONG_PTR exStyle = ::GetWindowLongPtrW(hListView, GWL_EXSTYLE);
  if ((exStyle & WS_EX_CLIENTEDGE) != 0)
  {
    exStyle &= ~(LONG_PTR)WS_EX_CLIENTEDGE;
    ::SetWindowLongPtrW(hListView, GWL_EXSTYLE, exStyle);
    ::SetWindowPos(hListView, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }

  DWORD lvEx = ListView_GetExtendedListViewStyle(hListView);
  lvEx |= LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT;
  ListView_SetExtendedListViewStyle(hListView, lvEx);

  const UINT dpi = GetDpiForHwnd(hListView);
  const CPalette pal = GetPalette();
  ListView_SetBkColor(hListView, pal.BgContent);
  ListView_SetTextBkColor(hListView, pal.BgContent);
  ListView_SetTextColor(hListView, pal.TextPrimary);

  HFONT font = GetCachedUiFont(dpi, 95, FW_NORMAL);
  if (font)
    ::SendMessageW(hListView, WM_SETFONT, (WPARAM)font, TRUE);

  ApplyUxTheme(hListView, pal.IsDark ? L"DarkMode_Explorer" : L"Explorer");

  HWND hHeader = ListView_GetHeader(hListView);
  if (hHeader && !::GetPropW(hHeader, kPropOrigHeaderProc))
  {
    WNDPROC orig = (WNDPROC)::SetWindowLongPtrW(hHeader, GWLP_WNDPROC, (LONG_PTR)ModernHeaderProc);
    if (orig)
      ::SetPropW(hHeader, kPropOrigHeaderProc, (HANDLE)orig);
  }
}

void StyleStatusBar(HWND hStatusBar)
{
  if (!hStatusBar)
    return;
  const UINT dpi = GetDpiForHwnd(hStatusBar);
  ::SendMessageW(hStatusBar, SB_SETMINHEIGHT, (WPARAM)ScaleDip(26, dpi), 0);
  if (!::GetPropW(hStatusBar, kPropOrigStatusProc))
  {
    WNDPROC orig = (WNDPROC)::SetWindowLongPtrW(hStatusBar, GWLP_WNDPROC, (LONG_PTR)ModernStatusBarProc);
    if (orig)
      ::SetPropW(hStatusBar, kPropOrigStatusProc, (HANDLE)orig);
  }
}

} // namespace NModernUI
