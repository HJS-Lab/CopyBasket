#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include "resource.h"

// Mutex name for single instance check
static const wchar_t* MUTEX_NAME = L"CopyBasketContextMenuMutex";

// DLL filename (located in same directory as EXE)
static const wchar_t* DLL_NAME = L"CopyBasket.dll";

// Registry path to check if DLL is registered
static const wchar_t* REG_PATH = L"SOFTWARE\\Classes\\*\\shellex\\ContextMenuHandlers\\CopyBasket";

// User settings registry key (HKCU). Mirrors BasketStore::REG_KEY in the main project;
// duplicated because CB-CMT does not link against BasketStore.
static const wchar_t* SETTINGS_KEY = L"Software\\CopyBasket";

// User-visible message strings (single source of truth)
static const wchar_t* MSG_WARNING_TEXT     = L"\x26A0 Deletes all program settings permanently";
static const wchar_t* MSG_CONFIRM_DELETE   = L"This will permanently delete all CopyBasket user settings.\n\nAre you sure?";
static const wchar_t* TITLE_CONFIRM_DELETE = L"Confirm Deletion";
static const wchar_t* MSG_DLL_NOT_FOUND    = L"CopyBasket.dll not found.\nThe DLL must be in the same directory as CB-CMT.exe.";
static const wchar_t* MSG_OP_FAILED        = L"Operation failed. Please ensure CopyBasket.dll exists.";
static const wchar_t* MSG_EXEC_FAILED      = L"Failed to execute regsvr32.";
static const wchar_t* MSG_ACTIVATED        = L"CopyBasket Context Menu activated successfully.";
static const wchar_t* MSG_DEACTIVATED      = L"CopyBasket Context Menu deactivated successfully.";
static const wchar_t* TITLE_SUCCESS        = L"Success";
static const wchar_t* TITLE_ERROR          = L"Error";

// Check if the CopyBasket context menu handler is registered
static BOOL IsCopyBasketRegistered()
{
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, REG_PATH, 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        return TRUE;
    }
    return FALSE;
}

// Delete CopyBasket user settings (registry + %APPDATA%\CopyBasket folder)
static BOOL DeleteUserSettings()
{
    BOOL success = RegDeleteKeyW(HKEY_CURRENT_USER, SETTINGS_KEY) == ERROR_SUCCESS;

    // Delete %APPDATA%\CopyBasket\ folder (contains basket.txt)
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData)))
    {
        wchar_t basketDir[MAX_PATH];
        StringCchPrintfW(basketDir, ARRAYSIZE(basketDir), L"%s\\CopyBasket", appData);

        // SHFileOperation requires double-null-terminated string
        wchar_t from[MAX_PATH + 1] = {};
        StringCchCopyW(from, MAX_PATH, basketDir);

        SHFILEOPSTRUCTW op = {};
        op.wFunc = FO_DELETE;
        op.pFrom = from;
        op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        SHFileOperationW(&op);
    }

    return success;
}

// Build full path to CopyBasket.dll in the EXE's directory. Returns FALSE on overflow.
static BOOL BuildDllPath(wchar_t* out, size_t cch)
{
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, ARRAYSIZE(exePath)) == 0)
        return FALSE;

    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash)
        *lastSlash = L'\0';

    return SUCCEEDED(StringCchPrintfW(out, cch, L"%s\\%s", exePath, DLL_NAME));
}

// Run regsvr32 synchronously. Returns regsvr32 exit code, or non-zero sentinel on launch failure.
// On launch failure, *outLaunched is set to FALSE.
static DWORD RunRegsvr32(LPCWSTR dllPath, BOOL activate, BOOL* outLaunched)
{
    *outLaunched = FALSE;

    // Params: "/u /s \"<dllPath>\"" — worst case ~6 chars overhead
    wchar_t params[MAX_PATH + 32];
    if (FAILED(StringCchPrintfW(params, ARRAYSIZE(params),
        activate ? L"/s \"%s\"" : L"/u /s \"%s\"", dllPath)))
    {
        return ERROR_BUFFER_OVERFLOW;
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    sei.lpFile = L"regsvr32.exe";
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei) || !sei.hProcess)
        return ERROR_GEN_FAILURE;

    *outLaunched = TRUE;
    WaitForSingleObject(sei.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    return exitCode;
}

// Handle the OK button: optional settings deletion, then activate/deactivate via regsvr32.
static void OnOkClicked(HWND hDlg)
{
    // 1. Handle "Delete User Settings" checkbox
    if (IsDlgButtonChecked(hDlg, IDC_CHECK_CLEANREG) == BST_CHECKED)
    {
        int confirm = MessageBoxW(hDlg, MSG_CONFIRM_DELETE, TITLE_CONFIRM_DELETE,
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);

        if (confirm != IDYES)
            return; // User cancelled, stay in dialog

        DeleteUserSettings();
    }

    // 2. Locate the DLL
    wchar_t dllPath[MAX_PATH];
    if (!BuildDllPath(dllPath, ARRAYSIZE(dllPath)) ||
        GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES)
    {
        MessageBoxW(hDlg, MSG_DLL_NOT_FOUND, TITLE_ERROR, MB_OK | MB_ICONERROR);
        return; // Stay in dialog so the user can drop the DLL next to the EXE and retry
    }

    // 3. Run regsvr32
    BOOL activate = IsDlgButtonChecked(hDlg, IDC_RADIO_ACTIVATE) == BST_CHECKED;
    BOOL launched = FALSE;
    DWORD exitCode = RunRegsvr32(dllPath, activate, &launched);

    if (!launched)
    {
        MessageBoxW(hDlg, MSG_EXEC_FAILED, TITLE_ERROR, MB_OK | MB_ICONERROR);
        return;
    }

    if (exitCode == 0)
    {
        MessageBoxW(hDlg, activate ? MSG_ACTIVATED : MSG_DEACTIVATED,
            TITLE_SUCCESS, MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        MessageBoxW(hDlg, MSG_OP_FAILED, TITLE_ERROR, MB_OK | MB_ICONERROR);
    }

    EndDialog(hDlg, IDOK);
}

// Dialog procedure
INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // Set dialog icon
        HICON hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APPICON));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

        // Select based on current registration status
        CheckRadioButton(hDlg, IDC_RADIO_ACTIVATE, IDC_RADIO_DEACTIVATE,
            IsCopyBasketRegistered() ? IDC_RADIO_DEACTIVATE : IDC_RADIO_ACTIVATE);

        // Set warning text (hidden by default)
        SetDlgItemTextW(hDlg, IDC_WARNING_TEXT, MSG_WARNING_TEXT);

        // Disable checkbox if no user settings exist yet
        HKEY hSettingsKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_READ, &hSettingsKey) == ERROR_SUCCESS)
            RegCloseKey(hSettingsKey);
        else
            EnableWindow(GetDlgItem(hDlg, IDC_CHECK_CLEANREG), FALSE);

        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_CHECK_CLEANREG:
        {
            // Toggle warning text visibility
            BOOL checked = IsDlgButtonChecked(hDlg, IDC_CHECK_CLEANREG) == BST_CHECKED;
            ShowWindow(GetDlgItem(hDlg, IDC_WARNING_TEXT), checked ? SW_SHOW : SW_HIDE);
            return TRUE;
        }

        case IDOK:
            OnOkClicked(hDlg);
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

// Application entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Single instance check
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (hMutex)
            CloseHandle(hMutex);
        return 0;
    }

    // Show the dialog
    DialogBoxW(hInstance, MAKEINTRESOURCEW(IDD_MAIN_DIALOG), NULL, DialogProc);

    // Cleanup
    if (hMutex)
        CloseHandle(hMutex);

    return 0;
}
