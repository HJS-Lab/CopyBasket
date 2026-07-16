# CopyBasket

[![Build & Release](https://github.com/HJS-cpu/CopyBasket/actions/workflows/release.yml/badge.svg)](https://github.com/HJS-cpu/CopyBasket/actions/workflows/release.yml)
[![Latest Release](https://img.shields.io/github/v/release/HJS-cpu/CopyBasket?sort=semver)](https://github.com/HJS-cpu/CopyBasket/releases/latest)
[![Live Website](https://img.shields.io/badge/Live_Website-hjs.page.gd-brightgreen)](https://hjs.page.gd/cb/)

A Windows Explorer shell extension that lets you collect files into a virtual "basket" and then copy or move them all at once to a target directory.

<!-- ![CopyBasket Screenshot](screenshot.png) -->

---

## ✨ Features

| Feature | Description |
|---------|-------------|
| **Virtual Basket** | Collect files and folders from anywhere into a persistent basket |
| **Copy & Move** | Copy or move basket contents to the current folder or a chosen directory |
| **Copy Path** | Copy selected file/folder paths to the clipboard |
| **Browse Dialog** | Choose a target folder via modern IFileDialog |
| **Basket Viewer** | ListView with native Windows icons, sortable columns (Name / Type / Path), statusbar, Ctrl+A, remove function |
| **Drag & Drop** | Drop files or folders from Explorer directly onto the open basket dialog to add them |
| **Directory Preview** | TreeView panel shows the full recursive contents of the selected basket entry, with real Explorer icons |
| **Resizable Split** | Adjustable splitter between ListView and TreeView — saved across sessions |
| **Incident Log** | Aborted or incomplete operations are logged in detail and surfaced via a TaskDialog with "Open Log" button |
| **Direct Operations** | "Copy to..." / "Move to..." work on selected files when the basket is empty |
| **Async File Ops** | All copy/move operations run on a background thread via IFileOperation — Explorer stays responsive |
| **Smart Basket** | Only successfully processed files are removed from the basket — partial failures keep remaining items |
| **Thread-safe Storage** | Basket file is serialised across Explorer processes via a named mutex; writes are atomic (temp file + `MoveFileExW`) so concurrent operations cannot lose entries or leave it half-written |
| **Navigation Pane** | Works with items selected in the Explorer navigation pane and virtual folders |
| **i18n** | German and English UI, switchable via Settings dialog |

---

## 📥 Download

**[⬇️ Download Latest Release](https://github.com/HJS-cpu/CopyBasket/releases/latest)**

Each release includes:
- **CopyBasket-X.Y.Z-setup.exe** — Installer (automatic registration, recommended)
- **CopyBasket_vX.Y.Z.zip** — Portable package (DLLs + CB-CMT.exe)

---

## 🖥️ System Requirements

- Windows 10 / 11
- No external dependencies

---

## 🔧 Installation

### Using the Installer (recommended)

1. Download **`CopyBasket-X.Y.Z-setup.exe`** from the [latest release](https://github.com/HJS-cpu/CopyBasket/releases/latest).
2. Run the installer — it will request admin rights, install the correct DLL (x64/x86), and register the shell extension automatically.
3. To uninstall, use "Programs and Features" in Control Panel.

### Using CB-CMT (portable)

1. Download **`CopyBasket_vX.Y.Z.zip`** from the [latest release](https://github.com/HJS-cpu/CopyBasket/releases/latest).
2. Extract `CopyBasket_x64.dll` (or `_x86.dll`) and `CB-CMT.exe` to the same folder (e.g. `C:\Program Files\CopyBasket\`).
3. Run **CB-CMT.exe** — it will prompt for elevation and let you activate or deactivate the context menu with one click.

### Manual (regsvr32)

```cmd
regsvr32 "C:\Program Files\CopyBasket\CopyBasket.dll"
```

To unregister:

```cmd
regsvr32 /u "C:\Program Files\CopyBasket\CopyBasket.dll"
```

---

## 📋 Context Menu

Right-click any file, folder, or the folder background to access the **CopyBasket** submenu:

| Item | Action |
|------|--------|
| **Add to Basket** | Add selected files/folders to the basket |
| **Copy Path** | Copy selected paths to the clipboard |
| **Show Basket (N files)** | Open the basket viewer dialog |
| **Copy Basket Here** | Copy basket contents to the current folder |
| **Copy to...** | Choose a folder, then copy |
| **Move Basket Here** | Move basket contents to the current folder |
| **Move to...** | Choose a folder, then move |
| **Clear Basket** | Remove all items from the basket |
| **Settings** | Change language (German / English) |

Items are grayed out when the basket is empty. "Add to Basket" and "Copy Path" are hidden on background clicks.

---

## 🛠️ Building from Source

### Prerequisites

- Visual Studio 2022 Build Tools (Toolset v143)
- Windows SDK

### Build

```bash
# Shell Extension (x64 + x86)
MSBuild.exe CopyBasket.sln /p:Configuration=Release /p:Platform=x64
MSBuild.exe CopyBasket.sln /p:Configuration=Release /p:Platform=Win32

# Registration Tool (x64)
MSBuild.exe "regsvr Tool\CopyBasketContextMenu.sln" /p:Configuration=Release /p:Platform=x64
```

Output:
- `x64\Release\CopyBasket.dll` / `Release\CopyBasket.dll`
- `regsvr Tool\bin\Release\CB-CMT.exe`

---

## ⚙️ How It Works

CopyBasket is a COM DLL implementing `IShellExtInit` and `IContextMenu`. It registers as a context menu handler for files, directories, and the directory background.

- **Basket Storage:** `%APPDATA%\CopyBasket\basket.txt` (UTF-16LE with BOM); all access serialised through a session-scoped named mutex (`Local\CopyBasket.basket.v1`) and writes are atomic via temp file + `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`
- **Incident Log:** `%APPDATA%\CopyBasket\operations.log` (rewritten per incident, only on abort/partial failure)
- **File Operations:** `IFileOperation` with `CFileOperationProgressSink` on a background thread (`_beginthreadex`)
- **Reliable Logging:** Pre-scan + post-check via filesystem verification — works for deeply nested directory trees regardless of IFileOperation callback behaviour
- **Icons:** Shared Windows system image list via `SHGetFileInfoW(SHGFI_SYSICONINDEX)` attached to both ListView and TreeView
- **Settings:** Language preference stored in `HKCU\Software\CopyBasket`
- **Dialog Persistence:** Window size, column widths, and split ratio saved in Registry

---

## 📄 License

This project is provided as-is. See the [LICENSE](LICENSE) file for details.

---

## 📝 Changelog

### v1.7.0
- **Security & reliability hardening** from a full code review — no user-facing feature changes, but several latent crash, data-loss, and privilege-escalation issues are closed:
  - **Buffer overflow fixed** — `ResolveClickTarget` copied the clicked path into a fixed `WCHAR[MAX_PATH]` with unbounded `lstrcpyW`. A navigation-pane folder whose `SIGDN_FILESYSPATH` exceeds `MAX_PATH` (long-path-aware locations) could overflow the buffer into adjacent members inside `explorer.exe`. Now bounded via `lstrcpynW`
  - **Junction/reparse-point loop fixed** — the file-operation pre-scan (`EnumerateFilesRecursive`) followed reparse points, so a cyclic junction (e.g. `Application Data` → `AppData\Roaming`) or a junction to a huge tree could loop forever / exhaust memory → Explorer crash with the operation stuck "busy". Reparse points are now treated as leaves, matching the TreeView's existing behavior
  - **DLL-unload race fixed** — the background file-op thread dropped `g_cRef` and then kept running DLL/CRT epilog code; a `CoFreeUnusedLibraries` firing in that window could unmap the DLL mid-execution. The thread now pins its module (`GetModuleHandleExW`) and exits via `FreeLibraryAndExitThread`
  - **Dialog window classes bound to the DLL** — `BasketDialog` and `SettingsDialog` registered their classes with `GetModuleHandle(NULL)` (explorer.exe) and skipped `UnregisterClassW` on the `CreateWindow`-failure path, which could leave a class with a DLL WndProc registered after the DLL unloaded → dangling-WndProc crash. Now registered with the DLL module handle, with tracked, guaranteed cleanup
  - **Remove-from-basket: truncation + lost update fixed** — the basket viewer read entries back through a `MAX_PATH` buffer and rewrote the whole file, which truncated paths longer than 259 chars and could drop entries added concurrently. The list now keeps the full path per item on the heap (`lParam`) and removes exactly the selected entries via the atomic `RemoveFiles` API
  - **Copy post-check accuracy** — a copy into a folder with an existing same-named file, "skipped" in the conflict dialog, was mis-reported as "succeeded" (and could be trimmed from the basket). The post-check now records the destination's pre-operation timestamp and only counts a copy as successful if the file was newly created or actually overwritten
- **Installer / tooling security:**
  - **`regsvr32` binary-planting fixed** — the NSIS installer and CB-CMT invoked `regsvr32` by bare name, which Windows resolves by searching the elevated process's own directory (the setup's download folder) first. A planted `regsvr32.exe` there would run with admin rights. Both now use the full `System32\regsvr32.exe` path
  - **Installer robustness** — checks `regsvr32`'s exit code (no more silent broken install), handles updating over a DLL still loaded in Explorer (rename-aside + `/REBOOTOK`), and preserves a custom install directory on update
- **CI hardening** — GitHub Actions pinned to commit SHAs, `github.ref_name` routed through an env variable (script-injection safe), and a tag ↔ `Version.h` consistency check that fails the release if they disagree
- **Minor robustness** — clipboard "Copy Path" guards `GlobalLock`/`SetClipboardData` failures, `QueryContextMenu` respects the `idCmdLast` command budget, basket writes flush before the atomic rename, `DisableThreadLibraryCalls` in `DllMain`, list/buttons are keyboard-tabbable, and the basket window is clamped to the work area so it can't open off-screen

### v1.6.0
- **Concurrent file-op guard** — a second right-click on "Copy basket here", "Move basket here", or "Copy/Move to..." while an operation is still running used to launch a second `IFileOperation` thread. Both threads would then race on the same source files and clobber each other's `operations.log` (opened with `CREATE_ALWAYS` + exclusive share — either the first incident was overwritten, or the second `CreateFileW` silently failed and the abort dialog disappeared). `FileOps` now exposes `IsBusy()`; `HandleFileOp` checks it up front and shows a localized "Operation in progress" message instead of starting a second job. The slot is claimed atomically in `LaunchFileOp` (`InterlockedCompareExchange`) as a race-tight defense and released in the thread proc — held across the abort dialog so the user has to acknowledge an incident before queueing the next op

### v1.5.9
- **Basket dialog TreeView: lazy loading** — the detail panel used to eagerly walk the entire directory subtree of the selected basket entry, which could hang or crash the dialog on deep or cyclic trees (e.g. a profile folder with `Application Data` → `AppData\Roaming` junctions). Each directory node is now inserted with `cChildren=1` and stores its full path on the heap (`lParam`); children are loaded one level at a time on `TVN_ITEMEXPANDINGW`. Reparse points get no expand arrow and are never traversed
- **Basket dialog TreeView: fix duplicate first-level entries** — selecting a folder showed every direct child twice. `PopulateTreeFromPath` pre-loads level 1 and then calls `TreeView_Expand`, which fires `TVN_ITEMEXPANDINGW` while `TVIS_EXPANDEDONCE` is still clear — the previous guard re-populated and produced duplicates. The lazy-load guard is now `!TreeView_GetChild(...)`, which asks the right question ("does this node already have children?") directly

### v1.5.8
- **Release-workflow infrastructure only** — bumped `softprops/action-gh-release` to v3 (native Node 24), pinned the runner to `windows-2025-vs2026` (the redirect target `windows-latest` is moving to by 2026-06-15), and removed the `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24` workaround which is no longer needed. Binary artifacts are identical to v1.5.7

### v1.5.7
- **CB-CMT: detect partial registration** — status detection now inspects all three shell-registration subkeys (`*`, `Directory`, `Directory\Background`) instead of only `*`. A partially-registered DLL (e.g. after a failed uninstall) is correctly reported as "not fully registered", so the dialog defaults to **Activate** and lets the user complete the registration in one click
- **CB-CMT: retry-on-error** — DLL-not-found, `ShellExecuteEx` failures, and non-zero `regsvr32` exit codes now keep the dialog open instead of force-closing. Consistent across all error paths: only success calls `EndDialog`, so the user can fix the underlying problem and retry without relaunching the tool
- **CB-CMT internal refactor** — 108-line `IDOK` case split into `BuildDllPath` / `RunRegsvr32` / `OnOkClicked` helpers; user-visible message strings hoisted to file-scope `static const wchar_t*` (single source of truth); bounded `StringCchPrintfW` everywhere (no more `wsprintfW`); `IsCopyBasketRegistered` and friends marked `static`

### v1.5.6
- **Thread-safe basket store** — all `basket.txt` access is now serialised through a session-scoped named mutex (`Local\CopyBasket.basket.v1`), and `WriteBasket` writes through a temp file + `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` so concurrent operations from multiple Explorer processes can no longer lose entries or leave the file half-written. New `BasketStore::RemoveFiles` API does a read-modify-write under a single lock so entries added during a long-running copy/move operation cannot be lost when the sink trims processed items
- **Internal refactoring** — `Microsoft::WRL::ComPtr` now used for `IShellItemArray` / `IShellItem` / `IDataObject` in `ShellExt.cpp` (manual `Release` calls eliminated); `GetCommandString` collapsed from two parallel 9-case switches into a single `LookupHelp` table; new `ResolveClickTarget` helper shared between the HDROP and IShellItemArray paths in `Initialize`; `SHELLEX_KEYS[]` table shared between `RegisterServer` and `UnregisterServer`; `AddTreeNodes` insert block extracted into a lambda

### v1.5.5
- **Dialogs anchored to Explorer's window** — conflict, abort, and incident dialogs no longer disappear behind other windows during file operations (especially relevant for cross-volume MOVE conflicts). `IFileOperation::SetOwnerWindow(lpcmi->hwnd)` plus `TASKDIALOGCONFIG::hwndParent` with `TDF_POSITION_RELATIVE_TO_WINDOW`; falls back to `GetForegroundWindow()` if the owner hwnd has become invalid
- **Internal refactoring** — single source of truth for `BasketStore::REG_KEY`, `BasketStore::ExtractFileName`, and dialog layout metrics (`ComputeLayoutMetrics`); settings-dialog strings (language names, copyright) routed through the i18n table; named layout constants (`BTN_W` / `BTN_H` / `MARGIN`) and TaskDialog button IDs / `BROWSE_THROTTLE_MS`; consolidated icon-lookup helper with attribute-hint fallback; `DlgWndProc` split into per-message handlers (`OnCreate` / `OnDropFiles` / `OnCommand` / `OnNotify`); four `InvokeCommand` file-op cases collapsed into one `HandleFileOp(isCopy, toPicker)` helper; `ExecuteFileOpCOM` decomposed into setup / perform / post-check phases; `Microsoft::WRL::ComPtr` replaces manual `Release` for `IFileOperation` and `IShellItem`

### v1.5.0
- **Drag & Drop into the basket viewer** — drop files or folders from Explorer directly onto the open basket dialog to add them (duplicate-safe)
- **Race-condition-safe basket update** after copy/move: `FinishOperations` now re-reads the basket from disk and subtracts the processed items, so entries added **during** a long-running operation are preserved
- **Centralised version strings** — `COPYBASKET_VERSION_STR` (wide) and `COPYBASKET_VERSION_STR_A` (narrow) are derived from the numeric macros via preprocessor stringification; `CopyBasket.rc` now uses them so `FileVersion` / `ProductVersion` stay in sync automatically
- **Internal refactor:** shared `RefreshFromDisk(DlgData*)` helper unifies the `ReadBasket` → `PopulateListView` → `UpdateStatusBar` sequence across `WM_CREATE`, `WM_DROPFILES`, and remove-selected

### v1.4.0
- **Basket dialog: TreeView detail panel** — shows the full recursive contents of the selected basket entry (folders and all nested files), read-only
- **Resizable splitter** between ListView and TreeView, with min-pane constraints; split ratio persisted in Registry (`SplitRatio`)
- **Native Windows system icons** in both ListView and TreeView — shared `SHGetFileInfoW` image list, matches Explorer's icons exactly (folders, `.txt`, `.exe`, custom app icons, etc.)
- **New "Type" column** in the basket ListView between Name and Path (shows "File" / "Folder"), with its own persisted width (`ColWidth2`)
- **Incident log for aborted / failed operations:**
  - Detailed log written to `%APPDATA%\CopyBasket\operations.log` (UTF-16LE, rewritten per incident)
  - Pre-scan + filesystem post-check — reliable even when moving deeply nested directory trees across volumes
  - `TaskDialogIndirect` notification with **"Open Log"** button showing how many files were not processed
  - Log is silent during normal operations; only appears when something actually went wrong

### v1.3.0
- Migrated file operations from `SHFileOperationW` to modern `IFileOperation` API with `CFileOperationProgressSink`
- Smart basket tracking: only successfully copied/moved files are removed, partial failures keep remaining items
- Navigation pane and virtual folder support via `IShellItemArray` fallback

### v1.2.0
- Basket dialog: statusbar with file count, sortable columns with sort arrows, Ctrl+A to select all, initial keyboard focus
- Basket dialog: deferred window positioning (`BeginDeferWindowPos`) for smooth resizing
- Right-click on a single folder uses it as target directory for "Copy/Move Basket Here"
- BrowseForFolder dialog re-entrance guard prevents duplicate dialogs
- CB-CMT: "Delete User Settings" checkbox for cleaning up registry and AppData
- Installer uninstaller now removes `%APPDATA%\CopyBasket` user data

### v1.1.0
- NSIS Installer with automatic `regsvr32` registration/unregistration
- Architecture detection (x64/x86) — installs the correct DLL
- Add/Remove Programs entry with uninstaller
- Tabbed settings dialog (Language + About)

### v1.0.0
- Initial release
- Virtual basket for collecting files and folders
- Copy / Move to current folder or chosen directory
- Copy path to clipboard
- ListView basket viewer with remove function
- Direct copy/move on selection when basket is empty
- Non-blocking async file operations
- German and English UI with settings dialog
- Custom basket icon in context menu
