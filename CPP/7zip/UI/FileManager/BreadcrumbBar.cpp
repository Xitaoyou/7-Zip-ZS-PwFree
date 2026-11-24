// BreadcrumbBar.cpp

#include "StdAfx.h"

#include "ModernTheme.h"
#include "BreadcrumbBar.h"
#include "../Common/UiLang.h"

extern HINSTANCE g_hInstance;

namespace NModernUI {

namespace {

const wchar_t * const kBreadcrumbClass = L"7zFM_BreadcrumbBar";
bool g_BreadcrumbClassRegistered = false;

bool LooksLikeArchiveName(const UString &name)
{
  const int dot = name.ReverseFind_Dot();
  if (dot < 0)
    return false;
  UString ext = name.Ptr((unsigned)dot + 1);
  ext.MakeLower_Ascii();
  return ext == L"7z" || ext == L"zip" || ext == L"rar" || ext == L"tar"
      || ext == L"gz" || ext == L"xz" || ext == L"zst" || ext == L"bz2"
      || ext == L"iso" || ext == L"cab" || ext == L"wim";
}

} // namespace

bool CBreadcrumbBar::Create(HWND hParent)
{
  Destroy();

  if (!g_BreadcrumbClassRegistered)
  {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CBreadcrumbBar::WndProc;
    wc.hInstance = g_hInstance;
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kBreadcrumbClass;
    if (::RegisterClassW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
      g_BreadcrumbClassRegistered = true;
  }

  _hParent = hParent;
  _hotPart = -2;
  _pressedPart = -2;
  _trackingMouse = false;

  _hWnd = ::CreateWindowExW(
      0, kBreadcrumbClass, L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
      0, 0, 100, GetPreferredHeight(),
      hParent, NULL, g_hInstance, this);
  return _hWnd != NULL;
}

void CBreadcrumbBar::Destroy()
{
  if (_hWnd)
  {
    ::DestroyWindow(_hWnd);
    _hWnd = NULL;
  }
  _segments.Clear();
}

int CBreadcrumbBar::GetPreferredHeight() const
{
  const UINT dpi = GetDpiForHwnd(_hWnd ? _hWnd : _hParent);
  return ScaleDip(36, dpi);
}

void CBreadcrumbBar::Move(int x, int y, int width, int height)
{
  if (!_hWnd)
    return;
  ::MoveWindow(_hWnd, x, y, width, height, TRUE);
  RecalcLayout();
}

void CBreadcrumbBar::Show(bool visible)
{
  if (_hWnd)
    ::ShowWindow(_hWnd, visible ? SW_SHOW : SW_HIDE);
}

void CBreadcrumbBar::Invalidate()
{
  if (_hWnd)
    ::InvalidateRect(_hWnd, NULL, FALSE);
}

void CBreadcrumbBar::SetPath(const UString &path)
{
  if (_path == path)
    return;
  _path = path;
  RebuildSegments();
  RecalcLayout();
  Invalidate();
}

bool CBreadcrumbBar::GetSegmentPath(unsigned index, UString &outPath) const
{
  if (index >= _segments.Size())
    return false;
  outPath = _segments[index].FullPath;
  return true;
}

void CBreadcrumbBar::RebuildSegments()
{
  _segments.Clear();
  if (_path.IsEmpty())
  {
    CBreadcrumbSeg &root = _segments.AddNew();
    root.Label = NUiLang::Tr(L"This PC", L"\x6B64\x7535\x8111");
    root.FullPath.Empty();
    return;
  }

  const unsigned len = _path.Len();
  unsigned pos = 0;
  UString accum;

  if (len >= 2 && _path[0] == L'\\' && _path[1] == L'\\')
  {
    accum = L"\\\\";
    pos = 2;
  }

  while (pos < len)
  {
    unsigned end = pos;
    while (end < len && _path[end] != L'\\' && _path[end] != L'/')
      end++;

    if (end > pos)
    {
      UString part;
      part.SetFrom(_path.Ptr(pos), end - pos);
      accum += part;
      if (end < len)
        accum += L'\\';

      CBreadcrumbSeg &seg = _segments.AddNew();
      seg.Label = part;
      seg.FullPath = accum;
      seg.IsArchive = LooksLikeArchiveName(part);
    }

    pos = end;
    while (pos < len && (_path[pos] == L'\\' || _path[pos] == L'/'))
      pos++;
  }
}

void CBreadcrumbBar::RecalcLayout()
{
  if (!_hWnd)
    return;

  RECT rc;
  ::GetClientRect(_hWnd, &rc);
  const UINT dpi = GetDpiForHwnd(_hWnd);
  const int padX = ScaleDip(8, dpi);
  const int padY = ScaleDip(5, dpi);
  const int upSize = ScaleDip(26, dpi);

  _upRect.left = padX;
  _upRect.top = (rc.bottom - rc.top - upSize) / 2;
  _upRect.right = _upRect.left + upSize;
  _upRect.bottom = _upRect.top + upSize;

  HDC hdc = ::GetDC(_hWnd);
  HFONT font = GetCachedUiFont(dpi, 90, FW_NORMAL);
  HGDIOBJ oldFont = (hdc && font) ? ::SelectObject(hdc, font) : NULL;

  int x = _upRect.right + ScaleDip(10, dpi);
  const int segPadX = ScaleDip(7, dpi);
  const int chevW = ScaleDip(14, dpi);
  const int segTop = padY;
  const int segBottom = (rc.bottom > padY) ? (rc.bottom - padY) : (padY + upSize);

  FOR_VECTOR (i, _segments)
  {
    SIZE sz = { ScaleDip(40, dpi), ScaleDip(16, dpi) };
    if (hdc)
      ::GetTextExtentPoint32W(hdc, _segments[i].Label, (int)_segments[i].Label.Len(), &sz);

    const int w = sz.cx + segPadX * 2;
    _segments[i].Rect.left = x;
    _segments[i].Rect.top = segTop;
    _segments[i].Rect.right = x + w;
    _segments[i].Rect.bottom = segBottom;
    x = _segments[i].Rect.right + chevW;
  }

  if (hdc)
  {
    if (oldFont)
      ::SelectObject(hdc, oldFont);
    ::ReleaseDC(_hWnd, hdc);
  }
}

int CBreadcrumbBar::HitTest(int x, int y) const
{
  POINT pt = { x, y };
  if (::PtInRect(&_upRect, pt))
    return -1;
  FOR_VECTOR (i, _segments)
  {
    if (::PtInRect(&_segments[i].Rect, pt))
      return (int)i;
  }
  return -2;
}

void CBreadcrumbBar::OnPaint()
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

  // Up button hover/press background.
  if (_hotPart == -1 || _pressedPart == -1)
  {
    FillRoundRectGdi(memDC, _upRect, ScaleDip(5, dpi),
        (_pressedPart == -1) ? pal.BgPressed : pal.BgHover, pal.BorderSubtle, false);
  }

  // Upward arrow inside _upRect.
  {
    const int ux = (_upRect.left + _upRect.right) / 2;
    const int uy = (_upRect.top + _upRect.bottom) / 2;
    const int ar = ScaleDip(5, dpi);
    LOGBRUSH lb = { BS_SOLID, pal.TextPrimary, 0 };
    HPEN arrowPen = ::ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        2, &lb, 0, NULL);
    HGDIOBJ oldP = ::SelectObject(memDC, arrowPen);
    ::MoveToEx(memDC, ux, uy + ar, NULL);
    ::LineTo(memDC, ux, uy - ar);
    ::MoveToEx(memDC, ux - ar * 3 / 4, uy - ar / 4, NULL);
    ::LineTo(memDC, ux, uy - ar);
    ::LineTo(memDC, ux + ar * 3 / 4, uy - ar / 4);
    ::SelectObject(memDC, oldP);
    ::DeleteObject(arrowPen);
  }

  // Rounded address bar container.
  RECT boxRc;
  boxRc.left = _upRect.right + ScaleDip(6, dpi);
  boxRc.top = ScaleDip(4, dpi);
  boxRc.right = rc.right - ScaleDip(8, dpi);
  boxRc.bottom = rc.bottom - ScaleDip(4, dpi);
  if (boxRc.right > boxRc.left + ScaleDip(20, dpi))
    FillRoundRectGdi(memDC, boxRc, ScaleDip(6, dpi), pal.BgContent, pal.BorderSubtle, true);

  // Segment hover / archive highlight + chevron separators.
  const int chevW = ScaleDip(14, dpi);
  HPEN chevPen = ::CreatePen(PS_SOLID, 1, pal.TextSecondary);
  HGDIOBJ oldChevPen = ::SelectObject(memDC, chevPen);

  FOR_VECTOR (i, _segments)
  {
    const RECT &sr = _segments[i].Rect;
    if (sr.left >= boxRc.right - ScaleDip(8, dpi))
      break;
    const bool isHot = ((int)i == _hotPart);
    const bool isPressed = ((int)i == _pressedPart);

    if (isPressed || isHot || _segments[i].IsArchive)
    {
      COLORREF fill = pal.BgHover;
      if (isPressed) fill = pal.BgPressed;
      else if (!isHot && _segments[i].IsArchive) fill = pal.PillBg;

      RECT pillRc = sr;
      pillRc.top += 1;
      pillRc.bottom -= 1;
      FillRoundRectGdi(memDC, pillRc, ScaleDip(4, dpi), fill, pal.BorderSubtle, false);
    }

    if (i + 1 < _segments.Size() && sr.right + chevW < boxRc.right)
    {
      const int cx = sr.right + chevW / 2;
      const int cy = (sr.top + sr.bottom) / 2;
      const int cr = ScaleDip(3, dpi);
      ::MoveToEx(memDC, cx - cr / 2, cy - cr, NULL);
      ::LineTo(memDC, cx + cr / 2 + 1, cy);
      ::LineTo(memDC, cx - cr / 2, cy + cr);
    }
  }

  ::SelectObject(memDC, oldChevPen);
  ::DeleteObject(chevPen);

  // Draw segment text labels.
  HFONT font = GetCachedUiFont(dpi, 90, FW_NORMAL);
  HFONT boldFont = GetCachedUiFont(dpi, 90, FW_SEMIBOLD);
  HGDIOBJ oldFont = font ? ::SelectObject(memDC, font) : NULL;
  ::SetBkMode(memDC, TRANSPARENT);

  FOR_VECTOR (i, _segments)
  {
    RECT sr = _segments[i].Rect;
    if (sr.left >= boxRc.right - ScaleDip(8, dpi))
      break;
    if (sr.right > boxRc.right - ScaleDip(4, dpi))
      sr.right = boxRc.right - ScaleDip(4, dpi);

    const bool isLast = (i + 1 == _segments.Size());
    HFONT useFont = (isLast || _segments[i].IsArchive) ? boldFont : font;
    if (useFont)
      ::SelectObject(memDC, useFont);
    ::SetTextColor(memDC, _segments[i].IsArchive ? pal.PillText : (isLast ? pal.TextPrimary : pal.TextSecondary));
    ::DrawTextW(memDC, _segments[i].Label, -1, &sr,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  }

  if (oldFont)
    ::SelectObject(memDC, oldFont);

  // Subtle 1px bottom line.
  HPEN btmPen = ::CreatePen(PS_SOLID, 1, pal.BorderSubtle);
  HGDIOBJ oldPen = ::SelectObject(memDC, btmPen);
  ::MoveToEx(memDC, rc.left, rc.bottom - 1, NULL);
  ::LineTo(memDC, rc.right, rc.bottom - 1);
  ::SelectObject(memDC, oldPen);
  ::DeleteObject(btmPen);

  ::BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
  ::SelectObject(memDC, oldBmp);
  ::DeleteObject(memBmp);
  ::DeleteDC(memDC);

  ::EndPaint(_hWnd, &ps);
}

LRESULT CALLBACK CBreadcrumbBar::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  CBreadcrumbBar *self = (CBreadcrumbBar *)::GetWindowLongPtrW(hWnd, GWLP_USERDATA);
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
    self = (CBreadcrumbBar *)cs->lpCreateParams;
    ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)self);
    if (self)
      self->_hWnd = hWnd;
  }
  if (self)
    return self->OnMessage(msg, wParam, lParam);
  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CBreadcrumbBar::OnMessage(UINT msg, WPARAM wParam, LPARAM lParam)
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
      if (hit != _hotPart)
      {
        _hotPart = hit;
        ::InvalidateRect(_hWnd, NULL, FALSE);
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      _trackingMouse = false;
      if (_hotPart != -2 || _pressedPart != -2)
      {
        _hotPart = -2;
        _pressedPart = -2;
        ::InvalidateRect(_hWnd, NULL, FALSE);
      }
      return 0;

    case WM_LBUTTONDOWN:
    {
      const int hit = HitTest((short)LOWORD(lParam), (short)HIWORD(lParam));
      if (hit == -2)
      {
        if (_hParent)
          ::PostMessageW(_hParent, kMsgBreadcrumbEdit, 0, 0);
        return 0;
      }
      _pressedPart = hit;
      ::SetCapture(_hWnd);
      ::InvalidateRect(_hWnd, NULL, FALSE);
      return 0;
    }

    case WM_LBUTTONUP:
    {
      if (::GetCapture() == _hWnd)
        ::ReleaseCapture();
      const int hit = HitTest((short)LOWORD(lParam), (short)HIWORD(lParam));
      const int action = (_pressedPart != -2 && _pressedPart == hit) ? _pressedPart : -2;
      _pressedPart = -2;
      ::InvalidateRect(_hWnd, NULL, FALSE);
      if (_hParent)
      {
        if (action == -1)
          ::PostMessageW(_hParent, kMsgBreadcrumbUp, 0, 0);
        else if (action >= 0)
          ::PostMessageW(_hParent, kMsgBreadcrumbNav, (WPARAM)action, 0);
      }
      return 0;
    }
  }
  return ::DefWindowProcW(_hWnd, msg, wParam, lParam);
}

} // namespace NModernUI
