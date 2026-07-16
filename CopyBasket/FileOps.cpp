#include <windows.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <shellapi.h>
#include <process.h>
#include <wrl/client.h>
#include <cstdio>
#include "FileOps.h"
#include "BasketStore.h"
#include "Strings.h"

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "comctl32.lib")

extern volatile LONG g_cRef;

namespace FileOps {

// Abort-dialog button IDs (TaskDialog) and BrowseForFolder re-entrance throttle.
static const int IDBTN_OPEN_LOG = 1001;
static const int IDBTN_CLOSE = 1002;
static const DWORD BROWSE_THROTTLE_MS = 500;

// Single-op-in-flight guard. Two concurrent IFileOperation threads in the same
// process would race on the same source files and clobber operations.log
// (CREATE_ALWAYS + exclusive share). Acquired atomically in LaunchFileOp,
// released by the background thread at exit. Process-local — Explorer's
// default single-process model makes this sufficient; if a user opens
// separate-process Explorer windows they each get their own slot.
static volatile LONG g_fileOpInFlight = 0;

BOOL IsBusy() {
    return g_fileOpInFlight != 0 ? TRUE : FALSE;
}

// ---------------------------------------------------------------------------
// CFileOperationProgressSink — removes files from basket after each success
// ---------------------------------------------------------------------------
class CFileOperationProgressSink : public IFileOperationProgressSink {
    volatile LONG m_cRef;
    bool m_removeFromBasket;
    std::vector<std::wstring> m_processed;  // successfully processed paths

    void TrackSuccess(IShellItem* psiItem) {
        if (!m_removeFromBasket || !psiItem) return;
        PWSTR pszPath = NULL;
        if (SUCCEEDED(psiItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
            m_processed.push_back(pszPath);
            CoTaskMemFree(pszPath);
        }
    }

public:
    CFileOperationProgressSink(bool removeFromBasket)
        : m_cRef(1), m_removeFromBasket(removeFromBasket) {
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
            *ppv = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef()  { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP StartOperations()    { return S_OK; }
    IFACEMETHODIMP FinishOperations(HRESULT) {
        if (m_removeFromBasket && !m_processed.empty()) {
            // Atomic remove preserves any entries the user added during the
            // operation (read-modify-write happens under a single lock).
            BasketStore::RemoveFiles(m_processed);
        }
        return S_OK;
    }
    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) { return S_OK; }
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) { return S_OK; }
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) { return S_OK; }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) { return S_OK; }
    IFACEMETHODIMP PreDeleteItem(DWORD, IShellItem*) { return S_OK; }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem*, HRESULT, IShellItem*) { return S_OK; }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) { return S_OK; }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) { return S_OK; }
    IFACEMETHODIMP UpdateProgress(UINT, UINT) { return S_OK; }
    IFACEMETHODIMP ResetTimer() { return S_OK; }
    IFACEMETHODIMP PauseTimer() { return S_OK; }
    IFACEMETHODIMP ResumeTimer() { return S_OK; }

    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem* psiItem, IShellItem*, LPCWSTR, HRESULT hrCopy, IShellItem*) {
        if (SUCCEEDED(hrCopy)) TrackSuccess(psiItem);
        return S_OK;
    }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem* psiItem, IShellItem*, LPCWSTR, HRESULT hrMove, IShellItem*) {
        if (SUCCEEDED(hrMove)) TrackSuccess(psiItem);
        return S_OK;
    }
};

// ---------------------------------------------------------------------------
// Expected-file enumeration — recursively collects source files from the
// paths passed into IFileOperation so the log can report on nested contents.
// ---------------------------------------------------------------------------
struct ExpectedFile {
    std::wstring sourcePath;
    std::wstring destPath;
    bool destPreexisted;        // did destPath already exist before the op?
    FILETIME destPreWriteTime;  // its last-write time if it preexisted
};

// Snapshot the destination's pre-op state so the COPY post-check can tell a
// real copy/overwrite from a conflict-dialog "skip" (dest left untouched).
static ExpectedFile MakeExpected(const std::wstring& src, const std::wstring& dst) {
    ExpectedFile ef;
    ef.sourcePath = src;
    ef.destPath = dst;
    ef.destPreexisted = false;
    ef.destPreWriteTime = FILETIME{};
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(dst.c_str(), GetFileExInfoStandard, &fad)) {
        ef.destPreexisted = true;
        ef.destPreWriteTime = fad.ftLastWriteTime;
    }
    return ef;
}

static void EnumerateFilesRecursive(const std::wstring& srcDir, const std::wstring& destDir,
                                    std::vector<ExpectedFile>& out) {
    std::wstring pattern = srcDir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring srcFull  = srcDir  + L"\\" + fd.cFileName;
        std::wstring destFull = destDir + L"\\" + fd.cFileName;
        // Recurse into real directories only. Reparse points (junctions /
        // symlinks) are treated as leaves: following them can loop forever on
        // cyclic junctions or explode into a whole-volume walk → bad_alloc.
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            EnumerateFilesRecursive(srcFull, destFull, out);
        } else {
            out.push_back(MakeExpected(srcFull, destFull));
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

static std::vector<ExpectedFile> BuildExpectedFiles(const std::vector<std::wstring>& files,
                                                     const std::wstring& destFolder) {
    std::vector<ExpectedFile> expected;
    for (const auto& path : files) {
        DWORD attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) continue;

        // Item destination = destFolder + last path component
        std::wstring name = BasketStore::ExtractFileName(path);
        std::wstring itemDest = destFolder + L"\\" + name;

        if ((attr & FILE_ATTRIBUTE_DIRECTORY) && !(attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
            EnumerateFilesRecursive(path, itemDest, expected);
        } else {
            expected.push_back(MakeExpected(path, itemDest));
        }
    }
    return expected;
}

// ---------------------------------------------------------------------------
// WriteOperationLog — append incident entry to operations.log (UTF-16LE)
// ---------------------------------------------------------------------------
static std::wstring WriteOperationLog(UINT wFunc, const std::wstring& destFolder, bool aborted,
                                       const std::vector<std::wstring>& succeeded,
                                       const std::vector<std::wstring>& notProcessed) {
    std::wstring dir = BasketStore::GetBasketDirPath();
    if (dir.empty()) return L"";

    std::wstring logPath = dir + L"operations.log";
    HANDLE hFile = CreateFileW(logPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return L"";

    // Write UTF-16LE BOM (file is always freshly created)
    {
        BYTE bom[2] = { 0xFF, 0xFE };
        DWORD written;
        WriteFile(hFile, bom, 2, &written, NULL);
    }

    const StringTable& S = GetStrings();

    // Helper lambda: write a wide string line to the file
    auto writeLine = [&](const std::wstring& line) {
        std::wstring buf = line + L"\r\n";
        DWORD written;
        WriteFile(hFile, buf.c_str(), (DWORD)(buf.size() * sizeof(WCHAR)), &written, NULL);
    };

    // Timestamp + operation type
    SYSTEMTIME st;
    GetLocalTime(&st);
    WCHAR szTime[64];
    swprintf_s(szTime, L"[%04d-%02d-%02d %02d:%02d:%02d] %s",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
               (wFunc == FO_COPY) ? S.LogOpCopy : S.LogOpMove);
    writeLine(szTime);

    // Target folder
    writeLine(std::wstring(S.LogTarget) + L" " + destFolder);
    writeLine(L"");

    // Succeeded files
    writeLine(S.LogSucceeded);
    if (succeeded.empty()) {
        writeLine(L"  -");
    } else {
        for (const auto& path : succeeded) {
            writeLine(L"  " + path);
        }
    }
    writeLine(L"");

    // Not processed / failed files
    writeLine(S.LogFailed);
    if (notProcessed.empty()) {
        writeLine(L"  -");
    } else {
        for (const auto& path : notProcessed) {
            writeLine(L"  " + path);
        }
    }
    writeLine(L"");

    // Status
    if (aborted) {
        writeLine(std::wstring(L"Status: ") + S.LogAborted);
    }

    writeLine(L"-----------------------------------------------------------");
    writeLine(L"");

    CloseHandle(hFile);
    return logPath;
}

// ---------------------------------------------------------------------------
// ShowAbortDialog — TaskDialog with "Open log" button
// ---------------------------------------------------------------------------
static void ShowAbortDialog(HWND hwndOwner, const std::wstring& logPath,
                             int notProcessedCount, int totalCount) {
    const StringTable& S = GetStrings();

    WCHAR content[256];
    swprintf_s(content, _countof(content), S.AbortMsgFmt, notProcessedCount, totalCount);

    TASKDIALOG_BUTTON buttons[] = {
        { IDBTN_OPEN_LOG, S.AbortBtnOpenLog },
        { IDBTN_CLOSE,    S.AbortBtnClose }
    };

    // Owner must still be valid; if not, fall back to foreground window so
    // the dialog cannot disappear behind Explorer.
    HWND hwndParent = (hwndOwner && IsWindow(hwndOwner)) ? hwndOwner : GetForegroundWindow();

    TASKDIALOGCONFIG tdc = { sizeof(tdc) };
    tdc.hwndParent = hwndParent;
    tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    tdc.pszWindowTitle = L"CopyBasket";
    tdc.pszMainIcon = TD_WARNING_ICON;
    tdc.pszMainInstruction = S.AbortTitle;
    tdc.pszContent = content;
    tdc.pButtons = buttons;
    tdc.cButtons = 2;
    tdc.nDefaultButton = IDBTN_CLOSE;

    int nButton = 0;
    TaskDialogIndirect(&tdc, &nButton, NULL, NULL);

    if (nButton == IDBTN_OPEN_LOG) {
        ShellExecuteW(NULL, L"open", logPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

// ---------------------------------------------------------------------------
// SchedulePerFileOps — queue copy/move calls into the IFileOperation
// ---------------------------------------------------------------------------
static void SchedulePerFileOps(IFileOperation* pfo, IShellItem* psiDest,
                               UINT wFunc, const std::vector<std::wstring>& files) {
    for (const auto& filePath : files) {
        IShellItem* psiFrom = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(filePath.c_str(), NULL,
                                                  IID_PPV_ARGS(&psiFrom)))) {
            if (wFunc == FO_COPY)
                pfo->CopyItem(psiFrom, psiDest, NULL, NULL);
            else
                pfo->MoveItem(psiFrom, psiDest, NULL, NULL);
            psiFrom->Release();
        }
    }
}

// ---------------------------------------------------------------------------
// ClassifyOutcome — filesystem-based post-check: which expected files made it
// ---------------------------------------------------------------------------
struct PostCheckResult {
    std::vector<std::wstring> actuallySucceeded;
    std::vector<std::wstring> notProcessed;
};

static PostCheckResult ClassifyOutcome(const std::vector<ExpectedFile>& expected, UINT wFunc) {
    PostCheckResult r;
    for (const auto& ef : expected) {
        bool srcExists = (GetFileAttributesW(ef.sourcePath.c_str()) != INVALID_FILE_ATTRIBUTES);

        WIN32_FILE_ATTRIBUTE_DATA destFad;
        bool destExists = GetFileAttributesExW(ef.destPath.c_str(),
                                               GetFileExInfoStandard, &destFad) != 0;

        if (wFunc == FO_MOVE) {
            if (!srcExists && destExists) r.actuallySucceeded.push_back(ef.sourcePath);
            else                          r.notProcessed.push_back(ef.sourcePath);
        } else { // FO_COPY
            // A destination that merely *exists* is not proof of success: it may
            // have pre-dated the operation and been skipped in the conflict
            // dialog. Count success only when the dest is newly created, or an
            // existing dest was actually overwritten (last-write time moved).
            bool copied;
            if (!destExists)               copied = false;
            else if (!ef.destPreexisted)   copied = true;
            else copied = (CompareFileTime(&destFad.ftLastWriteTime, &ef.destPreWriteTime) != 0);

            if (copied) r.actuallySucceeded.push_back(ef.sourcePath);
            else        r.notProcessed.push_back(ef.sourcePath);
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// ExecuteFileOpCOM — IFileOperation-based file copy/move (orchestrator)
// ---------------------------------------------------------------------------
static BOOL ExecuteFileOpCOM(HWND hwndOwner, UINT wFunc, const std::vector<std::wstring>& files,
                             const std::wstring& destFolder, bool removeFromBasket) {
    if (files.empty() || destFolder.empty()) return FALSE;

    // ---- Phase 1: COM setup, anchor dialogs, schedule per-file ops ----
    ComPtr<IFileOperation> pfo;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, NULL, CLSCTX_ALL,
                                  IID_PPV_ARGS(pfo.GetAddressOf()));
    if (FAILED(hr)) return FALSE;

    // Anchor any conflict/UAC dialogs to a real window so they cannot
    // disappear behind Explorer (especially relevant for cross-volume MOVE).
    if (hwndOwner && IsWindow(hwndOwner)) {
        pfo->SetOwnerWindow(hwndOwner);
    }
    pfo->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR);

    ComPtr<IShellItem> psiDest;
    hr = SHCreateItemFromParsingName(destFolder.c_str(), NULL,
                                     IID_PPV_ARGS(psiDest.GetAddressOf()));
    if (FAILED(hr)) return FALSE;  // pfo released by ComPtr dtor

    SchedulePerFileOps(pfo.Get(), psiDest.Get(), wFunc, files);

    // ---- Phase 2: pre-scan expected outcome, register sink, perform ops ----
    // Pre-scan recursively enumerates all source files so we can verify the
    // actual outcome afterwards (callbacks alone are not reliable for
    // deeply nested directories).
    std::vector<ExpectedFile> expectedFiles = BuildExpectedFiles(files, destFolder);

    CFileOperationProgressSink* pSink = new CFileOperationProgressSink(removeFromBasket);
    DWORD dwCookie = 0;
    pfo->Advise(pSink, &dwCookie);

    hr = pfo->PerformOperations();

    BOOL fAborted = FALSE;
    pfo->GetAnyOperationsAborted(&fAborted);

    // ---- Phase 3: filesystem post-check, write log / show dialog if needed ----
    PostCheckResult outcome = ClassifyOutcome(expectedFiles, wFunc);

    if (fAborted || !outcome.notProcessed.empty()) {
        std::wstring logPath = WriteOperationLog(wFunc, destFolder, fAborted != FALSE,
                                                  outcome.actuallySucceeded, outcome.notProcessed);
        if (!logPath.empty()) {
            int total = (int)(outcome.actuallySucceeded.size() + outcome.notProcessed.size());
            ShowAbortDialog(hwndOwner, logPath, (int)outcome.notProcessed.size(), total);
        }
    }

    // ---- Cleanup — pSink is manual because Unadvise must run before Release ----
    pfo->Unadvise(dwCookie);
    pSink->Release();
    // pfo and psiDest are released by their ComPtr destructors at scope end.

    return SUCCEEDED(hr) && !fAborted;
}

BOOL BrowseForFolder(HWND hwnd, std::wstring& folderOut) {
    // Guard: Explorer may call InvokeCommand once per selected item,
    // which would open multiple dialogs. Prevent re-entrant and rapid
    // sequential calls so only one dialog is shown.
    static volatile LONG s_dialogOpen = 0;
    static DWORD s_lastCloseTick = 0;

    if (InterlockedCompareExchange(&s_dialogOpen, 1, 0) != 0)
        return FALSE;

    DWORD now = GetTickCount();
    if ((now - s_lastCloseTick) < BROWSE_THROTTLE_MS) {
        InterlockedExchange(&s_dialogOpen, 0);
        return FALSE;
    }

    folderOut.clear();

    IFileDialog* pfd = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) {
        InterlockedExchange(&s_dialogOpen, 0);
        return FALSE;
    }

    DWORD dwOptions;
    pfd->GetOptions(&dwOptions);
    pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->SetTitle(GetStrings().BrowseTitle);

    hr = pfd->Show(hwnd);
    if (SUCCEEDED(hr)) {
        IShellItem* psi = NULL;
        hr = pfd->GetResult(&psi);
        if (SUCCEEDED(hr)) {
            PWSTR pszPath = NULL;
            hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
            if (SUCCEEDED(hr)) {
                folderOut = pszPath;
                CoTaskMemFree(pszPath);
            }
            psi->Release();
        }
    }

    pfd->Release();
    s_lastCloseTick = GetTickCount();
    InterlockedExchange(&s_dialogOpen, 0);
    return !folderOut.empty() ? TRUE : FALSE;
}

// --- Async file operations (background thread) ---

struct FileOpParams {
    HWND hwndOwner;
    UINT wFunc;
    std::vector<std::wstring> files;
    std::wstring destFolder;
    bool removeFromBasket;
};

static unsigned __stdcall FileOpThreadProc(void* pArg) {
    FileOpParams* params = (FileOpParams*)pArg;

    // Initialize COM for this thread (needed for shell operations)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    ExecuteFileOpCOM(params->hwndOwner, params->wFunc, params->files,
                     params->destFolder, params->removeFromBasket);

    CoUninitialize();
    delete params;

    // Pin this module for the rest of the thread. Dropping g_cRef to 0 below
    // lets DllCanUnloadNow return S_OK; without a pin, a CoFreeUnusedLibraries
    // firing in the window between the decrement and thread exit could unmap
    // this DLL while this very epilog still runs → access violation in Explorer.
    HMODULE hSelf = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       (LPCWSTR)&FileOpThreadProc, &hSelf);

    // Release the in-flight slot before the DLL refcount drop so a follow-up
    // operation triggered from the abort dialog (closed inside Execute...) is
    // never falsely rejected as "busy".
    InterlockedExchange(&g_fileOpInFlight, 0);
    InterlockedDecrement(&g_cRef);

    // Drop the pin and exit atomically without returning into (possibly
    // unmapped) DLL code. Trade-off: bypasses the CRT's _endthreadex teardown,
    // leaking a few bytes of CRT TLS per op — acceptable vs. an unmap crash.
    if (hSelf) FreeLibraryAndExitThread(hSelf, 0);
    return 0;  // reached only if the (near-infallible) module pin failed
}

static void LaunchFileOp(HWND hwndOwner, UINT wFunc, const std::vector<std::wstring>& files,
                         const std::wstring& destFolder, bool removeFromBasket) {
    if (files.empty() || destFolder.empty()) return;

    // Atomic claim of the single-op-in-flight slot. HandleFileOp already
    // checks IsBusy() up front for a friendly message — this is the
    // race-tight defense if two right-clicks slip through that window.
    if (InterlockedCompareExchange(&g_fileOpInFlight, 1, 0) != 0) return;

    FileOpParams* params = new FileOpParams();
    params->hwndOwner = hwndOwner;
    params->wFunc = wFunc;
    params->files = files;
    params->destFolder = destFolder;
    params->removeFromBasket = removeFromBasket;

    InterlockedIncrement(&g_cRef);
    unsigned threadId;
    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, FileOpThreadProc, params, 0, &threadId);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        // Thread creation failed — clean up, including the slot we just claimed
        delete params;
        InterlockedDecrement(&g_cRef);
        InterlockedExchange(&g_fileOpInFlight, 0);
    }
}

void CopyFilesToFolderAsync(const std::vector<std::wstring>& files,
                            const std::wstring& destFolder, bool removeFromBasket,
                            HWND hwndOwner) {
    LaunchFileOp(hwndOwner, FO_COPY, files, destFolder, removeFromBasket);
}

void MoveFilesToFolderAsync(const std::vector<std::wstring>& files,
                            const std::wstring& destFolder, bool removeFromBasket,
                            HWND hwndOwner) {
    LaunchFileOp(hwndOwner, FO_MOVE, files, destFolder, removeFromBasket);
}

} // namespace FileOps
