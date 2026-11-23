// ModernToolbar.cpp

#include "StdAfx.h"

#include "ModernTheme.h"
#include "ModernToolbar.h"

extern HINSTANCE g_hInstance;

namespace NModernUI {

namespace {

const wchar_t * const kToolbarClass = L"7zFM_ModernToolbar";
bool g_ToolbarClassRegistered = false;

COLORREF BlendColor(COLORREF fg, COLORREF bg, int alpha255)
{
  const int inv = 255 - alpha255;
  const int r = (GetRValue(fg) * alpha255 + GetRValue(bg) * inv) / 255;
  const int g = (GetGValue(fg) * alpha255 + GetGValue(bg) * inv) / 255;
  const int b = (GetBValue(fg) * alpha255 + GetBValue(bg) * inv) / 255;
  return RGB(r, g, b);
}

void DrawLineGdi(HDC hdc, HPEN pen, int x1, int y1, int x2, int y2)
{
  HGDIOBJ old = ::SelectObject(hdc, pen);
  ::MoveToEx(hdc, x1, y1, NULL);
  ::LineTo(hdc, x2, y2);
  ::SelectObject(hdc, old);
}

void DrawVectorIcon(HDC hdc, EToolbarIcon icon, int cx, int cy, int size, const CPalette &pal, COLORREF bgRef)
{
  const int half = size / 2;
  const int stroke = (size >= 22) ? 2 : 1;

  COLORREF mainColor = pal.TextPrimary;
  if (icon == kIconAdd)          mainColor = pal.Accent;
  else if (icon == kIconExtract) mainColor = pal.Success;
  else if (icon == kIconTest)    mainColor = pal.IsDark ? RGB(45, 212, 191) : RGB(13, 148, 136);
  else if (icon == kIconDelete)  mainColor = pal.Danger;

  const COLORREF tintColor = BlendColor(mainColor, bgRef, pal.IsDark ? 45 : 32);

  LOGBRUSH lbMain = { BS_SOLID, mainColor, 0 };
  HPEN pen = ::ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
      (DWORD)stroke, &lbMain, 0, NULL);
  LOGBRUSH lbSub = { BS_SOLID, pal.TextSecondary, 0 };
  HPEN subPen = ::ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
      (DWORD)stroke, &lbSub, 0, NULL);
  HBRUSH tintBr = ::CreateSolidBrush(tintColor);
  HBRUSH bgBr = ::CreateSolidBrush(bgRef);

  switch (icon)
  {
    case kIconAdd:
    {
      RECT r = { cx - half + 1, cy - half + 2, cx + half - 1, cy + half - 1 };
      HGDIOBJ oldBr = ::SelectObject(hdc, tintBr);
      HGDIOBJ oldPen = ::SelectObject(hdc, pen);
      ::RoundRect(hdc, r.left, r.top, r.right, r.bottom, 5, 5);
      ::SelectObject(hdc, oldBr);
      ::SelectObject(hdc, oldPen);
      const int arm = size / 4;
      DrawLineGdi(hdc, pen, cx - arm, cy, cx + arm, cy);
      DrawLineGdi(hdc, pen, cx, cy - arm, cx, cy + arm);
      break;
    }

    case kIconExtract:
    {
      const int left = cx - half + 2;
      const int right = cx + half - 2;
      const int bottom = cy + half - 2;
      const int midY = cy + 2;
      const int topY = cy - half + 2;
      DrawLineGdi(hdc, pen, left, midY, left, bottom);
      DrawLineGdi(hdc, pen, left, bottom, right, bottom);
      DrawLineGdi(hdc, pen, right, bottom, right, midY);
      DrawLineGdi(hdc, pen, cx, cy + 3, cx, topY);
      DrawLineGdi(hdc, pen, cx - size / 5, topY + size / 5, cx, topY);
      DrawLineGdi(hdc, pen, cx + size / 5, topY + size / 5, cx, topY);
      break;
    }

    case kIconTest:
    {
      HGDIOBJ oldBr = ::SelectObject(hdc, tintBr);
      HGDIOBJ oldPen = ::SelectObject(hdc, pen);
      ::Ellipse(hdc, cx - half + 1, cy - half + 1, cx + half - 1, cy + half - 1);
      ::SelectObject(hdc, oldBr);
      ::SelectObject(hdc, oldPen);
      DrawLineGdi(hdc, pen, cx - size / 5, cy, cx - size / 16, cy + size / 6);
      DrawLineGdi(hdc, pen, cx - size / 16, cy + size / 6, cx + size / 4, cy - size / 6);
      break;
    }

    case kIconCopy:
    {
      HGDIOBJ oldBr = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
      HGDIOBJ oldPen = ::SelectObject(hdc, subPen);
      ::RoundRect(hdc, cx - half + 1, cy - half + 1, cx + 2, cy + 4, 4, 4);
      ::SelectObject(hdc, bgBr);
      ::SelectObject(hdc, pen);
      ::RoundRect(hdc, cx - 3, cy - 3, cx + half - 1, cy + half - 1, 4, 4);
      ::SelectObject(hdc, oldBr);
      ::SelectObject(hdc, oldPen);
      break;
    }

    case kIconMove:
    {
      HGDIOBJ oldBr = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
      HGDIOBJ oldPen = ::SelectObject(hdc, subPen);
      ::RoundRect(hdc, cx - half + 1, cy - half + 3, cx + 4, cy + half - 2, 4, 4);
      ::SelectObject(hdc, oldBr);
      ::SelectObject(hdc, oldPen);
      const int ax1 = cx - 2;
      const int ax2 = cx + half - 1;
      DrawLineGdi(hdc, pen, ax1, cy, ax2, cy);
      DrawLineGdi(hdc, pen, ax2 - size / 5, cy - size / 5, ax2, cy);
      DrawLineGdi(hdc, pen, ax2 - size / 5, cy + size / 5, ax2, cy);
      break;
    }

    case kIconDelete:
    {
      const int topY = cy - half + 4;
      DrawLineGdi(hdc, pen, cx - half + 2, topY, cx + half - 2, topY);
      DrawLineGdi(hdc, pen, cx - size / 6, topY - 3, cx + size / 6, topY - 3);
      HGDIOBJ oldBr = ::SelectObject(hdc, tintBr);
      HGDIOBJ oldPen = ::SelectObject(hdc, pen);
      ::RoundRect(hdc, cx - half + 4, topY, cx + half - 4, cy + half - 1, 3, 3);
      ::SelectObject(hdc, oldBr);
      ::SelectObject(hdc, oldPen);
      DrawLineGdi(hdc, pen, cx - 2, topY + 4, cx - 2, cy + half - 4);
      DrawLineGdi(hdc, pen, cx + 2, topY + 4, cx + 2, cy + half - 4);
      break;
    }

    case kIconInfo:
    default:
    {
      HGDIOBJ oldBr = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
      HGDIOBJ oldPen = ::SelectObject(hdc, pen);
      ::Ellipse(hdc, cx - half + 1, cy - half + 1, cx + half - 1, cy + half - 1);
      ::SelectObject(hdc, oldBr);
      ::SelectObject(hdc, oldPen);
      DrawLineGdi(hdc, pen, cx, cy - size / 4, cx, cy - size / 4);
      DrawLineGdi(hdc, pen, cx, cy - 1, cx, cy + size / 4);
      break;
    }
  }

  ::DeleteObject(pen);
  ::DeleteObject(subPen);
  ::DeleteObject(tintBr);
  ::DeleteObject(bgBr);
}

} // namespace

bool CModernToolbar::Create(HWND hParent, bool showLabels, bool largeIcons)
{
  Destroy();

  if (!g_ToolbarClassRegistered)
  {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CModernToolbar::WndProc;
    wc.hInstance = g_hInstance;
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kToolbarClass;
    if (::RegisterClassW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
      g_ToolbarClassRegistered = true;
  }

  _hParent = hParent;
  _showLabels = showLabels;
  _largeIcons = largeIcons;
  _hotIndex = -1;
  _pressedIndex = -1;
  _trackingMouse = false;

  _hWnd = ::CreateWindowExW(
      0, kToolbarClass, L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
      0, 0, 100, GetPreferredHeight(),
      hParent, NULL, g_hInstance, this);
  return _hWnd != NULL;
}

void CModernToolbar::Destroy()
{
  if (_hWnd)
  {
    ::DestroyWindow(_hWnd);
    _hWnd = NULL;
  }
  _items.Clear();
  _itemRects.Clear();
  _hotIndex = -1;
  _pressedIndex = -1;
  _trackingMouse = false;
}

void CModernToolbar::ClearItems()
{
  _items.Clear();
  _itemRects.Clear();
  _hotIndex = -1;
  _pressedIndex = -1;
}

void CModernToolbar::AddButton(int commandID, EToolbarIcon icon, const UString &label)
{
  CToolbarItem &item = _items.AddNew();
  item.CommandID = commandID;
  item.Icon = icon;
  item.Label = label;
  item.IsSeparator = false;
  RecalcLayout();
}

void CModernToolbar::AddSeparator()
{
  CToolbarItem &item = _items.AddNew();
  item.IsSeparator = true;
  RecalcLayout();
}

int CModernToolbar::GetPreferredHeight() const
{
  const UINT dpi = GetDpiForHwnd(_hWnd ? _hWnd : _hParent);
  if (_showLabels)
    return ScaleDip(_largeIcons ? 62 : 52, dpi);
  return ScaleDip(_largeIcons ? 46 : 38, dpi);
}

void CModernToolbar::Move(int x, int y, int width, int height)
{
  if (!_hWnd)
    return;
  ::MoveWindow(_hWnd, x, y, width, height, TRUE);
  RecalcLayout();
}

void CModernToolbar::Invalidate()
{
  if (_hWnd)
    ::InvalidateRect(_hWnd, NULL, FALSE);
}

void CModernToolbar::RecalcLayout()
{
  _itemRects.Clear();
  if (!_hWnd)
    return;

  RECT rc;
  ::GetClientRect(_hWnd, &rc);
  const UINT dpi = GetDpiForHwnd(_hWnd);
  const int padX = ScaleDip(10, dpi);
  const int padY = ScaleDip(5, dpi);
  const int gap = ScaleDip(4, dpi);
  const int btnW = ScaleDip(_showLabels ? (_largeIcons ? 64 : 56) : (_largeIcons ? 42 : 36), dpi);
  const int sepW = ScaleDip(12, dpi);
  const int btnH = (rc.bottom - rc.top > padY * 2) ? (rc.bottom - rc.top - padY * 2) : GetPreferredHeight() - padY * 2;

  int x = padX;
  FOR_VECTOR (i, _items)
  {
    RECT r;
    r.top = padY;
    r.bottom = padY + btnH;
    r.left = x;
    if (_items[i].IsSeparator)
    {
      r.right = x + sepW;
      x = r.right + gap;
    }
    else
    {
      r.right = x + btnW;
      x = r.right + gap;
    }
    _itemRects.Add(r);
  }
}

int CModernToolbar::HitTest(int x, int y) const
{
  POINT pt = { x, y };
  FOR_VECTOR (i, _itemRects)
  {
    if (!_items[i].IsSeparator && ::PtInRect(&_itemRects[i], pt))
      return (int)i;
  }
  return -1;
}

void CModernToolbar::OnPaint()
{
  PAINTSTRUCT ps;
  HDC hdc = ::BeginPaint(_hWnd, &ps);
  RECT rc;
  ::GetClientRect(_hWnd, &rc);
  if (rc.right <= 0 || rc.bottom <= 0)
  {
    ::EndPaint(_hWnd, &ps);
    return;
  }

  HDC memDC = ::CreateCompatibleDC(hdc);
  HBITMAP memBmp = ::CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
  HGDIOBJ oldBmp = ::SelectObject(memDC, memBmp);

  const CPalette pal = GetPalette();
  const UINT dpi = GetDpiForHwnd(_hWnd);

  HBRUSH bgBr = ::CreateSolidBrush(pal.BgSurface);
  ::FillRect(memDC, &rc, bgBr);
  ::DeleteObject(bgBr);

  HPEN divPen = ::CreatePen(PS_SOLID, 1, pal.BorderSubtle);
  HGDIOBJ oldPen = ::SelectObject(memDC, divPen);
  ::MoveToEx(memDC, rc.left, rc.bottom - 1, NULL);
  ::LineTo(memDC, rc.right, rc.bottom - 1);

  const int radius = ScaleDip(6, dpi);
  const int iconSize = ScaleDip(_largeIcons ? 22 : 18, dpi);

  FOR_VECTOR (i, _items)
  {
    if (i >= _itemRects.Size())
      break;
    const RECT &r = _itemRects[i];
    if (_items[i].IsSeparator)
    {
      const int mx = (r.left + r.right) / 2;
      const int pad = ScaleDip(10, dpi);
      ::MoveToEx(memDC, mx, r.top + pad, NULL);
      ::LineTo(memDC, mx, r.bottom - pad);
      continue;
    }

    const bool isPressed = ((int)i == _pressedIndex);
    const bool isHot = ((int)i == _hotIndex);
    COLORREF btnBg = pal.BgSurface;
    if (isPressed || isHot)
    {
      btnBg = isPressed ? pal.BgPressed : pal.BgHover;
      FillRoundRectGdi(memDC, r, radius, btnBg, pal.BorderSubtle, false);
    }

    const int cx = (r.left + r.right) / 2;
    const int cy = _showLabels
        ? (r.top + (r.bottom - r.top) * 38 / 100)
        : ((r.top + r.bottom) / 2);
    DrawVectorIcon(memDC, _items[i].Icon, cx, cy, iconSize, pal, btnBg);
  }

  ::SelectObject(memDC, oldPen);
  ::DeleteObject(divPen);

  if (_showLabels)
  {
    HFONT font = CreateUiFont(dpi, 84, FW_NORMAL);
    HGDIOBJ oldFont = ::SelectObject(memDC, font);
    ::SetBkMode(memDC, TRANSPARENT);
    ::SetTextColor(memDC, pal.TextPrimary);

    FOR_VECTOR (i, _items)
    {
      if (i >= _itemRects.Size() || _items[i].IsSeparator)
        continue;
      RECT textRc = _itemRects[i];
      textRc.top = textRc.bottom - ScaleDip(19, dpi);
      ::DrawTextW(memDC, _items[i].Label, -1, &textRc,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    ::SelectObject(memDC, oldFont);
    ::DeleteObject(font);
  }

  ::BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
  ::SelectObject(memDC, oldBmp);
  ::DeleteObject(memBmp);
  ::DeleteDC(memDC);

  ::EndPaint(_hWnd, &ps);
}

LRESULT CALLBACK CModernToolbar::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  CModernToolbar *self = (CModernToolbar *)::GetWindowLongPtrW(hWnd, GWLP_USERDATA);
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
    self = (CModernToolbar *)cs->lpCreateParams;
    ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)self);
    if (self)
      self->_hWnd = hWnd;
  }
  if (self)
    return self->OnMessage(msg, wParam, lParam);
  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CModernToolbar::OnMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_ERASEBKGND:
      return 1;

    case WM_SIZE:
      RecalcLayout();
      ::InvalidateRect(_hWnd, NULL, FALSE);
      return 0;

    case WM_PAINT:
      OnPaint();
      return 0;

    case WM_MOUSEMOVE:
    {
      if (!_trackingMouse)
      {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = _hWnd;
        tme.dwHoverTime = 0;
        ::TrackMouseEvent(&tme);
        _trackingMouse = true;
      }
      const int hit = HitTest((short)LOWORD(lParam), (short)HIWORD(lParam));
      if (hit != _hotIndex)
      {
        _hotIndex = hit;
        ::InvalidateRect(_hWnd, NULL, FALSE);
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      _trackingMouse = false;
      if (_hotIndex != -1 || _pressedIndex != -1)
      {
        _hotIndex = -1;
        _pressedIndex = -1;
        ::InvalidateRect(_hWnd, NULL, FALSE);
      }
      return 0;

    case WM_LBUTTONDOWN:
    {
      const int hit = HitTest((short)LOWORD(lParam), (short)HIWORD(lParam));
      if (hit >= 0)
      {
        _pressedIndex = hit;
        ::SetCapture(_hWnd);
        ::InvalidateRect(_hWnd, NULL, FALSE);
      }
      return 0;
    }

    case WM_LBUTTONUP:
    {
      if (::GetCapture() == _hWnd)
        ::ReleaseCapture();
      const int hit = HitTest((short)LOWORD(lParam), (short)HIWORD(lParam));
      const int cmd = (_pressedIndex >= 0 && _pressedIndex == hit && (unsigned)hit < _items.Size())
          ? _items[(unsigned)hit].CommandID : 0;
      _pressedIndex = -1;
      ::InvalidateRect(_hWnd, NULL, FALSE);
      if (cmd != 0 && _hParent)
        ::PostMessageW(_hParent, WM_COMMAND, MAKEWPARAM(cmd, 0), (LPARAM)_hWnd);
      return 0;
    }
  }
  return ::DefWindowProcW(_hWnd, msg, wParam, lParam);
}

} // namespace NModernUI
