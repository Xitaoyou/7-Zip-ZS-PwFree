// BreadcrumbBar.h

#ifndef ZIP7_INC_BREADCRUMB_BAR_H
#define ZIP7_INC_BREADCRUMB_BAR_H

#include "../../../Common/MyWindows.h"
#include "../../../Common/MyString.h"
#include "../../../Common/MyVector.h"

namespace NModernUI {

// Custom notifications posted to parent panel window:
//   WM_APP + 0x201 : up-folder button clicked
//   WM_APP + 0x202 : breadcrumb segment clicked (wParam = segment index)
//   WM_APP + 0x203 : empty area clicked -> switch to edit combo mode
const UINT kMsgBreadcrumbUp   = WM_APP + 0x201;
const UINT kMsgBreadcrumbNav  = WM_APP + 0x202;
const UINT kMsgBreadcrumbEdit = WM_APP + 0x203;

struct CBreadcrumbSeg
{
  UString Label;
  UString FullPath;
  bool IsArchive;
  RECT Rect;

  CBreadcrumbSeg(): IsArchive(false) { memset(&Rect, 0, sizeof(Rect)); }
};

class CBreadcrumbBar
{
  HWND _hWnd;
  HWND _hParent;
  UString _path;
  CObjectVector<CBreadcrumbSeg> _segments;
  RECT _upRect;
  int _hotPart;     // -2 = none, -1 = up button, >= 0 = segment index
  int _pressedPart;
  bool _trackingMouse;

  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT OnMessage(UINT msg, WPARAM wParam, LPARAM lParam);
  void RebuildSegments();
  void RecalcLayout();
  int HitTest(int x, int y) const;
  void OnPaint();

public:
  CBreadcrumbBar():
      _hWnd(NULL), _hParent(NULL),
      _hotPart(-2), _pressedPart(-2), _trackingMouse(false)
  {
    memset(&_upRect, 0, sizeof(_upRect));
  }

  ~CBreadcrumbBar() { Destroy(); }

  bool Create(HWND hParent);
  void Destroy();
  void SetPath(const UString &path);
  bool GetSegmentPath(unsigned index, UString &outPath) const;
  int GetPreferredHeight() const;
  void Move(int x, int y, int width, int height);
  void Show(bool visible);
  void Invalidate();

  HWND Handle() const { return _hWnd; }
  operator HWND() const { return _hWnd; }
};

}

#endif
