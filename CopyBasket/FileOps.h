#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace FileOps {
    BOOL BrowseForFolder(HWND hwnd, std::wstring& folderOut);

    void CopyFilesToFolderAsync(const std::vector<std::wstring>& files,
                                const std::wstring& destFolder, bool removeFromBasket,
                                HWND hwndOwner);
    void MoveFilesToFolderAsync(const std::vector<std::wstring>& files,
                                const std::wstring& destFolder, bool removeFromBasket,
                                HWND hwndOwner);
}
