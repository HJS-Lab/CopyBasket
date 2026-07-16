;--------------------------------
; CopyBasket - NSIS Installer Script
; Modern NSIS 3.x Unicode build
;--------------------------------

Unicode True

!include "MUI2.nsh"
!include "x64.nsh"

;--------------------------------
; Version from source

!searchparse /file "..\CopyBasket\Version.h" '#define COPYBASKET_VERSION_MAJOR ' VER_MAJOR
!searchparse /file "..\CopyBasket\Version.h" '#define COPYBASKET_VERSION_MINOR ' VER_MINOR
!searchparse /file "..\CopyBasket\Version.h" '#define COPYBASKET_VERSION_PATCH ' VER_PATCH

;--------------------------------
; General

!define PRODUCT_NAME "CopyBasket"
!define PRODUCT_PUBLISHER "HJS"
!define PRODUCT_WEB_SITE "https://github.com/HJS-cpu/CopyBasket"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

SetCompressor lzma
RequestExecutionLevel admin

; Name and file
Name "${PRODUCT_NAME} v${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}"
OutFile "..\build\CopyBasket-${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}-setup.exe"

; Default installation folder
InstallDir "$PROGRAMFILES\CopyBasket"

; Get installation folder from registry if available
InstallDirRegKey HKLM "Software\${PRODUCT_NAME}" "InstallDir"

;--------------------------------
; Interface Settings

!define MUI_ABORTWARNING
!define MUI_ICON "..\Res\basket.ico"
!define MUI_UNICON "..\Res\basket.ico"

;--------------------------------
; Pages

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "German"

;--------------------------------
; Localized messages (must follow MUI_LANGUAGE so the languages exist)

LangString MSG_REG_FAILED ${LANG_ENGLISH} "CopyBasket could not be registered (regsvr32 error code $0).$\nSetup will abort."
LangString MSG_REG_FAILED ${LANG_GERMAN} "CopyBasket konnte nicht registriert werden (regsvr32-Fehlercode $0).$\nDie Installation wird abgebrochen."

;--------------------------------
; Installer Initialization

Function .onInit
  ; Force the 64-bit Program Files default on x64 — but only when INSTDIR is
  ; still the compiled-in 32-bit default, so a custom path restored by
  ; InstallDirRegKey from a prior install is preserved.
  ${If} ${RunningX64}
    StrCmp $INSTDIR "$PROGRAMFILES\CopyBasket" 0 +2
      StrCpy $INSTDIR "$PROGRAMFILES64\CopyBasket"
  ${EndIf}
FunctionEnd

;--------------------------------
; Installer Section

Section "CopyBasket" SecCore

  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Unregister previous version (if any, /s suppresses errors)
  IfFileExists "$INSTDIR\CopyBasket.dll" 0 skip_unreg
    ${If} ${RunningX64}
      ${DisableX64FSRedirection}
      ExecWait '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\CopyBasket.dll"'
      ${EnableX64FSRedirection}
    ${Else}
      ExecWait '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\CopyBasket.dll"'
    ${EndIf}
  skip_unreg:

  ; Install architecture-appropriate DLL. If a previous CopyBasket.dll is still
  ; loaded by a running Explorer the file is locked; renaming it aside works on
  ; a loaded module and frees the name for the new file. The stale copy is
  ; deleted now if possible, otherwise on the next reboot (/REBOOTOK).
  Delete /REBOOTOK "$INSTDIR\CopyBasket.dll.old"
  Rename /REBOOTOK "$INSTDIR\CopyBasket.dll" "$INSTDIR\CopyBasket.dll.old"
  ClearErrors
  ${If} ${RunningX64}
    File /oname=CopyBasket.dll "..\x64\Release\CopyBasket.dll"
  ${Else}
    File /oname=CopyBasket.dll "..\Release\CopyBasket.dll"
  ${EndIf}
  Delete /REBOOTOK "$INSTDIR\CopyBasket.dll.old"

  ; Register shell extension and verify it succeeded — a silent failure would
  ; leave the user with no context menu and no indication why.
  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
    ExecWait '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\CopyBasket.dll"' $0
    ${EnableX64FSRedirection}
  ${Else}
    ExecWait '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\CopyBasket.dll"' $0
  ${EndIf}
  ${If} $0 <> 0
    MessageBox MB_OK|MB_ICONSTOP "$(MSG_REG_FAILED)"
    Abort
  ${EndIf}

  ; Notify shell of changes
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0x0000, p 0, p 0)'

  ; Icon
  File "..\Res\basket.ico"

  ; Store installation folder
  WriteRegStr HKLM "Software\${PRODUCT_NAME}" "InstallDir" $INSTDIR

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Add/Remove Programs entry
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME} v${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\basket.ico"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoRepair" 1

  ; Estimate installed size (in KB)
  SectionGetSize ${SecCore} $0
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "EstimatedSize" $0

SectionEnd

;--------------------------------
; Uninstaller Section

Section "Uninstall"

  ; Unregister shell extension
  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
    ExecWait '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\CopyBasket.dll"'
    ${EnableX64FSRedirection}
  ${Else}
    ExecWait '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\CopyBasket.dll"'
  ${EndIf}

  ; Notify shell of changes
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0x0000, p 0, p 0)'

  ; Remove files. /REBOOTOK: if the DLL is still loaded by a running Explorer
  ; it's locked; schedule its removal for the next reboot instead of failing.
  Delete /REBOOTOK "$INSTDIR\CopyBasket.dll"
  Delete /REBOOTOK "$INSTDIR\CopyBasket.dll.old"
  Delete "$INSTDIR\basket.ico"
  Delete "$INSTDIR\Uninstall.exe"

  ; Remove user data of the account running this (elevated) uninstaller. On a
  ; single-admin machine that's the user's own profile; under over-the-shoulder
  ; elevation by a different admin it targets that admin's profile, so the real
  ; user's data may remain. A correct per-user cleanup isn't possible from an
  ; elevated context without the interactive user's token — accepted trade-off.
  RMDir /r "$APPDATA\CopyBasket"

  ; Remove registry keys
  DeleteRegKey HKCU "Software\${PRODUCT_NAME}"
  DeleteRegKey HKLM "Software\${PRODUCT_NAME}"
  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"

  ; Remove installation directory (only if empty)
  RMDir "$INSTDIR"

SectionEnd
