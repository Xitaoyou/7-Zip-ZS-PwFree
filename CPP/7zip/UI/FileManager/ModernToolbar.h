// ModernToolbar.h

#ifndef ZIP7_INC_MODERN_TOOLBAR_H
#define ZIP7_INC_MODERN_TOOLBAR_H

#include "../../../Common/MyWindows.h"
#include "../../../Common/MyString.h"
#include "../../../Common/MyVector.h"

namespace NModernUI {

enum EToolbarIcon
{
  kIconAdd = 0,
  kIconExtract,
  kIconTest,
  kIconCopy,
  kIconMove,
  kIconDelete,
  kIconInfo,
  kIconSettings
};

struct CToolbarItem
{
  int CommandID;
  EToolbarIcon Icon;
  UString Label;
  bool IsSeparator;
  bool RightAligned;

  CToolbarItem(): CommandID(0), Icon(kIconAdd), IsSeparator(false), RightAligned(false) {}
};

class CModernToolbar
{
  HWND _hWnd;
  HWND _hParent;
  CObjectVector<CToolbarItem> _items;
  CRecordVector<RECT> _itemRects;
  int _hotIndex;
  int _pressedIndex;
  bool _trackingMouse;
  bool _showLabels;
  bool _largeIcons;

  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT OnMessage(UINT msg, WPARAM wParam, LPARAM lParam);
  void RecalcLayout();
  int HitTest(int x, int y) const;
  void OnPaint();

public:
  CModernToolbar():
      _hWnd(NULL), _hParent(NULL),
      _hotIndex(-1), _pressedIndex(-1),
      _trackingMouse(false), _showLabels(true), _largeIcons(true) {}

  ~CModernToolbar() { Destroy(); }

  bool Create(HWND hParent, bool showLabels, bool largeIcons);
  void Destroy();
  void ClearItems();
  void AddButton(int commandID, EToolbarIcon icon, const UString &label, bool rightAligned = false);
  void AddSeparator();
  int GetPreferredHeight() const;
  void Move(int x, int y, int width, int height);
  void Invalidate();

  HWND Handle() const { return _hWnd; }
  operator HWND() const { return _hWnd; }
};

}

#endif
