# CopyBasket

Windows Explorer Shell Extension (COM DLL) zum Sammeln von Dateien in einen virtuellen "Korb" und anschließendem Kopieren/Verschieben an ein Zielverzeichnis.

## Build

Voraussetzung: Visual Studio 2022 Build Tools (Toolset v143).

```bash
# Release x64
MSYS_NO_PATHCONV=1 "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe" CopyBasket.sln /p:Configuration=Release /p:Platform=x64 /verbosity:minimal

# Release Win32
MSYS_NO_PATHCONV=1 "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe" CopyBasket.sln /p:Configuration=Release /p:Platform=Win32 /verbosity:minimal
```

Output: `x64\Release\CopyBasket.dll` bzw. `Release\CopyBasket.dll`

## Registrierung (als Admin)

```cmd
regsvr32 x64\Release\CopyBasket.dll
regsvr32 /u x64\Release\CopyBasket.dll
```

## Architektur

- **Sprache:** C++ mit Unicode durchgehend (WCHAR/std::wstring)
- **COM-Interfaces:** IShellExtInit + IContextMenu
- **CLSID:** {F2D8A4E6-3B7C-4D1E-9F5A-8C6E2A4B0D12} (GUID.h)
- **Korb-Speicher:** `%APPDATA%\CopyBasket\basket.txt` (UTF-16LE mit BOM)
- **Incident-Log:** `%APPDATA%\CopyBasket\operations.log` (UTF-16LE mit BOM, **ueberschrieben** pro Incident)
- **Dateioperationen:** IFileOperation + CFileOperationProgressSink (nicht-blockierend via Hintergrund-Thread, Korb-Entfernung gesammelt in FinishOperations). Bei Abbruch/Teilfehler wird via Pre-Scan/Post-Check ein Log geschrieben und TaskDialog angezeigt. `IFileOperation::SetOwnerWindow(lpcmi->hwnd)` und `TASKDIALOGCONFIG::hwndParent` werden gesetzt, damit Konflikt-/Abort-Dialoge nicht hinter dem Explorer verschwinden (relevant insbesondere fuer Cross-Volume-MOVE)
- **Thread-Sicherheit:** `g_cRef` als `volatile LONG` mit `InterlockedIncrement/Decrement`
- **Datei-Erkennung:** Primaer CF_HDROP, Fallback via `SHCreateShellItemArrayFromDataObject` (fuer Navigationsbereich, virtuelle Ordner etc.) mit `SFGAO_FOLDER`-Pruefung fuer korrekte Ordner-Ziel-Erkennung

## Quelldateien

| Datei | Inhalt |
|-------|--------|
| CopyBasket.cpp | DllMain, COM Boilerplate, DllRegisterServer/DllUnregisterServer |
| ShellExt.cpp | IShellExtInit::Initialize, IContextMenu (QueryContextMenu, InvokeCommand, GetCommandString) |
| BasketStore.h/.cpp | Korb-Datei lesen/schreiben/leeren, Duplikat-Pruefung. `GetBasketDirPath()` public (fuer Log-Pfad), `GetBasketFilePath()` intern/static |
| FileOps.h/.cpp | IFileOperation + CFileOperationProgressSink (async, Korb-Update gesammelt in FinishOperations), BrowseForFolder (IFileDialog, mit Re-Entrance-Guard), Incident-Log via Pre-Scan/Post-Check, TaskDialog mit "Log oeffnen". `HWND hwndOwner` wird via `FileOpParams` durch den Hintergrund-Thread gereicht und an `IFileOperation::SetOwnerWindow` + `TaskDialogIndirect` weitergegeben |
| BasketDialog.h/.cpp | ListView-Dialog mit TreeView-Detail-Panel, Splitter, System-Icons, Typ-Spalte, Entfernen-Funktion, Drag&Drop-Ziel (WM_DROPFILES). Shared `RefreshFromDisk(DlgData*)`-Helper fuer `ReadBasket + PopulateListView + UpdateStatusBar` |
| Strings.h/.cpp | i18n String-Tabellen (DE/EN), LoadLanguageSetting(), SaveLanguageSetting() |
| SettingsDialog.h/.cpp | Einstellungen-Dialog mit Tab Control (Sprache / \u00DCber mit Website-Link) |
| Version.h | Zentrale Versionskonstanten (COPYBASKET_VERSION_MAJOR/MINOR/PATCH/BUILD). `COPYBASKET_VERSION_STR` (wide) und `COPYBASKET_VERSION_STR_A` (narrow) werden via Preprocessor-Stringification aus den numerischen Macros abgeleitet — einzige Quelle der Wahrheit |
| CopyBasket.h | Klassendeklarationen (CShellExtClassFactory, CShellExt), CmdOffset Enum |
| GUID.h | CLSID_CopyBasket Definition |
| CopyBasket.def | DLL Exports (DllCanUnloadNow, DllGetClassObject, DllRegisterServer, DllUnregisterServer) |
| resource.h | Resource-IDs (IDI_BASKET, IDR_VERSION1) |
| CopyBasket.rc | Versioninfo-Resource (FILEVERSION/PRODUCTVERSION + FileVersion/ProductVersion-Strings alle aus `Version.h`-Macros abgeleitet) |
| res/basket.ico | Menu-Icon Resource |
| installer/CopyBasket.nsi | NSIS Installer-Skript (regsvr32, x64/x86 Erkennung) |
| portable/README.txt | Englische Portable-Dokumentation (Installation, Features, Changelog, Kontakt) — wird in `CopyBasket_vX.Y.Z.zip` gepackt |
| portable/Lies.mich.txt | Deutsche Portable-Dokumentation (synchron zur englischen Fassung) |
| .github/workflows/release.yml | GitHub Actions Build & Release Workflow |

## Registry-Punkte (3 Registrierungen)

- `HKCR\*\shellex\ContextMenuHandlers\CopyBasket` — Rechtsklick auf Dateien
- `HKCR\Directory\shellex\ContextMenuHandlers\CopyBasket` — Rechtsklick auf Ordner
- `HKCR\Directory\Background\shellex\ContextMenuHandlers\CopyBasket` — Rechtsklick auf Hintergrund

## Kontextmenu

Popup-Submenu "CopyBasket" mit:
- Zum Korb hinzufuegen (nur bei Datei/Ordner-Klick)
- Pfad kopieren (nur bei Datei/Ordner-Klick) — kopiert Pfad(e) in die Zwischenablage
- Korb anzeigen (X Dateien) — oeffnet ListView-Dialog mit Entfernen-Funktion
- ---Separator---
- Korb hierher kopieren — kopiert Korb-Inhalt in aktuellen Ordner (async)
- Kopieren nach... — Ordnerauswahl-Dialog, dann async kopieren
- Korb hierher verschieben — verschiebt Korb-Inhalt in aktuellen Ordner (async)
- Verschieben nach... — Ordnerauswahl-Dialog, dann async verschieben
- ---Separator---
- Korb leeren
- ---Separator---
- Einstellungen — oeffnet Tab-Dialog (Sprache / \u00DCber)

Items sind grayed wenn der Korb leer ist. "Zum Korb hinzufuegen" und "Pfad kopieren" sind bei Hintergrund-Klick ausgeblendet. "Einstellungen" ist immer aktiv. Funktioniert auch im Navigationsbereich (linke Seite) des Explorers.

"Kopieren nach..." und "Verschieben nach..." sind auch bei leerem Korb aktiv, wenn Dateien/Ordner selektiert sind — operieren dann direkt auf der Auswahl statt auf dem Korb.

Alle Dateioperationen (Kopieren/Verschieben) laufen nicht-blockierend auf einem Hintergrund-Thread (`_beginthreadex`) via `IFileOperation`. Erfolgreich verarbeitete Dateien werden via `CFileOperationProgressSink::PostCopyItem`/`PostMoveItem` in einer `m_processed`-Liste gesammelt. In `FinishOperations` wird der Korb **frisch von Platte gelesen**, die verarbeiteten Items abgezogen, und das Ergebnis zurueckgeschrieben — so bleiben waehrend der Operation hinzugefuegte Eintraege erhalten (Race-Condition-sicher). Bei Abbruch oder Fehler bleiben nur die noch nicht verarbeiteten Dateien im Korb. Bei Fallback auf selektierte Dateien (leerer Korb) wird der Korb nicht angefasst.

### Ordner-Ziel bei "hierher"-Befehlen

Rechtsklick auf einen einzelnen Ordner: `m_szFolder` wird in `Initialize()` auf den angeklickten Ordner gesetzt (nicht auf das uebergeordnete Verzeichnis). Bei Rechtsklick auf Dateien oder mehrere Elemente bleibt `m_szFolder` das aktuelle Verzeichnis (Elternordner). Die IShellItemArray-Fallback-Logik (Navigationsbereich, virtuelle Ordner etc.) spiegelt dieselbe Logik: Elternverzeichnis aus erstem Item extrahieren, nur bei einzelnem Ordner (`SFGAO_FOLDER`-Pruefung) auf den angeklickten Ordner ueberschreiben.

### BrowseForFolder Guard

"Kopieren nach..." und "Verschieben nach..." verwenden `BrowseForFolder()` mit Re-Entrance-Guard:
- **InterlockedCompareExchange** verhindert gleichzeitige Dialog-Oeffnungen (Re-Entrance waehrend modaler Messagepump)
- **Zeitfenster (500ms)** verhindert sequentielle Mehrfach-Oeffnungen (Explorer ruft InvokeCommand ggf. pro selektiertem Item auf)
- Erster Aufruf erhaelt alle Dateien via HDROP, nachfolgende werden unterdrueckt

### Menu-Icon

Das Hauptmenu-Item "CopyBasket" zeigt ein eigenes Icon:
- **Resource:** `Res\basket.ico` eingebunden als `IDI_BASKET` (resource.h), geladen via `LoadImageW(g_hModule, ...)`
- **Konvertierung:** Hilfsfunktion `IconToBitmap()` wandelt HICON in 32-Bit-ARGB-HBITMAP (fuer Transparenz)
- **Groesse:** Systemmetriken `SM_CXSMICON`/`SM_CYSMICON` fuer konsistente Darstellung
- **Zuweisung:** Via `MENUITEMINFOW` mit `MIIM_BITMAP`-Flag und `hbmpItem` in `QueryContextMenu`
- **Lebenszyklus:** Bitmap wird im Konstruktor erstellt (`m_hMenuBitmap`) und im Destruktor mit `DeleteObject` freigegeben

### Korb-Dialog

- **Nicht-modal:** Explorer bleibt bedienbar waehrend der Dialog offen ist
- **Auto-Close:** Dialog schliesst sich bei Fokusverlust (WM_ACTIVATE/WA_INACTIVE)
- **Fenster-Style:** Resizable, kein Minimize/Maximize (`WS_OVERLAPPEDWINDOW & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX)`)
- **Layout:** Obere Haelfte ListView, darunter Splitter, darunter TreeView (Detail-Panel), darunter Buttons + Statusbar. `LayoutControls()` verwendet `BeginDeferWindowPos`/`DeferWindowPos`/`EndDeferWindowPos` fuer atomares Repositionieren aller Child-Controls
- **Icon:** Basket-Icon in Titelleiste via `WM_SETICON` (ICON_SMALL + ICON_BIG, `LR_SHARED`)
- **Persistenz:** Fenstergroesse, Spaltenbreiten und Splitter-Position in Registry (`HKCU\Software\CopyBasket\DialogWidth/Height/ColWidth0/ColWidth1/ColWidth2/SplitRatio`)
- **ListView-Spalten:** 0=Dateiname, 1=Typ ("Datei"/"Verzeichnis" via `GetFileAttributesW`), 2=voller Pfad
- **System-Icons:** Shared Windows-Image-List via `SHGetFileInfoW(SHGFI_SYSICONINDEX | SHGFI_SMALLICON)` einmalig in WM_CREATE geholt, an ListView (`LVSIL_SMALL`) und TreeView (`TVSIL_NORMAL`) gehaengt. Pro Eintrag wird der Icon-Index gesetzt — Ordner, .txt, .exe usw. zeigen die echten Explorer-Icons. Keine Cleanup-Arbeiten (System-Image-List gehoert der Shell)
- **Spalten-Sortierung:** Klick auf Spaltenueberschrift sortiert alphabetisch (case-insensitive via `_wcsicmp`), erneuter Klick kehrt Richtung um. Sortier-Pfeil im Header via `HDF_SORTUP`/`HDF_SORTDOWN` (natives Windows-Theme). Implementiert mit `ListView_SortItemsEx` und `LVN_COLUMNCLICK`
- **Strg+A:** Selektiert alle Eintraege im ListView (via `LVN_KEYDOWN` + `ListView_SetItemState` mit Index -1)
- **Initialer Fokus:** ListView erhaelt beim Oeffnen sofort den Fokus (`SetFocus` nach `ShowWindow`), sodass Tastaturkuerzel (Strg+A, Entf) direkt nutzbar sind
- **Statusbar:** Native Windows-Statusbar (`STATUSCLASSNAMEW`) am unteren Fensterrand mit `SBARS_SIZEGRIP`. Zeigt Dateianzahl im Korb (z.B. "5 Dateien" / "5 files"). Aktualisiert sich automatisch beim Entfernen von Eintraegen. Layout wird in `LayoutControls()` beruecksichtigt (Statusbar-Hoehe von verfuegbarer Flaeche abgezogen)
- **Drag&Drop:** Dialog akzeptiert Datei-/Ordner-Drops aus dem Explorer via `DragAcceptFiles(hwnd, TRUE)` (einmalig in `WM_CREATE`). `WM_DROPFILES`-Handler extrahiert Pfade via `DragQueryFileW`, uebergibt sie an `BasketStore::AddFiles` (Duplikat-Pruefung dort) und ruft `RefreshFromDisk(dd)` fuer die UI-Aktualisierung. Zusammenspiel mit Auto-Close: waehrend eines Drags bleibt der Dialog offen, da der Drop-Target-Status nicht als WA_INACTIVE zaehlt

#### TreeView-Detail-Panel

- **Zweck:** Bei Auswahl eines Verzeichnis-Eintrags in der ListView zeigt die TreeView dessen kompletten Inhalt rekursiv (inkl. Unterverzeichnisse)
- **Read-Only:** TreeView ist reine Leseansicht. `TVN_SELCHANGINGW` wird abgefangen und gibt `TRUE` zurueck → keine Selektion moeglich. Remove-Button wirkt ausschliesslich auf ListView
- **Populate:** `AddTreeNodes()` rekursiv via `FindFirstFileW`/`FindNextFileW`, Verzeichnisse zuerst, dann Dateien (stabile Sortierung). Jeder Knoten bekommt Icon via `GetSysIconIndex()` (`SHGetFileInfoW`)
- **Update-Trigger:** `UpdateTreeForSelection()` wird bei `LVN_ITEMCHANGED` (LVIS_SELECTED/LVIS_FOCUSED), Remove und Delete-Key aufgerufen. Speichert letzten Pfad (`lastTreePath`) um redundantes Neupopulieren bei identischem Pfad zu vermeiden
- **Flicker-Schutz:** `SendMessage(hTree, WM_SETREDRAW, FALSE/TRUE, 0)` um das Populieren
- **Keine Auswahl → leer:** Bei keinem oder File-Eintrag zeigt die TreeView nichts

#### Splitter zwischen ListView und TreeView

- **Eigene Window-Klasse** `CopyBasketSplitter` (registriert in `Show()`, `UnregisterClassW` beim Ende) mit `IDC_SIZENS`-Cursor und `COLOR_BTNFACE`-Background
- **Drag-Logik in `SplitterWndProc`:**
  - `WM_LBUTTONDOWN` → `SetCapture(hwnd)`
  - `WM_MOUSEMOVE` mit aktiver Capture → konvertiert Splitter-local zu Parent-Client-Koordinaten, berechnet neue Ratio, ruft `LayoutControls()` auf Parent
  - `WM_LBUTTONUP` → `ReleaseCapture()`
- **Ratio-Clamping:** 10–90% mit `MIN_PANE_H = 60` Pixel Mindesthoehe pro Pane
- **Persistenz:** `splitRatioPct` (int, 10–90) wird in Registry als `SplitRatio` gespeichert

### Incident-Log bei Abbruch/Teilfehler

Bei Abbruch oder fehlgeschlagenen Dateioperationen (insbesondere beim Verschieben verschachtelter Verzeichnisse ueber Laufwerksgrenzen) wird ein Log geschrieben und ein Dialog gezeigt. Ziel: Der User sieht exakt, welche Dateien tatsaechlich verarbeitet wurden und welche nicht — auch fuer Dateien in Unterverzeichnissen.

- **Strategie:** Pre-Scan + Post-Check statt Verlassen auf IFileOperation-Callbacks
  - **Pre-Scan:** `BuildExpectedFiles()` + `EnumerateFilesRecursive()` enumeriert rekursiv alle Quelldateien mit berechnetem Ziel-Pfad (`struct ExpectedFile { sourcePath; destPath; }`)
  - **Operation:** `IFileOperation::PerformOperations()` wie gehabt
  - **Post-Check:** Nach `GetAnyOperationsAborted()` wird jede erwartete Datei via `GetFileAttributesW` geprueft — fuer MOVE gilt eine Datei als erfolgreich, wenn Quelle weg und Ziel existiert; fuer COPY wenn Ziel existiert
- **Trigger:** `fAborted || !notProcessed.empty()`
- **Owner-Window:** `lpcmi->hwnd` aus `InvokeCommand` wird via `FileOpParams::hwndOwner` durch den Hintergrund-Thread bis in `ExecuteFileOpCOM` gereicht. Dort:
  - `IFileOperation::SetOwnerWindow(hwndOwner)` — verankert Konflikt-/UAC-/Replace-Skip-Dialoge von Windows am Explorer-Fenster, damit sie nicht hinter Fenstern verschwinden (insb. bei Cross-Volume-MOVE mit Konflikten)
  - `TASKDIALOGCONFIG::hwndParent = hwndOwner` (mit `IsWindow()`-Check, Fallback auf `GetForegroundWindow()` falls Hwnd zwischenzeitlich invalid) plus `TDF_POSITION_RELATIVE_TO_WINDOW` — Abort-Dialog erscheint immer im Vordergrund relativ zum Explorer
- **Log-Datei:** `%APPDATA%\CopyBasket\operations.log` (UTF-16LE mit BOM, `CREATE_ALWAYS` → wird pro Incident ueberschrieben, keine History)
  - Inhalt: Zeitstempel, Operationstyp (KOPIEREN/VERSCHIEBEN), Zielordner, Liste "Erfolgreich:" + "Fehlgeschlagen:" + Status (ABGEBROCHEN falls zutreffend)
  - i18n Log-Strings via `StringTable`: `LogOpCopy`, `LogOpMove`, `LogTarget`, `LogAborted`, `LogSucceeded`, `LogFailed`
- **Abort-Dialog:** `TaskDialogIndirect` (Vista+, via `comctl32.lib`)
  - Inhalt: "%d von %d Dateien wurden nicht verarbeitet. Details wurden protokolliert."
  - Buttons: **"Log oeffnen"** (oeffnet Log-Datei via `ShellExecuteW`) und **"Schliessen"**
  - Default-Button: "Schliessen"
  - Icon: `TD_WARNING_ICON`
  - i18n Dialog-Strings via `StringTable`: `AbortTitle`, `AbortMsgFmt`, `AbortBtnOpenLog`, `AbortBtnClose`
- **CFileOperationProgressSink:** bleibt minimal — nur das urspruengliche Korb-Tracking. Das Logging passiert vollstaendig in `ExecuteFileOpCOM` per Filesystem-Check, damit es unabhaengig vom Callback-Verhalten zuverlaessig funktioniert

### Einstellungen-Dialog

- **Tab Control:** Zwei Registerkarten (Sprache / \u00DCber)
- **Sprache-Tab:** ComboBox mit Deutsch/English, speichert via `SaveLanguageSetting()`
- **\u00DCber-Tab:** Titel (bold, 3px groesser), klickbarer Website-Link (SysLink/WC_LINK), Copyright
- **Website-Link:** `hjs.page.gd/cb` — zentriert via `LM_GETIDEALSIZE`, oeffnet Browser via `ShellExecuteW`, schliesst Dialog nach Klick
- **Bold-Font:** Eigener `LOGFONT` mit `FW_BOLD` + vergroesserter Hoehe (+3px), Freigabe in `WM_DESTROY`

## i18n

- **Architektur:** `StringTable`-Struct mit `const wchar_t*`/`const char*`-Membern, zwei statische Instanzen (s_DE/s_EN), globaler Pointer auf aktive Tabelle
- **Zugriff:** `GetStrings().MemberName` — typ-sicher, Zero-Overhead
- **Spracheinstellung:** Registry `HKCU\Software\CopyBasket\Language` = `"de"` oder `"en"` (Default: Deutsch)
- **Laden:** `LoadLanguageSetting()` im CShellExt-Konstruktor
- **Aendern:** Einstellungen-Dialog → `SaveLanguageSetting()` → sofort wirksam beim naechsten Rechtsklick

## Installer

- **Technologie:** NSIS 3.x (Unicode), Modern UI 2
- **Skript:** `installer\CopyBasket.nsi`
- **Architektur-Erkennung:** Automatisch x64/x86 via `x64.nsh` (`${RunningX64}`)
- **Registrierung:** `regsvr32 /s` wird automatisch beim Installieren ausgefuehrt
- **Deregistrierung:** `regsvr32 /u /s` wird automatisch beim Deinstallieren ausgefuehrt
- **FS-Redirection:** `${DisableX64FSRedirection}` fuer korrekten 64-Bit regsvr32-Aufruf aus 32-Bit NSIS
- **Installationsverzeichnis:** `$PROGRAMFILES64\CopyBasket` (64-Bit) bzw. `$PROGRAMFILES\CopyBasket` (32-Bit)
- **Version:** Automatisch aus `Version.h` extrahiert via `!searchparse`
- **Inhalt:** CopyBasket.dll (architektur-passend), basket.ico, Uninstall.exe (kein CB-CMT.exe — nur in Portable-Version)
- **Deinstallation:** Entfernt Benutzereinstellungen (`HKCU\Software\CopyBasket`) und Benutzerdaten (`%APPDATA%\CopyBasket`)
- **Add/Remove Programs:** Eintrag unter `HKLM\...\Uninstall\CopyBasket`
- **Shell-Benachrichtigung:** `SHChangeNotify` nach (De-)Registrierung
- **Sprachen:** Deutsch + Englisch
- **Output:** `build\CopyBasket-X.Y.Z-setup.exe`
- **Build:** Nur via GitHub Actions (NSIS lokal nicht verfuegbar), getriggert durch Tag-Push (`git tag vX.Y.Z && git push origin vX.Y.Z`)
- **NSIS-Installation im Workflow:** `.github/workflows/release.yml` installiert NSIS via **scoop** (inkl. `scoop bucket add extras`, da NSIS im extras-Bucket liegt). chocolatey und direkte SourceForge-Downloads waren unzuverlaessig (Cloudflare-Challenge, 503/504), scoop hat eigene Infrastruktur und ist auf GH Actions stabil

### Release-Assets (GitHub)

- `CopyBasket_vX.Y.Z.zip` — Portable (DLLs + CB-CMT.exe fuer manuelle Registrierung, inkl. `README.txt` und `Lies.mich.txt`)
- `CopyBasket-X.Y.Z-setup.exe` — NSIS Installer (regsvr32 automatisch, kein CB-CMT.exe)

### Portable-Dokumentation

- **Quellordner:** `portable\README.txt` (Englisch) + `portable\Lies.mich.txt` (Deutsch)
- **Inhalt:** Installation via CB-CMT.exe, Feature-Overview, komplette Changelog-History, Kontakt-/Link-Block, Lizenz
- **Einbindung:** `.github\workflows\release.yml` kopiert beide Dateien im Schritt "Prepare release assets" nach `_release\` und packt sie in die Portable-ZIP
- **Versions-Header:** kein Versionsstring im Header (steht schon im ZIP-Dateinamen) — pro Release muss nur der Changelog-Abschnitt nachgefuehrt werden, kein Header-Drift
- **Keine Umlaut-Escapes:** Die `.txt`-Dateien nutzen echte UTF-8-Umlaute (kein `\u00XX`-Escaping wie im C++-Code)

### CB-CMT.exe (CopyBasket Context Menu Tool)

- **Zweck:** GUI-Tool zur manuellen Aktivierung/Deaktivierung der Shell Extension (nur Portable-Version)
- **Quellcode:** `regsvr Tool\CopyBasketContextMenu\`
- **Funktionen:**
  - Radio-Buttons: Activate / Deactivate (Statuserkennung via Registry, Vorauswahl)
  - Checkbox: "Delete User Settings" — loescht `HKCU\Software\CopyBasket` und `%APPDATA%\CopyBasket\` (ausgegraut wenn kein Schluessel vorhanden)
  - Warnhinweis bei Checkbox-Aktivierung, Sicherheitsabfrage vor Loeschung
  - DLL-Existenzpruefung vor regsvr32-Aufruf
- **Build:** `MSYS_NO_PATHCONV=1 "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe" "regsvr Tool/CopyBasketContextMenu.sln" /p:Configuration=Release /p:Platform=x64 /verbosity:minimal`
- **Output:** `regsvr Tool\bin\Release\CB-CMT.exe`

## Konventionen

- Kein Precompiled Header
- ASCII-sichere Bezeichner im Code, Umlaute via Unicode-Escapes (z.B. `\u00FC` fuer ue)
- `g_cRef` immer mit `InterlockedIncrement`/`InterlockedDecrement` zugreifen (Data-Race-Schutz wegen Hintergrund-Threads)
- Gemeinsame Logik in Helper-Funktionen extrahieren (z.B. `ExecuteFileOpCOM` fuer IFileOperation)
- Referenzprojekt: `C:\Users\HJS\Claude.ai\wscitecm\` (SciTE Context Menu Extension)

## GitHub

Repository: https://github.com/HJS-cpu/CopyBasket
