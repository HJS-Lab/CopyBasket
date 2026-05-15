//---------------------------------------------------------------------------
// ShellExt.cpp
// IShellExtInit + IContextMenu Implementation
//---------------------------------------------------------------------------

#include "CopyBasket.h"
#include "BasketStore.h"
#include "FileOps.h"
#include "BasketDialog.h"
#include "Strings.h"
#include "SettingsDialog.h"
#include "resource.h"
#include <shellapi.h>

using Microsoft::WRL::ComPtr;

extern volatile LONG g_cRef;
extern HINSTANCE g_hModule;

//---------------------------------------------------------------------------
// CShellExt
//---------------------------------------------------------------------------
CShellExt::CShellExt() {
    m_cRef = 0L;
    m_cbFiles = 0;
    m_bIsBackground = TRUE;
    m_szFolder[0] = L'\0';
    m_hMenuBitmap = NULL;
    ZeroMemory(&m_stgMedium, sizeof(m_stgMedium));

    // Load language setting from registry
    LoadLanguageSetting();

    // Load basket icon from DLL resource
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    HICON hIcon = (HICON)LoadImageW(g_hModule, MAKEINTRESOURCE(IDI_BASKET),
                                     IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (hIcon) {
        m_hMenuBitmap = IconToBitmap(hIcon, cx, cy);
        DestroyIcon(hIcon);
    }

    InterlockedIncrement(&g_cRef);
}

CShellExt::~CShellExt() {
    // m_pDataObj is released by ComPtr destructor
    if (m_stgMedium.hGlobal) {
        ReleaseStgMedium(&m_stgMedium);
    }
    if (m_hMenuBitmap) {
        DeleteObject(m_hMenuBitmap);
    }
    InterlockedDecrement(&g_cRef);
}

STDMETHODIMP CShellExt::QueryInterface(REFIID riid, LPVOID FAR* ppv) {
    *ppv = NULL;
    if (IsEqualIID(riid, IID_IShellExtInit) || IsEqualIID(riid, IID_IUnknown)) {
        *ppv = (LPSHELLEXTINIT)this;
    } else if (IsEqualIID(riid, IID_IContextMenu)) {
        *ppv = (LPCONTEXTMENU)this;
    }
    if (*ppv) {
        AddRef();
        return NOERROR;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CShellExt::AddRef() {
    return ++m_cRef;
}

STDMETHODIMP_(ULONG) CShellExt::Release() {
    if (--m_cRef) return m_cRef;
    delete this;
    return 0L;
}

//---------------------------------------------------------------------------
// ResolveClickTarget — set m_szFolder from the first clicked item.
// Default: parent directory. Special case: a single-folder click uses the
// folder itself so "hierher" operations target inside it. Used by both the
// HDROP and the IShellItemArray code paths in Initialize.
//---------------------------------------------------------------------------
void CShellExt::ResolveClickTarget(LPCWSTR firstPath, bool isFolder, bool isSingleItem) {
    if (m_szFolder[0] == L'\0') {
        lstrcpyW(m_szFolder, firstPath);
        WCHAR* pSlash = wcsrchr(m_szFolder, L'\\');
        if (pSlash) *pSlash = L'\0';
    }
    if (isSingleItem && isFolder) {
        lstrcpyW(m_szFolder, firstPath);
    }
}

//---------------------------------------------------------------------------
// IShellExtInit::Initialize
//---------------------------------------------------------------------------
STDMETHODIMP CShellExt::Initialize(LPCITEMIDLIST pIDFolder, LPDATAOBJECT pDataObj, HKEY hRegKey) {
    m_szFolder[0] = L'\0';
    m_bIsBackground = TRUE;
    m_cbFiles = 0;

    m_pDataObj.Reset();
    if (m_stgMedium.hGlobal) {
        ReleaseStgMedium(&m_stgMedium);
        ZeroMemory(&m_stgMedium, sizeof(m_stgMedium));
    }

    if (pIDFolder) {
        SHGetPathFromIDListW(pIDFolder, m_szFolder);
    }

    if (!pDataObj) return S_OK;
    m_pDataObj = pDataObj;  // ComPtr assignment AddRef's

    // Primary: CF_HDROP (regular file/folder selections in the right pane)
    FORMATETC fmte = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (SUCCEEDED(pDataObj->GetData(&fmte, &m_stgMedium))) {
        m_cbFiles = DragQueryFileW((HDROP)m_stgMedium.hGlobal, (UINT)-1, NULL, 0);
        if (m_cbFiles > 0) {
            m_bIsBackground = FALSE;

            WCHAR szFile[MAX_PATH];
            DragQueryFileW((HDROP)m_stgMedium.hGlobal, 0, szFile, MAX_PATH);

            bool isSingle = (m_cbFiles == 1);
            bool isFolder = false;
            if (isSingle) {
                DWORD attr = GetFileAttributesW(szFile);
                isFolder = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
            }
            ResolveClickTarget(szFile, isFolder, isSingle);
        } else {
            ReleaseStgMedium(&m_stgMedium);
            ZeroMemory(&m_stgMedium, sizeof(m_stgMedium));
        }
    }

    // Fallback: CF_HDROP unavailable (nav pane, virtual folders, etc.)
    if (m_bIsBackground) {
        ComPtr<IShellItemArray> psia;
        if (SUCCEEDED(SHCreateShellItemArrayFromDataObject(pDataObj, IID_PPV_ARGS(psia.GetAddressOf())))) {
            DWORD count = 0;
            psia->GetCount(&count);
            if (count > 0) {
                m_bIsBackground = FALSE;
                m_cbFiles = count;

                ComPtr<IShellItem> psi;
                if (SUCCEEDED(psia->GetItemAt(0, psi.GetAddressOf()))) {
                    PWSTR pszPath = NULL;
                    if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                        bool isSingle = (count == 1);
                        bool isFolder = false;
                        if (isSingle) {
                            SFGAOF attrs = 0;
                            isFolder = SUCCEEDED(psi->GetAttributes(SFGAO_FOLDER, &attrs)) && (attrs & SFGAO_FOLDER);
                        }
                        ResolveClickTarget(pszPath, isFolder, isSingle);
                        CoTaskMemFree(pszPath);
                    }
                }
            }
        }
    }

    return S_OK;
}

//---------------------------------------------------------------------------
// IContextMenu::QueryContextMenu
//---------------------------------------------------------------------------
STDMETHODIMP CShellExt::QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) {
    if (uFlags & CMF_DEFAULTONLY)
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

    HMENU hSubMenu = CreatePopupMenu();
    if (!hSubMenu)
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

    const StringTable& S = GetStrings();
    int basketCount = BasketStore::GetFileCount();
    UINT grayed = (basketCount == 0) ? MF_GRAYED : 0;
    // "...nach" items: enabled when basket has files OR files are selected
    UINT grayedTo = (basketCount == 0 && m_bIsBackground) ? MF_GRAYED : 0;
    UINT pos = 0;

    // "Zum Korb hinzufuegen" - only for file/folder clicks
    if (!m_bIsBackground) {
        InsertMenuW(hSubMenu, pos++, MF_STRING | MF_BYPOSITION,
                    idCmdFirst + CMD_ADD, S.MenuAddToBasket);
    }

    // "Korb anzeigen (X Dateien)"
    WCHAR szShow[128];
    wsprintfW(szShow, S.MenuShowBasketFmt, basketCount);
    InsertMenuW(hSubMenu, pos++, MF_STRING | MF_BYPOSITION | grayed,
                idCmdFirst + CMD_SHOW, szShow);

    // Separator
    InsertMenuW(hSubMenu, pos++, MF_SEPARATOR | MF_BYPOSITION, 0, NULL);

    InsertMenuW(hSubMenu, pos++, MF_STRING | MF_BYPOSITION | grayed,
                idCmdFirst + CMD_COPY_HERE, S.MenuCopyHere);

    InsertMenuW(hSubMenu, pos++, MF_STRING | MF_BYPOSITION | grayedTo,
                idCmdFirst + CMD_COPY_TO, S.MenuCopyTo);

    InsertMenuW(hSubMenu, pos++, MF_STRING | MF_BYPOSITION | grayed,
                idCmdFirst + CMD_MOVE_HERE, S.MenuMoveHere);

    InsertMenuW(hSubMenu, pos++, MF_STRING | MF_BYPOSITION | grayedTo,
                idCmdFirst + CMD_MOVE_TO, S.MenuMoveTo);

    // Separator
    InsertMenuW(hSubMenu, pos++, MF_SEPARATOR | MF_BYPOSITION, 0, NULL);

    InsertMenuW(hSubMenu, pos++, MF_STRING | MF_BYPOSITION | grayed,
                idCmdFirst + CMD_CLEAR, S.MenuClearBasket);

    // Separator
    InsertMenuW(hSubMenu, pos++, MF_SEPARATOR | MF_BYPOSITION, 0, NULL);

    // "Pfad kopieren" - only for file/folder clicks
    if (!m_bIsBackground) {
        InsertMenuW(hSubMenu, pos++, MF_STRING | MF_BYPOSITION,
                    idCmdFirst + CMD_COPY_PATH, S.MenuCopyPath);
        InsertMenuW(hSubMenu, pos++, MF_SEPARATOR | MF_BYPOSITION, 0, NULL);
    }

    InsertMenuW(hSubMenu, pos++, MF_STRING | MF_BYPOSITION,
                idCmdFirst + CMD_SETTINGS, S.MenuSettings);

    // Insert submenu as popup into main context menu (with icon)
    MENUITEMINFOW mii = {};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_FTYPE;
    mii.fType = MFT_STRING;
    mii.hSubMenu = hSubMenu;
    mii.dwTypeData = (LPWSTR)L"CopyBasket";
    if (m_hMenuBitmap) {
        mii.fMask |= MIIM_BITMAP;
        mii.hbmpItem = m_hMenuBitmap;
    }
    InsertMenuItemW(hMenu, indexMenu, TRUE, &mii);

    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, CMD_COUNT);
}

//---------------------------------------------------------------------------
// HandleFileOp — shared dispatch for CMD_COPY_HERE/CMD_COPY_TO/CMD_MOVE_HERE/CMD_MOVE_TO
//---------------------------------------------------------------------------
void CShellExt::HandleFileOp(LPCMINVOKECOMMANDINFO lpcmi, bool isCopy, bool toPicker) {
    std::vector<std::wstring> files = BasketStore::ReadBasket();
    bool fromBasket = !files.empty();

    // Picker variants fall back to the current selection when the basket is empty.
    if (toPicker && files.empty()) files = GetSelectedFiles();
    if (files.empty()) return;

    std::wstring folder;
    if (toPicker) {
        if (!FileOps::BrowseForFolder(lpcmi->hwnd, folder)) return;
    } else {
        if (m_szFolder[0] == L'\0') return;
        folder = m_szFolder;
    }

    if (isCopy)
        FileOps::CopyFilesToFolderAsync(files, folder, fromBasket, lpcmi->hwnd);
    else
        FileOps::MoveFilesToFolderAsync(files, folder, fromBasket, lpcmi->hwnd);
}

//---------------------------------------------------------------------------
// IContextMenu::InvokeCommand
//---------------------------------------------------------------------------
STDMETHODIMP CShellExt::InvokeCommand(LPCMINVOKECOMMANDINFO lpcmi) {
    if (HIWORD(lpcmi->lpVerb))
        return E_INVALIDARG;

    UINT idCmd = LOWORD(lpcmi->lpVerb);

    switch (idCmd) {
    case CMD_ADD: {
        std::vector<std::wstring> files = GetSelectedFiles();
        if (!files.empty()) {
            BasketStore::AddFiles(files);
        }
        break;
    }

    case CMD_COPY_PATH: {
        std::vector<std::wstring> files = GetSelectedFiles();
        if (!files.empty()) {
            std::wstring text;
            for (size_t i = 0; i < files.size(); i++) {
                if (i > 0) text += L"\r\n";
                text += files[i];
            }
            if (OpenClipboard(lpcmi->hwnd)) {
                EmptyClipboard();
                SIZE_T cb = (text.size() + 1) * sizeof(WCHAR);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, cb);
                if (hMem) {
                    WCHAR* p = (WCHAR*)GlobalLock(hMem);
                    memcpy(p, text.c_str(), cb);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
            }
        }
        break;
    }

    case CMD_SHOW: {
        BasketDialog::Show(lpcmi->hwnd);
        break;
    }

    case CMD_COPY_HERE: HandleFileOp(lpcmi, /*isCopy*/ true,  /*toPicker*/ false); break;
    case CMD_COPY_TO:   HandleFileOp(lpcmi, /*isCopy*/ true,  /*toPicker*/ true);  break;
    case CMD_MOVE_HERE: HandleFileOp(lpcmi, /*isCopy*/ false, /*toPicker*/ false); break;
    case CMD_MOVE_TO:   HandleFileOp(lpcmi, /*isCopy*/ false, /*toPicker*/ true);  break;

    case CMD_CLEAR:
        BasketStore::ClearBasket();
        break;

    case CMD_SETTINGS:
        SettingsDialog::Show(lpcmi->hwnd);
        break;

    default:
        return E_INVALIDARG;
    }

    return S_OK;
}

//---------------------------------------------------------------------------
// IContextMenu::GetCommandString
//---------------------------------------------------------------------------
namespace {
    struct HelpPair { LPCWSTR w; LPCSTR a; };

    HelpPair LookupHelp(const StringTable& S, UINT idCmd) {
        switch (idCmd) {
        case CMD_ADD:       return { S.HelpAdd,      S.HelpAddA      };
        case CMD_COPY_PATH: return { S.HelpCopyPath, S.HelpCopyPathA };
        case CMD_SHOW:      return { S.HelpShow,     S.HelpShowA     };
        case CMD_COPY_HERE: return { S.HelpCopyHere, S.HelpCopyHereA };
        case CMD_COPY_TO:   return { S.HelpCopyTo,   S.HelpCopyToA   };
        case CMD_MOVE_HERE: return { S.HelpMoveHere, S.HelpMoveHereA };
        case CMD_MOVE_TO:   return { S.HelpMoveTo,   S.HelpMoveToA   };
        case CMD_CLEAR:     return { S.HelpClear,    S.HelpClearA    };
        case CMD_SETTINGS:  return { S.HelpSettings, S.HelpSettingsA };
        }
        return { NULL, NULL };
    }
}

STDMETHODIMP CShellExt::GetCommandString(UINT_PTR idCmd, UINT uFlags, UINT FAR* reserved, LPSTR pszName, UINT cchMax) {
    if (uFlags != GCS_HELPTEXTW && uFlags != GCS_HELPTEXTA) return S_OK;

    HelpPair h = LookupHelp(GetStrings(), (UINT)idCmd);
    if (uFlags == GCS_HELPTEXTW) {
        if (h.w) lstrcpynW((LPWSTR)pszName, h.w, cchMax);
    } else {
        if (h.a) lstrcpynA(pszName, h.a, cchMax);
    }
    return S_OK;
}

//---------------------------------------------------------------------------
// GetSelectedFiles - Extract file paths from stored HDROP
//---------------------------------------------------------------------------
std::vector<std::wstring> CShellExt::GetSelectedFiles() {
    std::vector<std::wstring> files;

    // Primary: extract from HDROP
    if (m_stgMedium.hGlobal) {
        HDROP hDrop = (HDROP)m_stgMedium.hGlobal;
        UINT count = DragQueryFileW(hDrop, (UINT)-1, NULL, 0);
        for (UINT i = 0; i < count; i++) {
            WCHAR szFile[MAX_PATH];
            if (DragQueryFileW(hDrop, i, szFile, MAX_PATH) > 0) {
                files.push_back(szFile);
            }
        }
    }

    // Fallback: IShellItemArray (e.g. nav pane folders)
    if (files.empty() && m_pDataObj) {
        ComPtr<IShellItemArray> psia;
        if (SUCCEEDED(SHCreateShellItemArrayFromDataObject(m_pDataObj.Get(), IID_PPV_ARGS(psia.GetAddressOf())))) {
            DWORD count = 0;
            psia->GetCount(&count);
            for (DWORD i = 0; i < count; i++) {
                ComPtr<IShellItem> psi;
                if (SUCCEEDED(psia->GetItemAt(i, psi.GetAddressOf()))) {
                    PWSTR pszPath = NULL;
                    if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                        files.push_back(pszPath);
                        CoTaskMemFree(pszPath);
                    }
                }
            }
        }
    }

    return files;
}

//---------------------------------------------------------------------------
// IconToBitmap - Convert HICON to 32-bit ARGB HBITMAP for menu use
//---------------------------------------------------------------------------
HBITMAP CShellExt::IconToBitmap(HICON hIcon, int cx, int cy) {
    if (!hIcon) return NULL;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcScreen = GetDC(NULL);
    void* bits = NULL;
    HBITMAP hbmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbmp) {
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HGDIOBJ hOld = SelectObject(hdcMem, hbmp);
        DrawIconEx(hdcMem, 0, 0, hIcon, cx, cy, 0, NULL, DI_NORMAL);
        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
    }
    ReleaseDC(NULL, hdcScreen);

    return hbmp;
}
