#pragma once

#include <string>
#include <vector>

namespace BasketStore {
    // Registry key used by all settings/state stores. Single source of truth.
    extern const wchar_t* const REG_KEY;

    std::wstring GetBasketDirPath();
    void AddFiles(const std::vector<std::wstring>& files);
    std::vector<std::wstring> ReadBasket();
    void WriteBasket(const std::vector<std::wstring>& files);
    void ClearBasket();
    int GetFileCount();
}
