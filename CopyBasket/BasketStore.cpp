#include <windows.h>
#include <shlobj.h>
#include "BasketStore.h"

namespace BasketStore {

const wchar_t* const REG_KEY = L"Software\\CopyBasket";

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

void AddFiles(const std::vector<std::wstring>& files) {
    std::wstring path = GetBasketFilePath();
    if (path.empty()) return;

    // Read existing entries for duplicate check
    std::vector<std::wstring> existing = ReadBasket();

    HANDLE hFile = CreateFileW(path.c_str(), FILE_APPEND_DATA, 0, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    // If file is new/empty, write UTF-16LE BOM
    if (GetFileSize(hFile, NULL) == 0) {
        BYTE bom[2] = { 0xFF, 0xFE };
        DWORD written;
        WriteFile(hFile, bom, 2, &written, NULL);
    }

    for (const auto& file : files) {
        // Duplicate check (case-insensitive)
        bool found = false;
        for (const auto& e : existing) {
            if (_wcsicmp(e.c_str(), file.c_str()) == 0) {
                found = true;
                break;
            }
        }
        if (found) continue;

        std::wstring line = file + L"\r\n";
        DWORD written;
        WriteFile(hFile, line.c_str(), (DWORD)(line.size() * sizeof(WCHAR)), &written, NULL);
        existing.push_back(file);
    }

    CloseHandle(hFile);
}

std::vector<std::wstring> ReadBasket() {
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
    DWORD bytesRead;
    ReadFile(hFile, buf.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    // Skip UTF-16LE BOM if present
    WCHAR* start = (WCHAR*)buf.data();
    DWORD charCount = bytesRead / sizeof(WCHAR);
    if (charCount > 0 && start[0] == 0xFEFF) {
        start++;
        charCount--;
    }

    // Parse lines
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
    if (!current.empty()) {
        result.push_back(current);
    }

    return result;
}

void WriteBasket(const std::vector<std::wstring>& files) {
    if (files.empty()) {
        ClearBasket();
        return;
    }

    std::wstring path = GetBasketFilePath();
    if (path.empty()) return;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    // Write UTF-16LE BOM
    BYTE bom[2] = { 0xFF, 0xFE };
    DWORD written;
    WriteFile(hFile, bom, 2, &written, NULL);

    for (const auto& file : files) {
        std::wstring line = file + L"\r\n";
        WriteFile(hFile, line.c_str(), (DWORD)(line.size() * sizeof(WCHAR)), &written, NULL);
    }

    CloseHandle(hFile);
}

void ClearBasket() {
    std::wstring path = GetBasketFilePath();
    if (!path.empty()) {
        DeleteFileW(path.c_str());
    }
}

int GetFileCount() {
    return (int)ReadBasket().size();
}

} // namespace BasketStore
