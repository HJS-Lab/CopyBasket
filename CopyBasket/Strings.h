#pragma once

#include <windows.h>

struct StringTable {
    // Context menu items
    const wchar_t* MenuAddToBasket;
    const wchar_t* MenuShowBasketFmt;
    const wchar_t* MenuCopyPath;
    const wchar_t* MenuCopyHere;
    const wchar_t* MenuCopyTo;
    const wchar_t* MenuMoveHere;
    const wchar_t* MenuMoveTo;
    const wchar_t* MenuClearBasket;
    const wchar_t* MenuSettings;

    // Help texts (wide)
    const wchar_t* HelpAdd;
    const wchar_t* HelpShow;
    const wchar_t* HelpCopyPath;
    const wchar_t* HelpCopyHere;
    const wchar_t* HelpCopyTo;
    const wchar_t* HelpMoveHere;
    const wchar_t* HelpMoveTo;
    const wchar_t* HelpClear;
    const wchar_t* HelpSettings;

    // Help texts (ANSI)
    const char* HelpAddA;
    const char* HelpShowA;
    const char* HelpCopyPathA;
    const char* HelpCopyHereA;
    const char* HelpCopyToA;
    const char* HelpMoveHereA;
    const char* HelpMoveToA;
    const char* HelpClearA;
    const char* HelpSettingsA;

    // Basket dialog
    const wchar_t* DlgBasketTitle;
    const wchar_t* DlgColFilename;
    const wchar_t* DlgColType;
    const wchar_t* DlgColPath;
    const wchar_t* DlgTypeFile;
    const wchar_t* DlgTypeFolder;
    const wchar_t* DlgBtnRemove;
    const wchar_t* DlgBtnClose;
    const wchar_t* DlgStatusFiles;

    // File operations
    const wchar_t* BrowseTitle;

    // Settings dialog
    const wchar_t* SettingsTitle;
    const wchar_t* SettingsLanguageLabel;
    const wchar_t* SettingsOK;
    const wchar_t* SettingsCancel;
    const wchar_t* SettingsTabLanguage;
    const wchar_t* SettingsTabAbout;
    const wchar_t* SettingsLangNameDe;   // endonym: shown identically in both UIs
    const wchar_t* SettingsLangNameEn;
    const wchar_t* SettingsCopyright;

    // Abort notification dialog
    const wchar_t* AbortTitle;
    const wchar_t* AbortMsgFmt;
    const wchar_t* AbortBtnOpenLog;
    const wchar_t* AbortBtnClose;

    // Busy notification (another file op is still in flight)
    const wchar_t* BusyTitle;
    const wchar_t* BusyMsg;

    // Operation log
    const wchar_t* LogOpCopy;
    const wchar_t* LogOpMove;
    const wchar_t* LogTarget;
    const wchar_t* LogAborted;
    const wchar_t* LogSucceeded;
    const wchar_t* LogFailed;
};

const StringTable& GetStrings();
void LoadLanguageSetting();
void SaveLanguageSetting(const wchar_t* lang);
