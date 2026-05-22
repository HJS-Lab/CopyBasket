#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace FileOps {
    BOOL BrowseForFolder(HWND hwnd, std::wstring& folderOut);

    // Returns TRUE while a previous Copy/MoveFilesToFolderAsync call is still
    // running on its background thread. Caller should reject new operations
    // with a user-visible message instead of letting two IFileOperation
    // instances race on the same source files / operations.log.
    BOOL IsBusy();

    void CopyFilesToFolderAsync(const std::vector<std::wstring>& files,
                                const std::wstring& destFolder, bool removeFromBasket,
                                HWND hwndOwner);
    void MoveFilesToFolderAsync(const std::vector<std::wstring>& files,
                                const std::wstring& destFolder, bool removeFromBasket,
                                HWND hwndOwner);
}
