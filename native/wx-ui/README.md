# RBL Studio — primary wxWidgets frontend

This C++20 + wxWidgets 3.2 + Scintilla application is the primary RBL Studio
frontend selected by the repository launchers and user-scope installers. The
previous Rust/egui frontend remains in `src/app.rs` and `src/editor.rs` as a
rollback/reference implementation. When the wx executable is unavailable,
`RBL_STUDIO_ALLOW_LEGACY=1` permits fallback to a separately built legacy
executable from a developer checkout; it is not included in the wx installer
payload. The wx application remains UNRELEASED until
the acceptance and distribution gates below are closed.

## Architecture

- C++20 + wxWidgets 3.2 for the native desktop shell.
- `wxAuiManager` for dockable Project and Output/Console panes.
- `wxAuiNotebook` for editor tabs.
- A compact action toolbar grouped into file, Run/Stop/Check/Build and tooling
  actions; five selectable UI themes (RBL Dark, RBL Light, Nord, Monokai and
  High Contrast); independent editor color schemes; active-tab styling; and a
  four-part status bar for version, workspace, task state and caret position.
- Icon-backed Project tree with a distinct RBL file glyph, highlighted `.rbl`
  entries and an expanded workspace root; Console, Problems and Search tabs
  have their own icons and use the same dark tool-window palette.
- A small custom RBL application icon and hand-drawn folder/source/text-file
  glyphs, rather than relying on the Windows shell's yellow-folder/file icons.
- `wxStyledTextCtrl` (Scintilla) for UTF-8 editing, UTF-8 BOM and UTF-16 LE/BE
  round-tripping, line numbers, indentation, selection, undo/redo, wrapping,
  basic keyword/builtin autocomplete, brace matching, custom RBL styling and
  folding.
- Docked Console, Problems and Search views. Compiler diagnostics are parsed
  from source-position output and can navigate to the original source line;
  unsaved-buffer snapshot paths are mapped back to the original document.
- Bounded asynchronous Find in Files: 100,000 entries, 128 MiB total scanned,
  2 MiB per file, 10,000 results and recursion depth 32; symlinks and generated
  directories are excluded.
- Categorized Settings pages cover Appearance, Editor, Behavior and Toolchain,
  with UI theme, independent editor color scheme, compact mode, font size, tab
  width, line numbers, soft wrapping, indentation guides, syntax highlighting,
  debounced syntax auto-check, save-before-Run/Build/Assembly and Windows
  native/WSL toolchain selection. Apply, OK, Cancel and Restore Defaults are
  available. Values are persisted in `ui.ini`; compatible legacy settings are
  migrated from `settings.json` once.
- View menu commands can show/hide Project and Run/Output panes and restore the
  default workspace layout. A separate New Text File action creates plain-text
  documents that are not sent to the RBL compiler.
- Command Palette (`Ctrl+Shift+P`) filters available commands as the user types
  and supports arrow-key selection and Enter activation. Quick Open (`Ctrl+P`)
  builds a bounded workspace file index on a background thread, filters by file
  name or relative path, ranks filename-prefix matches first and supports
  keyboard selection. It indexes text/source formats rather than binary outputs,
  skips generated directories and symlink/reparse
  entries; selected paths use the regular validated document-open path.
- Find (`Ctrl+F`) and Replace (`Ctrl+H`) open a persistent dockable panel with
  Next/Previous, Replace, Replace All and Match Case. Search wraps around the
  current document; Replace All is grouped into one undo action.
- The main toolbar reflects context: compiler actions require an active RBL
  document and a ready backend; Stop is enabled only while a task is running.
- Command palette, Quick Open, Go to Line, About and RBL Quick Reference expose
  the main IDE actions without requiring the menu hierarchy.
- Save All, Close Other Tabs, Close All Tabs and a bounded recently-closed-file
  stack cover the common document lifecycle workflows. Workspace root and
  window size are persisted with the dock layout.
- Open-document guardrails: 32 tabs, 16 MiB per document and 64 MiB aggregate
  UTF-8 editor text; Scintilla maximum lengths are recalculated after edits.
  A conservative 32 MiB cumulative edit-event threshold clears all undo buffers
  together. This bounds repeated edit-history growth but is not an exact measure
  of Scintilla's currently retained undo allocation.
- A separate Rust `rbl-studio-worker` process for compiler/runtime tasks.
- A bounded NDJSON protocol connects the frontend to the worker. The worker
  reuses the existing Rust process supervisor, including stdin, snapshots,
  process cancellation, Windows Job Objects and Unix process groups.
- Application root and workspace root are independent. The frontend resolves
  the installation root from the canonical executable path (or a validated
  explicit `RBL_STUDIO_ROOT`), initializes the worker once with that root, and
  sends the selected workspace with each task. Tool scripts are resolved from
  the application root; task working directory and editor snapshots use the
  workspace. This is required for projects opened outside the installation.

The frontend is deliberately a separate CMake project and does not modify the
compiler or runtime. The Rust/egui application is retained as an optional legacy
binary behind Cargo feature legacy-egui; build it explicitly with
`cargo build --release --features legacy-egui --bin rbl-studio`. The default Cargo
build graph contains only the worker dependencies. The top-level CMD/VBS/SH
launchers and Windows/Linux installers select wx by default; the legacy executable
is not included in the wx installer.

### Configuration profile

The wx frontend stores `ui.ini` and migrates legacy `settings.json` from the
user configuration directory under `RBLStudio`. For portable deployments and
isolated acceptance tests, `RBL_STUDIO_CONFIG_DIR` may point directly to an
application-specific configuration directory; when unset, the platform's
normal per-user configuration directory is used.

## Linux development build

Install wxWidgets 3.2 development files with AUI and STC components, then:

```sh
cmake -S native/wx-ui -B build/wx-ui -DCMAKE_BUILD_TYPE=Release
cmake --build build/wx-ui --parallel
cargo build --bin rbl-studio-worker --release
RBL_STUDIO_WORKER="$PWD/target/release/rbl-studio-worker" \
  ./build/wx-ui/rbl-studio-wx
```

On Ubuntu 24.04 the tested development package is `libwxgtk3.2-dev` (wxWidgets
3.2.4); its `wx-config` exposes `aui` and `stc` components. The Linux CMake
Release build has passed.

## Windows development build

Use the MSYS2 UCRT64 toolchain consistently for wxWidgets and the GUI:

```powershell
pacman -S --needed mingw-w64-ucrt-x86_64-wxwidgets3.2-common `
  mingw-w64-ucrt-x86_64-wxwidgets3.2-msw
./tools/build_wx_ide.ps1
```

The script builds the GUI and Rust worker, then copies the required wxWidgets
and MinGW runtime DLLs beside the executables. The resulting directory is
portable and does not require MSYS2 on the user's PATH.

## Resource target

The acceptance ceiling is 512 MiB for the complete IDE process set, including
the Rust worker and 32 open tabs. Earlier prototype measurements (not a final
acceptance profile):

- Windows 11 Release: GUI about 34 MiB working set + worker about 10 MiB.
- Ubuntu 24.04 / WSL: GUI about 52 MiB RSS + worker about 2.8 MiB.
- WSL with 32 empty tabs: GUI about 57 MiB RSS + worker about 2.9 MiB.

The Windows portable directory with the runtime DLLs is about 25 MiB before
symbols and optional assets. These are prototype measurements, not final
release acceptance numbers.

## Remaining acceptance work

- File open/save, UTF-8/UTF-16 encoding preservation, external-change warnings,
  unsaved-change prompts, AUI layout persistence, basic find/replace, Find in
  Files, diagnostics navigation and editor settings are implemented. File
  watching, symbol-aware completion/navigation, refactoring and multi-cursor
  editing are not implemented.
- The custom RBL lexer/folding scanner is still an approximation, not the
  compiler's lexer/parser. Differential lexical tests, incremental styling
  profiling and broader editor interaction tests remain open. Folding scans
  now process at most 2,048 lines per UI timer slice and are disabled above
  50,000 logical lines; this bounds per-frame work but does not make folding
  compiler-accurate.
- Both Windows UCRT64 Release and Linux/WSL CMake builds pass. The Rust worker
  passes platform-specific unit tests, Clippy with `-D warnings`, and the
  interactive two-prompt `unit_converter.rbl` smoke test on Windows and Linux.
- Latest Windows idle startup sample: GUI 30.5 MiB working set plus worker
  9.8 MiB working set. This is not the 32-tab / 16 MiB / active-task acceptance
  test; the 512 MiB ceiling remains unproven for peak workloads.
- No release, commit, tag or push had been made at this 2026-11-09 snapshot. The
  wxWidgets frontend was still a candidate at that point; current launch/install
  status and remaining gates are recorded at the top of this README and in the
  2026-11-10 follow-up below.

## Licensing

wxWidgets is distributed under the wxWindows Library Licence 3.1. Scintilla is
used through the wxWidgets `wxStyledTextCtrl` integration; do not substitute
Riverbank QScintilla without a separate GPL/commercial licensing review.
The Windows portable build script copies the wxWidgets and selected MinGW
runtime/dependency license directories into `licenses/` and writes
`THIRD_PARTY_NOTICES.txt`. This preserves the package license texts; it does
not by itself complete the separate corresponding-source/source-offer review
for every redistributed toolchain DLL.

## Verification snapshot — 2026-11-09

Latest source was rebuilt on Windows UCRT64/MinGW and Linux/WSL after the
current correctness changes. Windows standalone startup was also checked with
`PATH` restricted to Windows system directories; the adjacent Rust worker
started and connected. The Windows worker interactive smoke passed. Linux
verification passed CMake build, `cargo fmt -- --check`, worker build, 21/21
worker tests, Clippy with `-D warnings`, and the same interactive smoke.

The full Windows `python rbl.py test` suite passed: 61 direct-ASM regression
tests, 63 diagnostics tests, runtime ABI checks, Linux SysV call-alignment
instrumentation, GC pressure, stdin and streaming tests. The Windows Rust
worker unit-test invocation could not be completed in this environment: the
MinGW linker could not resolve Rust standard-library paths beneath the
non-ASCII Windows user-profile directory. This is an environment/toolchain
limitation, not a reported source failure; the Windows Release worker itself
builds and passes its interactive smoke.

Interactive GUI checks confirmed the default workspace can recover from an
invalid saved AUI perspective, the project/editor/output panes render, and 32
blank tabs can be opened before the explicit `Tab limit reached (32)` message
appears. One post-load sample with the project tree expanded measured about
86.9 MiB GUI working set plus 10.0 MiB worker working set. The test had no
active compiler child and is not a peak process-tree measurement.

### Initial acceptance snapshot — 2026-11-09 (historical)

Still unverified at this initial wx snapshot: GUI EOF/cancel (GUI-driven two-line
`input()` is verified), diagnostic navigation against unsaved buffers,
Save/Save As and external-change prompts, UTF-16 LE/BE round-trips and
malformed-input cases, EOL preservation,
16 MiB and aggregate 64 MiB boundary tests, Find in Files cancellation and
large-result responsiveness, settings migration/persistence, and peak memory
with active compiler/tests. The existing egui resource report must not be
treated as measurements of this wx frontend. Therefore the 512 MiB whole-
process-tree target was not yet demonstrated at that time, and wx remained a
candidate rather than the default IDE.

The latest visual pass adds the toolbar, project/file icon hierarchy, tool
window styling, line-number gutter styling and stale-layout recovery for the
new core panes. Windows and Linux source builds pass after this pass.

### Direct Windows GUI validation — 2026-11-09

The user desktop was used directly for a reference comparison: PyCharm
Community Edition 2024.3 was launched with this repository, and its main
window was captured as `build/wx-ui-win/reference-pycharm.png`. The wx candidate
was then launched, captured, and iteratively corrected against that reference.
Latest screenshot: `build/wx-ui-win/ide-final-visual.png`.

An actual GUI workflow was exercised, not just worker smoke: the source file
`examples/utilities/unit_converter.rbl` was opened through the IDE's Open
dialog, Run was invoked through the GUI, and two lines (`1`, `10`) were entered
through the GUI stdin field and Send button. Console output included
`10.000 km = 6.214 mi`; the process finished with exit code 0 and the status
bar returned to Finished. Evidence screenshot:
`build/wx-ui-win/ide-run-complete.png`. The Check command was also invoked from
the GUI and the Problems tool window was displayed without diagnostics for
this valid source; screenshot: `build/wx-ui-win/ide-check-pass.png`.

The direct GUI session used an explicit `RBL_PYTHON` path. A deliberate
launch with PATH restricted to Windows system directories proved that the
portable GUI and adjacent worker start, but Run cannot discover Python when
Python is neither on PATH nor supplied through `RBL_PYTHON`; this is expected
from the current worker contract and must be reflected in launch instructions.

## Latest verification snapshot — 2026-11-10

The output-pipeline regression was reproduced with a generated RBL program
which emitted about 20 MiB of console text. The failure was the worker's
two-second stdout backpressure deadline, not a compiler/runtime failure. The
worker now buffers up to 128 bounded output events (1 MiB), flushes control
events immediately, and flushes a short output burst after 25 ms. The GUI reads
up to 256 KiB per worker poll and batches adjacent output events before updating
the native console. The original two-second backpressure deadline and bounded
queues remain unchanged. The GUI stress run completed with exit code 0 and
remained responsive; the observed process-set working set was about 78 MB and
private bytes about 46 MB after completion.

Worker protocol verification now covers the complete interactive path:
initialization, two sequential `stdin_line` requests and acknowledgements,
child receipt of both values, `stdin_eof`, its acknowledgement, the child's
EOF diagnostic, task completion, and shutdown. The test also checks malformed
stdin requests and input after task completion. `tests/studio_worker_smoke.py`
passes on Windows and Ubuntu 24.04. The bounded-output integration test passes
on both platforms and delivered 6,156,119 bytes on Windows and 6,150,119 bytes
on Linux without losing output or tripping the two-second failsafe.

The application/workspace root split is now exercised end-to-end by
`tests/studio_worker_root_split.py`: the worker is initialized with an
installation fixture containing backend scripts, then runs tasks against two
separate workspace fixtures. The test verifies that the child process uses the
workspace as its current directory while resolving `tools/rbltool.py` from the
installation. `ResolveApplicationRoot` canonicalizes the executable itself
before walking parent directories; a POSIX symlink-executable regression is
included in the focused CTest target. Explicit invalid root overrides still
fail closed.

The Windows wx candidate opened 32 documents of exactly 2 MiB each (64 MiB
aggregate). The GUI remained responsive. A post-load sample measured about
175 MB GUI working set / 150 MB private bytes, plus about 10 MB working set /
3 MB private bytes for the worker (about 185 MB / 153 MB for the process set).
After invoking Close All Tabs, the GUI fell to about 37 MB working set /
11 MB private bytes; the worker remained near 10 MB working set / 3 MB
private bytes. These are observed samples, not a
continuous peak-memory trace, but the tested 32-tab workload remained well
below the 512 MiB target. The separate 20 MiB output test also remained below
that target. The earlier egui measurements are not used as evidence for wx.

A separate exact-boundary document test opened a single 16 MiB file (16,777,216
bytes) successfully. The GUI measured about 69 MB working set / 44 MB private
bytes, with the worker near 10 MB working set / 3 MB private bytes. The file
was displayed in Scintilla with syntax styling active.

Windows `python rbl.py test` completed with exit code 0: 61 direct-ASM
regressions, 63 diagnostics cases, runtime ABI/call-alignment checks, GC and
string-I/O pressure, streaming/backpressure, toolchain manifest checks, and
build-cache checks passed. Two developer-only native C probes were explicitly
skipped because GCC `cc1`/headers are unavailable in the Windows environment.
The Windows Release worker and wx CMake target were rebuilt successfully.
The separate Windows corpus check also passed: 121 RBL source candidates,
114 positive files compiled and assembled, 7 declared negative fixtures were
rejected as expected, and there were no failures. A Unicode-named plain-text
IDE search fixture is explicitly excluded from compiler-corpus enumeration.
The documentation check passed with 65 Markdown files, 457 relative links,
and no missing targets or invalid fragments.

Ubuntu 24.04 / WSL verification passed `cargo fmt -- --check`, the Release
worker build, 22/22 worker unit tests, Clippy with `-D warnings`, the wx CMake
build, interactive stdin/EOF smoke, and the output backpressure integration
test. The Linux worker and GUI builds therefore include the current output
batching implementation.

The project tree is lazy-loaded one directory at a time, excludes symlinks and
generated directories, and scans at most 1,500 entries per directory. Find in
Files remains separately bounded by 100,000 entries, 128 MiB scanned, 2 MiB
per file, 10,000 results and depth 32.

## Follow-up verification — 2026-11-10

The current source now selects Scintilla's insertion EOL mode from the first
line terminator in the loaded file (CRLF, CR or LF) before inserting the decoded
text. This prevents newly inserted lines on Windows from defaulting to CRLF in
an LF/CR document. The character-added indentation handler also accepts CR-only
newlines. Existing line-ending bytes are not deliberately normalized by this
change. The Windows GUI file round-trip suite now verifies the supported
encoding/EOL cases byte-for-byte (21/21); Find in Files separately recognizes
CR-only, CRLF and LF line endings.

Save now performs a second disk fingerprint check after the temporary file has
been flushed and closed. If the destination changes during serialization or
temporary-file I/O, the user receives a second explicit overwrite prompt; a
missing or unreadable destination cancels the save. This narrows, but cannot
eliminate, the final cross-platform check-to-atomic-replace race. It has compile
coverage on Windows/WSL. Real GUI Save and external conflict/deletion decisions
now pass; the concurrent-writer race in the final check-to-replace interval
remains unverified.

Earlier Windows automation attempts could locate the Save As dialog but could
not reliably activate it from a separate process. The later interactive
keyboard/clipboard automation below resolved that limitation and verified
Save As for both new and existing documents.

## Real GUI file round-trip — 2026-11-10

Added `tests/wx_gui_file_roundtrip.py`, a Windows-only test requiring `pywin32`
and an interactive desktop. It launches the actual wx executable with each real
fixture, posts the File > Save command to the main frame, then compares the
source file byte-for-byte. The current run passed **21/21**:

- UTF-8 LF, CRLF and CR.
- UTF-8 BOM with CRLF.
- UTF-16 LE with CRLF and UTF-16 BE with LF.
- Mixed CRLF/LF/CR in one file.
- Editing the actual Scintilla buffer and saving through File > Save.
- Save As for a new document and for an existing document, with exact destination
  bytes and confirmation that the original file remains unchanged in the latter
  case.
- External modification: declining preserves the external bytes; accepting
  explicitly overwrites with the editor buffer.
- External deletion: declining leaves the file deleted; accepting recreates it.
- Dirty tab close and whole-window close: Cancel retains the editor, No discards
  without changing disk bytes, and Yes saves before the window closes.
- UTF-16 LE unpaired high surrogate and UTF-16 BE unpaired low surrogate are
  rejected with the encoding warning and the original bytes remain unchanged.

All valid fixtures included Cyrillic text and a non-BMP rocket character in a
Cyrillic filename. This verifies Save, Save As, exact encoding/EOL round-trip,
edited-buffer saving, external conflict/deletion choices and dirty tab/window
close prompts. It does **not** cover every malformed-UTF16 sequence or the
concurrent writer race at the final atomic replacement. Re-run with
`python tests/wx_gui_file_roundtrip.py`
after a Windows wx build; this is a GUI test, not part of headless `rbl.py test`.

Closing the IDE while Find in Files is active now requests cancellation, vetoes
the immediate close and defers the final close until the search completion event
has been handled. This keeps the event target alive while the worker reports
completion instead of joining the search thread in the frame destructor during
the normal close path. A real GUI close-during-search test now passes. A
filesystem operation blocked by the OS can still delay worker completion.

New verification on 2026-11-10:

- Windows UCRT64 wx CMake Release build passed after the startup, search EOL,
  global project-tree bound and settings-profile changes. Current GUI SHA-256:
  `F84AEE828DD9E9F5A782C46623BF6B32E21FDBF06CA9D88FE763DA6F7E5665CB`.
- Ubuntu 24.04/WSL wx CMake Release build passed. The Release worker passed
  `cargo fmt -- --check`, 23/23 unit tests, Clippy with `-D warnings`, stdin/EOF
  smoke and output-backpressure integration. Windows worker stdin/EOF smoke and
  backpressure integration also passed; the Windows GNU worker suite passed
  22/22 with formatting and Clippy `-D warnings`.
- `tests/studio_worker_shutdown_order.py` passed on Windows and WSL. With stdout
  consumption deliberately stalled during a multi-megabyte task burst, shutdown
  drained accepted events and preserved `finished` before `bye` (2,310 Windows /
  1,155 WSL output events).
- `python rbl.py test` exited 0: 61 direct ASM tests, 63 diagnostics tests,
  Runtime ABI/call-alignment, GC and string-I/O pressure, streaming/backpressure,
  toolchain manifest and build-cache checks. Two developer-only native C probes
  were skipped because the bundled Windows toolchain lacks GCC `cc1`/headers.
- `python tests/check_corpus.py` exited 0: 121 candidates, 114 positive sources
  compiled and assembled, 7 expected negative fixtures rejected, 0 failures.
- `python tests/check_documentation.py` exited 0: 66 Markdown files, 474
  relative links, no missing targets or invalid fragments. `git diff --check`
  exited 0 (Git emitted only line-ending conversion warnings).
- `build/wx-ui-win/gui-printwindow-acceptance.png` is a real `PrintWindow` capture
  of the current GUI at 96 DPI / 100% scale. It shows the menu bar, toolbar,
  project explorer, editor, Console/Problems/Search tabs, stdin controls and
  status bar without clipping at 1440×900. Windows reference-image lookup
  returned no saved PyCharm references, so a pixel/interaction comparison to
  those references is inconclusive. DPI 125/150/200% is unavailable in this
  session and remains unverified.
- `python tests/wx_gui_file_roundtrip.py`: 27/27 PASS, including Save/Save As,
  edited-buffer Save, exact encoding/EOL round-trips, external modification and
  deletion choices, dirty tab/window prompts, malformed-surrogate rejection,
  and hard-link warning/cancel/confirm behavior.
- `python tests/wx_gui_file_size_limit.py`: 2/2 PASS. Exactly 16 MiB opens;
  16 MiB + one byte shows the expected warning and is rejected without closing
  the IDE.
- `tests/wx_gui_diagnostics.py`: saved-file navigation, unsaved snapshot
  navigation and stale-diagnostic activation after tab closure passed.
- `tests/wx_gui_settings_migration.py`: 4/4 PASS for valid legacy values,
  oversized integers, negative values and malformed JSON; a second GUI launch
  loaded the persisted values through the actual Editor Settings dialog.
- `tests/wx_gui_find_in_files.py`: CR-only, CRLF and LF locations passed 3/3.
  The cancellation/re-entry and close-during-search GUI tests also passed.
- `benchmarks/wx_search_resource_acceptance.py`: 12,000 candidate hits were
  capped at 10,000; latest isolated run completed in 3.72 s with 76.9 MiB
  process-tree RSS / 29.2 MiB USS peak and 92.0 ms maximum UI ping. A combined
  test batch under concurrent load once exceeded its 1 s ping timeout; isolated
  rerun passed.
- `tests/wx_gui_cancel_input.py`: two consecutive infinite-loop tasks were
  cancelled with exit code 130; stdin/Send/EOF controls disabled immediately
  after Stop and the worker remained alive for the second run.
- `tests/wx_gui_stdin_eof.py`: a line followed immediately by EOF reached the
  child in FIFO order; the child persisted the line and then observed EOF.
- `tests/wx_gui_task_dispatch.py`: Check, Build, Run and Tests each reached
  `Finished` (4/4). Restoring a saved workspace starts exactly one worker.
- `tests/wx_gui_tests_process.py`: the actual GUI Tests command ID 6020 launched
  a child `python.exe .../tests/run_tests.py` (PID observed via psutil) and reached
  `Finished`; this closes the distinction between status-only dispatch and a
  real test-runner process.
- Project-tree acceptance verified the 1,500-entry per-directory limit and the
  50,000-node global limit. Expanding 34 × 1,500 files reached 50,000 nodes at
  276.3 MiB process-tree RSS / 224.7 MiB USS. Directory-heavy expansion with
  35 × 1,500 child directories reached 49,999 nodes (directory placeholders are
  counted) at 88.5 MiB process-tree RSS.
- An earlier 319.5 s resource run retained 313 process-tree samples across
  32 tabs / 64 MiB, edit-history operations, the actual Tests task, and tab
  closure. Peak tree Working Set / Private Bytes were 257.2 / 191.1 MiB; peak
  GUI-only values were 192.2 / 169.9 MiB. Final post-close tree values were
  57.6 / 17.7 MiB. The sample is below 512 MiB but is not a worst-case bound.
- The latest sustained sample on current binaries lasted 319.9 s / 313 samples;
  peak process-tree Working Set / Private Bytes were 263.0 / 192.4 MiB, with
  final post-close values 57.9 / 16.2 MiB. Mean process-tree Working Set was
  205.5 MiB; mean private bytes 165.8 MiB; process-tree CPU delta 1.69 s.
  Current worker SHA-256:
  `05B144E3AF0FE31D2D7E9696DED305F0AA9E1B7443D6BA642F84EDDE504790AE`.

The recovered 2026-11-10 resource CSV contains 313 samples over ~321 seconds:
peak process-tree Working Set / Private Bytes were 261.8 / 192.2 MiB and UI ping
p95/p99/max was 0.1 / 0.3 / 106.9 ms. Memory and responsiveness thresholds passed,
but the requested two-hour duration was not completed. The acceptance script has
since been changed to reject early exit/short duration and to record handles plus
GDI/USER objects; a corrected multi-hour run remains pending.

The corrected script's 60-second smoke (`wx-resource-soak-20261110-080424.csv`)
completed 80 samples over 60.5 seconds: peak Working Set / Private Bytes 265.2 /
193.4 MiB, 1,218 handles, 355 GDI objects, 110 USER objects, and UI ping
p95/p99/max 0.1 / 0.4 / 109.7 ms. `DURATION=PASS`; this validates the guard and
resource counters, not the multi-hour leak criterion.

Filesystem follow-up: Linux now preserves owner/group, mode, explicit access ACLs
and supported xattrs, removes inherited destination-only access ACLs, rejects
FIFO substitutions without blocking, and brackets recovery-link cleanup with
directory fsync. Windows Save warns before breaking a multiply hard-linked file;
the GUI round-trip covers both cancel and confirm paths. When an existing
destination cannot get a verified new-content recovery hard link, Windows Save
now aborts before `ReplaceFileW` and preserves the original plus temporary data.
Windows `ReplaceFileW`
preserves DACLs by documented behavior, but owner/group/SACL preservation is not
verified. POSIX Save now pins one parent directory FD and uses descriptor-relative
  temp creation, recovery/backup links, replacement and cleanup; three directory
  fsync stages bracket the transaction. On Linux filesystems supporting
  `renameat2(RENAME_EXCHANGE)`, Save atomically exchanges the new temp name with
  the destination, verifies the displaced identity/content, and rolls back the
  exchange when the observed leaf is not the checked original. The displaced
  object remains recoverable until validation/cleanup completes. Filesystems
  without exchange support (notably DrvFS) retain a narrow leaf
  check-to-`renameat` race and now show a compatibility warning. DrvFS can also
  reject relative operations through a pinned directory FD after the parent
  directory itself is renamed; these operations now report a nonzero I/O error
  and fail closed rather than following the replacement symlink. Normal
  descriptor-relative saves on a stable parent remain separate from this
  adversarial rename case. Windows still
  has a path-based leaf check-to-`ReplaceFileW` interval; the backup path and
  identity validation preserve a displaced object on mismatch but do not
  provide compare-and-swap. Before replacement, Windows now creates a separate
  hard-link backup of the original destination and revalidates the destination,
  temporary file, original backup and new-content recovery link immediately
  before `ReplaceFileW`. Its separate displaced-file path preserves whatever
  actually occupied the destination at replacement time. Recovery cleanup opens each artifact with
  `FILE_FLAG_OPEN_REPARSE_POINT`, rechecks its file ID on the handle, and marks
  that opened file for deletion by handle rather than calling `DeleteFileW` on
  a re-resolved name. The new-content recovery link is removed before the
  original-content backup, so a cleanup failure retains the old file copy.
  Recovery hard-links are not immutable snapshots
  against concurrent in-place writes. Find in Files and Project Tree also use
  pinned-root traversal and identity revalidation.

Other remaining acceptance gaps are multi-DPI visual acceptance (only 100%/96 DPI
was available in this session), longer continuous compiler/search stress, and
filesystem-error/permission edge cases for Find in Files. A LOW Windows cleanup
edge also remains if `GetFileInformationByHandle` fails immediately after
`CREATE_NEW`: identity-based cleanup may leave an orphan temporary file, but does
not redirect writes or overwrite user data. The 512 MiB target is supported by
measured workloads and explicit limits, but it is not a hard allocator quota or
a proof for every workload.

The Windows dependency build now writes the exact MSYS2 UCRT64 package versions
reported by `pacman -Q` into the generated `THIRD_PARTY_NOTICES.txt`. The tracked
inventory is [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). License texts
are copied. Twenty exact-version MSYS2 toolchain/wx source archives and signatures have been
collected and verified, and a local source-material archive has been assembled;
that large MSYS2 archive is not attached to the runtime package and its source-
offer procedure has not been legally reviewed. The separate Rust worker source
bundle is included in newly built wx payloads and installed by the updated
installer; existing RC directories predate this change. Portable binary
redistribution remains blocked pending the MSYS2 source/legal review.

The Scintilla/SciTE copyright and permission notice is now sourced from the exact
wxWidgets 3.2.11 archive and copied into Windows portable and Linux build/install
license directories. The source-material distribution/legally reviewed written
offer, residual Binutils security review, corrected multi-hour soak and DPI
acceptance remain release blockers. Existing-target POSIX saves now fail closed
when atomic name exchange is unsupported; the unsafe `renameat` compatibility
replacement has been removed. No commit, tag, push or release has been created.

## Release-candidate continuation — 2026-11-10

The wx frontend is now the primary target of the top-level CMD/VBS/SH launchers
and the Windows/Linux user-scope installers. The Rust/egui source remains for
rollback/reference; its separately built executable is not part of the wx
installer payload. The worker root protocol is split: initialization receives
the canonical application root, while each task receives its workspace root.
The worker resolves tool scripts from the installation and uses the workspace
for task working directory and editor snapshots. `tests/studio_worker_root_split.py`
passed; `ResolveApplicationRoot` now canonicalizes the executable path before
walking ancestors, with a POSIX symlink regression in CTest.

Launcher/install follow-up: `RBLStudio.sh` now resolves relative and absolute
symlink chains before setting `RBL_STUDIO_ROOT`; the WSL regression test covers
paths with spaces and argument preservation (1/1 PASS). The Linux installer now
stores canonical XDG data/config roots in its state file, validates them during
upgrade/uninstall, rejects symlinked integration directories/files, and
preserves the same MIME registration location if XDG environment variables
change. Isolated Ubuntu acceptance passed registered and `--no-register`
install/uninstall, XDG-root restoration after environment changes, symlink and
dangling-symlink refusal, and injected MIME-rename failure with prior-install
rollback. Windows `-NoRegister` install/uninstall passed in a path with spaces
and preserved an unowned user file. The registered Windows install/upgrade/
uninstall acceptance now passes in a unique isolated
`HKCU:\Software\RBLStudioAcceptance\<GUID>\Classes` subtree; the real
`HKCU:\Software\Classes` hive is not touched. Injected native registry
import/rollback failures and custom ACL preservation remain untested. CMD/VBS
argument round-trip passed for spaces, trailing backslash, Unicode and
root/worker environment paths. The VBS launcher's exact `QuoteWindowsArgument`
function was additionally exercised against a native argv-capture stub with
empty strings, embedded quotes, trailing backslashes and Unicode; all values
round-tripped exactly. The CMD batch launcher forwards `%*` and is intended for
valid file-path arguments; arbitrary embedded-quote arguments are not supported
by `cmd.exe` batch expansion (Windows file names cannot contain a quote).

The pre-final Windows two-hour soak completed 7,201 seconds with 6,991 samples,
119 task cycles, 348 edit cycles, 267.9 MiB peak Working Set, 195.6 MiB peak
Private Bytes and 170.9 ms maximum UI ping; duration/resource/UI checks passed.
It loaded the earlier resource harness revision and is preliminary evidence
only. The harness has since been tightened
to verify all 32 Scintilla controls, assert actual append/undo/redo length
transitions, require successful Check/Build/Run/Tests completion, fail on
unfinished tasks, and use a UI-ping timeout longer than the 500 ms threshold.
A fresh Windows wx/worker build and the strengthened 300-second harness smoke
both pass. A strengthened 7,200-second run started at 10:52:12 on the fresh
Windows build and was still running at the 11:14 checkpoint; it is not a PASS
until normal exit and CSV/verdict inspection. Source changes made after that
binary was built require another rebuild and final soak.

RU/EN guides, top-level READMEs, this README, the engineering audit, resource
report and release manifest now identify wx as the primary frontend and keep
legacy egui/resource measurements clearly historical. The documentation checker reported 70 Markdown files, 489 relative links, zero missing targets and
zero bad fragments after the 2026-11-10 documentation reconciliation. The Quick Reference's Save All and Check shortcuts were
corrected to match the actual menu (`Ctrl+Shift+S` and `F7`).

The release remains **UNRELEASED / RELEASE BLOCKED** pending the final Windows
two-hour soak, final build/GUI acceptance for the latest worker/Save/DPI changes,
high-DPI visual checks, continuous stress, residual filesystem race disposition,
Binutils CVE mitigation and legal review of corresponding-source/source-offer
obligations. No commit, tag, push or release has been created.

### Acceptance follow-up — 2026-11-10, 11:31 local

- The deterministic registered Windows installer acceptance now passes in a
  unique `HKCU:\Software\RBLStudioAcceptance\<GUID>\Classes` root. It covers
  isolated registry export/import round-trip, foreign ProgID refusal, install,
  upgrade, manifest-root mismatch refusal, uninstall restoration of the prior
  association, and preservation of a user-changed default. The real
  `HKCU:\Software\Classes` hive is not used. The first live run exposed that
  `New-Item -Force` could clear existing Registry provider default values during
  upgrade; installation now creates `.rbl` and ProgID keys only when absent.
  Pure registry policy tests pass. This is acceptance of the normal transaction
  path; injected failures of native registry import/rollback and custom registry
  ACL preservation remain untested.
- The acceptance payload was staged from the current GUI/worker binaries with
  the exact Scintilla license notice. The existing `build/wx-ui-win` directory
  itself does not contain `licenses/Scintilla/License.txt`, so it is not a
  complete installer payload. The installer and acceptance harness now fail
  early when that required notice is missing. The next packaging build must use
  `tools/build_wx_ide.ps1` and verify the notice in its output directory; do not
  rebuild the directory while the active soak uses it.
- DPI static checks pass 12/12 and the app-root CTest passes 1/1. The AUI
  `dock_size(dir,layer,row)=size` parser validates/preserves the three topology
  integers and scales only the pixel-size value. No live 125–200% or
  multi-monitor display is available; high-DPI visual acceptance remains open.
- The strengthened 7,200-second Windows resource soak started at 10:52:12 on
  the current Windows GUI/worker binaries and was still active at this update.
  No final duration/resource/UI PASS is claimed until normal exit and CSV
  inspection. Keep the process and its build directory untouched.
- The 330,067,308-byte corresponding-source archive exists locally; its SHA-256
  was rechecked as
  `6bc84404a077bcc438bafd0b6add5436bb8d1058f339aa5d1ebd988529c7fa57`. It is not
  attached to a distributable package, and no written offer has been legally
  reviewed. Binary redistribution remains blocked.
- The exact Binutils 2.47-4 source review confirms `cache_bwrite` reachability
  during ordinary PE output linking through BFD's generic foreign-section
  write path. The default RBL object-input path is narrower, but the CVE's
  vulnerable write-size condition has not been ruled out. CVE-2026-90801 remains
  unresolved and release-blocking.

The release remains **UNRELEASED / RELEASE BLOCKED**. Final packaging rebuild,
multi-monitor DPI visual acceptance, worker shutdown C++ GUI build/acceptance,
final soak verdict and legal/security gates are still open. No commit, tag, push
or release has been created.

### Source integration follow-up — 2026-11-10, 12:00 local

- POSIX Save uses `renameat2(RENAME_EXCHANGE)` for existing destinations on
  filesystems that support it. The new temp name and destination are exchanged
  atomically; the displaced identity, link count and bytes are checked against
  the original snapshot. A mismatch triggers a guarded exchange-back attempt and
  retains recovery artifacts if rollback cannot be verified. Filesystems that
  do not support atomic exchange (including DrvFS) now fail closed for existing
  destination replacement; the unconditional `renameat` fallback was removed
  because it could discard a third-party leaf racing into the destination.
  First saves to absent destinations still use exclusive `linkat` creation.
- The comprehensive root `tests/posix_file_metadata.cpp` regression is now the
  C++ metadata test source used by Linux CTest (built with `-Werror`); the older
  minimal duplicate under `native/wx-ui/tests` is no longer the registered
  test. CTest also registers the Save security and worker lifecycle static
  checks; Windows CTest registers the pure registry policy test when
  PowerShell is available.
- The Windows package inventory explicitly includes the `libstdc++` split
  package (mapped to the GCC 16.2.0-4 source archive) and copies both
  `libwinpthread` and `winpthreads` license directories. The current RC bundle
  still needs regeneration with the updated notices after the active soak.
- Registered Windows acceptance now also covers Start Menu junction/reparse
  refusal on both install and uninstall, plus a deterministic late COM shortcut
  failure after registry writes. The late failure rolls back the isolated ProgID
  and copied installation while preserving the user-owned shortcut directory.
  The full acceptance passed again in a fresh GUID registry subtree.
- Worker shutdown now drains stdout/stderr during the five-second GUI close
  grace period, then uses `wxSIGKILL` with child cleanup. Restart has a bounded
  hard-kill fallback. After two seconds of stdout backpressure the worker drops
  bulk output frames but retains ordered control frames, allowing `Finished`
  then `bye` to arrive if the parent resumes reading. The Windows worker target
  passed 25/25 Rust tests and runtime shutdown/backpressure/stdin/EOF/smoke
  integration. A fresh GUI build is still required to compile and accept the
  changed C++ lifecycle code.
- Worker task subprocesses use a Windows Job Object with `KILL_ON_JOB_CLOSE`
  and a dedicated POSIX process group. POSIX cancellation terminates descendants
  that remain in that group; a descendant that deliberately detaches with
  `setsid()` or creates another process group is outside this guarantee (no
  cgroup/subreaper containment is installed). The bundled compiler/toolchain
  children do not intentionally detach. If the parent stops reading stdout
  permanently, the GUI's bounded shutdown deadline and hard-kill fallback are
  still required.
- Windows Save cleanup's handle semantics are now covered by
  `tests/test_windows_save_cleanup.ps1`: deleting one hard-link name preserves
  the original name, and a path replaced after the cleanup handle is opened is
  left untouched. The runtime PowerShell test passed. CTest and `python rbl.py
  test` now include this regression on Windows.
- DPI static checks now pass 12/12. Project-tree glyphs, Output/Problems image
  lists and toolbar bundles are regenerated at destination DIP sizes; the
  fallback app icon and Settings/Console spacing are also DPI-scaled. Live
  125–200% and multi-monitor visual acceptance remains unavailable on the
  current 96-DPI display.
- The strengthened Windows soak was still active at 12:34:02 with the original
  10:52:12 PID set. Its final CSV/verdict has not been written; keep the process
  and `build/wx-ui-win` untouched until normal completion.

The release remains **UNRELEASED / RELEASE BLOCKED**. The final soak, fresh
Windows/Linux builds, GUI acceptance, high-DPI visual test, source-offer/legal
review and Binutils CVE-2026-90801 mitigation remain required.



## Documentation and licensing audit — 2026-11-10

The older 10:52/12:34 soak statements above are historical checkpoints, not the
current process state. The second strengthened 7,200-second attempt ended early
after task #45 without a CSV. The next 480-second run using the strengthened
harness exited 1 after the initial Run Tests dispatch; its log contains only the
startup lines and no CSV/verdict. No current-RC long-duration soak PASS is
claimed. The preliminary 7,201-second run used the earlier harness and remains
preliminary evidence only.

The Rust worker license inventory now follows the active default-feature Cargo
graph: 24 Windows crates, 26 Linux crates, 28 unique packages. The legacy egui
binary is opt-in through legacy-egui and its eframe/rfd dependencies are not part
of the wx worker build. Both build scripts copy the complete corresponding-source
bundle (worker source, manifests and 28 crate archives) to
source/Rust-worker-source.tar.zst; installers verify its SHA-256 and retain it
under the installed application root. Bundle: build/source-material/RBL-0.8.0-Rust-Worker-Source-Materials-20261110-1955.tar.zst
(8,099,357 bytes; SHA-256 df5881aa72dc556d9fff6217fd436cec9ad9cb345ac899058a12d23bbdf40da5).
The MPL-2.0 option-ext source is included. This does not complete the separate
MSYS2 source-distribution/legal review. Existing RC output directories predate
this payload change and must be rebuilt before they pass the updated installer checks.

Current distribution blockers remain: corresponding-source delivery/legal
review, CVE-2026-90801, live high-DPI/multi-monitor acceptance, interactive Linux
GUI acceptance and a successful current-RC 7,200-second soak.


## Worker control-reserve regression — 2026-11-10

The second-pass audit found that protocol requests could continue to be consumed
after process-event intake reached the output high-water mark, potentially using
the eight reserved control slots. The coordinator now gates both request_rx and
event_rx at the same high-water mark, allowing the bounded stdin reader to apply
backpressure before the reserve is consumed.

Validation on a fresh Linux worker build:
- cargo test --locked --offline --bin rbl-studio-worker: 27/27 PASS.
- tests/studio_worker_shutdown_order.py: 385 output events, Finished before
  bye, exit 0.
- tests/studio_worker_control_reserve.py: 10,000 protocol_error responses
  retained while stdout was stalled; bye was the final event; exit 0.

A permanently blocked stdout consumer still triggers the bounded writer
abandonment policy (100 ms send / 250 ms completion wait). This is a deliberate
transport-failure limit, not a normal-path FIFO ordering failure.
