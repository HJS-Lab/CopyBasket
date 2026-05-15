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
