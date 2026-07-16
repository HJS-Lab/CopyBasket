#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <shellapi.h>
#include "SettingsDialog.h"
#include "Strings.h"
#include "Version.h"
#include "BasketStore.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

extern HINSTANCE g_hModule;

namespace SettingsDialog {

static const WCHAR* WND_CLASS = L"CopyBasketSettings";
static const int IDC_TAB = 2000;
static const int IDC_COMBO = 2001;
static const int IDC_OK = 2002;
static const int IDC_CANCEL_BTN = 2003;
static const int IDC_LABEL = 2004;
static const int IDC_ABOUT_TEXT = 2005;
static const int IDC_LINK = 2006;
static const int IDC_COPYRIGHT = 2007;

struct DlgData {
    HWND hTab;
    // Language page
    HWND hLabel;
    HWND hCombo;
    // About page
    HWND hAboutText;
    HWND hLink;
    HWND hCopyright;
    HFONT hBoldFont;
    // Buttons
    HWND hBtnOK;
    HWND hBtnCancel;
    int initialSel;
};

static int GetCurrentLangIndex() {
    WCHAR szLang[16] = {};
    DWORD cbData = sizeof(szLang);
    LSTATUS res = RegGetValueW(HKEY_CURRENT_USER, BasketStore::REG_KEY,
                                L"Language", RRF_RT_REG_SZ, NULL, szLang, &cbData);
    if (res == ERROR_SUCCESS && _wcsicmp(szLang, L"en") == 0) {
        return 1;
    }
    return 0;
}

static void ShowPage(DlgData* dd, int page) {
    BOOL showLang = (page == 0);
    BOOL showAbout = (page == 1);

    ShowWindow(dd->hLabel, showLang ? SW_SHOW : SW_HIDE);
    ShowWindow(dd->hCombo, showLang ? SW_SHOW : SW_HIDE);
    ShowWindow(dd->hAboutText, showAbout ? SW_SHOW : SW_HIDE);
    ShowWindow(dd->hLink, showAbout ? SW_SHOW : SW_HIDE);
    ShowWindow(dd->hCopyright, showAbout ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK DlgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgData* dd = (DlgData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        dd = (DlgData*)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)dd);

        const StringTable& S = GetStrings();
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // Tab Control
        dd->hTab = CreateWindowExW(
            0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            8, 8, 304, 130,
            hwnd, (HMENU)(INT_PTR)IDC_TAB, GetModuleHandle(NULL), NULL);
        SendMessage(dd->hTab, WM_SETFONT, (WPARAM)hFont, TRUE);
        SetWindowTheme(dd->hTab, L"", L"");

        TCITEMW tie = {};
        tie.mask = TCIF_TEXT;
        tie.pszText = (LPWSTR)S.SettingsTabLanguage;
        SendMessageW(dd->hTab, TCM_INSERTITEMW, 0, (LPARAM)&tie);
        tie.pszText = (LPWSTR)S.SettingsTabAbout;
        SendMessageW(dd->hTab, TCM_INSERTITEMW, 1, (LPARAM)&tie);

        // Language page controls (inside tab area)
        dd->hLabel = CreateWindowExW(
            0, L"STATIC", S.SettingsLanguageLabel,
            WS_CHILD | WS_VISIBLE,
            24, 68, 80, 20,
            hwnd, (HMENU)(INT_PTR)IDC_LABEL, GetModuleHandle(NULL), NULL);
        SendMessage(dd->hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

        dd->hCombo = CreateWindowExW(
            0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            108, 65, 190, 120,
            hwnd, (HMENU)(INT_PTR)IDC_COMBO, GetModuleHandle(NULL), NULL);
        SendMessage(dd->hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(dd->hCombo, CB_ADDSTRING, 0, (LPARAM)S.SettingsLangNameDe);
        SendMessageW(dd->hCombo, CB_ADDSTRING, 0, (LPARAM)S.SettingsLangNameEn);
        SendMessageW(dd->hCombo, CB_SETCURSEL, dd->initialSel, 0);

        // Bold font for title (1px larger)
        LOGFONTW lf = {};
        GetObjectW(hFont, sizeof(lf), &lf);
        lf.lfWeight = FW_BOLD;
        lf.lfHeight = lf.lfHeight + (lf.lfHeight < 0 ? -3 : 3);
        dd->hBoldFont = CreateFontIndirectW(&lf);

        // About page controls — Title (bold, centered)
        WCHAR szTitle[128];
        wsprintfW(szTitle, L"CopyBasket v%s", COPYBASKET_VERSION_STR);

        dd->hAboutText = CreateWindowExW(
            0, L"STATIC", szTitle,
            WS_CHILD | SS_CENTER,
            24, 48, 272, 20,
            hwnd, (HMENU)(INT_PTR)IDC_ABOUT_TEXT, GetModuleHandle(NULL), NULL);
        SendMessage(dd->hAboutText, WM_SETFONT, (WPARAM)dd->hBoldFont, TRUE);

        // About page controls — Website link (centered via LM_GETIDEALSIZE)
        dd->hLink = CreateWindowExW(
            0, WC_LINK,
            L"<a href=\"https://hjs.page.gd/cb\">hjs.page.gd/cb</a>",
            WS_CHILD,
            0, 72, 272, 20,
            hwnd, (HMENU)(INT_PTR)IDC_LINK, GetModuleHandle(NULL), NULL);
        SendMessage(dd->hLink, WM_SETFONT, (WPARAM)hFont, TRUE);

        SIZE idealSize = {};
        SendMessageW(dd->hLink, LM_GETIDEALSIZE, 272, (LPARAM)&idealSize);
        int linkW = idealSize.cx;
        int linkX = 24 + (272 - linkW) / 2;
        SetWindowPos(dd->hLink, NULL, linkX, 72, linkW, 20, SWP_NOZORDER);

        // About page controls — Copyright (centered)
        dd->hCopyright = CreateWindowExW(
            0, L"STATIC",
            S.SettingsCopyright,
            WS_CHILD | SS_CENTER,
            24, 96, 272, 20,
            hwnd, (HMENU)(INT_PTR)IDC_COPYRIGHT, GetModuleHandle(NULL), NULL);
        SendMessage(dd->hCopyright, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Buttons
        dd->hBtnOK = CreateWindowExW(
            0, L"BUTTON", S.SettingsOK,
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            140, 148, 85, 28,
            hwnd, (HMENU)(INT_PTR)IDC_OK, GetModuleHandle(NULL), NULL);
        SendMessage(dd->hBtnOK, WM_SETFONT, (WPARAM)hFont, TRUE);

        dd->hBtnCancel = CreateWindowExW(
            0, L"BUTTON", S.SettingsCancel,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            230, 148, 85, 28,
            hwnd, (HMENU)(INT_PTR)IDC_CANCEL_BTN, GetModuleHandle(NULL), NULL);
        SendMessage(dd->hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        ShowPage(dd, 0);
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* pnm = (NMHDR*)lParam;
        if (pnm->idFrom == IDC_TAB && pnm->code == TCN_SELCHANGE) {
            int sel = (int)SendMessageW(dd->hTab, TCM_GETCURSEL, 0, 0);
            ShowPage(dd, sel);
        }
        if (pnm->idFrom == IDC_LINK && (pnm->code == NM_CLICK || pnm->code == NM_RETURN)) {
            ShellExecuteW(NULL, L"open", L"https://hjs.page.gd/cb", NULL, NULL, SW_SHOWNORMAL);
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_OK) {
            if (dd) {
                int sel = (int)SendMessageW(dd->hCombo, CB_GETCURSEL, 0, 0);
                const wchar_t* lang = (sel == 1) ? L"en" : L"de";
                SaveLanguageSetting(lang);
            }
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == IDC_CANCEL_BTN || LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (dd && dd->hBoldFont) {
            DeleteObject(dd->hBoldFont);
            dd->hBoldFont = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Show(HWND hwndParent) {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_TAB_CLASSES | ICC_LINK_CLASS };
    InitCommonControlsEx(&icex);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DlgWndProc;
    wc.hInstance = g_hModule;  // WndProc lives in this DLL — bind the class to it
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WND_CLASS;
    // fRegistered is FALSE if the class already exists (another dialog open) —
    // only unregister a class we actually registered, never one out from under
    // a concurrent dialog.
    BOOL fRegistered = RegisterClassW(&wc);
    if (!fRegistered && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return;

    DlgData dd = {};
    dd.initialSel = GetCurrentLangIndex();

    const StringTable& S = GetStrings();

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = 336, wh = 220;
    int x = (sw - ww) / 2;
    int y = (sh - wh) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        WND_CLASS, S.SettingsTitle,
        WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        x, y, ww, wh,
        hwndParent, NULL, g_hModule, &dd);

    if (!hwnd) {
        if (fRegistered) UnregisterClassW(WND_CLASS, g_hModule);
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    if (hwndParent) EnableWindow(hwndParent, FALSE);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (hwndParent) {
        EnableWindow(hwndParent, TRUE);
        SetForegroundWindow(hwndParent);
    }

    if (fRegistered) UnregisterClassW(WND_CLASS, g_hModule);
}

} // namespace SettingsDialog
