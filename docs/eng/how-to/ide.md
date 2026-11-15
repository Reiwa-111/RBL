# last verified: v0.8.0 | 2026.11.12 | IDE WORKFLOW AND RELEASE STATUS

## RBL Studio — native IDE

RBL Studio's primary desktop IDE is a C++20 application built with wxWidgets 3.2 and `wxStyledTextCtrl`/Scintilla. A separate Rust `rbl-studio-worker` supervises compiler and toolchain processes through a bounded NDJSON protocol. Chromium, Electron and embedded WebView engines are not used. Fresh Windows/Linux portable build payloads include app-local CPython 3.14.10 for backend actions; an explicit `RBL_PYTHON` overrides it. A source checkout still needs Python 3.9+ for development. The Linux payload includes Clang's integrated assembler, LLD and wxWidgets 3.2.4 shared libraries; GNU Binutils and system wxWidgets are not required. The native shell installer uses the host GTK 3 runtime, while the Flatpak variant uses GNOME Platform 51 to provide GTK inside the sandbox.

The workflow includes a multi-tab Scintilla editor, project tree, Find/Replace, Find in Files, command palette, Quick Open, diagnostics navigation, task output and interactive stdin/EOF. A full language server, semantic completion, refactorings and an integrated debugger are not implemented.

## Launch

Linux:

```bash
bash RBLStudio.sh
```

Windows PowerShell:

```powershell
.\RBLStudio.cmd
```

The top-level CMD/VBS/SH launchers select the wx GUI and adjacent Rust worker from a portable directory or the last successful build pointer. If wx is unavailable, `RBL_STUDIO_ALLOW_LEGACY=1` permits fallback to a separately built Rust/egui executable in a developer checkout; that executable is not included in the installer. Linux requires a working wxWidgets/GTK display environment (X11 or Wayland).

For a Linux installation that does not depend on host GTK, use the [Flatpak packaging guide](linux-packaging.md). It declares the GNOME Platform runtime, so Flatpak installs the GTK runtime into the application sandbox. The native shell installer still requires the host GTK 3 runtime.

**Windows development build:** use the MSYS2 UCRT64 wxWidgets 3.2 toolchain and the repository-pinned Binutils overlay. `tools/build_wx_ide.ps1` builds the C++ GUI and the Rust worker, then collects the required runtime DLLs. End users do not need MSYS2 on `PATH` when using the staged portable payload.

## Working with documents

- Open text files with any extension through **File → Open**. Files are also listed in the project tree, which supports folder collapse/expand; scanning is bounded by 1,500 entries per directory and 50,000 nodes globally.
- Text documents support UTF-8, UTF-8 BOM, UTF-16 LE and UTF-16 BE. The original encoding, BOM and line-ending style (LF, CRLF or CR) are preserved when saving. Line endings are normalized to LF inside the editor. Mixed LF/CRLF/CR files use the first detected style; if restoring it would exceed the file-size limit, LF is used instead.
- Files containing NUL/binary data, invalid UTF-8/UTF-16 or unsupported ANSI code pages are rejected as text.
- Document guardrails: 16 MiB per file, 64 MiB aggregate editor text and at most 32 tabs. The exact encoded result size, including BOM and line-ending expansion, is checked before saving. These limits are safety bounds, not a guarantee that every workload stays below the separate 512 MiB whole-process-tree acceptance target.
- RBL syntax highlighting can be enabled or disabled in Settings. Other extensions remain plain text. The editor supports soft wrapping, indentation guides, line numbers, font size and tab width.
- Save and Save As do not restrict the extension: `.txt`, `.md`, `.json`, `.py`, `.rs`, `.toml` and other text documents can be saved normally.
- Create a plain text document with **File → New Text File**; **File → New** creates a new RBL document.

**Compiler actions accept `.rbl` only.** Run, Build, Check and ASM reject every other extension. Ordinary text files can still be opened and saved without being renamed or treated as source.

## Navigation and keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+N` | New RBL document |
| `Ctrl+O` | Open any file |
| `Ctrl+Shift+O` | Open project folder |
| `Ctrl+S` | Save |
| `Ctrl+Shift+S` | Save All open documents |
| File → Save As | Save under another name/extension |
| `Ctrl+Z` / `Ctrl+Y` (`Ctrl+Shift+Z`) | Document-wide Undo / Redo, including Replace/Replace All and edits across large-file windows |
| `Ctrl+F` | Find in document |
| `Ctrl+H` | Replace in document |
| `Ctrl+G` | Go to line |
| `Ctrl+P` | Quick-open a project file |
| `Ctrl+Shift+P` | Command palette |
| `Ctrl+Shift+F` | Find in project files |
| `F5` | Run the active `.rbl` file |
| `Ctrl+B` | Build the active `.rbl` file |
| `F7` | Check the active `.rbl` file |
| `Ctrl+T` | Run project tests |
| `Ctrl+Shift+T` | Run benchmarks |
| `Ctrl+W` | Close current tab |
| `Shift+F5` | Stop the active task |
| `F1` | RBL Quick Reference |
| `Tab` / `Shift+Tab` | Indent / outdent current line |
| Click `▾` / `▸` in the gutter | Collapse / expand an RBL block |

The Project Tree is populated lazily when a directory is expanded, but enumeration currently runs synchronously on the UI thread; slow network or removable directories may temporarily delay the window. Find in Files runs on a background thread and is bounded to 100,000 entries, 128 MiB total scanned data, 2 MiB per file, 10,000 results and recursion depth 32. Symlinks and generated directories are excluded. Clicking a search result or diagnostic opens the file and moves the cursor to the relevant line.

## Panels and settings

- **Project** — lazily populated project tree, folder collapse/expand and manual refresh.
- **Editor** — horizontally scrollable tabs, dirty marker, find/replace, line numbers, caret position and text encoding.
- **Run / Problems / Console / Search** — dockable tool windows. Diagnostics are parsed from worker output, including stderr-only diagnostics, and can navigate to the original source location.
- **Command palette** — quick access to common operations without navigating menus.
- **Settings** — RBL Dark, RBL Light, Nord, Monokai and High Contrast UI themes; an independent editor color scheme; compact mode; editor font size, tab width, line numbers, RBL syntax highlighting, indentation guides, soft wrapping, automatic checks, save-before-Run/Build/Assembly and Windows toolchain/WSL mode. Window layout and workspace are persisted between launches.
- **View** — show/hide Project and Run/Output panes and restore the default layout.

The Scintilla editor provides native selection, undo/redo, line numbering, folding, brace matching and horizontal/vertical scrolling. The application preserves encoding and EOL metadata when saving supported text files. The status bar reports workspace, task state and caret position.

RBL structural blocks have gutter fold markers and indentation guides. The custom scanner skips comments and string literals, but it is an approximation and is not shared with the compiler parser; unusual or malformed source may therefore be folded differently from compiler structure.

When an RBL program calls `input()`, the Run panel displays a `STDIN` field and focuses it automatically. Type a line and press Enter or click **Send**; multiple input lines are supported. **EOF** closes stdin for programs that read until end-of-file. `print()` and `input(prompt)` flush stdout so prompts and prior output appear before the next input is requested.

The **Stop** command cancels the active task. The worker remains alive between tasks and is restarted only when its lifecycle requires it.

The application checks external file changes before overwriting and warns when a save would split a multiply hard-linked file. Project Tree, Find in Files and Save use bounded traversal and filesystem identity checks. On Linux, existing-target saves require `renameat2(RENAME_EXCHANGE)`; unsupported filesystems fail closed with `operation_not_supported` rather than falling back to unconditional `renameat`. This is an intentional safety/availability restriction; first-time saves still use exclusive `linkat`. A displaced symlink is identified without following it so the exchange can be safely rolled back when both entry identities still match. Windows uses `ReplaceFileW` with recovery artifacts and identity checks, but the path-based target replacement is not a full compare-and-swap. See the engineering audit for exact limitations.

## Verification and measurements

Windows UCRT64 build:

```powershell
.\tools\build_wx_ide.ps1
```

Linux/WSL build:

```bash
./tools/build_wx_ide.sh
```

The Windows long-duration resource acceptance is run with `python benchmarks/wx_resource_acceptance.py --duration 7200 --interval 1 --cycle-interval 60 --max-ui-ping-ms 500`. Its CSV records process-tree memory, handles, GDI/USER objects and UI responsiveness. See [`../../../benchmarks/IDE_RESOURCE_USAGE.md`](../../../benchmarks/IDE_RESOURCE_USAGE.md) for measurement context. For backend diagnostics use `python3 rbl.py doctor` or **Run → Toolchain doctor**.

GitHub `v0.8.0` is published as a **pre-release without attached assets**, not as a stable release. The wx frontend remains **RELEASE BLOCKED for stable/binary distribution**. The current Windows SaveDocument suite passed 27/27 with an isolated fixture. Resource smoke runs passed at 60/120/300 seconds using 32 documents, the path-backed `resource-task.rbl`, all four task types and graceful GUI/worker shutdown; the 300-second run completed 84 cycles. A 7,200-second attempt failed before sampling when the harness could not acquire notebook focus; it was not retried after the user requested no further IDE launches. The two-hour soak remains NOT PASSED. On 2026-11-12, fresh Windows/Linux payloads included pinned app-local CPython 3.14.10; installer acceptance passed in isolated roots, including rollback and user-data preservation. Installed CLI and headless worker backend actions passed without system Python. Windows wrapper acceptance used only System32 plus the app's bundled MinGW toolchain; Linux backend acceptance used a scrubbed PATH with `as`, `ld`, `bash` and `dirname`, but no Python or `cc`. The full Windows/Linux `python rbl.py test` suites passed; package safety tests passed 9/9 on Ubuntu and 8/9 on Windows with the POSIX-only symlink case skipped. Pristine-VM acceptance and GUI startup/backend-action acceptance remain NOT RUN. Linux Binutils remain external prerequisites. DPI static checks pass 12/12, but live 125-200% multi-monitor acceptance remains open. Linux GUI builds and headless CTest pass, but interactive Linux desktop acceptance has not been performed in a graphical session. The Rust worker and Windows MSYS2 corresponding-source bundles are local; neither is attached to the public pre-release, and component-specific legal review remains open. Binutils 2.47-4 is listed for CVE-2026-90801; exact exploitability for the default RBL link path remains unresolved and release-blocking. The Windows destination-leaf replacement race and POSIX filesystems lacking atomic exchange are deferred to RBL 0.9; POSIX saves remain fail-closed. The separate LOW orphan-temp cleanup edge is fixed by delete-on-handle cleanup. See the [engineering audit](../explanation/ide-engineering-audit.md) and [release manifest](../../../RELEASE_MANIFEST.txt) for current evidence.

## Known limitations

- ANSI/code-page files must be converted to UTF-8 or UTF-16 first. The editor does not guess an encoding when there is no BOM, avoiding silent text corruption.
- Binary formats are not displayed or edited.
- Syntax highlighting for other languages, semantic completion, symbol indexing, rename/refactor and debugging are not available yet.
- Files larger than 16 MiB are intentionally not loaded; use an external viewer or split the file to stay within the memory budget.

## Legacy Rust/egui frontend

The older Rust/egui application remains in `rbl-ide/legacy-egui/app.rs` and `rbl-ide/legacy-egui/editor.rs` for
rollback/reference. When the wx executable is unavailable,
`RBL_STUDIO_ALLOW_LEGACY=1` permits fallback to a separately built executable
from a developer checkout; it is not included in the wx installer payload and
is not the primary RBL Studio 0.8.0 frontend.

The wx primary frontend's implementation, build instructions, historical test
records and remaining acceptance gates are maintained in
[`rbl-ide/wx-ui/README.md`](../../../rbl-ide/wx-ui/README.md). Do not redistribute
the portable Windows bundle until exact corresponding sources accompany it or
an applicable source-offer process is legally reviewed; see the
[`wx third-party inventory`](../../../rbl-ide/wx-ui/THIRD_PARTY_NOTICES.md).
## Current IDE acceptance - 2026-11-11

Windows SaveDocument passed 27/27 with an isolated fixture. Resource smoke runs passed at 60, 120 and 300 seconds using 32 documents, the path-backed `resource-task.rbl`, all four task types and graceful GUI/worker shutdown. The 300-second run completed 84 cycles (316 samples); peak process-tree working set was 282.7 MiB, private bytes 208.0 MiB, handles 1,243, GDI/USER objects 441/132 and maximum UI ping 0.9 ms.

A 7,200-second attempt failed before sampling because the harness did not acquire notebook focus. It was not retried after the user requested no further IDE launches. The two-hour soak is NOT PASSED. Live 125-200% DPI, interactive Linux desktop acceptance and remaining manual GUI workflows are NOT RUN. Gate D remains PARTIAL.

The Windows orphan-temp cleanup edge is fixed by delete-on-handle cleanup; if cleanup itself fails, the retained path is reported. The `ReplaceFileW` destination-leaf race and CVE-2026-90801 remain release blockers. See the Gate E review and release manifest for evidence.
