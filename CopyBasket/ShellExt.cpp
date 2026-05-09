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

extern volatile LONG g_cRef;
extern HINSTANCE g_hModule;

//---------------------------------------------------------------------------
// CShellExt
//---------------------------------------------------------------------------
CShellExt::CShellExt() {
    m_cRef = 0L;
    m_pDataObj = NULL;
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
    if (m_pDataObj) {
        m_pDataObj->Release();
        m_pDataObj = NULL;
    }
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
// IShellExtInit::Initialize
//---------------------------------------------------------------------------
STDMETHODIMP CShellExt::Initialize(LPCITEMIDLIST pIDFolder, LPDATAOBJECT pDataObj, HKEY hRegKey) {
    m_szFolder[0] = L'\0';
    m_bIsBackground = TRUE;
    m_cbFiles = 0;

    // Release previous state
    if (m_pDataObj) {
        m_pDataObj->Release();
        m_pDataObj = NULL;
    }
    if (m_stgMedium.hGlobal) {
        ReleaseStgMedium(&m_stgMedium);
        ZeroMemory(&m_stgMedium, sizeof(m_stgMedium));
    }

    // Get folder path from pidlFolder
    if (pIDFolder) {
        SHGetPathFromIDListW(pIDFolder, m_szFolder);
    }

    // Try to get selected files via CF_HDROP
    if (pDataObj) {
        m_pDataObj = pDataObj;
        pDataObj->AddRef();

        FORMATETC fmte = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        if (SUCCEEDED(pDataObj->GetData(&fmte, &m_stgMedium))) {
            m_cbFiles = DragQueryFileW((HDROP)m_stgMedium.hGlobal, (UINT)-1, NULL, 0);
            if (m_cbFiles > 0) {
                m_bIsBackground = FALSE;

                // If no folder from pidlFolder, extract from first file path
                if (m_szFolder[0] == L'\0') {
                    WCHAR szFile[MAX_PATH];
                    DragQueryFileW((HDROP)m_stgMedium.hGlobal, 0, szFile, MAX_PATH);
                    lstrcpyW(m_szFolder, szFile);
                    WCHAR* pSlash = wcsrchr(m_szFolder, L'\\');
                    if (pSlash) *pSlash = L'\0';
                }

                // If single folder selected, use it as target for "hierher" operations
                if (m_cbFiles == 1) {
                    WCHAR szFile[MAX_PATH];
                    DragQueryFileW((HDROP)m_stgMedium.hGlobal, 0, szFile, MAX_PATH);
                    DWORD attr = GetFileAttributesW(szFile);
                    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                        lstrcpyW(m_szFolder, szFile);
                    }
                }
            } else {
                ReleaseStgMedium(&m_stgMedium);
                ZeroMemory(&m_stgMedium, sizeof(m_stgMedium));
            }
        }

        // Fallback: CF_HDROP unavailable (nav pane, virtual folders, etc.)
        if (m_bIsBackground) {
            IShellItemArray* psia = NULL;
            if (SUCCEEDED(SHCreateShellItemArrayFromDataObject(pDataObj, IID_PPV_ARGS(&psia)))) {
                DWORD count = 0;
                psia->GetCount(&count);
                if (count > 0) {
                    m_bIsBackground = FALSE;
                    m_cbFiles = count;

                    IShellItem* psi = NULL;
                    if (SUCCEEDED(psia->GetItemAt(0, &psi))) {
                        PWSTR pszPath = NULL;
                        if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                            // If no folder from pidlFolder, extract parent from first item
                            if (m_szFolder[0] == L'\0') {
                                lstrcpyW(m_szFolder, pszPath);
                                WCHAR* pSlash = wcsrchr(m_szFolder, L'\\');
                                if (pSlash) *pSlash = L'\0';
                            }
                            // Single folder: use as target for "hierher" operations
                            if (count == 1) {
                                SFGAOF attrs = 0;
                                if (SUCCEEDED(psi->GetAttributes(SFGAO_FOLDER, &attrs)) && (attrs & SFGAO_FOLDER)) {
                                    lstrcpyW(m_szFolder, pszPath);
                                }
                            }
                            CoTaskMemFree(pszPath);
                        }
                        psi->Release();
                    }
                }
                psia->Release();
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

    case CMD_COPY_HERE: {
        std::vector<std::wstring> files = BasketStore::ReadBasket();
        if (!files.empty() && m_szFolder[0] != L'\0') {
            FileOps::CopyFilesToFolderAsync(files, m_szFolder, true, lpcmi->hwnd);
        }
        break;
    }

    case CMD_COPY_TO: {
        std::vector<std::wstring> files = BasketStore::ReadBasket();
        bool fromBasket = !files.empty();
        if (files.empty()) files = GetSelectedFiles();
        if (!files.empty()) {
            std::wstring folder;
            if (FileOps::BrowseForFolder(lpcmi->hwnd, folder)) {
                FileOps::CopyFilesToFolderAsync(files, folder, fromBasket, lpcmi->hwnd);
            }
        }
        break;
    }

    case CMD_MOVE_HERE: {
        std::vector<std::wstring> files = BasketStore::ReadBasket();
        if (!files.empty() && m_szFolder[0] != L'\0') {
            FileOps::MoveFilesToFolderAsync(files, m_szFolder, true, lpcmi->hwnd);
        }
        break;
    }

    case CMD_MOVE_TO: {
        std::vector<std::wstring> files = BasketStore::ReadBasket();
        bool fromBasket = !files.empty();
        if (files.empty()) files = GetSelectedFiles();
        if (!files.empty()) {
            std::wstring folder;
            if (FileOps::BrowseForFolder(lpcmi->hwnd, folder)) {
                FileOps::MoveFilesToFolderAsync(files, folder, fromBasket, lpcmi->hwnd);
            }
        }
        break;
    }

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
STDMETHODIMP CShellExt::GetCommandString(UINT_PTR idCmd, UINT uFlags, UINT FAR* reserved, LPSTR pszName, UINT cchMax) {
    const StringTable& S = GetStrings();
    if (uFlags == GCS_HELPTEXTW) {
        LPCWSTR szHelp = NULL;
        switch (idCmd) {
        case CMD_ADD:       szHelp = S.HelpAdd; break;
        case CMD_COPY_PATH: szHelp = S.HelpCopyPath; break;
        case CMD_SHOW:      szHelp = S.HelpShow; break;
        case CMD_COPY_HERE: szHelp = S.HelpCopyHere; break;
        case CMD_COPY_TO:   szHelp = S.HelpCopyTo; break;
        case CMD_MOVE_HERE: szHelp = S.HelpMoveHere; break;
        case CMD_MOVE_TO:   szHelp = S.HelpMoveTo; break;
        case CMD_CLEAR:     szHelp = S.HelpClear; break;
        case CMD_SETTINGS:  szHelp = S.HelpSettings; break;
        }
        if (szHelp) {
            lstrcpynW((LPWSTR)pszName, szHelp, cchMax);
        }
    } else if (uFlags == GCS_HELPTEXTA) {
        LPCSTR szHelp = NULL;
        switch (idCmd) {
        case CMD_ADD:       szHelp = S.HelpAddA; break;
        case CMD_COPY_PATH: szHelp = S.HelpCopyPathA; break;
        case CMD_SHOW:      szHelp = S.HelpShowA; break;
        case CMD_COPY_HERE: szHelp = S.HelpCopyHereA; break;
        case CMD_COPY_TO:   szHelp = S.HelpCopyToA; break;
        case CMD_MOVE_HERE: szHelp = S.HelpMoveHereA; break;
        case CMD_MOVE_TO:   szHelp = S.HelpMoveToA; break;
        case CMD_CLEAR:     szHelp = S.HelpClearA; break;
        case CMD_SETTINGS:  szHelp = S.HelpSettingsA; break;
        }
        if (szHelp) {
            lstrcpynA(pszName, szHelp, cchMax);
        }
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
        IShellItemArray* psia = NULL;
        if (SUCCEEDED(SHCreateShellItemArrayFromDataObject(m_pDataObj, IID_PPV_ARGS(&psia)))) {
            DWORD count = 0;
            psia->GetCount(&count);
            for (DWORD i = 0; i < count; i++) {
                IShellItem* psi = NULL;
                if (SUCCEEDED(psia->GetItemAt(i, &psi))) {
                    PWSTR pszPath = NULL;
                    if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                        files.push_back(pszPath);
                        CoTaskMemFree(pszPath);
                    }
                    psi->Release();
                }
            }
            psia->Release();
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
