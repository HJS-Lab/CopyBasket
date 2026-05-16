#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "BasketDialog.h"
#include "BasketStore.h"
#include "Strings.h"
#include "resource.h"

extern HINSTANCE g_hModule;

#pragma comment(lib, "comctl32.lib")

namespace BasketDialog {

static const WCHAR* WND_CLASS = L"CopyBasketDlg";
static const WCHAR* SPLITTER_CLASS = L"CopyBasketSplitter";
static const int IDC_LISTVIEW = 1001;
static const int IDC_REMOVE = 1002;
static const int IDC_CLOSE = 1003;
static const int IDC_TREEVIEW = 1004;
static const int IDC_SPLITTER = 1005;
static const int MIN_WIDTH = 450;
static const int MIN_HEIGHT = 400;
static const int SPLITTER_H = 6;
static const int MIN_PANE_H = 60;
static const int BTN_W = 100;
static const int BTN_H = 28;
static const int MARGIN = 8;

using BasketStore::REG_KEY;

struct DlgData {
    HWND hList;
    HWND hTree;
    HWND hSplitter;
    HWND hBtnRemove;
    HWND hBtnClose;
    HWND hStatusBar;
    int sortColumn;
    bool sortAscending;
    int splitRatioPct;  // list portion of split (10-90)
    std::wstring lastTreePath;  // avoid redundant repopulation
};

struct SortContext {
    HWND hList;
    int column;
    bool ascending;
};

static int CALLBACK CompareItems(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) {
    SortContext* ctx = (SortContext*)lParamSort;
    WCHAR sz1[MAX_PATH] = {}, sz2[MAX_PATH] = {};
    ListView_GetItemText(ctx->hList, (int)lParam1, ctx->column, sz1, MAX_PATH);
    ListView_GetItemText(ctx->hList, (int)lParam2, ctx->column, sz2, MAX_PATH);
    int result = _wcsicmp(sz1, sz2);
    return ctx->ascending ? result : -result;
}

static void SetHeaderSortArrow(HWND hList, int column, bool ascending) {
    HWND hHeader = ListView_GetHeader(hList);
    int count = Header_GetItemCount(hHeader);
    for (int i = 0; i < count; i++) {
        HDITEMW hdi = {};
        hdi.mask = HDI_FORMAT;
        Header_GetItem(hHeader, i, &hdi);
        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == column) {
            hdi.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItem(hHeader, i, &hdi);
    }
}

static void SortListView(DlgData* dd) {
    SortContext ctx = { dd->hList, dd->sortColumn, dd->sortAscending };
    ListView_SortItemsEx(dd->hList, CompareItems, (LPARAM)&ctx);
    SetHeaderSortArrow(dd->hList, dd->sortColumn, dd->sortAscending);
}

static DWORD RegReadDword(const WCHAR* name, DWORD def) {
    DWORD val = def;
    DWORD cb = sizeof(val);
    RegGetValueW(HKEY_CURRENT_USER, REG_KEY, name,
                  RRF_RT_REG_DWORD, NULL, &val, &cb);
    return val;
}

static void RegWriteDword(HKEY hKey, const WCHAR* name, DWORD val) {
    RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
}

static void SaveDialogState(HWND hwnd, DlgData* dd) {
    RECT rc;
    GetWindowRect(hwnd, &rc);

    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL,
                         0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegWriteDword(hKey, L"DialogWidth", rc.right - rc.left);
        RegWriteDword(hKey, L"DialogHeight", rc.bottom - rc.top);
        if (dd->hList) {
            RegWriteDword(hKey, L"ColWidth0", ListView_GetColumnWidth(dd->hList, 0));
            RegWriteDword(hKey, L"ColWidth1", ListView_GetColumnWidth(dd->hList, 1));
            RegWriteDword(hKey, L"ColWidth2", ListView_GetColumnWidth(dd->hList, 2));
        }
        RegWriteDword(hKey, L"SplitRatio", (DWORD)dd->splitRatioPct);
        RegCloseKey(hKey);
    }
}

// Returns the system image-list icon index for a path. If the file no longer
// exists, falls back to extension-/attribute-based lookup so removed entries
// still render with a reasonable icon. attrHint avoids a duplicate
// GetFileAttributesW call when the caller already has it.
static int GetSysIconIndex(const std::wstring& path,
                           DWORD attrHint = INVALID_FILE_ATTRIBUTES) {
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi),
                       SHGFI_SYSICONINDEX | SHGFI_SMALLICON)) {
        return sfi.iIcon;
    }
    DWORD attr = (attrHint != INVALID_FILE_ATTRIBUTES) ? attrHint
                                                       : GetFileAttributesW(path.c_str());
    DWORD fallbackAttr = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                             ? FILE_ATTRIBUTE_DIRECTORY
                             : FILE_ATTRIBUTE_NORMAL;
    if (SHGetFileInfoW(path.c_str(), fallbackAttr, &sfi, sizeof(sfi),
                       SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES)) {
        return sfi.iIcon;
    }
    return 0;
}

// Lazy-loading TreeView: each directory node stores its full path on the heap
// (via lParam) and is inserted with cChildren=1 so the expand arrow shows
// without a content scan. The actual children are loaded on TVN_ITEMEXPANDINGW
// (see OnNotify). Reparse points (junctions / symlinks) get a node but no
// arrow — traversing them can be cyclic (e.g. C:\Users\<name>\Application
// Data -> AppData\Roaming and back), which would otherwise hang the UI.
// lParam strings are freed on TVN_DELETEITEMW.

static HTREEITEM InsertTreeNode(HWND hTree, HTREEITEM hParent,
                                const std::wstring& displayName,
                                const std::wstring& fullPath,
                                int icon, bool isDirectory, bool isReparsePoint) {
    TVINSERTSTRUCTW tvi = {};
    tvi.hParent = hParent;
    tvi.hInsertAfter = TVI_LAST;
    tvi.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
    tvi.item.pszText = (LPWSTR)displayName.c_str();
    tvi.item.iImage = icon;
    tvi.item.iSelectedImage = icon;
    // Show expand arrow only for traversable directories. Files and reparse
    // points get cChildren=0 (no arrow).
    bool canExpand = isDirectory && !isReparsePoint;
    tvi.item.cChildren = canExpand ? 1 : 0;
    // Store full path on heap so we can recover it on expand without walking
    // the tree. Freed in TVN_DELETEITEMW handler.
    tvi.item.lParam = canExpand ? (LPARAM)new std::wstring(fullPath) : 0;
    return TreeView_InsertItem(hTree, &tvi);
}

// Populate exactly one level of children under hParent. Directories first
// (each as a lazy-load node), then files. Safe to call from
// TVN_ITEMEXPANDINGW — no recursion, no rescan of sub-levels.
static void PopulateChildren(HWND hTree, HTREEITEM hParent, const std::wstring& dir) {
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    struct DirEntry { std::wstring name; DWORD attrs; };
    std::vector<DirEntry> dirs;
    std::vector<std::wstring> files;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            dirs.push_back({ fd.cFileName, fd.dwFileAttributes });
        else
            files.push_back(fd.cFileName);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    for (const auto& d : dirs) {
        std::wstring childPath = dir + L"\\" + d.name;
        int icon = GetSysIconIndex(childPath, d.attrs);
        bool isReparse = (d.attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        InsertTreeNode(hTree, hParent, d.name, childPath, icon, true, isReparse);
    }
    for (const auto& name : files) {
        std::wstring childPath = dir + L"\\" + name;
        int icon = GetSysIconIndex(childPath);
        InsertTreeNode(hTree, hParent, name, childPath, icon, false, false);
    }
}

// Retrieve the heap-stored full path for a tree item (empty if not set).
static std::wstring GetTreeItemPath(HWND hTree, HTREEITEM hItem) {
    TVITEMW tvi = {};
    tvi.mask = TVIF_PARAM | TVIF_HANDLE;
    tvi.hItem = hItem;
    if (!TreeView_GetItem(hTree, &tvi)) return std::wstring();
    auto* p = (std::wstring*)tvi.lParam;
    return p ? *p : std::wstring();
}

static void PopulateTreeFromPath(HWND hTree, const std::wstring& path) {
    SendMessage(hTree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(hTree);

    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        std::wstring name = BasketStore::ExtractFileName(path);
        int rootIcon = GetSysIconIndex(path, attr);
        bool isReparse = (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        HTREEITEM hRoot = InsertTreeNode(hTree, TVI_ROOT, name, path,
                                          rootIcon, true, isReparse);
        // Load first level immediately and expand the root, so the user sees
        // the directory contents without an extra click. Deeper levels load
        // lazily on TVN_ITEMEXPANDINGW.
        if (!isReparse) {
            PopulateChildren(hTree, hRoot, path);
            TreeView_Expand(hTree, hRoot, TVE_EXPAND);
        }
    }

    SendMessage(hTree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTree, NULL, TRUE);
}

static void UpdateTreeForSelection(DlgData* dd) {
    if (!dd || !dd->hTree) return;

    int sel = ListView_GetNextItem(dd->hList, -1, LVNI_SELECTED | LVNI_FOCUSED);
    if (sel < 0) sel = ListView_GetNextItem(dd->hList, -1, LVNI_SELECTED);

    std::wstring path;
    if (sel >= 0) {
        WCHAR szPath[MAX_PATH] = {};
        ListView_GetItemText(dd->hList, sel, 2, szPath, MAX_PATH);
        path = szPath;
    }

    if (path == dd->lastTreePath) return;  // no change → avoid flicker
    dd->lastTreePath = path;

    PopulateTreeFromPath(dd->hTree, path);
}

static void PopulateListView(HWND hList, const std::vector<std::wstring>& files) {
    ListView_DeleteAllItems(hList);
    const StringTable& S = GetStrings();
    for (int i = 0; i < (int)files.size(); i++) {
        const std::wstring& path = files[i];

        // Extract filename (last path component)
        std::wstring name = BasketStore::ExtractFileName(path);

        // Determine type (file or folder)
        DWORD attr = GetFileAttributesW(path.c_str());
        const wchar_t* typeText = S.DlgTypeFile;
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            typeText = S.DlgTypeFolder;
        }

        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT | LVIF_IMAGE;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)name.c_str();
        lvi.iImage = GetSysIconIndex(path, attr);
        ListView_InsertItem(hList, &lvi);

        ListView_SetItemText(hList, i, 1, (LPWSTR)typeText);
        ListView_SetItemText(hList, i, 2, (LPWSTR)path.c_str());
    }
}

static void UpdateStatusBar(DlgData* dd) {
    if (!dd->hStatusBar) return;
    int count = ListView_GetItemCount(dd->hList);
    WCHAR szText[64] = {};
    swprintf(szText, 64, GetStrings().DlgStatusFiles, count);
    SendMessageW(dd->hStatusBar, SB_SETTEXTW, 0, (LPARAM)szText);
}

static void RefreshFromDisk(DlgData* dd) {
    std::vector<std::wstring> files = BasketStore::ReadBasket();
    PopulateListView(dd->hList, files);
    UpdateStatusBar(dd);
}

static void RemoveSelected(DlgData* dd) {
    std::vector<std::wstring> remaining;
    int count = ListView_GetItemCount(dd->hList);

    for (int i = 0; i < count; i++) {
        if (ListView_GetItemState(dd->hList, i, LVIS_SELECTED) & LVIS_SELECTED)
            continue;

        WCHAR szPath[MAX_PATH] = {};
        ListView_GetItemText(dd->hList, i, 2, szPath, MAX_PATH);
        if (szPath[0] != L'\0') {
            remaining.push_back(szPath);
        }
    }

    BasketStore::WriteBasket(remaining);
    RefreshFromDisk(dd);
}

static void LayoutControls(HWND hwnd, DlgData* dd);  // forward declaration

// Shared layout math used by both LayoutControls and SplitterWndProc.
// contentH = client height below status bar.
// availH   = height available for ListView + Splitter + TreeView.
struct LayoutMetrics {
    int width;     // client width
    int contentH;
    int availH;
};

static LayoutMetrics ComputeLayoutMetrics(HWND hwnd, DlgData* dd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int sbHeight = 0;
    if (dd->hStatusBar) {
        RECT sbRect;
        GetWindowRect(dd->hStatusBar, &sbRect);
        sbHeight = sbRect.bottom - sbRect.top;
    }
    LayoutMetrics m;
    m.width    = rc.right;
    m.contentH = rc.bottom - sbHeight;
    m.availH   = m.contentH - BTN_H - 3 * MARGIN - SPLITTER_H;
    return m;
}

static LRESULT CALLBACK SplitterWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        if (GetCapture() == hwnd) {
            HWND hParent = GetParent(hwnd);
            DlgData* dd = (DlgData*)GetWindowLongPtr(hParent, GWLP_USERDATA);
            if (!dd) return 0;

            // Convert mouse position (splitter-local) to parent client coords
            POINT pt;
            pt.x = (short)LOWORD(lParam);
            pt.y = (short)HIWORD(lParam);
            ClientToScreen(hwnd, &pt);
            ScreenToClient(hParent, &pt);

            LayoutMetrics m = ComputeLayoutMetrics(hParent, dd);
            if (m.availH < 2 * MIN_PANE_H) return 0;
            int availH = m.availH;

            // pt.y is where the splitter's top edge should be (in parent coords)
            int newListH = pt.y - MARGIN;
            if (newListH < MIN_PANE_H) newListH = MIN_PANE_H;
            if (newListH > availH - MIN_PANE_H) newListH = availH - MIN_PANE_H;

            int newRatio = newListH * 100 / availH;
            if (newRatio < 10) newRatio = 10;
            if (newRatio > 90) newRatio = 90;

            if (newRatio != dd->splitRatioPct) {
                dd->splitRatioPct = newRatio;
                LayoutControls(hParent, dd);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void LayoutControls(HWND hwnd, DlgData* dd) {
    // Notify status bar to self-position before measuring it.
    if (dd->hStatusBar) SendMessageW(dd->hStatusBar, WM_SIZE, 0, 0);

    LayoutMetrics m = ComputeLayoutMetrics(hwnd, dd);
    int w = m.width;
    int contentH = m.contentH;
    int availH = m.availH;
    if (availH < 2 * MIN_PANE_H) availH = 2 * MIN_PANE_H;

    int listH = availH * dd->splitRatioPct / 100;
    if (listH < MIN_PANE_H) listH = MIN_PANE_H;
    if (listH > availH - MIN_PANE_H) listH = availH - MIN_PANE_H;
    int treeH = availH - listH;

    HDWP hdwp = BeginDeferWindowPos(5);
    if (hdwp) {
        hdwp = DeferWindowPos(hdwp, dd->hList, NULL,
            MARGIN, MARGIN, w - 2 * MARGIN, listH,
            SWP_NOZORDER | SWP_NOACTIVATE);
        hdwp = DeferWindowPos(hdwp, dd->hSplitter, NULL,
            MARGIN, MARGIN + listH, w - 2 * MARGIN, SPLITTER_H,
            SWP_NOZORDER | SWP_NOACTIVATE);
        hdwp = DeferWindowPos(hdwp, dd->hTree, NULL,
            MARGIN, MARGIN + listH + SPLITTER_H, w - 2 * MARGIN, treeH,
            SWP_NOZORDER | SWP_NOACTIVATE);
        hdwp = DeferWindowPos(hdwp, dd->hBtnRemove, NULL,
            w - 2 * BTN_W - 2 * MARGIN, contentH - BTN_H - MARGIN, BTN_W, BTN_H,
            SWP_NOZORDER | SWP_NOACTIVATE);
        hdwp = DeferWindowPos(hdwp, dd->hBtnClose, NULL,
            w - BTN_W - MARGIN, contentH - BTN_H - MARGIN, BTN_W, BTN_H,
            SWP_NOZORDER | SWP_NOACTIVATE);
        EndDeferWindowPos(hdwp);
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

// ---------------------------------------------------------------------------
// DlgWndProc message handlers (extracted for readability)
// ---------------------------------------------------------------------------

static LRESULT OnCreate(HWND hwnd, DlgData* dd) {
    dd->hList = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0,
        hwnd, (HMENU)(INT_PTR)IDC_LISTVIEW, GetModuleHandle(NULL), NULL);
    ListView_SetExtendedListViewStyle(dd->hList,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    // Fetch the shared system small-icon image list once — it's a shell
    // global, so we can safely attach the same handle to both controls.
    SHFILEINFOW sfiProbe = {};
    HIMAGELIST hSysImgList = (HIMAGELIST)SHGetFileInfoW(
        L"C:\\", 0, &sfiProbe, sizeof(sfiProbe),
        SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    if (hSysImgList) {
        ListView_SetImageList(dd->hList, hSysImgList, LVSIL_SMALL);
    }

    // Columns with saved widths
    int col0w = (int)RegReadDword(L"ColWidth0", 220);
    int col1w = (int)RegReadDword(L"ColWidth1", 90);
    int col2w = (int)RegReadDword(L"ColWidth2", 320);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.cx = col0w;
    col.pszText = (LPWSTR)GetStrings().DlgColFilename;
    col.iSubItem = 0;
    ListView_InsertColumn(dd->hList, 0, &col);

    col.cx = col1w;
    col.pszText = (LPWSTR)GetStrings().DlgColType;
    col.iSubItem = 1;
    ListView_InsertColumn(dd->hList, 1, &col);

    col.cx = col2w;
    col.pszText = (LPWSTR)GetStrings().DlgColPath;
    col.iSubItem = 2;
    ListView_InsertColumn(dd->hList, 2, &col);

    dd->hSplitter = CreateWindowExW(
        0, SPLITTER_CLASS, L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0,
        hwnd, (HMENU)(INT_PTR)IDC_SPLITTER, GetModuleHandle(NULL), NULL);

    dd->hTree = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
        0, 0, 0, 0,
        hwnd, (HMENU)(INT_PTR)IDC_TREEVIEW, GetModuleHandle(NULL), NULL);

    if (hSysImgList) {
        TreeView_SetImageList(dd->hTree, hSysImgList, TVSIL_NORMAL);
    }

    dd->hBtnRemove = CreateWindowExW(
        0, L"BUTTON", GetStrings().DlgBtnRemove,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hwnd, (HMENU)(INT_PTR)IDC_REMOVE, GetModuleHandle(NULL), NULL);

    dd->hBtnClose = CreateWindowExW(
        0, L"BUTTON", GetStrings().DlgBtnClose,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hwnd, (HMENU)(INT_PTR)IDC_CLOSE, GetModuleHandle(NULL), NULL);

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessage(dd->hBtnRemove, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(dd->hBtnClose, WM_SETFONT, (WPARAM)hFont, TRUE);

    dd->hStatusBar = CreateWindowExW(
        0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        hwnd, nullptr, GetModuleHandle(NULL), nullptr);

    RefreshFromDisk(dd);
    DragAcceptFiles(hwnd, TRUE);
    LayoutControls(hwnd, dd);

    // Window icon from DLL resource
    HICON hIconSmall = (HICON)LoadImageW(g_hModule, MAKEINTRESOURCE(IDI_BASKET),
                                          IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON),
                                          GetSystemMetrics(SM_CYSMICON),
                                          LR_SHARED);
    HICON hIconBig = (HICON)LoadImageW(g_hModule, MAKEINTRESOURCE(IDI_BASKET),
                                        IMAGE_ICON,
                                        GetSystemMetrics(SM_CXICON),
                                        GetSystemMetrics(SM_CYICON),
                                        LR_SHARED);
    if (hIconSmall) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    if (hIconBig)   SendMessage(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIconBig);

    return 0;
}

static LRESULT OnDropFiles(DlgData* dd, HDROP hDrop) {
    if (!dd) return 0;
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
    std::vector<std::wstring> paths;
    paths.reserve(count);
    for (UINT i = 0; i < count; i++) {
        UINT len = DragQueryFileW(hDrop, i, NULL, 0);
        if (len == 0) continue;
        std::wstring p(len, L'\0');
        DragQueryFileW(hDrop, i, &p[0], len + 1);
        paths.push_back(p);
    }
    DragFinish(hDrop);
    if (!paths.empty()) {
        BasketStore::AddFiles(paths);
        RefreshFromDisk(dd);
    }
    return 0;
}

static LRESULT OnCommand(HWND hwnd, DlgData* dd, WORD id) {
    if (id == IDC_REMOVE) {
        if (dd) {
            RemoveSelected(dd);
            UpdateTreeForSelection(dd);
        }
    } else if (id == IDC_CLOSE || id == IDCANCEL) {
        DestroyWindow(hwnd);
    }
    return 0;
}

static LRESULT OnNotify(DlgData* dd, NMHDR* nm, LPARAM lParam) {
    if (nm->idFrom == IDC_TREEVIEW) {
        // TreeView is a read-only viewer — never allow selection
        if (nm->code == TVN_SELCHANGINGW) {
            return TRUE;
        }
        // Lazy-load children on first expand. TVIS_EXPANDEDONCE is not yet
        // set while expanding for the first time; once set, we skip re-
        // populating (children persist across collapse/expand cycles).
        if (nm->code == TVN_ITEMEXPANDINGW) {
            NMTREEVIEWW* ntv = (NMTREEVIEWW*)lParam;
            if (ntv->action == TVE_EXPAND &&
                !(ntv->itemNew.state & TVIS_EXPANDEDONCE)) {
                HWND hTree = nm->hwndFrom;
                HTREEITEM hItem = ntv->itemNew.hItem;
                std::wstring path = GetTreeItemPath(hTree, hItem);
                if (!path.empty()) {
                    SendMessage(hTree, WM_SETREDRAW, FALSE, 0);
                    PopulateChildren(hTree, hItem, path);
                    // If the directory turned out to be empty, hide the
                    // expand arrow so the user isn't misled.
                    if (!TreeView_GetChild(hTree, hItem)) {
                        TVITEMW tvi = {};
                        tvi.mask = TVIF_CHILDREN | TVIF_HANDLE;
                        tvi.hItem = hItem;
                        tvi.cChildren = 0;
                        TreeView_SetItem(hTree, &tvi);
                    }
                    SendMessage(hTree, WM_SETREDRAW, TRUE, 0);
                }
            }
            return 0;
        }
        // Free the heap-allocated path string we attached in InsertTreeNode.
        if (nm->code == TVN_DELETEITEMW) {
            NMTREEVIEWW* ntv = (NMTREEVIEWW*)lParam;
            if (ntv->itemOld.lParam) {
                delete (std::wstring*)ntv->itemOld.lParam;
            }
            return 0;
        }
        return 0;
    }
    if (nm->idFrom == IDC_LISTVIEW) {
        if (nm->code == LVN_KEYDOWN) {
            NMLVKEYDOWN* kd = (NMLVKEYDOWN*)lParam;
            if (kd->wVKey == VK_DELETE && dd) {
                RemoveSelected(dd);
                UpdateTreeForSelection(dd);
            } else if (kd->wVKey == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                ListView_SetItemState(dd->hList, -1, LVIS_SELECTED, LVIS_SELECTED);
            }
        } else if (nm->code == LVN_COLUMNCLICK && dd) {
            NMLISTVIEW* nlv = (NMLISTVIEW*)lParam;
            if (nlv->iSubItem == dd->sortColumn) {
                dd->sortAscending = !dd->sortAscending;
            } else {
                dd->sortColumn = nlv->iSubItem;
                dd->sortAscending = true;
            }
            SortListView(dd);
        } else if (nm->code == LVN_ITEMCHANGED && dd) {
            NMLISTVIEW* nlv = (NMLISTVIEW*)lParam;
            if ((nlv->uChanged & LVIF_STATE) &&
                ((nlv->uNewState & LVIS_SELECTED) || (nlv->uNewState & LVIS_FOCUSED))) {
                UpdateTreeForSelection(dd);
            }
        }
    }
    return 0;
}

static LRESULT CALLBACK DlgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgData* dd = (DlgData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        dd = (DlgData*)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)dd);
        return OnCreate(hwnd, dd);
    }

    case WM_SIZE:
        if (dd) LayoutControls(hwnd, dd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = MIN_WIDTH;
        mmi->ptMinTrackSize.y = MIN_HEIGHT;
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) DestroyWindow(hwnd);
        return 0;

    case WM_DROPFILES:
        return OnDropFiles(dd, (HDROP)wParam);

    case WM_COMMAND:
        return OnCommand(hwnd, dd, LOWORD(wParam));

    case WM_NOTIFY:
        return OnNotify(dd, (NMHDR*)lParam, lParam);

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (dd) SaveDialogState(hwnd, dd);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Show(HWND hwndParent) {
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DlgWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WND_CLASS;
    RegisterClassW(&wc);

    WNDCLASSW wcSplit = {};
    wcSplit.lpfnWndProc = SplitterWndProc;
    wcSplit.hInstance = GetModuleHandle(NULL);
    wcSplit.hCursor = LoadCursor(NULL, IDC_SIZENS);
    wcSplit.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcSplit.lpszClassName = SPLITTER_CLASS;
    RegisterClassW(&wcSplit);

    DlgData dd = {};
    dd.splitRatioPct = (int)RegReadDword(L"SplitRatio", 60);
    if (dd.splitRatioPct < 10) dd.splitRatioPct = 10;
    if (dd.splitRatioPct > 90) dd.splitRatioPct = 90;

    // Load saved window size or use defaults
    int ww = (int)RegReadDword(L"DialogWidth", 620);
    int wh = (int)RegReadDword(L"DialogHeight", 420);

    // Center on screen
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - ww) / 2;
    int y = (sh - wh) / 2;

    HWND hwnd = CreateWindowExW(
        0,
        WND_CLASS, GetStrings().DlgBasketTitle,
        (WS_OVERLAPPEDWINDOW & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_CLIPCHILDREN,
        x, y, ww, wh,
        hwndParent, NULL, GetModuleHandle(NULL), &dd);

    if (!hwnd) return;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetFocus(dd.hList);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    UnregisterClassW(WND_CLASS, GetModuleHandle(NULL));
    UnregisterClassW(SPLITTER_CLASS, GetModuleHandle(NULL));
}

} // namespace BasketDialog
