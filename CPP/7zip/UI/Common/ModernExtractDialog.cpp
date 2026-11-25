// ModernExtractDialog.cpp - Bandizip-style native Shell Extract Path dialog (ASCII-safe)

#include "StdAfx.h"

#include <CommCtrl.h>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <ShellAPI.h>

#include "ModernExtractDialog.h"
#include "UiLang.h"
#include "../FileManager/ModernTheme.h"

extern HINSTANCE g_hInstance;

namespace NModernUI {

namespace {

const wchar_t * const kDlgClassName = L"UnlockZip_ExtractPathDlg";
const wchar_t * const kRegKeyPath   = L"Software\\7-Zip-Zstandard\\UnlockZip";

enum
{
  kIdComboPath    = 2001,
  kIdRadioAll     = 2002,
  kIdRadioSel     = 2003,
  kIdBtnNewFolder = 2004,
  kIdChkArcFolder = 2005,
  kIdChkDeleteArc = 2006,
  kIdChkRemember  = 2007,
  kIdBtnAdvanced  = 2008,
  kIdBtnOk        = IDOK,
  kIdBtnCancel    = IDCANCEL,
  kIdFallbackTree = 2009,
  kIdChkOpenDir   = 2010
};

struct CQuickPlace
{
  int Csidl;
  const wchar_t *LabelEn;
  const wchar_t *LabelZh;
  HICON Icon;
  UString Path;
  RECT Rect;
};

typedef HRESULT (WINAPI *PFN_SHCreateItemFromParsingName)(
    PCWSTR pszPath, IBindCtx *pbc, REFIID riid, void **ppv);
typedef HRESULT (WINAPI *PFN_SHCreateItemFromIDList)(
    PCIDLIST_ABSOLUTE pidl, REFIID riid, void **ppv);

PFN_SHCreateItemFromParsingName GetSHCreateItemFromParsingName()
{
  static PFN_SHCreateItemFromParsingName pfn = (PFN_SHCreateItemFromParsingName)(void *)
      ::GetProcAddress(::GetModuleHandleW(L"shell32.dll"), "SHCreateItemFromParsingName");
  return pfn;
}

PFN_SHCreateItemFromIDList GetSHCreateItemFromIDList()
{
  static PFN_SHCreateItemFromIDList pfn = (PFN_SHCreateItemFromIDList)(void *)
      ::GetProcAddress(::GetModuleHandleW(L"shell32.dll"), "SHCreateItemFromIDList");
  return pfn;
}

UString StripTrailingSlash(const UString &s)
{
  UString r = s;
  while (r.Len() > 3 && (r.Back() == L'\\' || r.Back() == L'/'))
    r.DeleteBack();
  return r;
}

void EnsureTrailingSlash(UString &s)
{
  if (!s.IsEmpty() && s.Back() != L'\\' && s.Back() != L'/')
    s += L'\\';
}

void LoadPersistedOptions(CModernExtractOptions &opt, UStringVector &history)
{
  HKEY hKey = NULL;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegKeyPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return;

  DWORD val = 0;
  DWORD sz = sizeof(val);
  if (::RegQueryValueExW(hKey, L"ExtractRemember", NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
    opt.RememberSettings = (val != 0);

  if (opt.RememberSettings)
  {
    sz = sizeof(val);
    if (::RegQueryValueExW(hKey, L"ExtractToArcFolder", NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
      opt.ExtractToArchiveFolder = (val != 0);
    sz = sizeof(val);
    if (::RegQueryValueExW(hKey, L"ExtractDeleteArc", NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
      opt.DeleteArchiveAfterExtract = (val != 0);
    sz = sizeof(val);
    if (::RegQueryValueExW(hKey, L"ExtractOpenTargetFolder", NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
      opt.OpenTargetFolderAfterExtract = (val != 0);
  }

  for (unsigned i = 0; i < 10; i++)
  {
    wchar_t name[32];
    wsprintfW(name, L"ExtractPathHistory%u", i);
    wchar_t buf[MAX_PATH * 2];
    DWORD bytes = sizeof(buf);
    DWORD type = 0;
    if (::RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)buf, &bytes) == ERROR_SUCCESS
        && type == REG_SZ && buf[0] != 0)
    {
      history.Add(UString(buf));
    }
  }

  ::RegCloseKey(hKey);
}

void SavePersistedOptions(const CModernExtractOptions &opt, const UString &chosenPath, const UStringVector &oldHistory)
{
  HKEY hKey = NULL;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegKeyPath, 0, NULL,
      REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
    return;

  const DWORD rem = opt.RememberSettings ? 1u : 0u;
  ::RegSetValueExW(hKey, L"ExtractRemember", 0, REG_DWORD, (const BYTE *)&rem, sizeof(rem));

  if (opt.RememberSettings)
  {
    const DWORD toArc = opt.ExtractToArchiveFolder ? 1u : 0u;
    const DWORD delArc = opt.DeleteArchiveAfterExtract ? 1u : 0u;
    const DWORD openDir = opt.OpenTargetFolderAfterExtract ? 1u : 0u;
    ::RegSetValueExW(hKey, L"ExtractToArcFolder", 0, REG_DWORD, (const BYTE *)&toArc, sizeof(toArc));
    ::RegSetValueExW(hKey, L"ExtractDeleteArc", 0, REG_DWORD, (const BYTE *)&delArc, sizeof(delArc));
    ::RegSetValueExW(hKey, L"ExtractOpenTargetFolder", 0, REG_DWORD, (const BYTE *)&openDir, sizeof(openDir));
  }

  UStringVector merged;
  const UString cleanChosen = StripTrailingSlash(chosenPath);
  if (!cleanChosen.IsEmpty())
    merged.Add(cleanChosen);
  FOR_VECTOR (i, oldHistory)
  {
    if (merged.Size() >= 10)
      break;
    if (!oldHistory[i].IsEqualTo_NoCase(cleanChosen))
      merged.Add(oldHistory[i]);
  }
  FOR_VECTOR (i, merged)
  {
    wchar_t name[32];
    wsprintfW(name, L"ExtractPathHistory%u", i);
    const UString &v = merged[i];
    ::RegSetValueExW(hKey, name, 0, REG_SZ,
        (const BYTE *)(const wchar_t *)v, (DWORD)(v.Len() + 1) * sizeof(wchar_t));
  }

  ::RegCloseKey(hKey);
}

class CModernExtractDlg;

class CTreeEventsSink : public INameSpaceTreeControlEvents
{
  LONG _refCount;
  CModernExtractDlg *_owner;
public:
  explicit CTreeEventsSink(CModernExtractDlg *owner): _refCount(1), _owner(owner) {}
  void Detach() { _owner = NULL; }

  STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
  {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_INameSpaceTreeControlEvents)
    {
      *ppv = static_cast<INameSpaceTreeControlEvents *>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef()  { return (ULONG)::InterlockedIncrement(&_refCount); }
  STDMETHODIMP_(ULONG) Release()
  {
    const ULONG c = (ULONG)::InterlockedDecrement(&_refCount);
    if (c == 0) delete this;
    return c;
  }

  STDMETHODIMP OnItemClick(IShellItem *, NSTCEHITTEST, NSTCECLICKTYPE) { return S_FALSE; }
  STDMETHODIMP OnPropertyItemCommit(IShellItem *) { return S_FALSE; }
  STDMETHODIMP OnItemStateChanging(IShellItem *, NSTCITEMSTATE, NSTCITEMSTATE) { return S_OK; }
  STDMETHODIMP OnItemStateChanged(IShellItem *, NSTCITEMSTATE, NSTCITEMSTATE) { return S_OK; }
  STDMETHODIMP OnSelectionChanged(IShellItemArray *psiaSelection);
  STDMETHODIMP OnKeyboardInput(UINT, WPARAM, LPARAM) { return S_FALSE; }
  STDMETHODIMP OnBeforeExpand(IShellItem *) { return S_OK; }
  STDMETHODIMP OnAfterExpand(IShellItem *) { return S_OK; }
  STDMETHODIMP OnBeginLabelEdit(IShellItem *) { return S_OK; }
  STDMETHODIMP OnEndLabelEdit(IShellItem *) { return S_OK; }
  STDMETHODIMP OnGetToolTip(IShellItem *, LPWSTR, int) { return E_NOTIMPL; }
  STDMETHODIMP OnBeforeItemDelete(IShellItem *) { return S_OK; }
  STDMETHODIMP OnItemAdded(IShellItem *, BOOL) { return S_OK; }
  STDMETHODIMP OnItemDeleted(IShellItem *, BOOL) { return S_OK; }
  STDMETHODIMP OnBeforeContextMenu(IShellItem *, REFIID, void **) { return E_NOTIMPL; }
  STDMETHODIMP OnAfterContextMenu(IShellItem *, IContextMenu *, REFIID, void **) { return E_NOTIMPL; }
  STDMETHODIMP OnBeforeStateImageChange(IShellItem *) { return S_OK; }
  STDMETHODIMP OnGetDefaultIconIndex(IShellItem *, int *, int *) { return E_NOTIMPL; }
};

class CModernExtractDlg
{
public:
  HWND _hWnd;
  HWND _hParent;
  HWND _hLabelPath;
  HWND _hComboPath;
  HWND _hTreeHwnd;
  HWND _hLabelExtract;
  HWND _hRadioAll;
  HWND _hRadioSel;
  HWND _hBtnNewFolder;
  HWND _hChkArcFolder;
  HWND _hChkDeleteArc;
  HWND _hChkOpenDir;
  HWND _hChkRemember;
  HWND _hBtnAdvanced;
  HWND _hBtnOk;
  HWND _hBtnCancel;

  INameSpaceTreeControl *_nstc;
  CTreeEventsSink *_sink;
  DWORD _adviseCookie;
  bool _updatingTree;

  HBRUSH _bgSurfaceBrush;
  HBRUSH _bgContentBrush;
  CQuickPlace _places[4];
  RECT _sidebarRect;
  RECT _treeFrameRect;
  int _hotPlace;
  int _activePlace;
  bool _trackingMouse;

  CModernExtractOptions &_opt;
  UStringVector _history;
  EExtractDialogResult _result;
  bool _done;

  CModernExtractDlg(HWND parent, CModernExtractOptions &opt):
    _hWnd(NULL), _hParent(parent),
    _hLabelPath(NULL), _hComboPath(NULL), _hTreeHwnd(NULL),
    _hLabelExtract(NULL), _hRadioAll(NULL), _hRadioSel(NULL),
    _hBtnNewFolder(NULL), _hChkArcFolder(NULL), _hChkDeleteArc(NULL), _hChkOpenDir(NULL),
    _hChkRemember(NULL), _hBtnAdvanced(NULL), _hBtnOk(NULL), _hBtnCancel(NULL),
    _nstc(NULL), _sink(NULL), _adviseCookie(0), _updatingTree(false),
    _bgSurfaceBrush(NULL), _bgContentBrush(NULL),
    _hotPlace(-1), _activePlace(-1), _trackingMouse(false),
    _opt(opt), _result(kExtractCancel), _done(false)
  {
    memset(&_sidebarRect, 0, sizeof(_sidebarRect));
    memset(&_treeFrameRect, 0, sizeof(_treeFrameRect));
    const int csidls[4] = { CSIDL_DESKTOPDIRECTORY, CSIDL_MYDOCUMENTS, CSIDL_MYPICTURES, CSIDL_DRIVES };
    const wchar_t *en[4] = { L"Desktop", L"Documents", L"Pictures", L"This PC" };
    const wchar_t *zh[4] = {
      L"\x684C\x9762",
      L"\x6211\x7684\x6587\x6863",
      L"\x6211\x7684\x56FE\x7247",
      L"\x6211\x7684\x7535\x8111"
    };
    for (int i = 0; i < 4; i++)
    {
      _places[i].Csidl = csidls[i];
      _places[i].LabelEn = en[i];
      _places[i].LabelZh = zh[i];
      _places[i].Icon = NULL;
      memset(&_places[i].Rect, 0, sizeof(RECT));
    }
  }

  ~CModernExtractDlg()
  {
    for (int i = 0; i < 4; i++)
    {
      if (_places[i].Icon)
        ::DestroyIcon(_places[i].Icon);
    }
    if (_bgSurfaceBrush) ::DeleteObject(_bgSurfaceBrush);
    if (_bgContentBrush) ::DeleteObject(_bgContentBrush);
  }

  void InitQuickPlaces()
  {
    for (int i = 0; i < 4; i++)
    {
      PIDLIST_ABSOLUTE pidl = NULL;
      if (SUCCEEDED(::SHGetSpecialFolderLocation(_hWnd, _places[i].Csidl, &pidl)) && pidl)
      {
        SHFILEINFOW sfi;
        memset(&sfi, 0, sizeof(sfi));
        if (::SHGetFileInfoW((LPCWSTR)pidl, 0, &sfi, sizeof(sfi),
            SHGFI_PIDL | SHGFI_ICON | SHGFI_LARGEICON))
        {
          _places[i].Icon = sfi.hIcon;
        }
        wchar_t buf[MAX_PATH];
        buf[0] = 0;
        if (::SHGetPathFromIDListW(pidl, buf))
          _places[i].Path = buf;
        ::CoTaskMemFree(pidl);
      }
    }
  }

  void OnTreeSelection(IShellItemArray *psia)
  {
    if (_updatingTree || !psia)
      return;
    DWORD count = 0;
    if (FAILED(psia->GetCount(&count)) || count == 0)
      return;
    IShellItem *psi = NULL;
    if (SUCCEEDED(psia->GetItemAt(0, &psi)) && psi)
    {
      LPWSTR fsPath = NULL;
      if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &fsPath)) && fsPath)
      {
        ::SetWindowTextW(_hComboPath, fsPath);
        ::CoTaskMemFree(fsPath);
      }
      psi->Release();
    }
  }

  void SelectPathInTree(const UString &dirPath)
  {
    if (!_nstc || dirPath.IsEmpty())
      return;
    PFN_SHCreateItemFromParsingName pfn = GetSHCreateItemFromParsingName();
    if (!pfn)
      return;
    const UString clean = StripTrailingSlash(dirPath);
    IShellItem *psi = NULL;
    if (SUCCEEDED(pfn(clean, NULL, IID_IShellItem, (void **)&psi)) && psi)
    {
      _updatingTree = true;
      _nstc->EnsureItemVisible(psi);
      _nstc->SetItemState(psi, NSTCIS_SELECTED | NSTCIS_EXPANDED, NSTCIS_SELECTED);
      _updatingTree = false;
      psi->Release();
    }
  }

  void SelectCsidlInTree(int csidl)
  {
    if (!_nstc)
      return;
    PFN_SHCreateItemFromIDList pfn = GetSHCreateItemFromIDList();
    if (!pfn)
      return;
    PIDLIST_ABSOLUTE pidl = NULL;
    if (SUCCEEDED(::SHGetSpecialFolderLocation(_hWnd, csidl, &pidl)) && pidl)
    {
      IShellItem *psi = NULL;
      if (SUCCEEDED(pfn(pidl, IID_IShellItem, (void **)&psi)) && psi)
      {
        _updatingTree = true;
        _nstc->EnsureItemVisible(psi);
        _nstc->SetItemState(psi, NSTCIS_SELECTED | NSTCIS_EXPANDED, NSTCIS_SELECTED | NSTCIS_EXPANDED);
        _updatingTree = false;
        psi->Release();
      }
      ::CoTaskMemFree(pidl);
    }
  }

  void InitShellTree(const RECT &rc)
  {
    HRESULT hr = ::CoCreateInstance(CLSID_NamespaceTreeControl, NULL,
        CLSCTX_INPROC_SERVER, IID_INameSpaceTreeControl, (void **)&_nstc);
    if (FAILED(hr) || !_nstc)
      return;

    RECT initRc = rc;
    const NSTCSTYLE style =
        NSTCS_HASEXPANDOS |
        NSTCS_SHOWSELECTIONALWAYS |
        NSTCS_SINGLECLICKEXPAND |
        NSTCS_FULLROWSELECT |
        NSTCS_NOEDITLABELS;

    hr = _nstc->Initialize(_hWnd, &initRc, style);
    if (FAILED(hr))
    {
      _nstc->Release();
      _nstc = NULL;
      return;
    }

    IOleWindow *oleWin = NULL;
    if (SUCCEEDED(_nstc->QueryInterface(IID_IOleWindow, (void **)&oleWin)) && oleWin)
    {
      oleWin->GetWindow(&_hTreeHwnd);
      oleWin->Release();
    }

    _sink = new CTreeEventsSink(this);
    _nstc->TreeAdvise(_sink, &_adviseCookie);

    // Populate Desktop root (includes User folders, Downloads, Documents, This PC, Drives)
    PFN_SHCreateItemFromIDList pfnId = GetSHCreateItemFromIDList();
    if (pfnId)
    {
      PIDLIST_ABSOLUTE pidlDesktop = NULL;
      if (SUCCEEDED(::SHGetSpecialFolderLocation(_hWnd, CSIDL_DESKTOP, &pidlDesktop)) && pidlDesktop)
      {
        IShellItem *psiDesktop = NULL;
        if (SUCCEEDED(pfnId(pidlDesktop, IID_IShellItem, (void **)&psiDesktop)) && psiDesktop)
        {
          _nstc->AppendRoot(psiDesktop, SHCONTF_FOLDERS, NSTCRS_VISIBLE | NSTCRS_EXPANDED, NULL);
          psiDesktop->Release();
        }
        ::CoTaskMemFree(pidlDesktop);
      }
    }
  }

  void LayoutControls()
  {
    if (!_hWnd)
      return;
    RECT rc;
    ::GetClientRect(_hWnd, &rc);
    const UINT dpi = GetDpiForHwnd(_hWnd);
    const int W = rc.right - rc.left;
    const int H = rc.bottom - rc.top;

    const int pad = ScaleDip(14, dpi);
    const int gap = ScaleDip(10, dpi);
    const int topRowH = ScaleDip(26, dpi);
    const int labelW = ScaleDip(82, dpi);
    const int bottomAreaH = ScaleDip(170, dpi);
    const int sideW = ScaleDip(104, dpi);

    // Top row: Target path(D) + ComboBox
    ::MoveWindow(_hLabelPath, pad, pad + ScaleDip(4, dpi), labelW, ScaleDip(20, dpi), TRUE);
    ::MoveWindow(_hComboPath, pad + labelW + ScaleDip(4, dpi), pad,
        W - pad * 2 - labelW - ScaleDip(4, dpi), ScaleDip(220, dpi), TRUE);

    // Middle split: Quick places sidebar + Native Shell Tree
    const int midTop = pad + topRowH + gap;
    const int midBottom = H - bottomAreaH;
    const int midH = (midBottom > midTop + 60) ? (midBottom - midTop) : 60;

    _sidebarRect.left = pad;
    _sidebarRect.top = midTop;
    _sidebarRect.right = pad + sideW;
    _sidebarRect.bottom = midTop + midH;

    const int cardPad = ScaleDip(6, dpi);
    const int cardH = (midH - cardPad * 5) / 4;
    for (int i = 0; i < 4; i++)
    {
      _places[i].Rect.left = _sidebarRect.left + cardPad;
      _places[i].Rect.right = _sidebarRect.right - cardPad;
      _places[i].Rect.top = _sidebarRect.top + cardPad + i * (cardH + cardPad);
      _places[i].Rect.bottom = _places[i].Rect.top + cardH;
    }

    _treeFrameRect.left = _sidebarRect.right + gap;
    _treeFrameRect.top = midTop;
    _treeFrameRect.right = W - pad;
    _treeFrameRect.bottom = midTop + midH;

    if (_hTreeHwnd)
    {
      ::MoveWindow(_hTreeHwnd,
          _treeFrameRect.left + 1, _treeFrameRect.top + 1,
          (_treeFrameRect.right - _treeFrameRect.left) - 2,
          (_treeFrameRect.bottom - _treeFrameRect.top) - 2, TRUE);
    }

    // Bottom section
    int y = midTop + midH + ScaleDip(14, dpi);
    const int row1H = ScaleDip(26, dpi);
    const int newFolderW = ScaleDip(106, dpi);
    ::MoveWindow(_hLabelExtract, pad, y + ScaleDip(4, dpi), ScaleDip(70, dpi), ScaleDip(20, dpi), TRUE);
    ::MoveWindow(_hRadioAll, pad + ScaleDip(76, dpi), y + ScaleDip(3, dpi), ScaleDip(120, dpi), ScaleDip(20, dpi), TRUE);
    ::MoveWindow(_hRadioSel, pad + ScaleDip(204, dpi), y + ScaleDip(3, dpi), ScaleDip(140, dpi), ScaleDip(20, dpi), TRUE);
    ::MoveWindow(_hBtnNewFolder, W - pad - newFolderW, y, newFolderW, row1H, TRUE);

    y += row1H + ScaleDip(14, dpi);
    const int remW = ScaleDip(116, dpi);
    ::MoveWindow(_hChkArcFolder, pad, y, W - pad * 2 - remW - gap, ScaleDip(22, dpi), TRUE);
    ::MoveWindow(_hChkRemember, W - pad - remW, y, remW, ScaleDip(22, dpi), TRUE);

    y += ScaleDip(26, dpi);
    ::MoveWindow(_hChkDeleteArc, pad, y, W - pad * 2, ScaleDip(22, dpi), TRUE);

    y += ScaleDip(26, dpi);
    ::MoveWindow(_hChkOpenDir, pad, y, W - pad * 2 - ScaleDip(190, dpi), ScaleDip(22, dpi), TRUE);

    const int btnW = ScaleDip(88, dpi);
    const int btnH = ScaleDip(30, dpi);
    const int btnY = H - pad - btnH;
    if (_hBtnAdvanced)
    {
      ::MoveWindow(_hBtnAdvanced, pad, btnY, ScaleDip(104, dpi), btnH, TRUE);
      ::ShowWindow(_hBtnAdvanced, _opt.ShowAdvancedLink ? SW_SHOW : SW_HIDE);
    }
    ::MoveWindow(_hBtnCancel, W - pad - btnW, btnY, btnW, btnH, TRUE);
    ::MoveWindow(_hBtnOk, W - pad - btnW * 2 - ScaleDip(8, dpi), btnY, btnW, btnH, TRUE);
  }

  void CreateChildren()
  {
    const UINT dpi = GetDpiForHwnd(_hWnd);
    HFONT font = GetCachedUiFont(dpi, 92, FW_NORMAL);

    _hLabelPath = ::CreateWindowExW(0, L"STATIC",
        NUiLang::Tr(L"Target path(&D)", L"\x76EE\x6807\x8DEF\x5F84(&D)"),
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 80, 20, _hWnd, NULL, g_hInstance, NULL);

    _hComboPath = ::CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        0, 0, 200, 200, _hWnd, (HMENU)(INT_PTR)kIdComboPath, g_hInstance, NULL);

    _hLabelExtract = ::CreateWindowExW(0, L"STATIC",
        NUiLang::Tr(L"Extract:", L"\x89E3\x538B\x7F29:"),
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 70, 20, _hWnd, NULL, g_hInstance, NULL);

    _hRadioAll = ::CreateWindowExW(0, L"BUTTON",
        NUiLang::Tr(L"All files(&A)", L"\x5168\x90E8\x6587\x4EF6(&A)"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
        0, 0, 120, 20, _hWnd, (HMENU)(INT_PTR)kIdRadioAll, g_hInstance, NULL);

    _hRadioSel = ::CreateWindowExW(0, L"BUTTON",
        NUiLang::Tr(L"Selected files(&S)", L"\x9009\x5B9A\x7684\x6587\x4EF6(&S)"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
        0, 0, 140, 20, _hWnd, (HMENU)(INT_PTR)kIdRadioSel, g_hInstance, NULL);

    _hBtnNewFolder = ::CreateWindowExW(0, L"BUTTON",
        NUiLang::Tr(L"New Folder(&N)", L"\x65B0\x5EFA\x6587\x4EF6\x5939(&N)"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 100, 26, _hWnd, (HMENU)(INT_PTR)kIdBtnNewFolder, g_hInstance, NULL);

    _hChkArcFolder = ::CreateWindowExW(0, L"BUTTON",
        NUiLang::Tr(
            L"Extract to folder with archive name(&E)",
            L"\x89E3\x538B\x5230\x9009\x5B9A\x6587\x4EF6\x5939\x4E0B\x4E0E\x538B\x7F29\x5305\x540C\x540D\x7684\x6587\x4EF6\x5939(&E)"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 360, 22, _hWnd, (HMENU)(INT_PTR)kIdChkArcFolder, g_hInstance, NULL);

    _hChkRemember = ::CreateWindowExW(0, L"BUTTON",
        NUiLang::Tr(L"Remember(&R)", L"\x8BB0\x4F4F\x8BBE\x7F6E(&R)"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 110, 22, _hWnd, (HMENU)(INT_PTR)kIdChkRemember, g_hInstance, NULL);

    _hChkDeleteArc = ::CreateWindowExW(0, L"BUTTON",
        NUiLang::Tr(
            L"Delete archive after extraction(&L)",
            L"\x89E3\x538B\x540E\x5220\x9664\x538B\x7F29\x6587\x4EF6(&L)"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 260, 22, _hWnd, (HMENU)(INT_PTR)kIdChkDeleteArc, g_hInstance, NULL);

    _hChkOpenDir = ::CreateWindowExW(0, L"BUTTON",
        NUiLang::Tr(
            L"Open folder and close when done(&O)",
            L"\x89E3\x538B\x5B8C\x6210\x540E\x6253\x5F00\x76EE\x6807\x76EE\x5F55\x5E76\x5173\x95ED\x7A97\x53E3(&O)"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 320, 22, _hWnd, (HMENU)(INT_PTR)kIdChkOpenDir, g_hInstance, NULL);

    _hBtnAdvanced = ::CreateWindowExW(0, L"BUTTON",
        NUiLang::Tr(L"Advanced...", L"\x9AD8\x7EA7\x9009\x9879..."),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 96, 28, _hWnd, (HMENU)(INT_PTR)kIdBtnAdvanced, g_hInstance, NULL);

    _hBtnOk = ::CreateWindowExW(0, L"BUTTON",
        NUiLang::Tr(L"OK", L"\x786E\x5B9A"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 88, 28, _hWnd, (HMENU)(INT_PTR)kIdBtnOk, g_hInstance, NULL);

    _hBtnCancel = ::CreateWindowExW(0, L"BUTTON",
        NUiLang::Tr(L"Cancel", L"\x53D6\x6D88"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 88, 28, _hWnd, (HMENU)(INT_PTR)kIdBtnCancel, g_hInstance, NULL);

    HWND ctrls[] = {
      _hLabelPath, _hComboPath, _hLabelExtract, _hRadioAll, _hRadioSel,
      _hBtnNewFolder, _hChkArcFolder, _hChkDeleteArc, _hChkOpenDir, _hChkRemember,
      _hBtnAdvanced, _hBtnOk, _hBtnCancel
    };
    for (unsigned i = 0; i < sizeof(ctrls) / sizeof(ctrls[0]); i++)
    {
      if (ctrls[i] && font)
        ::SendMessageW(ctrls[i], WM_SETFONT, (WPARAM)font, FALSE);
    }

    // Load saved preferences + history
    LoadPersistedOptions(_opt, _history);

    const UString initPath = StripTrailingSlash(_opt.TargetDir);
    if (!initPath.IsEmpty())
      ::SendMessageW(_hComboPath, CB_ADDSTRING, 0, (LPARAM)(const wchar_t *)initPath);
    FOR_VECTOR (i, _history)
    {
      if (!_history[i].IsEqualTo_NoCase(initPath))
        ::SendMessageW(_hComboPath, CB_ADDSTRING, 0, (LPARAM)(const wchar_t *)_history[i]);
    }
    ::SetWindowTextW(_hComboPath, initPath);

    ::EnableWindow(_hRadioSel, _opt.CanSelectFiles ? TRUE : FALSE);
    if (_opt.CanSelectFiles && _opt.ExtractSelectedOnly)
      ::SendMessageW(_hRadioSel, BM_SETCHECK, BST_CHECKED, 0);
    else
      ::SendMessageW(_hRadioAll, BM_SETCHECK, BST_CHECKED, 0);

    ::SendMessageW(_hChkArcFolder, BM_SETCHECK, _opt.ExtractToArchiveFolder ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(_hChkDeleteArc, BM_SETCHECK, _opt.DeleteArchiveAfterExtract ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(_hChkOpenDir, BM_SETCHECK, _opt.OpenTargetFolderAfterExtract ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(_hChkRemember, BM_SETCHECK, _opt.RememberSettings ? BST_CHECKED : BST_UNCHECKED, 0);

    InitQuickPlaces();

    RECT initTreeRc = { 120, 50, 580, 350 };
    InitShellTree(initTreeRc);
    LayoutControls();
    SelectPathInTree(initPath);
  }

  UString GetComboPath() const
  {
    wchar_t buf[MAX_PATH * 2];
    buf[0] = 0;
    ::GetWindowTextW(_hComboPath, buf, (int)(sizeof(buf) / sizeof(buf[0]) - 1));
    UString s(buf);
    s.Trim();
    return s;
  }

  void HandleNewFolder()
  {
    UString base = GetComboPath();
    if (base.IsEmpty())
      return;
    EnsureTrailingSlash(base);

    const wchar_t *baseName = NUiLang::Tr(L"New Folder", L"\x65B0\x5EFA\x6587\x4EF6\x5939");
    UString candidate = base + baseName;
    for (unsigned idx = 2; idx <= 999; idx++)
    {
      if (::GetFileAttributesW(candidate) == INVALID_FILE_ATTRIBUTES)
        break;
      wchar_t suffix[32];
      wsprintfW(suffix, L" (%u)", idx);
      candidate = base + baseName + suffix;
    }

    if (::CreateDirectoryW(candidate, NULL))
    {
      ::SetWindowTextW(_hComboPath, candidate);
      SelectPathInTree(candidate);
    }
  }

  void CollectAndFinish(EExtractDialogResult res)
  {
    if (res == kExtractOk || res == kExtractAdvanced)
    {
      _opt.TargetDir = GetComboPath();
      _opt.ExtractSelectedOnly = (::SendMessageW(_hRadioSel, BM_GETCHECK, 0, 0) == BST_CHECKED);
      _opt.ExtractToArchiveFolder = (::SendMessageW(_hChkArcFolder, BM_GETCHECK, 0, 0) == BST_CHECKED);
      _opt.DeleteArchiveAfterExtract = (::SendMessageW(_hChkDeleteArc, BM_GETCHECK, 0, 0) == BST_CHECKED);
      _opt.OpenTargetFolderAfterExtract = (::SendMessageW(_hChkOpenDir, BM_GETCHECK, 0, 0) == BST_CHECKED);
      _opt.RememberSettings = (::SendMessageW(_hChkRemember, BM_GETCHECK, 0, 0) == BST_CHECKED);
      if (res == kExtractOk)
        SavePersistedOptions(_opt, _opt.TargetDir, _history);
    }
    _result = res;
    _done = true;
    ::DestroyWindow(_hWnd);
  }

  void OnPaint()
  {
    PAINTSTRUCT ps;
    HDC hdc = ::BeginPaint(_hWnd, &ps);
    RECT rc;
    ::GetClientRect(_hWnd, &rc);

    const CPalette pal = GetPalette();
    const UINT dpi = GetDpiForHwnd(_hWnd);

    HBRUSH bgBr = ::CreateSolidBrush(pal.BgSurface);
    ::FillRect(hdc, &rc, bgBr);
    ::DeleteObject(bgBr);

    // Draw left quick-access sidebar card container
    FillRoundRectGdi(hdc, _sidebarRect, ScaleDip(8, dpi), pal.BgContent, pal.BorderSubtle, true);

    // Draw right tree container border
    FillRoundRectGdi(hdc, _treeFrameRect, ScaleDip(4, dpi), pal.BgContent, pal.BorderSubtle, true);

    // Draw quick-access place cards
    HFONT font = GetCachedUiFont(dpi, 85, FW_NORMAL);
    HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
    ::SetBkMode(hdc, TRANSPARENT);

    const int iconSize = ScaleDip(32, dpi);
    for (int i = 0; i < 4; i++)
    {
      const RECT &cr = _places[i].Rect;
      if (i == _activePlace)
        FillRoundRectGdi(hdc, cr, ScaleDip(6, dpi), pal.BgSelected, pal.Accent, false);
      else if (i == _hotPlace)
        FillRoundRectGdi(hdc, cr, ScaleDip(6, dpi), pal.BgHover, pal.BorderSubtle, false);

      const int cx = (cr.left + cr.right) / 2;
      const int iconTop = cr.top + ScaleDip(6, dpi);
      if (_places[i].Icon)
      {
        ::DrawIconEx(hdc, cx - iconSize / 2, iconTop, _places[i].Icon,
            iconSize, iconSize, 0, NULL, DI_NORMAL);
      }

      RECT textRc = cr;
      textRc.top = iconTop + iconSize + ScaleDip(2, dpi);
      ::SetTextColor(hdc, (i == _activePlace) ? pal.Accent : pal.TextPrimary);
      ::DrawTextW(hdc, NUiLang::Tr(_places[i].LabelEn, _places[i].LabelZh), -1, &textRc,
          DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (oldFont)
      ::SelectObject(hdc, oldFont);

    // Subtle divider between tree/sidebar and bottom options
    const int divY = _sidebarRect.bottom + ScaleDip(7, dpi);
    HPEN divPen = ::CreatePen(PS_SOLID, 1, pal.BorderSubtle);
    HGDIOBJ oldPen = ::SelectObject(hdc, divPen);
    ::MoveToEx(hdc, _sidebarRect.left, divY, NULL);
    ::LineTo(hdc, _treeFrameRect.right, divY);
    ::SelectObject(hdc, oldPen);
    ::DeleteObject(divPen);

    ::EndPaint(_hWnd, &ps);
  }

  int HitTestPlace(int x, int y) const
  {
    POINT pt = { x, y };
    for (int i = 0; i < 4; i++)
    {
      if (::PtInRect(&_places[i].Rect, pt))
        return i;
    }
    return -1;
  }

  LRESULT OnMessage(UINT msg, WPARAM wParam, LPARAM lParam)
  {
    switch (msg)
    {
      case WM_CREATE:
      {
        const CPalette pal = GetPalette();
        _bgSurfaceBrush = ::CreateSolidBrush(pal.BgSurface);
        _bgContentBrush = ::CreateSolidBrush(pal.BgContent);
        ApplyWindowChrome(_hWnd);
        CreateChildren();
        return 0;
      }

      case WM_SIZE:
        LayoutControls();
        ::InvalidateRect(_hWnd, NULL, FALSE);
        return 0;

      case WM_GETMINMAXINFO:
      {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        const UINT dpi = GetDpiForHwnd(_hWnd);
        mmi->ptMinTrackSize.x = ScaleDip(520, dpi);
        mmi->ptMinTrackSize.y = ScaleDip(440, dpi);
        return 0;
      }

      case WM_CTLCOLORSTATIC:
      case WM_CTLCOLORBTN:
      {
        HDC hdc = (HDC)wParam;
        const CPalette pal = GetPalette();
        ::SetBkColor(hdc, pal.BgSurface);
        ::SetTextColor(hdc, pal.TextPrimary);
        return (LRESULT)_bgSurfaceBrush;
      }

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
        const int hit = HitTestPlace((short)LOWORD(lParam), (short)HIWORD(lParam));
        if (hit != _hotPlace)
        {
          _hotPlace = hit;
          ::InvalidateRect(_hWnd, &_sidebarRect, FALSE);
        }
        return 0;
      }

      case WM_MOUSELEAVE:
        _trackingMouse = false;
        if (_hotPlace != -1)
        {
          _hotPlace = -1;
          ::InvalidateRect(_hWnd, &_sidebarRect, FALSE);
        }
        return 0;

      case WM_LBUTTONDOWN:
      {
        const int hit = HitTestPlace((short)LOWORD(lParam), (short)HIWORD(lParam));
        if (hit >= 0)
        {
          _activePlace = hit;
          ::InvalidateRect(_hWnd, &_sidebarRect, FALSE);
          if (!_places[hit].Path.IsEmpty())
          {
            ::SetWindowTextW(_hComboPath, _places[hit].Path);
            SelectPathInTree(_places[hit].Path);
          }
          else
          {
            SelectCsidlInTree(_places[hit].Csidl);
          }
        }
        return 0;
      }

      case WM_COMMAND:
      {
        const WORD id = LOWORD(wParam);
        const WORD code = HIWORD(wParam);
        if (id == kIdBtnOk)
        {
          CollectAndFinish(kExtractOk);
          return 0;
        }
        if (id == kIdBtnCancel)
        {
          CollectAndFinish(kExtractCancel);
          return 0;
        }
        if (id == kIdBtnAdvanced)
        {
          CollectAndFinish(kExtractAdvanced);
          return 0;
        }
        if (id == kIdBtnNewFolder)
        {
          HandleNewFolder();
          return 0;
        }
        if (id == kIdComboPath && code == CBN_SELENDOK)
        {
          const int sel = (int)::SendMessageW(_hComboPath, CB_GETCURSEL, 0, 0);
          if (sel >= 0)
          {
            wchar_t buf[MAX_PATH * 2];
            buf[0] = 0;
            ::SendMessageW(_hComboPath, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)buf);
            if (buf[0] != 0)
              SelectPathInTree(UString(buf));
          }
          return 0;
        }
        break;
      }

      case WM_CLOSE:
        CollectAndFinish(kExtractCancel);
        return 0;

      case WM_DESTROY:
        if (_nstc)
        {
          if (_adviseCookie)
          {
            _nstc->TreeUnadvise(_adviseCookie);
            _adviseCookie = 0;
          }
          _nstc->Release();
          _nstc = NULL;
        }
        if (_sink)
        {
          _sink->Detach();
          _sink->Release();
          _sink = NULL;
        }
        _done = true;
        return 0;
    }
    return ::DefWindowProcW(_hWnd, msg, wParam, lParam);
  }

  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
  {
    CModernExtractDlg *self = (CModernExtractDlg *)::GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE)
    {
      CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
      self = (CModernExtractDlg *)cs->lpCreateParams;
      ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)self);
      if (self)
        self->_hWnd = hWnd;
    }
    if (self)
      return self->OnMessage(msg, wParam, lParam);
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
  }
};

STDMETHODIMP CTreeEventsSink::OnSelectionChanged(IShellItemArray *psiaSelection)
{
  if (_owner)
    _owner->OnTreeSelection(psiaSelection);
  return S_OK;
}

bool g_ExtractDlgClassRegistered = false;

} // namespace

EExtractDialogResult ShowModernExtractDialog(HWND hwndParent, CModernExtractOptions &opt)
{
  if (!g_ExtractDlgClassRegistered)
  {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CModernExtractDlg::WndProc;
    wc.hInstance = g_hInstance;
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kDlgClassName;
    if (::RegisterClassW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
      g_ExtractDlgClassRegistered = true;
  }

  CModernExtractDlg dlg(hwndParent, opt);

  const UINT dpi = GetDpiForHwnd(hwndParent);
  const int w = ScaleDip(640, dpi);
  const int h = ScaleDip(520, dpi);

  RECT workArea = { 0, 0, 1280, 800 };
  ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
  int x = workArea.left + (workArea.right - workArea.left - w) / 2;
  int y = workArea.top + (workArea.bottom - workArea.top - h) / 2;
  if (hwndParent && ::IsWindowVisible(hwndParent))
  {
    RECT pr;
    if (::GetWindowRect(hwndParent, &pr))
    {
      x = pr.left + (pr.right - pr.left - w) / 2;
      y = pr.top + (pr.bottom - pr.top - h) / 2;
    }
  }

  HWND hWnd = ::CreateWindowExW(
      WS_EX_DLGMODALFRAME,
      kDlgClassName,
      NUiLang::Tr(L"Choose Extract Path", L"\x9009\x62E9\x89E3\x538B\x8DEF\x5F84"),
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
      x, y, w, h,
      hwndParent, NULL, g_hInstance, &dlg);

  if (!hWnd)
    return kExtractCancel;

  HICON hAppIcon = (HICON)::LoadImageW(g_hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
  if (hAppIcon)
    ::SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);

  const bool parentWasEnabled = (hwndParent && ::IsWindowEnabled(hwndParent));
  if (parentWasEnabled)
    ::EnableWindow(hwndParent, FALSE);

  ::ShowWindow(hWnd, SW_SHOW);
  ::UpdateWindow(hWnd);
  ::SetFocus(dlg._hComboPath);

  MSG msg;
  while (!dlg._done && ::GetMessageW(&msg, NULL, 0, 0) > 0)
  {
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
    {
      dlg.CollectAndFinish(kExtractCancel);
      break;
    }
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN)
    {
      // Confirm unless combo dropdown is open
      if (!::SendMessageW(dlg._hComboPath, CB_GETDROPPEDSTATE, 0, 0))
      {
        dlg.CollectAndFinish(kExtractOk);
        break;
      }
    }
    if (!::IsDialogMessageW(hWnd, &msg))
    {
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }
  }

  if (parentWasEnabled)
  {
    ::EnableWindow(hwndParent, TRUE);
    ::SetForegroundWindow(hwndParent);
  }
  if (hAppIcon)
    ::DestroyIcon(hAppIcon);

  return dlg._result;
}

void AppendArchiveNameSubfolder(UString &dirPath, const UString &archivePath)
{
  if (archivePath.IsEmpty())
    return;

  // Extract file name from archivePath
  int slash = archivePath.ReverseFind_PathSepar();
  UString name = (slash >= 0) ? UString(archivePath.Ptr((unsigned)slash + 1)) : archivePath;
  if (name.IsEmpty())
    return;

  // Strip extension (including compound .tar.gz / .tar.xz / .tar.zst / .tar.bz2)
  int dot = name.ReverseFind_Dot();
  if (dot > 0)
  {
    name.DeleteFrom((unsigned)dot);
    int dot2 = name.ReverseFind_Dot();
    if (dot2 > 0)
    {
      UString ext2 = name.Ptr((unsigned)dot2 + 1);
      ext2.MakeLower_Ascii();
      if (ext2 == L"tar")
        name.DeleteFrom((unsigned)dot2);
    }
  }

  if (name.IsEmpty())
    return;

  // Avoid double-appending if dirPath already ends with this archive folder name
  UString cleanDir = StripTrailingSlash(dirPath);
  int lastSep = cleanDir.ReverseFind_PathSepar();
  UString lastPart = (lastSep >= 0) ? UString(cleanDir.Ptr((unsigned)lastSep + 1)) : cleanDir;
  if (!lastPart.IsEqualTo_NoCase(name))
  {
    EnsureTrailingSlash(cleanDir);
    cleanDir += name;
  }
  EnsureTrailingSlash(cleanDir);
  dirPath = cleanDir;
}

bool MoveFileToRecycleBin(const UString &filePath)
{
  if (filePath.IsEmpty())
    return false;
  // Double-null terminated buffer required by SHFileOperationW
  const unsigned len = filePath.Len();
  wchar_t *buf = new wchar_t[len + 2];
  memcpy(buf, (const wchar_t *)filePath, len * sizeof(wchar_t));
  buf[len] = 0;
  buf[len + 1] = 0;

  SHFILEOPSTRUCTW op;
  memset(&op, 0, sizeof(op));
  op.wFunc = FO_DELETE;
  op.pFrom = buf;
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

  const int res = ::SHFileOperationW(&op);
  delete []buf;
  return (res == 0 && !op.fAnyOperationsAborted);
}

void OpenFolderInExplorer(const UString &dirPath)
{
  UString clean = StripTrailingSlash(dirPath);
  if (clean.IsEmpty())
    return;
  ::ShellExecuteW(NULL, L"open", clean, NULL, NULL, SW_SHOWNORMAL);
}

} // namespace NModernUI
