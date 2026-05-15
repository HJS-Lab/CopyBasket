================================================================================
                                  CopyBasket
                       Windows Explorer Shell Extension
================================================================================

A Windows Explorer shell extension that lets you collect files into a virtual
"basket" and then copy or move them all at once to a target directory.

--------------------------------------------------------------------------------
CONTENTS OF THIS PACKAGE
--------------------------------------------------------------------------------

  CopyBasket_x64.dll    Shell extension DLL (64-bit Windows)
  CopyBasket_x86.dll    Shell extension DLL (32-bit Windows)
  CB-CMT.exe            CopyBasket Context Menu Tool (activate / deactivate)
  README.txt            This file
  Lies.mich.txt         German documentation

--------------------------------------------------------------------------------
INSTALLATION
--------------------------------------------------------------------------------

1. Copy CopyBasket_x64.dll (or CopyBasket_x86.dll on 32-bit Windows) and
   CB-CMT.exe to a permanent folder, for example:

       C:\Program Files\CopyBasket\

   Do NOT delete or move the DLL after activation — the shell extension is
   loaded from that path.

2. Run CB-CMT.exe. The tool will request administrator rights.

3. Select "Activate" and confirm. CB-CMT registers the DLL via regsvr32
   and notifies the Windows shell.

4. Right-click any file, folder, or folder background in Explorer — the
   "CopyBasket" submenu should now appear.

--------------------------------------------------------------------------------
UNINSTALLATION
--------------------------------------------------------------------------------

Run CB-CMT.exe again, select "Deactivate", and confirm.

To also remove user settings (language preference, dialog size, basket file):
enable the "Delete User Settings" checkbox before confirming deactivation.

Afterwards the folder containing the DLL and CB-CMT.exe can simply be deleted.

--------------------------------------------------------------------------------
FEATURES
--------------------------------------------------------------------------------

  * Collect files and folders from anywhere into a persistent basket
  * Copy or move basket contents to the current folder or a chosen directory
  * Copy selected paths to the clipboard
  * Basket viewer: sortable ListView with native Windows icons, TreeView
    detail panel for folders, resizable splitter, statusbar, Ctrl+A, remove
  * Drag & Drop files or folders from Explorer directly into the open basket
  * Non-blocking async file operations (IFileOperation on a background thread)
  * Incident log with TaskDialog notification for aborted / partial operations
  * German and English UI, switchable via settings

--------------------------------------------------------------------------------
CHANGELOG
--------------------------------------------------------------------------------

v1.5.8
  - Release-workflow infrastructure only: bumped softprops/action-gh-
    release to v3 (native Node 24), pinned the runner to
    windows-2025-vs2026, removed the obsolete Node-24 forcing env var.
    Binary artifacts are identical to v1.5.7

v1.5.7
  - CB-CMT now checks all three shell-registration subkeys (*, Directory,
    Directory\Background) instead of only one. A partially-registered DLL
    (e.g. after a failed uninstall) is reported as "not fully registered"
    and the dialog defaults to Activate so the user can complete the
    registration in one click
  - CB-CMT keeps the dialog open on errors (DLL missing, ShellExecuteEx
    failure, regsvr32 exit code != 0) — only success closes the dialog,
    so the user can fix the problem and retry without relaunching
  - CB-CMT internal refactor: IDOK case split into BuildDllPath /
    RunRegsvr32 / OnOkClicked helpers; message strings hoisted to file-
    scope constants; bounded StringCchPrintfW throughout

v1.5.6
  - Thread-safe basket store: all basket.txt access serialised through a
    session-scoped named mutex; WriteBasket writes via temp file +
    atomic rename so concurrent operations from multiple Explorer
    processes can no longer lose entries or leave the file half-written
  - Sink trims processed items via a new RemoveFiles API that does the
    read-modify-write under a single lock, preserving entries added
    during a long-running copy/move
  - Internal refactor: WRL ComPtr for IShellItemArray / IShellItem /
    IDataObject in ShellExt.cpp; GetCommandString collapsed into a
    single lookup table; ResolveClickTarget helper shared between HDROP
    and IShellItemArray paths; SHELLEX_KEYS table shared between
    Register and Unregister; AddTreeNodes insert block as a lambda

v1.5.5
  - Conflict, abort, and incident dialogs are now anchored to Explorer's
    window so they no longer disappear behind other windows (relevant
    during cross-volume MOVE conflicts)
  - Internal refactoring: extracted helpers and removed duplication across
    BasketDialog, ShellExt, FileOps, and Strings — single source of truth
    for the registry path, path-tail extraction, and layout metrics;
    InvokeCommand and ExecuteFileOpCOM decomposed into helpers; DlgWndProc
    split into per-message handlers; WRL ComPtr for COM cleanup; settings
    dialog strings routed through the i18n table

v1.5.0
  - Drag & Drop into the basket viewer: drop files or folders from Explorer
    directly onto the open basket dialog (duplicate-safe)
  - Race-condition-safe basket update: FinishOperations re-reads the basket
    from disk and subtracts processed items, preserving entries added during
    a long-running copy / move operation
  - Centralised version strings: Version.h macros now derive wide and narrow
    version strings; CopyBasket.rc uses them for FileVersion / ProductVersion
  - Internal refactor: shared RefreshFromDisk helper in BasketDialog

v1.4.0
  - Basket dialog: TreeView detail panel shows the full recursive contents
    of a selected folder entry (read-only)
  - Resizable splitter between ListView and TreeView, persisted in Registry
  - Native Windows system icons in ListView and TreeView
  - New "Type" column (File / Folder)
  - Incident log for aborted / failed operations with pre-scan + filesystem
    post-check; TaskDialog with "Open Log" button

v1.3.0
  - Migrated to modern IFileOperation API with CFileOperationProgressSink
  - Smart basket tracking: only successfully processed files are removed
  - Navigation pane and virtual folder support via IShellItemArray fallback

v1.2.0
  - Basket dialog: statusbar, sortable columns, Ctrl+A, initial focus
  - BeginDeferWindowPos for smooth resizing
  - Right-click on a single folder uses it as target for "Copy / Move Here"
  - BrowseForFolder re-entrance guard
  - CB-CMT: "Delete User Settings" checkbox
  - Installer removes %APPDATA%\CopyBasket on uninstall

v1.1.0
  - NSIS Installer with automatic regsvr32 registration / unregistration
  - Architecture detection (x64 / x86)
  - Add / Remove Programs entry
  - Tabbed settings dialog (Language + About)

v1.0.0
  - Initial release
  - Virtual basket for collecting files and folders
  - Copy / Move to current or chosen directory
  - Copy path to clipboard
  - ListView basket viewer with remove function
  - Non-blocking async file operations
  - German and English UI

--------------------------------------------------------------------------------
CONTACT & LINKS
--------------------------------------------------------------------------------

  Author:     Hans-Joachim Schlingensief (HJS)
  Email:      hajo.schlingensief@gmail.com
  Website:    https://hjs.page.gd/cb
  GitHub:     https://github.com/HJS-cpu/CopyBasket

  Latest release:
      https://github.com/HJS-cpu/CopyBasket/releases/latest

  Issue tracker:
      https://github.com/HJS-cpu/CopyBasket/issues

--------------------------------------------------------------------------------
LICENSE
--------------------------------------------------------------------------------

This software is provided as-is. See the LICENSE file in the source
repository for details.

(c) 2026 Hans-Joachim Schlingensief
