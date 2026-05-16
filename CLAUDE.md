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
- **Korb-Speicher:** `%APPDATA%\CopyBasket\basket.txt` (UTF-16LE mit BOM); alle Zugriffe (`AddFiles`/`ReadBasket`/`WriteBasket`/`RemoveFiles`/`ClearBasket`) serialisiert ueber session-scoped Named Mutex `Local\CopyBasket.basket.v1` (RAII via `BasketLock`). `WriteBasket` schreibt in `basket.txt.tmp` + `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` — atomar, kein Half-Write bei parallelen Operationen aus mehreren Explorer-Prozessen
- **Incident-Log:** `%APPDATA%\CopyBasket\operations.log` (UTF-16LE mit BOM, **ueberschrieben** pro Incident)
- **Dateioperationen:** `IFileOperation` async auf Hintergrund-Thread; Incident-Log + TaskDialog bei Abbruch/Teilfehler; alle Dialoge am Explorer-Hwnd verankert
- **Thread-Sicherheit:** `g_cRef` als `volatile LONG` mit `InterlockedIncrement/Decrement`; `BasketStore` via Named Mutex (siehe Korb-Speicher) — UI-Thread, Background-FileOp-Thread und parallele Explorer-Prozesse koennen sich nicht mehr in die Quere kommen
- **Datei-Erkennung:** CF_HDROP primaer, Fallback via `SHCreateShellItemArrayFromDataObject` (Navigationsbereich, virtuelle Ordner) mit `SFGAO_FOLDER`-Pruefung. `m_szFolder`-Ableitung (Parent-Verzeichnis bzw. Single-Folder-Override) zentral in `CShellExt::ResolveClickTarget(firstPath, isFolder, isSingleItem)` — beide Pfade rufen denselben Helper

## Quelldateien

| Datei | Inhalt |
|-------|--------|
| CopyBasket.cpp | DllMain, COM Boilerplate, DllRegisterServer/DllUnregisterServer. `SHELLEX_KEYS[]`-Tabelle (drei `ContextMenuHandlers`-Subkeys) wird sowohl von `RegisterServer` als auch von `UnregisterServer` iteriert — kein Drift moeglich beim Hinzufuegen neuer Shell-Locations |
| ShellExt.cpp | IShellExtInit::Initialize, IContextMenu (QueryContextMenu, InvokeCommand, GetCommandString). Die vier File-Op-Commands (CMD_COPY_HERE/CMD_COPY_TO/CMD_MOVE_HERE/CMD_MOVE_TO) sind in InvokeCommand Einzeiler — gemeinsame Logik in `CShellExt::HandleFileOp(lpcmi, isCopy, toPicker)`. `ResolveClickTarget(firstPath, isFolder, isSingleItem)` zentralisiert die `m_szFolder`-Ableitung fuer HDROP- und IShellItemArray-Pfad. `GetCommandString` nutzt `LookupHelp(idCmd)`-Tabelle (eine Stelle fuer wide- und ANSI-Help-Strings statt zwei parallele Switches). `Microsoft::WRL::ComPtr` durchgaengig fuer `IShellItemArray`/`IShellItem` lokal und `m_pDataObj` als Member (`ComPtr<IDataObject>`) — keine manuellen `Release`-Aufrufe |
| BasketStore.h/.cpp | Korb-Datei lesen/schreiben/leeren, Duplikat-Pruefung. `GetBasketDirPath()` public (fuer Log-Pfad), `GetBasketFilePath()` intern/static. Alle oeffentlichen Funktionen halten den RAII-`BasketLock` (Named Mutex `Local\CopyBasket.basket.v1`); interne `*Unlocked`-Varianten erlauben Komposition ohne Re-Entrance. `WriteBasket` schreibt via `basket.txt.tmp` + `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` — atomar, alle `WriteFile`-Returns geprueft, bei Fehler bleibt die existierende Datei unberuehrt. `AddFiles` ist Read-Modify-Write ueber `WriteBasket` (kein duplizierter BOM/Line-Write-Code). `RemoveFiles(paths)` macht atomares Read-Modify-Write unter einem einzigen Lock (vom Sink genutzt, damit waehrend einer Operation hinzugefuegte Eintraege nicht verloren gehen). **Single Source of Truth** fuer Registry-Pfad (`extern const wchar_t* const REG_KEY = L"Software\\CopyBasket"`) und Pfad-Tail-Extraktion (`ExtractFileName(path)` via `find_last_of(L"\\/")`) — beide werden von BasketDialog, SettingsDialog, Strings und FileOps verwendet, nicht lokal duplizieren |
| FileOps.h/.cpp | IFileOperation + CFileOperationProgressSink (async, Korb-Update gesammelt in FinishOperations), BrowseForFolder (IFileDialog, mit Re-Entrance-Guard), Incident-Log via Pre-Scan/Post-Check, TaskDialog mit "Log oeffnen". `HWND hwndOwner` wird via `FileOpParams` durch den Hintergrund-Thread gereicht und an `IFileOperation::SetOwnerWindow` + `TaskDialogIndirect` weitergegeben. `ExecuteFileOpCOM` ist in drei Phasen organisiert: (1) COM-Setup + `SchedulePerFileOps`, (2) Pre-Scan + `PerformOperations`, (3) Filesystem-Post-Check via `ClassifyOutcome` + ggf. Incident-Log. `Microsoft::WRL::ComPtr` (`<wrl/client.h>`) verwaltet `IFileOperation`/`IShellItem`; nur der Sink bleibt manuell wegen `Unadvise`-vor-`Release`-Anforderung |
| BasketDialog.h/.cpp | ListView-Dialog mit TreeView-Detail-Panel, Splitter, System-Icons, Typ-Spalte, Entfernen-Funktion, Drag&Drop-Ziel (WM_DROPFILES). Shared `RefreshFromDisk(DlgData*)`-Helper fuer `ReadBasket + PopulateListView + UpdateStatusBar`. `DlgWndProc` ist reiner Dispatcher; per-Message-Handler in `OnCreate`/`OnDropFiles`/`OnCommand`/`OnNotify` (triviale Cases wie `WM_SIZE`/`WM_DESTROY` bleiben inline). Layout-Math zentralisiert in `ComputeLayoutMetrics()` (Single Source of Truth fuer Splitter + LayoutControls). `GetSysIconIndex(path, attrHint)` mit Fallback auf `SHGFI_USEFILEATTRIBUTES` fuer nicht mehr existierende Dateien. TreeView nutzt **Lazy Loading**: `InsertTreeNode(hParent, name, fullPath, icon, isDirectory, isReparsePoint)` legt Knoten mit `cChildren=1` und Heap-Pfad in `lParam` an, `PopulateChildren()` laedt genau eine Ebene, `TVN_ITEMEXPANDINGW`-Handler triggert das Nachladen tieferer Ebenen (siehe TreeView-Detail-Panel-Sektion) |
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

Erfolgreich verarbeitete Dateien werden im Sink (`PostCopyItem`/`PostMoveItem`) gesammelt; `FinishOperations` ruft `BasketStore::RemoveFiles(processed)` — atomares Read-Modify-Write unter einem einzigen `BasketLock`, sodass waehrend der Operation hinzugefuegte Eintraege erhalten bleiben (auch wenn ein paralleler `AddFiles`-Aufruf genau zwischen Read und Write des Sinks reinkommt). Bei Selektion-Fallback (leerer Korb) wird der Korb nicht angefasst.

### Ordner-Ziel bei "hierher"-Befehlen

`m_szFolder` ist standardmaessig das Elternverzeichnis. **Sonderfall:** Rechtsklick auf einen einzelnen Ordner → `m_szFolder` zeigt auf den angeklickten Ordner selbst. Beides gilt sowohl fuer den CF_HDROP-Pfad als auch fuer den IShellItemArray-Fallback (`SFGAO_FOLDER`-Pruefung).

### BrowseForFolder Guard

`BrowseForFolder()` (fuer "Kopieren nach..." / "Verschieben nach...") schuetzt gegen Mehrfach-Oeffnung: `InterlockedCompareExchange` blockiert Re-Entrance waehrend modaler Messagepump, ein 500ms-Zeitfenster (`BROWSE_THROTTLE_MS`) blockiert sequentielle Aufrufe (Explorer ruft InvokeCommand ggf. pro Selektion auf). Erster Aufruf bekommt alle Dateien via HDROP.

### Menu-Icon

`IDI_BASKET` (`Res\basket.ico`) via `IconToBitmap()` in 32-Bit-ARGB-HBITMAP konvertiert (Transparenz), in `QueryContextMenu` per `MENUITEMINFOW`/`MIIM_BITMAP` zugewiesen. Lebenszyklus: `CShellExt::m_hMenuBitmap`, im Destruktor freigegeben.

### Korb-Dialog

- **Nicht-modal**, schliesst sich bei Fokusverlust (`WM_ACTIVATE/WA_INACTIVE`). Resizable, kein Min/Max
- **Layout:** ListView | Splitter | TreeView | Buttons + Statusbar (`BeginDeferWindowPos`)
- **Persistenz:** Fenstergroesse, 3 Spaltenbreiten, Splitter-Position in `HKCU\Software\CopyBasket\` (`DialogWidth/Height`, `ColWidth0..2`, `SplitRatio`)
- **ListView-Spalten:** 0=Dateiname, 1=Typ ("Datei"/"Verzeichnis"), 2=voller Pfad. Spalten-Sortierung via `LVN_COLUMNCLICK` + `ListView_SortItemsEx` mit nativen Sortier-Pfeilen (`HDF_SORTUP`/`HDF_SORTDOWN`)
- **System-Icons:** Shared Shell-Image-List einmalig in `WM_CREATE` an ListView (`LVSIL_SMALL`) + TreeView (`TVSIL_NORMAL`). Kein Cleanup (Image-List gehoert der Shell)
- **Tastatur:** Strg+A selektiert alles; ListView erhaelt initialen Fokus, damit Tastaturkuerzel sofort wirken
- **Drag&Drop:** `DragAcceptFiles` + `WM_DROPFILES` → `BasketStore::AddFiles` (Duplikat-Pruefung) + `RefreshFromDisk`. Drop-Target-Status zaehlt nicht als `WA_INACTIVE`, der Dialog bleibt also waehrend des Drags offen

#### TreeView-Detail-Panel

- **Zweck:** Bei Auswahl eines Ordner-Eintrags im ListView zeigt der TreeView dessen Inhalt — Wurzel + erste Ebene sofort, tiefere Ebenen lazy beim Aufklappen. Bei Datei-Auswahl oder keiner Auswahl: leer
- **Read-Only:** `TVN_SELCHANGINGW` wird abgefangen (return `TRUE`) → keine Selektion. Remove-Button wirkt nur auf ListView
- **Lazy Loading (wichtig, frueher rekursiv eager → UI-Hang/Crash bei tiefen/zyklischen Baeumen):** `InsertTreeNode(hParent, name, fullPath, icon, isDirectory, isReparsePoint)` legt einen Knoten an und entscheidet anhand `isDirectory && !isReparsePoint`, ob ein Expand-Pfeil (`cChildren=1`) gezeigt und der volle Pfad als Heap-`std::wstring*` in `lParam` gespeichert wird (sonst `cChildren=0` und `lParam=0`). `PopulateChildren()` laedt **genau eine Ebene** (`FindFirstFileW`/`FindNextFileW`; Verzeichnisse zuerst, dann Dateien, stabile Sortierung; Icons via `GetSysIconIndex()`). `PopulateTreeFromPath()` fuegt Wurzel ein, ruft `PopulateChildren` fuer die erste Ebene, expandiert die Wurzel. Tiefere Ebenen werden beim ersten Klick auf den Expand-Pfeil im `TVN_ITEMEXPANDINGW`-Handler nachgeladen — der prueft `TVIS_EXPANDEDONCE`, um Doppel-Population nach Collapse zu vermeiden, und setzt `cChildren=0` zurueck, falls die Ebene leer war (Pfeil verschwindet). Flicker-Schutz: `WM_SETREDRAW` FALSE/TRUE um den Populate-Block
- **Reparse Points:** Junctions/Symlinks (z.B. `Application Data` → `AppData\Roaming` im Profilordner) bekommen `cChildren=0` und kein Pfad-`lParam` → werden nie traversiert. Sonst zyklische Endlosschleife → UI-Hang
- **Speicher:** Pfad-`lParam` (Heap-`std::wstring*`) wird im `TVN_DELETEITEMW`-Handler freigegeben — `TreeView_DeleteAllItems` triggert das fuer jeden Knoten automatisch
- **Update-Trigger:** `UpdateTreeForSelection()` bei `LVN_ITEMCHANGED` (SELECTED/FOCUSED), Remove und Entf-Taste. Cached letzten Pfad in `lastTreePath`, um redundantes Neupopulieren zu vermeiden

#### Splitter zwischen ListView und TreeView

- **Eigene Window-Klasse** `CopyBasketSplitter` (`IDC_SIZENS`, `COLOR_BTNFACE`)
- **Drag** via Standard-Mouse-Capture in `SplitterWndProc`; konvertiert Mausposition zu Parent-Koordinaten und ruft `LayoutControls()`
- **Ratio:** clampt auf 10–90% (`MIN_PANE_H = 60`), persistiert in Registry als `SplitRatio`

### Incident-Log bei Abbruch/Teilfehler

Ziel: User sieht exakt, welche Dateien tatsaechlich verarbeitet wurden — auch fuer Dateien in Unterverzeichnissen.

- **Strategie:** Pre-Scan + Post-Check via Filesystem statt Verlassen auf `IFileOperation`-Callbacks (Callbacks sind bei tief verschachtelten Verzeichnissen ueber Laufwerksgrenzen nicht zuverlaessig)
  - Pre-Scan: `BuildExpectedFiles()` + `EnumerateFilesRecursive()` → `ExpectedFile { sourcePath; destPath; }`
  - Post-Check: `ClassifyOutcome` prueft jede erwartete Datei via `GetFileAttributesW`. MOVE: erfolgreich wenn Quelle weg und Ziel existiert. COPY: erfolgreich wenn Ziel existiert
- **Trigger:** `fAborted || !notProcessed.empty()`
- **Owner-Window:** `lpcmi->hwnd` wird via `FileOpParams::hwndOwner` durch den Hintergrund-Thread bis in `ExecuteFileOpCOM` gereicht. `SetOwnerWindow(hwndOwner)` und `TASKDIALOGCONFIG::hwndParent` (mit `IsWindow()`-Check + `GetForegroundWindow()`-Fallback, `TDF_POSITION_RELATIVE_TO_WINDOW`) — Dialoge verankert am Explorer
- **Log-Datei:** `%APPDATA%\CopyBasket\operations.log` (UTF-16LE+BOM, `CREATE_ALWAYS` — pro Incident ueberschrieben). Inhalt: Zeitstempel, Operationstyp, Zielordner, "Erfolgreich"/"Fehlgeschlagen"-Listen, ggf. "ABGEBROCHEN"
- **Abort-Dialog:** `TaskDialogIndirect` mit "Log oeffnen" (`ShellExecuteW`) + "Schliessen". Strings via `StringTable` (`AbortTitle`, `AbortMsgFmt`, `LogOp*`, ...)
- Der `CFileOperationProgressSink` bleibt minimal (nur Korb-Tracking); Logging passiert in `ExecuteFileOpCOM` per Filesystem-Check, unabhaengig vom Callback-Verhalten

### Einstellungen-Dialog

Tab Control mit zwei Seiten:
- **Sprache:** ComboBox (Endonyme: Deutsch/English aus `StringTable`), speichert via `SaveLanguageSetting()`
- **\u00DCber:** Bold-Titel + Website-Link `hjs.page.gd/cb` (`WC_LINK`, oeffnet Browser via `ShellExecuteW` und schliesst Dialog) + Copyright (alles aus `StringTable`)

## i18n

`StringTable`-Struct mit zwei statischen Instanzen (`s_DE`/`s_EN`) und globalem Pointer auf die aktive Tabelle. Zugriff via `GetStrings().MemberName` (typ-sicher, Zero-Overhead). Spracheinstellung in Registry `HKCU\Software\CopyBasket\Language` (`"de"`/`"en"`, Default `de`); `LoadLanguageSetting()` im CShellExt-Konstruktor, `SaveLanguageSetting()` im Einstellungen-Dialog wirkt sofort beim naechsten Rechtsklick.

## Installer

- **NSIS 3.x** (Unicode, Modern UI 2), Skript: `installer\CopyBasket.nsi`. Sprachen: Deutsch + Englisch
- **Architektur:** automatisch x64/x86 via `x64.nsh`; `${DisableX64FSRedirection}` fuer 64-Bit regsvr32-Aufruf aus 32-Bit NSIS
- **Registrierung:** `regsvr32 /s` beim Installieren, `/u /s` beim Deinstallieren; `SHChangeNotify` nach beiden
- **Installation:** `$PROGRAMFILES64\CopyBasket` bzw. `$PROGRAMFILES\CopyBasket`. Inhalt: DLL + basket.ico + Uninstall.exe (kein CB-CMT.exe — nur Portable). Eintrag unter `HKLM\...\Uninstall\CopyBasket`
- **Deinstallation** entfernt `HKCU\Software\CopyBasket` + `%APPDATA%\CopyBasket`
- **Version:** automatisch aus `Version.h` via `!searchparse`
- **Build:** nur via GitHub Actions (NSIS lokal nicht verfuegbar), getriggert durch Tag-Push (`git tag vX.Y.Z && git push origin vX.Y.Z`). **Vor dem Tag-Push muss `release-notes/vX.Y.Z.md` existieren und gecommittet sein** (siehe Abschnitt "Release Notes"), sonst scheitert der Workflow am `Create GitHub Release`-Step. Workflow installiert NSIS via **scoop** (`scoop bucket add extras`); chocolatey und SourceForge-Direkt-Downloads waren unzuverlaessig
- **Output:** `build\CopyBasket-X.Y.Z-setup.exe`

### Release-Assets (GitHub)

- `CopyBasket_vX.Y.Z.zip` — Portable (DLLs + CB-CMT.exe fuer manuelle Registrierung, inkl. `README.txt` und `Lies.mich.txt`)
- `CopyBasket-X.Y.Z-setup.exe` — NSIS Installer (regsvr32 automatisch, kein CB-CMT.exe)

### Release Notes

- **Pflicht-Datei pro Release:** `release-notes/vX.Y.Z.md` — wird vom Workflow ueber `body_path` als GitHub-Release-Body verwendet. Fehlt sie, scheitert der `Create GitHub Release`-Step und kein Release wird angelegt (Artefakte sind dann gebaut aber nicht published)
- **Inhalt:** Highlights / Vorher-Nachher-Tabellen / "Files changed"-Block / Download-Links. Ausfuehrlicher als die README.md-Changelog-Einzeiler — die Notes ersetzen die ehemals auto-generierten GitHub-Release-Notes komplett (`generate_release_notes: true` ist raus)
- **Reihenfolge pro Release:**
  1. `Version.h` bumpen
  2. Changelog-Eintraege in `README.md` + `portable/README.txt` + `portable/Lies.mich.txt`
  3. `release-notes/vX.Y.Z.md` schreiben
  4. Alles in einem Release-Commit (`Release vX.Y.Z: ...`) committen + pushen
  5. `git tag -a vX.Y.Z -m "vX.Y.Z" && git push origin vX.Y.Z`
- **Backfill alter Releases:** `gh release edit vX.Y.Z --notes-file release-notes/vX.Y.Z.md`

### Portable-Dokumentation

- **Quellordner:** `portable\README.txt` (Englisch) + `portable\Lies.mich.txt` (Deutsch)
- **Inhalt:** Installation via CB-CMT.exe, Feature-Overview, komplette Changelog-History, Kontakt-/Link-Block, Lizenz
- **Einbindung:** `.github\workflows\release.yml` kopiert beide Dateien im Schritt "Prepare release assets" nach `_release\` und packt sie in die Portable-ZIP
- **Versions-Header:** kein Versionsstring im Header (steht schon im ZIP-Dateinamen) — pro Release muss nur der Changelog-Abschnitt nachgefuehrt werden, kein Header-Drift
- **Keine Umlaut-Escapes:** Die `.txt`-Dateien nutzen echte UTF-8-Umlaute (kein `\u00XX`-Escaping wie im C++-Code)

### CB-CMT.exe (CopyBasket Context Menu Tool)

GUI-Tool zur manuellen Aktivierung/Deaktivierung der Shell Extension (nur Portable-Version). Quellcode: `regsvr Tool\CopyBasketContextMenu\`, Output: `regsvr Tool\bin\Release\CB-CMT.exe`.

Funktionen: Activate/Deactivate-Radio mit Statuserkennung; optionale Checkbox "Delete User Settings" (`HKCU\Software\CopyBasket` + `%APPDATA%\CopyBasket\`) mit Warnhinweis und Sicherheitsabfrage; DLL-Existenzpruefung vor `regsvr32`.

Statuserkennung prueft **alle drei** Shell-Subkeys aus `SHELLEX_KEYS[]` (Liste `REG_PATHS[]` in `regsvr Tool/CopyBasketContextMenu/main.cpp`, parallel zu `SHELLEX_KEYS[]` in `CopyBasket.cpp` — bei Erweiterung der Shell-Locations beide pflegen). Partiell registriert (z.B. nach kaputtem Uninstall) zaehlt als „nicht registriert" → Default ist Activate, damit der User die Registrierung komplettieren kann. Fehler (DLL fehlt, regsvr32 schlaegt fehl, Exit-Code ≠ 0) lassen den Dialog **offen** fuer Retry; nur Erfolg ruft `EndDialog`.

Build: `MSBuild "regsvr Tool/CopyBasketContextMenu.sln" /p:Configuration=Release /p:Platform=x64`

## Konventionen

- Kein Precompiled Header
- ASCII-sichere Bezeichner im Code, Umlaute via Unicode-Escapes (z.B. `\u00FC` fuer ue) \u2014 gilt insbesondere fuer `Strings.cpp`. Die portable docs (`portable/*.txt`) sind UTF-8 mit echten Umlauten
- `g_cRef` immer mit `InterlockedIncrement`/`InterlockedDecrement` zugreifen (Data-Race-Schutz wegen Hintergrund-Threads)
- Gemeinsame Logik in Helper-Funktionen extrahieren (z.B. `ExecuteFileOpCOM` fuer IFileOperation, `CShellExt::HandleFileOp` fuer die vier COPY/MOVE-Commands, `ComputeLayoutMetrics` fuer Splitter+LayoutControls)
- **Single Source of Truth \u2014 nicht hartkodieren:**
  - Registry-Pfad: `BasketStore::REG_KEY` (deklariert in `BasketStore.h`, definiert in `BasketStore.cpp`)
  - Pfad-Tail-Extraktion: `BasketStore::ExtractFileName(path)` \u2014 kein `rfind(L'\\')`/`find_last_of` lokal duplizieren
  - Layout-Konstanten: `BTN_W`/`BTN_H`/`MARGIN` als file-scope `static const int` in `BasketDialog.cpp` (Muster wie `MIN_WIDTH`/`SPLITTER_H`/`MIN_PANE_H`)
  - i18n-Strings via `GetStrings().Member` aus `Strings.h/.cpp` (auch fuer Sprachnamen im Combo und Copyright-Text)
  - Shell-Registrierungs-Subkeys: `SHELLEX_KEYS[]` in `CopyBasket.cpp` \u2014 Register und Unregister iterieren dieselbe Liste
  - Help-Strings im Kontextmenu: `LookupHelp(idCmd)` in `ShellExt.cpp` liefert wide+ANSI-Pair, keine zwei parallelen Switches
- COM-Pointer durchgaengig via `Microsoft::WRL::ComPtr` (`<wrl/client.h>`): `ExecuteFileOpCOM` (lokales `IFileOperation`/`IShellItem`), `ShellExt` (lokales `IShellItemArray`/`IShellItem` + Member `m_pDataObj` als `ComPtr<IDataObject>`). Nur der `CFileOperationProgressSink` bleibt manuell, weil `Unadvise` vor `Release` laufen muss
- BasketStore-Zugriffe immer ueber die oeffentliche API (`ReadBasket`/`WriteBasket`/`AddFiles`/`RemoveFiles`/`ClearBasket`) \u2014 die halten den `BasketLock`. Direkter `CreateFileW` auf `basket.txt` ist verboten. Atomare Read-Modify-Write-Operationen (Read + Mutation + Write) sollten als eigene API-Funktion mit einem einzigen `BasketLock` realisiert werden (Vorbild: `RemoveFiles`)
- Referenzprojekt: `C:\Users\HJS\Claude.ai\wscitecm\` (SciTE Context Menu Extension)

## GitHub

Repository: https://github.com/HJS-cpu/CopyBasket
