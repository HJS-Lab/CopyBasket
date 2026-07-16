//---------------------------------------------------------------------------
// CopyBasket.cpp
// DLL Entry Point, COM Boilerplate, Registration
//---------------------------------------------------------------------------

#ifndef STRICT
#define STRICT
#endif

#define INC_OLE2

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>

#pragma data_seg(".text")
#define INITGUID
#include <initguid.h>
#include <shlguid.h>
#include "GUID.h"
#include "resource.h"
#include "CopyBasket.h"
#pragma data_seg()

//---------------------------------------------------------------------------
// Global variables
//---------------------------------------------------------------------------
volatile LONG g_cRef = 0;
HINSTANCE g_hModule = NULL;

typedef struct {
    HKEY    hRootKey;
    LPCWSTR szSubKey;
    LPCWSTR lpszValueName;
    LPCWSTR szData;
} DOREGSTRUCT;

// Subkeys under HKCR that wire CopyBasket into Explorer as a context-menu
// handler. One entry per shell location. Shared between RegisterServer and
// UnregisterServer so the list lives in exactly one place.
static const wchar_t* const SHELLEX_KEYS[] = {
    L"*\\shellex\\ContextMenuHandlers\\CopyBasket",
    L"Directory\\shellex\\ContextMenuHandlers\\CopyBasket",
    L"Directory\\Background\\shellex\\ContextMenuHandlers\\CopyBasket",
};

static BOOL RegisterServer(CLSID clsid, LPCWSTR lpszTitle);
static BOOL UnregisterServer(CLSID clsid, LPCWSTR lpszTitle);

//---------------------------------------------------------------------------
// DllMain
//---------------------------------------------------------------------------
extern "C" int APIENTRY
DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hInstance;
        // No per-thread work in DllMain and no static TLS — opt out of
        // DLL_THREAD_ATTACH/DETACH so Explorer's constant thread churn does
        // not serialize through here under the loader lock.
        DisableThreadLibraryCalls(hInstance);
    }
    return 1;
}

//---------------------------------------------------------------------------
// DllCanUnloadNow
//---------------------------------------------------------------------------
STDAPI DllCanUnloadNow(void) {
    return (g_cRef == 0 ? S_OK : S_FALSE);
}

//---------------------------------------------------------------------------
// DllGetClassObject
//---------------------------------------------------------------------------
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppvOut) {
    *ppvOut = NULL;
    if (IsEqualIID(rclsid, CLSID_CopyBasket)) {
        CShellExtClassFactory* pcf = new CShellExtClassFactory;
        HRESULT hr = pcf->QueryInterface(riid, ppvOut);
        // On QI failure no AddRef happened, so m_cRef stays 0 and the object
        // would leak (and its ctor's g_cRef increment would pin the DLL
        // forever). Release the construction reference explicitly.
        if (FAILED(hr)) delete pcf;
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

//---------------------------------------------------------------------------
// DllRegisterServer
//---------------------------------------------------------------------------
STDAPI DllRegisterServer() {
    return RegisterServer(CLSID_CopyBasket, L"CopyBasket") ? S_OK : E_FAIL;
}

//---------------------------------------------------------------------------
// DllUnregisterServer
//---------------------------------------------------------------------------
STDAPI DllUnregisterServer() {
    return UnregisterServer(CLSID_CopyBasket, L"CopyBasket") ? S_OK : E_FAIL;
}

//---------------------------------------------------------------------------
// RegisterServer
//---------------------------------------------------------------------------
static BOOL RegisterServer(CLSID clsid, LPCWSTR lpszTitle) {
    WCHAR szCLSID[128];
    WCHAR szModule[MAX_PATH];
    LPWSTR pwsz;

    StringFromIID(clsid, &pwsz);
    if (!pwsz) return FALSE;
    lstrcpyW(szCLSID, pwsz);
    CoTaskMemFree(pwsz);

    GetModuleFileNameW(g_hModule, szModule, MAX_PATH);

    // COM-server infrastructure: friendly name + InprocServer32 path + threading model.
    DOREGSTRUCT clsidEntries[] = {
        { HKEY_CLASSES_ROOT, L"CLSID\\%s",                 NULL,              lpszTitle   },
        { HKEY_CLASSES_ROOT, L"CLSID\\%s\\InprocServer32", NULL,              szModule    },
        { HKEY_CLASSES_ROOT, L"CLSID\\%s\\InprocServer32", L"ThreadingModel", L"Apartment"},
    };
    for (const auto& e : clsidEntries) {
        WCHAR szSubKey[MAX_PATH];
        wsprintfW(szSubKey, e.szSubKey, szCLSID);
        HKEY hKey;
        if (RegCreateKeyExW(e.hRootKey, szSubKey, 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
            return FALSE;
        RegSetValueExW(hKey, e.lpszValueName, 0, REG_SZ,
                       (const BYTE*)e.szData, (lstrlenW(e.szData) + 1) * sizeof(WCHAR));
        RegCloseKey(hKey);
    }

    // Shell ContextMenuHandlers — one entry per location (see SHELLEX_KEYS).
    DWORD cbCLSID = (lstrlenW(szCLSID) + 1) * sizeof(WCHAR);
    for (const wchar_t* path : SHELLEX_KEYS) {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CLASSES_ROOT, path, 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
            return FALSE;
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)szCLSID, cbCLSID);
        RegCloseKey(hKey);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return TRUE;
}

//---------------------------------------------------------------------------
// UnregisterServer
//---------------------------------------------------------------------------
static BOOL UnregisterServer(CLSID clsid, LPCWSTR lpszTitle) {
    WCHAR szCLSID[128];
    LPWSTR pwsz;

    StringFromIID(clsid, &pwsz);
    if (!pwsz) return FALSE;
    lstrcpyW(szCLSID, pwsz);
    CoTaskMemFree(pwsz);

    for (const wchar_t* path : SHELLEX_KEYS) {
        RegDeleteKeyW(HKEY_CLASSES_ROOT, path);
    }

    WCHAR szKey[MAX_PATH];
    wsprintfW(szKey, L"CLSID\\%s\\InprocServer32", szCLSID);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, szKey);

    wsprintfW(szKey, L"CLSID\\%s", szCLSID);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, szKey);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return TRUE;
}

//---------------------------------------------------------------------------
// CShellExtClassFactory
//---------------------------------------------------------------------------
CShellExtClassFactory::CShellExtClassFactory() {
    m_cRef = 0L;
    InterlockedIncrement(&g_cRef);
}

CShellExtClassFactory::~CShellExtClassFactory() {
    InterlockedDecrement(&g_cRef);
}

STDMETHODIMP CShellExtClassFactory::QueryInterface(REFIID riid, LPVOID FAR* ppv) {
    *ppv = NULL;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
        *ppv = (LPCLASSFACTORY)this;
        AddRef();
        return NOERROR;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CShellExtClassFactory::AddRef() {
    return ++m_cRef;
}

STDMETHODIMP_(ULONG) CShellExtClassFactory::Release() {
    if (--m_cRef) return m_cRef;
    delete this;
    return 0L;
}

STDMETHODIMP CShellExtClassFactory::CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid, LPVOID* ppvObj) {
    *ppvObj = NULL;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    CShellExt* pShellExt = new CShellExt();
    if (!pShellExt) return E_OUTOFMEMORY;
    return pShellExt->QueryInterface(riid, ppvObj);
}

STDMETHODIMP CShellExtClassFactory::LockServer(BOOL fLock) {
    return NOERROR;
}
