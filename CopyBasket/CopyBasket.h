#pragma once

#ifndef STRICT
#define STRICT
#endif

#define INC_OLE2

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <string>
#include <vector>

// Command offsets for the context menu
enum CmdOffset {
    CMD_ADD = 0,
    CMD_COPY_PATH,
    CMD_SHOW,
    CMD_COPY_HERE,
    CMD_COPY_TO,
    CMD_MOVE_HERE,
    CMD_MOVE_TO,
    CMD_CLEAR,
    CMD_SETTINGS,
    CMD_COUNT
};

class CShellExtClassFactory : public IClassFactory {
protected:
    ULONG m_cRef;

public:
    CShellExtClassFactory();
    ~CShellExtClassFactory();

    STDMETHODIMP QueryInterface(REFIID, LPVOID FAR*);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();
    STDMETHODIMP CreateInstance(LPUNKNOWN, REFIID, LPVOID FAR*);
    STDMETHODIMP LockServer(BOOL);
};
typedef CShellExtClassFactory* LPCSHELLEXTCLASSFACTORY;

class CShellExt : public IContextMenu, IShellExtInit {
protected:
    ULONG m_cRef;
    LPDATAOBJECT m_pDataObj;
    STGMEDIUM m_stgMedium;
    UINT m_cbFiles;
    BOOL m_bIsBackground;
    WCHAR m_szFolder[MAX_PATH];
    HBITMAP m_hMenuBitmap;

public:
    CShellExt();
    ~CShellExt();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID, LPVOID FAR*);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IShellExtInit
    STDMETHODIMP Initialize(LPCITEMIDLIST pIDFolder, LPDATAOBJECT pDataObj, HKEY hKeyID);

    // IContextMenu
    STDMETHODIMP QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags);
    STDMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO lpcmi);
    STDMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uFlags, UINT FAR* reserved, LPSTR pszName, UINT cchMax);

private:
    std::vector<std::wstring> GetSelectedFiles();
    static HBITMAP IconToBitmap(HICON hIcon, int cx, int cy);

    // Shared dispatch for the four file-operation context-menu commands.
    // isCopy=false → move; toPicker=false → use m_szFolder, toPicker=true → ask user.
    void HandleFileOp(LPCMINVOKECOMMANDINFO lpcmi, bool isCopy, bool toPicker);
};
typedef CShellExt* LPCSHELLEXT;
