#include <windows.h>
#include <shlobj.h>
#include "BasketStore.h"

namespace BasketStore {

const wchar_t* const REG_KEY = L"Software\\CopyBasket";

// Named mutex serializes all basket-file access. Scope is the logon session
// (Local\ prefix); %APPDATA% is per-user, so this covers every code path
// that can touch basket.txt — UI thread, background FileOp thread, and any
// other Explorer process loading the same DLL.
static const wchar_t* const BASKET_MUTEX_NAME = L"Local\\CopyBasket.basket.v1";

class BasketLock {
    HANDLE m_h;
    bool m_owned;
public:
    BasketLock() : m_h(CreateMutexW(NULL, FALSE, BASKET_MUTEX_NAME)), m_owned(false) {
        if (m_h) {
            DWORD w = WaitForSingleObject(m_h, INFINITE);
            // WAIT_OBJECT_0 and WAIT_ABANDONED both leave us owning the mutex
            // (ABANDONED = a prior holder crashed; the basket file is still
            // consistent because every write is an atomic temp+rename). Only
            // then may the destructor release it. If CreateMutexW itself failed
            // (m_h == NULL, e.g. resource exhaustion) we proceed lock-less —
            // best effort; the atomic write still rules out file corruption.
            m_owned = (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED);
        }
    }
    ~BasketLock() {
        if (m_h) {
            if (m_owned) ReleaseMutex(m_h);
            CloseHandle(m_h);
        }
    }
    BasketLock(const BasketLock&) = delete;
    BasketLock& operator=(const BasketLock&) = delete;
};

std::wstring ExtractFileName(const std::wstring& fullPath) {
    size_t pos = fullPath.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? fullPath.substr(pos + 1) : fullPath;
}

std::wstring GetBasketDirPath() {
    WCHAR szAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szAppData))) {
        std::wstring path(szAppData);
        path += L"\\CopyBasket";
        CreateDirectoryW(path.c_str(), NULL);
        path += L"\\";
        return path;
    }
    return L"";
}

static std::wstring GetBasketFilePath() {
    std::wstring dir = GetBasketDirPath();
    if (dir.empty()) return L"";
    return dir + L"basket.txt";
}

// Read the basket file without taking the lock (callers that need locking
// must hold BasketLock themselves; ReadBasket below wraps this for the
// public API).
static std::vector<std::wstring> ReadBasketUnlocked() {
    std::vector<std::wstring> result;
    std::wstring path = GetBasketFilePath();
    if (path.empty()) return result;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return result;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize <= 2) {
        CloseHandle(hFile);
        return result;
    }

    std::vector<BYTE> buf(fileSize);
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, buf.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    if (!ok) return result;

    // Skip UTF-16LE BOM if present
    WCHAR* start = (WCHAR*)buf.data();
    DWORD charCount = bytesRead / sizeof(WCHAR);
    if (charCount > 0 && start[0] == 0xFEFF) {
        start++;
        charCount--;
    }

    std::wstring current;
    for (DWORD i = 0; i < charCount; i++) {
        if (start[i] == L'\r') continue;
        if (start[i] == L'\n') {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current += start[i];
        }
    }
    if (!current.empty()) result.push_back(current);
    return result;
}

// Write all entries to basket.txt via temp-file + atomic rename. Holds the
// lock for the whole operation; on any I/O failure the existing file is
// left untouched. The unlocked variant is used by AddFiles to avoid
// re-entering the same mutex.
static bool WriteBasketUnlocked(const std::vector<std::wstring>& files) {
    std::wstring path = GetBasketFilePath();
    if (path.empty()) return false;

    if (files.empty()) {
        // Empty basket: delete the file (matches old ClearBasket behavior).
        DeleteFileW(path.c_str());
        return true;
    }

    std::wstring tempPath = path + L".tmp";
    HANDLE hFile = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    auto writeAll = [&](const void* data, DWORD cb) -> bool {
        DWORD written = 0;
        return WriteFile(hFile, data, cb, &written, NULL) && written == cb;
    };

    bool ok = true;
    BYTE bom[2] = { 0xFF, 0xFE };
    ok = ok && writeAll(bom, 2);

    for (const auto& file : files) {
        if (!ok) break;
        std::wstring line = file + L"\r\n";
        ok = writeAll(line.c_str(), (DWORD)(line.size() * sizeof(WCHAR)));
    }

    // Flush the temp file's data before the metadata-only rename commits, so a
    // power/system crash can't leave basket.txt pointing at unwritten blocks.
    if (ok) FlushFileBuffers(hFile);
    CloseHandle(hFile);

    if (!ok) {
        DeleteFileW(tempPath.c_str());
        return false;
    }

    // Atomic replace: existing basket.txt is replaced in one filesystem op.
    if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return true;
}

void AddFiles(const std::vector<std::wstring>& files) {
    if (files.empty()) return;
    BasketLock lock;

    std::vector<std::wstring> combined = ReadBasketUnlocked();
    for (const auto& file : files) {
        bool dup = false;
        for (const auto& e : combined) {
            if (_wcsicmp(e.c_str(), file.c_str()) == 0) { dup = true; break; }
        }
        if (!dup) combined.push_back(file);
    }
    WriteBasketUnlocked(combined);
}

std::vector<std::wstring> ReadBasket() {
    BasketLock lock;
    return ReadBasketUnlocked();
}

void WriteBasket(const std::vector<std::wstring>& files) {
    BasketLock lock;
    WriteBasketUnlocked(files);
}

void RemoveFiles(const std::vector<std::wstring>& files) {
    if (files.empty()) return;
    BasketLock lock;

    std::vector<std::wstring> basket = ReadBasketUnlocked();
    for (const auto& target : files) {
        for (auto it = basket.begin(); it != basket.end(); ++it) {
            if (_wcsicmp(it->c_str(), target.c_str()) == 0) {
                basket.erase(it);
                break;
            }
        }
    }
    WriteBasketUnlocked(basket);
}

void ClearBasket() {
    BasketLock lock;
    std::wstring path = GetBasketFilePath();
    if (!path.empty()) DeleteFileW(path.c_str());
}

int GetFileCount() {
    return (int)ReadBasket().size();
}

} // namespace BasketStore
