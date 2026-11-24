// ModernTheme.h

#ifndef ZIP7_INC_MODERN_THEME_H
#define ZIP7_INC_MODERN_THEME_H

#include "../../../Common/MyWindows.h"
#include "../../../Common/MyString.h"

namespace NModernUI {

struct CPalette
{
  bool IsDark;
  COLORREF BgSurface;
  COLORREF BgContent;
  COLORREF BgHeader;
  COLORREF BgHover;
  COLORREF BgPressed;
  COLORREF BgSelected;
  COLORREF BorderSubtle;
  COLORREF TextPrimary;
  COLORREF TextSecondary;
  COLORREF Accent;
  COLORREF AccentBg;
  COLORREF PillBg;
  COLORREF PillText;
  COLORREF Danger;
  COLORREF Success;
};

// Returns true when dark mode is currently active (via DarkMode lib or OS theme).
bool IsDarkThemeActive();

// Returns the Bandizip-inspired light or dark color palette.
CPalette GetPalette();

// Per-Monitor DPI helpers with graceful fallback on older Windows.
UINT GetDpiForHwnd(HWND hWnd);
int ScaleDip(int dip, UINT dpi);

// Returns a cached modern UI font (do NOT call DeleteObject on the returned handle).
HFONT GetCachedUiFont(UINT dpi, int pointSize10, int weight = FW_NORMAL);

// Applies Win11 rounded corners and Win10/11 immersive dark/light title bar.
void ApplyWindowChrome(HWND hWnd);

// Removes classic 3D sunken edges, enables double buffering, applies modern
// typography, and subclasses the ListView header for flat taller styling.
void StyleListView(HWND hListView);

// Subclasses a standard Win32 StatusBar to render as a clean 26dip flat bar
// with a subtle 1px top border and an accent capsule pill for PwFree notes.
void StyleStatusBar(HWND hStatusBar);

// Helper to draw a rounded rectangle using pure Win32 GDI.
void FillRoundRectGdi(HDC hdc, const RECT &rc, int radius, COLORREF fill, COLORREF border, bool drawBorder);

}

#endif
