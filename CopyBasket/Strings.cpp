#include "Strings.h"

static const StringTable s_DE = {
    // Context menu items
    L"Zum Korb hinzuf\u00FCgen",
    L"Korb anzeigen (%d Dateien)",
    L"Pfad kopieren",
    L"Korb hierher kopieren",
    L"Kopieren nach...",
    L"Korb hierher verschieben",
    L"Verschieben nach...",
    L"Korb leeren",
    L"Einstellungen",

    // Help texts (wide)
    L"Ausgew\u00E4hlte Dateien zum Korb hinzuf\u00FCgen",
    L"Inhalt des Korbs anzeigen",
    L"Pfad der Auswahl in die Zwischenablage kopieren",
    L"Dateien aus dem Korb hierher kopieren",
    L"Dateien aus dem Korb in einen Ordner kopieren",
    L"Dateien aus dem Korb hierher verschieben",
    L"Dateien aus dem Korb in einen Ordner verschieben",
    L"Korb leeren",
    L"Einstellungen \u00F6ffnen",

    // Help texts (ANSI)
    "Add selected files to basket",
    "Show basket contents",
    "Copy selected path to clipboard",
    "Copy basket files here",
    "Copy basket files to a folder",
    "Move basket files here",
    "Move basket files to a folder",
    "Clear basket",
    "Open settings",

    // Basket dialog
    L"CopyBasket - Korb Inhalt",
    L"Dateiname",
    L"Typ",
    L"Pfad",
    L"Datei",
    L"Verzeichnis",
    L"Entfernen",
    L"Schlie\u00DFen",
    L"%d Dateien",

    // File operations
    L"Zielordner w\u00E4hlen",

    // Settings dialog
    L"Einstellungen",
    L"Sprache:",
    L"OK",
    L"Abbrechen",
    L"Sprache",
    L"\u00DCber",
    L"Deutsch",
    L"English",
    L"\u00A9 2026 HJS (Hans-Joachim Schlingensief)",

    // Abort notification dialog
    L"Vorgang abgebrochen",
    L"%d von %d Dateien wurden nicht verarbeitet.\nDetails wurden protokolliert.",
    L"Log \u00F6ffnen",
    L"Schlie\u00DFen",

    // Operation log
    L"KOPIEREN",
    L"VERSCHIEBEN",
    L"Ziel:",
    L"ABGEBROCHEN",
    L"Erfolgreich:",
    L"Fehlgeschlagen:",
};

static const StringTable s_EN = {
    // Context menu items
    L"Add to basket",
    L"Show basket (%d files)",
    L"Copy path",
    L"Copy basket here",
    L"Copy to...",
    L"Move basket here",
    L"Move to...",
    L"Clear basket",
    L"Settings",

    // Help texts (wide)
    L"Add selected files to basket",
    L"Show basket contents",
    L"Copy selected path to clipboard",
    L"Copy basket files here",
    L"Copy basket files to a folder",
    L"Move basket files here",
    L"Move basket files to a folder",
    L"Clear basket",
    L"Open settings",

    // Help texts (ANSI)
    "Add selected files to basket",
    "Show basket contents",
    "Copy selected path to clipboard",
    "Copy basket files here",
    "Copy basket files to a folder",
    "Move basket files here",
    "Move basket files to a folder",
    "Clear basket",
    "Open settings",

    // Basket dialog
    L"CopyBasket - Basket Contents",
    L"Filename",
    L"Type",
    L"Path",
    L"File",
    L"Folder",
    L"Remove",
    L"Close",
    L"%d files",

    // File operations
    L"Choose destination folder",

    // Settings dialog
    L"Settings",
    L"Language:",
    L"OK",
    L"Cancel",
    L"Language",
    L"About",
    L"Deutsch",
    L"English",
    L"\u00A9 2026 HJS (Hans-Joachim Schlingensief)",

    // Abort notification dialog
    L"Operation cancelled",
    L"%d of %d files were not processed.\nDetails have been logged.",
    L"Open log",
    L"Close",

    // Operation log
    L"COPY",
    L"MOVE",
    L"Target:",
    L"CANCELLED",
    L"Succeeded:",
    L"Failed:",
};

static const StringTable* s_pActive = &s_DE;

const StringTable& GetStrings() {
    return *s_pActive;
}

void LoadLanguageSetting() {
    WCHAR szLang[16] = {};
    DWORD cbData = sizeof(szLang);
    LSTATUS res = RegGetValueW(HKEY_CURRENT_USER, L"Software\\CopyBasket",
                                L"Language", RRF_RT_REG_SZ, NULL, szLang, &cbData);
    if (res == ERROR_SUCCESS && _wcsicmp(szLang, L"en") == 0) {
        s_pActive = &s_EN;
    } else {
        s_pActive = &s_DE;
    }
}

void SaveLanguageSetting(const wchar_t* lang) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\CopyBasket", 0, NULL,
                         0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD cbData = (DWORD)((wcslen(lang) + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"Language", 0, REG_SZ, (const BYTE*)lang, cbData);
        RegCloseKey(hKey);
    }
    LoadLanguageSetting();
}
