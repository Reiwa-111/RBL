# last verified: v0.8.0 | 2026.11.10 | IDE ENGINEERING AUDIT

# RBL Studio 0.8.0 — engineering audit and readiness status

Record date: 2026-11-10. This is an engineering status report, not authorization to publish.

## Overall status

**IDE 0.8.0: NOT RELEASE-READY / UNRELEASED.** The primary launcher/install path selects the C++20/wxWidgets 3.2 + Scintilla frontend with a separate Rust worker. Windows SaveDocument byte-roundtrip acceptance is 27/27 PASS on the current GUI candidate. The preliminary 480-second resource run passed resource/UI thresholds but used the earlier harness and did not verify graceful shutdown. The second 7,200-second attempt failed early after task #45 without a CSV. A subsequent strengthened 480-second run exited 1 after the initial Run Tests dispatch; its retained log contains only startup output and no CSV/verdict. No current-RC two-hour soak PASS is claimed. Windows registered install/upgrade/uninstall acceptance passes in an isolated HKCU GUID subtree. DPI static checks pass 12/12, but live 125-200% multi-monitor and interactive Linux GUI acceptance remain unverified. MSYS2 corresponding-source/legal review, rebuilding RCs with the Rust source payload, and Binutils CVE-2026-90801 remain release blockers.

This report deliberately separates evidence from assumptions. A successful compile, unit-test suite or `--self-test` does not prove real GUI behavior or whole-application memory compliance under load.

## Architecture

- The primary UI is C++20 + wxWidgets 3.2 + `wxStyledTextCtrl`/Scintilla in `native/wx-ui/`, with a separate Rust `rbl-studio-worker` process.
- The previous Rust + `eframe/egui` UI remains in `src/app.rs` and `src/editor.rs` as rollback/reference and is selected only by `RBL_STUDIO_ALLOW_LEGACY=1`.
- The Python/Tkinter GUI was removed. `tools/rbltool.py` remains the compiler/toolchain backend driver and does not render the UI.
- Earlier Cargo dependency scans found no Wry/WebView/CEF/Chromium/Electron/WebKit engine packages. External links may be opened in the system browser; that does not embed a browser engine.
- The IDE edits ordinary text files regardless of extension. Compiler operations are restricted to `.rbl` and checked again at the backend boundary.
- Project traversal/search, process output, tabs and editor history have software limits. These are not a hard allocator/OS quota for the entire process.

### Initial native frontend transition snapshot (2026-11-09; historical)

At this initial snapshot, the wx frontend was still parallel to the Cargo/egui application. It is now the primary launcher/install target; the previous Rust/egui UI remains available for rollback and comparison. The wx frontend provides Run/Build/Check/ASM/Tests/Benchmarks/Doctor, streamed stdin/EOF/cancel, tabs and a project tree, UTF-8/BOM and UTF-16 LE/BE, find/replace, Find in Files, a Problems list with source navigation, editor settings, a command palette and quick reference. Search traversal is bounded by entry/byte/result limits; the project tree does not follow symlinks. The worker uses a bounded NDJSON protocol and reuses `src/process.rs`.

Initial verification at that point: Windows UCRT64 and Linux/WSL CMake Release builds; worker unit tests (20 Windows / 21 Linux), Clippy `-D warnings`, and the interactive two-`input()` `unit_converter.rbl` smoke test on both platforms. One Windows idle sample measured ~30.5 MiB GUI Working Set + ~9.8 MiB worker Working Set. These are historical baseline figures, not the final acceptance evidence; current tests and remaining gaps are recorded below. See [`native/wx-ui/README.md`](../../../native/wx-ui/README.md) for implementation and build details.

### Legacy egui frontend architecture review (historical)

The layout follows egui's panel model: top/bottom panels are declared before the side panel, and `CentralPanel` is last. The Project pane uses a bounded resizable `SidePanel`; a docking framework is not justified for the current single-project/single-editor/one-output layout and would add state/dependency surface without solving the reported defects.

The editor review compared [Zed's DisplayMap](https://zed.dev/blog/zed-decoded-rope-sumtree), which centralizes folds, wrapping and other display transforms, with [Helix's architecture](https://github.com/helix-editor/helix/blob/master/docs/architecture.md), which separates a rope-backed core, selections/transactions/history and a view layer. [Lapce](https://docs.lapce.dev/) was also reviewed as a native Rust/GPU editor reference. These designs show why reliable editable folding needs an explicit source-to-display coordinate map, not a temporary string projection. RBL Studio keeps its existing bounded text buffer and document-wide undo model; a full Rope/DisplayMap migration is a separate architectural project, not a safe cosmetic patch. No browser engine or heavyweight editor dependency was added.

## Legacy egui frontend baseline — historical measurements

| Area | Current source/test status |
|---|---|
| Text documents and tabs | Implemented; tests cover encodings, BOM, line endings and budgets |
| UTF-8 / UTF-16 LE/BE | Implemented; ANSI and binary content are not auto-decoded |
| Large documents | Virtualized editor windows; 16 MiB file limit |
| Undo/Redo and Replace All | Document history and a dedicated Replace All swap; UTF-8 boundary/history tests |
| `.rbl` compiler gating | Implemented and tested |
| Processes | Background execution, bounded streaming output, cancellation and interactive Run stdin; Windows children start suspended and are assigned to a Job Object before their first instruction |
| Project tree/search | Bounded background traversal, coalesced refresh, surfaced filesystem errors and search across the discovered file set; large real-world tree UX still needs acceptance |
| Panel geometry | Project is user-resizable within 190–360 egui points (245 default); Output is capped at 45% and sits above the bottom-most status strip; manual GUI acceptance is still required |
| Input and scrolling | Enter is preserved as egui's native Key event; the editor fills its viewport inside a bidirectional ScrollArea; first-focus same-frame paste remains subject to document limits |
| Run stdin | Dedicated auto-focused STDIN field; lines are forwarded to the child process, `print()`/`input(prompt)` flush stdout, and EOF closes child stdin; process-level integration test runs the unit converter through two prompts |
| Gutter and code structure | Gutter matches the editor background and has a divider; RBL guides and folding share matched block-parenthesis ranges; plain text uses bounded indentation spans |
| Editor interactions | Dialog text fields retain keyboard input; Save As prevents duplicate open owners; Replace All restores its mapped caret position |
| Editor preferences | Persistent syntax-highlighting and soft-wrap controls, plus existing theme/font/tab/line-number settings |
| Native GUI | Implemented; unit/self-tests do not replace interactive GUI acceptance |

## Resources: historical egui measurements (not wx evidence)

Exact values, hashes, methodology and limitations are recorded in the [resource report](../../../benchmarks/IDE_RESOURCE_USAGE.md). Existing accepted observations:

| Platform / workload | IDE memory | Other metrics | Status |
|---|---:|---|---|
| Windows idle | Working Set avg 131.7 / peak 131.9 MiB | Private Bytes avg 196.3 MiB; CPU 0.00% at one-second sampling | Measured |
| Windows, one 16 MiB TXT | Working Set avg 172.5 / peak 172.8 MiB | Private Bytes avg 235.7 MiB | Measured |
| Windows, one 16 MiB RBL | Working Set avg 172.9 / peak 173.2 MiB | Private Bytes avg 236.2 MiB | Measured |
| WSL idle | RSS avg/peak 131.3 MiB | VmData avg 274.5 MiB | Measured |
| WSL, one 16 MiB TXT | RSS avg/peak 176.8 MiB | VmData avg 322.4 MiB | Measured |
| WSL, one 16 MiB RBL | RSS avg/peak 174.6 MiB | VmData avg 321.5 MiB | Measured |
| 32 tabs (32 × 1 MiB documents) | Working Set peak 194.7 MiB | IDE + descendants Private Bytes peak 254.6 MiB; one process | Measured (memory only) |
| Sustained editing, Find/Replace/Undo, active compiler + descendants | — | No accepted interactive/process-tree profile | Not measured |

One-second sampling can miss short-lived peaks. Windows Working Set/Private Bytes and Linux RSS/VmData/PSS are different metrics. Do not sum per-process RSS as an exact physical footprint when shared pages exist; PSS is preferable for Linux process-tree accounting when available. Windows reporting must distinguish the IDE, descendants and aggregate process-tree estimate.

These figures describe the older egui frontend only. They are not evidence for
the primary wx frontend. The 64 MiB editor-buffer budget is not evidence of a
whole-process limit; wx acceptance is recorded separately below.

## Independent audits

Independent read-only reviews covered layout/panel ordering, editor/folding/scrolling, and process/stdin lifecycle. The follow-up corrected the bottom-panel order (status is now the bottom-most strip), made the Project panel bounded-resizable, keyed the editor scroll state by document page, replaced the faulty guide-span calculation, and added an explicit stdin EOF command. A process-level test now runs `examples/utilities/unit_converter.rbl`, waits for both flushed prompts, sends `1` and `10`, and checks the formatted conversion result. EOF has a separate child-process test. Earlier confirmed issues in shortcut routing, duplicate Save As ownership, Replace All caret restoration, asynchronous project traversal/search bounds, filesystem-error reporting, stream-read status and Windows Job Object launch ordering remain covered by regression tests.

Known editor limitation: folding still uses a bounded lexical scanner rather than the compiler AST. While any fold is active, the projected view is read-only; clicking the code area expands the current page before editing. This avoids corrupting source offsets, but it is not yet equivalent to an editor with a source-aware display map. The 30 px visual strip reported by the user is consistent with the reserved 18-point RBL fold gutter when line numbers are disabled; its exact on-screen location still needs GUI confirmation.

Each finding must include file/line, reproducible evidence, severity, minimal fix and retest result. Assumptions and UX suggestions must not be labelled confirmed defects.

## Verification and exit criteria

Release readiness requires:

1. Completed independent audits and disposition of confirmed blocker/high findings.
2. Text-format verification: unknown extensions, UTF-8/UTF-16, BOM, LF/CRLF/CR, invalid encoding, NUL/binary rejection, Save/Save As and `.rbl`-only commands.
3. Verified interactive GUI automation (or a manual test record) for tabs, editing, find/replace, undo/redo, save, process output and cancellation.
4. Resource matrix: idle, ordinary document, 16 MiB, 32 tabs, sustained multi-document editing, mass Replace/Undo, active compiler/tests; IDE, descendants and aggregate memory; average, peak, sampling interval, duration, repetitions and CPU.
5. Full Windows GNU and Linux/WSL verification after the last change: fmt, tests, Clippy `-D warnings`, release build, version/self-test, launcher/bootstrap without a prebuilt binary, runtime dependencies and real GUI startup.
6. `git diff --check`, documentation links, no browser engines and no stale references to the removed Python GUI.

Until these criteria are met, status remains **NOT RELEASE-READY**. Commit, push, tag and publication require separate owner authorization.

## Related documents

- [RBL Studio guide](../how-to/ide.md)
- [Memory and CPU measurements](../../../benchmarks/IDE_RESOURCE_USAGE.md)
- [Release notes](../release-notes.md)
- [Manifest](../../../RELEASE_MANIFEST.txt)

## Native wxWidgets IDE — primary frontend (2026-11-09 onward)

This section applies to the new `native/wx-ui/` implementation using wxWidgets
3.2, `wxStyledTextCtrl`/Scintilla and the Rust worker. The resource tables and
interactive checks above describe the older egui frontend; they are not
measurements or readiness evidence for the wx candidate.

Latest confirmed verification:

- Windows UCRT64/MinGW and Linux/WSL CMake builds passed.
- Windows standalone startup passed with PATH restricted to Windows system
  directories; the adjacent Rust worker started and connected. Windows worker
  interactive smoke passed.
- Linux/WSL worker: 23/23 tests, Clippy `-D warnings`, `cargo fmt -- --check`,
  stdin/EOF, output backpressure and shutdown-order integration passed.
- Windows GNU worker: 22/22 tests, Clippy `-D warnings`, formatting, stdin/EOF,
  output backpressure and shutdown-order integration passed.
- The full Windows `python rbl.py test` suite passed: 61 direct ASM regression
  tests, 63 diagnostics tests, Runtime ABI, SysV call alignment, GC pressure,
  stdin and streaming checks.
- GUI checks confirmed recovery from an invalid AUI perspective, rendering of
  Project/editor/Output, creation of 32 blank tabs, and the `Tab limit reached
  (32)` message on further creation attempts.
- One post-load sample with the project tree expanded measured about 86.9 MiB
  GUI Working Set and 10.0 MiB worker Working Set. No compiler child was active;
  this is not a process-tree peak profile.

The Windows Rust worker suite is now freshly verified through the dedicated
GNU toolchain (22/22); the earlier MSVC-profile relink failure is historical.
Remaining wx-candidate acceptance items are multi-DPI visual checks above 100%,
multi-hour leak observation, continuous compiler/search stress, large diagnostic
lists, filesystem permission/error edge cases and the final cross-platform
check-to-replace TOCTOU interval. The 512 MiB target passed the measured
workloads, including the current 200.5 s sample, 10,000 search results and
directory-heavy tree expansion, but is not a hard allocator quota or proof for
every possible workload. The wx frontend is now selected by the repository
launchers and user-scope installers. This does not mean release acceptance is
complete. No commit, push, tag or release was made.

The latest visual pass adds a compact File/Run/Tools action toolbar, project
tree and Console/Problems/Search icons, a distinct RBL file glyph, a custom
window icon, hand-drawn folder/file glyphs, a consistent dark palette,
active-tab styling, workspace/task/caret status fields, and recovery for older
AUI layouts that predate the toolbar. PyCharm Community Edition 2024.3 was
launched locally with this repository and captured as a visual reference
(`build/wx-ui-win/reference-pycharm.png`). The latest wx window was captured as
`build/wx-ui-win/ide-final-visual.png` and inspected directly.

Direct GUI acceptance now includes opening `examples/utilities/unit_converter.rbl`
through the Open dialog, starting Run from the toolbar, sending `1` and `10`
through the GUI input field, and observing `10.000 km = 6.214 mi` followed by
exit code 0 and Finished status (`build/wx-ui-win/ide-run-complete.png`). Check
was also invoked through the GUI; the Problems panel remained empty for this
valid program (`build/wx-ui-win/ide-check-pass.png`). The console's text colour
and toolbar palette were corrected after reviewing the first live screenshot.
Windows and Linux builds passed after the icon and theme changes.

## wx frontend follow-up — 2026-11-10

The wxWidgets frontend is the primary launcher/install path; the Cargo/egui
frontend and its source have not been removed and remain an explicit legacy
fallback. The latest follow-up source changes:

- Selects Scintilla's inserted-line EOL mode from the first CRLF/CR/LF found in
  the loaded source before `SetText`, and accepts CR-only newlines in the
  character-added indentation handler. Windows UCRT64 and WSL CMake builds
  passed after these two changes. Exact-byte GUI round-trip acceptance remains
  open; no claim is made that all encoding/EOL cases have passed.
- Cancels Find in Files and defers final frame close until the search completion
  event is processed, avoiding the normal close path's unbounded destructor
  join. A real close-during-search GUI test is still required; a filesystem call
  blocked by the OS can still delay worker completion.
- Windows `python rbl.py test` exited 0: 61 direct ASM cases, 63 diagnostics
  cases, Runtime ABI/call alignment, GC/string I/O pressure, streaming and
  toolchain/build-cache checks. Two developer-only native C probes skipped due
  to the intentionally minimal Windows bundle lacking GCC `cc1`/headers.
- Windows corpus check exited 0 (121 candidates, 114 positive assembled, 7
  expected rejects). Documentation check exited 0 (66 Markdown, 474 links,
  no missing targets/fragments). `git diff --check` exited 0.
- WSL rebuilt the wx CMake target after the close-during-search patch; after the
  final worker stream-reader change, `cargo fmt -- --check`, 23/23 worker tests,
  Clippy `-D warnings`, stdin/EOF smoke and backpressure integration passed.
- Both Windows UCRT64 and Ubuntu 24.04/WSL wx CMake targets were rebuilt after
  the final Save revalidation patch.
- `tools/build_wx_ide.ps1` now pins GCC's assembler/linker search to the
  repository's signature-verified Binutils 2.47-4 through `COMPILER_PATH` and
  fails closed if `gcc -print-prog-name` resolves either tool elsewhere. Each
  build directory gets a separate Cargo target directory so the worker is
  relinked under that environment. A standalone GCC compile/link smoke passed.
- Windows GNU worker build and 22/22 unit tests passed; Windows stdin/EOF smoke
  and backpressure integration passed against the rebuilt worker.

The native Windows Save/Save As automation attempted during this follow-up did
not reliably open a source file through the actual file dialog. It did not
prove that Save or Save As modified a file. Treat it as inconclusive, not a
pass. External-change/delete/conflict prompts, dirty-close behavior, exact-byte
UTF-8/BOM/UTF-16 and EOL round-trips, malformed surrogate rejection, diagnostic
navigation in unsaved buffers, Find in Files cancellation/responsiveness,
settings migration/persistence, sustained memory/CPU sampling and DPI scaling
remain open acceptance criteria.

The later real GUI round-trip test `python tests/wx_gui_file_roundtrip.py`
passed 21/21. It exercised Save and Save As for new/existing documents; exact
bytes for UTF-8 LF/CRLF/CR, UTF-8 BOM, UTF-16 LE/BE, mixed EOL, Cyrillic/non-BMP
content and Cyrillic filenames; edited-buffer Save; external modification and
deletion decisions; dirty tab/window close Cancel/No/Yes; and rejection of both
malformed surrogate fixtures without changing their source bytes.

Independent wx frontend review (read-only, worker-19) additionally identified:

- MEDIUM (mitigated, not eliminated): Save re-hashes the destination after the
  temporary file is flushed and closed, and prompts again if the file changed
  since the initial check. POSIX Save pins the parent directory and uses
  descriptor-relative operations throughout. On Linux filesystems supporting
  renameat2(RENAME_EXCHANGE), it atomically swaps the new temp name with the
  destination, verifies the displaced identity and bytes, and conditionally
  exchanges back if the displaced leaf is not the checked original. Filesystems
  without exchange support (notably DrvFS) fail closed for existing-target saves
  with operation_not_supported; the unsafe unconditional renameat fallback was
  removed. First-time saves still use exclusive linkat creation. DrvFS can also
  reject relative operations through a pinned directory FD after a parent
  directory rename; those operations fail closed rather than switching paths.
  Windows holds no-delete handles for the parent chain, but the destination leaf
  can still change before path-based ReplaceFileW; its backup path and identity
  validation preserve the displaced object on mismatch but do not provide
  compare-and-swap. Recovery artifacts are reopened with reparse-point handles,
  checked by file ID, and cleaned by handle rather than re-resolving a pathname.
- POSIX metadata: `fchown` preserves owner/group and `fchmod` preserves mode bits
  including special bits. Linux copies access ACLs and supported xattrs; inherited
  destination-only access ACLs are removed. `security.*`/`trusted.*` labels are
  intentionally not copied and trigger a warning; non-Linux POSIX platforms warn
  that ACL/xattr preservation is unavailable. Windows `ReplaceFileW` preserves
  the existing DACL and named streams by documented behavior, but owner/group and
  SACL preservation are not verified.
- Atomic replacement breaks a multiply hard-linked destination's link set: only
  the selected pathname receives the new file. Save now warns and defaults to No;
  other hard-link names retain the previous bytes if the user confirms.
- Temp-file creation is exclusive (`CREATE_NEW` on Windows, `O_EXCL` on POSIX),
  not the old non-exclusive `ofstream` mode. POSIX file opens use `O_NONBLOCK` so
  a raced FIFO cannot block the GUI before regular-file validation.
- POSIX save fsyncs the same pinned directory descriptor before replacement
  (making recovery links durable), after replacement and before cleanup, and
  again after cleanup. A failed pre-replacement sync aborts the save; a failed
  post-replacement sync retains recovery links. Recovery hard-links are not
  immutable snapshots against concurrent in-place writes.
- Undo-budget accounting sums edit-event lengths rather than measuring the
  editor's actual retained undo allocation; it clears the undo buffers at a
  conservative threshold but is not an exact allocation cap.
- POSIX Find in Files traversal is rooted at a pinned directory FD and uses
  `fdopendir`/`readdir`, `fstatat(AT_SYMLINK_NOFOLLOW)` and `openat`; file identities
  are carried into search results and checked again on activation. Focused security
  tests pass 4/4. POSIX Project Tree uses one shared root FD, relative component
  walks and expected identities (12/12 tests); Save dirfd tests pass 4/4. The
  POSIX metadata harness passes all 16 named scenarios.
- Diagnostics parsing accepts only worker events marked as stderr. Worker exit
  without a `finished` event finalizes the received diagnostic stream; the
  10,000-row GUI/crash acceptance is pending on the rebuilt Windows binary.

The search-close change above mitigates the ordinary window-close join path,
but does not prove cancellation can interrupt a blocking OS filesystem read.

The latest independent worker/backend read-only audit (worker-17) found the
output high-water pause/resume, bounded writer batching, control-event flush,
snapshot cleanup and Finished ordering coherent in source. The final pending
UTF-8 fragment's ignored send failure was fixed in `src/process.rs` and covered
by `output_reader_reports_disconnected_receiver_for_final_utf8_fragment`.
Current WSL worker tests pass 23/23; Windows GNU worker tests pass 22/22 (one
platform-specific test is absent). Windows and WSL stdin/EOF smoke and 6.1 MB
backpressure integration tests pass.

Remaining backend lifecycle risks from that audit:

- MEDIUM Unix: an abrupt worker panic/crash/SIGKILL bypasses normal process-group
  cleanup and can orphan the direct child; no Linux parent-death signal or
  process-group RAII guard is installed.
- LOW/MEDIUM Unix: a descendant that calls `setsid()` while retaining inherited
  stdout/stderr can escape the original process group. Reader-thread joins may
  then remain blocked and delay `Finished`/shutdown.
- LOW protocol semantics: duplicate `stdin_eof` and valid `cancel` requests are
  fire-and-forget rather than acknowledged individually; this should remain
  explicit in the protocol contract.

The first worker-20 report described a stale coordinator snapshot. Re-reading
current src/bin/rbl-studio-worker.rs and exercising a fresh Linux build confirmed
that the normal shutdown path drains pending output and enqueues bye on the same
FIFO channel; the shutdown-order test passed (385 output events, Finished before
bye, exit 0).

A second-pass review found a separate P2 control-event loss edge: process-event
intake paused at the output high-water mark, but protocol-request intake remained
active and could consume the eight reserved control slots. The coordinator now
applies the same high-water gate to request_rx and event_rx, propagating bounded
backpressure to stdin before the reserve can be consumed. A focused unit test
covers the gate, all 27 current worker unit tests pass, and the new
10,000-frame stdout-stall integration test retained every protocol_error response
and emitted bye last (exit 0).

The bounded writer still cannot guarantee delivery if the parent permanently
stops reading stdout: after its 100 ms send and 250 ms completion deadlines it
uses the documented abandonment path. This is a bounded transport-failure
policy, not a normal-path FIFO ordering defect. Other backend caveats remain the
Unix abrupt-worker process-group cleanup gap, descendants that escape via
setsid(), fire-and-forget stdin_eof/cancel semantics, and a very small post-reap
PGID reuse race.

Resource figures for wx are recorded separately in
[`benchmarks/IDE_RESOURCE_USAGE.md`](../../../benchmarks/IDE_RESOURCE_USAGE.md).
The earlier 200.4 s sample is historical. A later 319.5 s run on GUI hash
`3735FC7C08608E464DD0313C586BE82E7C39F0625AA6826A66C1E2E61F501AE2` opened 32
documents / 64 MiB, exercised Scintilla append/undo/redo, dispatched the actual
Tests task and closed all tabs. Across 313 process-tree samples, peak tree
working set/private bytes were 257.2/191.1 MiB; peak GUI-only values were
192.2/169.9 MiB. Final post-close tree values were 57.6/17.7 MiB. The measured
workload stayed below 512 MiB, but the task's child processes were active only
briefly; this is not continuous compiler stress or a worst-case allocator bound.

The latest rebuilt GUI/worker hashes were
`F84AEE828DD9E9F5A782C46623BF6B32E21FDBF06CA9D88FE763DA6F7E5665CB` and
`05B144E3AF0FE31D2D7E9696DED305F0AA9E1B7443D6BA642F84EDDE504790AE`. A sustained
319.9 s sample on those binaries retained 313 process-tree samples: peak Working
Set / Private Bytes were 263.0 / 192.4 MiB, mean Working Set 205.5 MiB, and
post-close values 57.9 / 16.2 MiB.

A separate project-tree run expanded 34 × 1,500 files to 50,000 nodes without
exceeding the global limit. Peak process-tree memory during that run was 276.3
MiB RSS / 224.7 MiB USS. A directory-heavy workload with 35 × 1,500 child
directories reached 49,999 nodes; directory placeholders are now counted as
real nodes. Peak process-tree RSS was 88.5 MiB. The single-directory 1,500-entry
limit was separately checked with 1,501 entries.

Legacy `settings.json` numeric migration no longer uses exception-throwing
`std::stof`/`std::stoi` on untrusted oversized values; integer prefixes are
parsed with `std::from_chars` and out-of-range values leave safe defaults. The
isolated-profile GUI test now passes 4/4 for valid, oversized, negative and
malformed legacy settings, and verifies the persisted values through the actual
Editor Settings dialog after a second GUI launch. `RBL_STUDIO_CONFIG_DIR` now
provides an explicit per-application profile override for portable and isolated
acceptance runs.

### GUI acceptance follow-up — 2026-11-10

- File round-trip GUI suite: 21/21 PASS, including Save As for new and existing
  files, exact encoding/EOL bytes, external modification/deletion decisions and
  dirty tab/window prompts.
- File-size boundary GUI test: 2/2 PASS; exactly 16 MiB opens and 16 MiB + 1
  byte is rejected by the real warning dialog without closing the IDE.
- Diagnostic navigation: saved-file and unsaved-snapshot navigation passed;
  activating a stale diagnostic after closing its source tab did not crash or
  close the IDE.
- Find in Files: CR-only, CRLF and LF result positions passed 3/3. Cancellation,
  search re-entry and close-during-search passed real GUI tests. A 12,000-hit
  workload was capped at 10,000 and populated in 50-row timer batches. Latest
  isolated run: 3.72 s, 76.9 MiB process-tree RSS / 29.2 MiB USS, 92.0 ms maximum
  UI ping. A combined test batch under concurrent load once exceeded the 1 s
  timeout; isolated rerun passed.
- Worker GUI integration: Stop disables stdin/Send/EOF immediately and two
  consecutive runs cancel cleanly with exit 130. A line followed immediately by
  EOF reaches the child in FIFO order; the child persists the line before
  observing EOF.
- Worker shutdown: a saturated stdout pipe was drained without losing accepted
  events; `finished` preceded `bye` on Windows and WSL.
- Worker startup: restoring a saved workspace starts exactly one worker.
  Check, Build, Run and Tests each reached `Finished` through GUI command
  dispatch (4/4). A separate process-level test observed the actual
  `python.exe .../tests/run_tests.py` child from GUI Tests command ID 6020 and
  verified its successful `Finished` status.
- Project tree: 1,500 entries per directory and 50,000 nodes globally are
  enforced and dynamically tested, including directory placeholders. Find in Files remains bounded by 100,000
  entries, 128 MiB scanned, 2 MiB per file, 10,000 results and depth 32.
- Windows `python rbl.py test` exited 0 (61 direct ASM, 63 diagnostics plus
  runtime ABI, GC, I/O, streaming, manifest and cache checks); corpus validation
  passed 121 candidates / 114 positive / 7 expected negative. Documentation
  validation passed 66 Markdown / 474 links. WSL wx build and worker 23/23,
  Windows GNU worker 22/22, formatting, Clippy, stdin/EOF, backpressure and
  shutdown-order checks passed.

The 2026-11-10 follow-up closed several targeted acceptance items:

- Save/Save As round-trip: 27/27, including UTF-8/BOM/UTF-16, LF/CRLF/CR,
  read-only failure/retry, external deletion, overwrite confirmation and
  repeated external writes while a modal conflict prompt is open, plus the
  hard-link warning/cancel/confirm behavior.
- POSIX metadata harness: PASS for permission bits, owner/group, user xattrs,
  explicit ACL copy, removal of inherited default ACL, hard-link count, symlink
  refusal, FIFO rejection without blocking, static path substitution and
  same-directory backup/recovery hard-links. This is not a proof of a fully
  atomic compare-and-swap.
- Diagnostics: 10,000-row cap and sort order PASS; measured UI ping 14 ms and
  process-tree RSS 66.4 MiB in the synthetic scale test. Oversized-line discard,
  navigation and edit-before-first-output stale invalidation PASS.
- Find in Files: EOL 3/3, inaccessible-directory handling, depth bound,
  directory symlink/junction non-following and external-junction containment
  PASS. File-symlink creation was INCONCLUSIVE where host policy prevented it.
- Current Windows GNU Release and Linux/WSL Release builds completed with exit 0.

The recovered `wx-resource-soak-20261110-070042.csv` contains 313 samples over
approximately 321 seconds. Peak process-tree Working Set / Private Bytes were
261.8 / 192.2 MiB; UI ping p95/p99/max was 0.1 / 0.3 / 106.9 ms. The 512 MiB and
500 ms thresholds passed for that sample, but it did not run for the requested
two hours. The acceptance script has since been hardened to fail on early exit
or insufficient duration and to record process handles plus GDI/USER objects;
the corrected multi-hour rerun is pending.

The corrected script's 60-second smoke (`wx-resource-soak-20261110-080424.csv`)
completed with 80 samples over 60.5 seconds. Peak Working Set / Private Bytes
were 265.2 / 193.4 MiB; peak process-tree handles 1,218; peak GDI/USER objects
355 / 110; UI ping p95/p99/max 0.1 / 0.4 / 109.7 ms. `DURATION=PASS`; this validates
the new metrics and duration guard, not the multi-hour leak criterion.

Still NOT TESTED: 125/150/175/200% multi-monitor visual DPI acceptance, a
completed strengthened multi-hour GUI/worker leak soak, prolonged compiler/search
stress with real child processes, and interactive Linux GUI acceptance. The
second strengthened 7,200-second attempt failed early after task #45 without a
CSV. A subsequent strengthened 480-second run exited 1 after the initial Run
Tests dispatch and produced no CSV/verdict. No current-RC long-duration soak
PASS is available.

POSIX existing-target saves fail closed when RENAME_EXCHANGE is unavailable;
the unconditional renameat fallback is removed. First-time saves use exclusive
linkat. Windows retains a path-based ReplaceFileW leaf race and a LOW cleanup
edge that may leave an orphan temp if handle metadata retrieval fails directly
after exclusive creation. A fresh package build is required to include the new
Rust worker license payload and installer marker checks.

The 20 exact-version source archives for the compiler toolchain and wx/runtime
dependency closure have been collected and signature-verified. Upstream hashes
and Git source commits were checked; see
toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md. The Rust worker dependency
closure is separately inventoried as 28 exact active default-feature crates;
legacy eframe/rfd dependencies are opt-in and excluded from the worker build.
The complete worker source bundle is build/source-material/RBL-0.8.0-Rust-Worker-Source-Materials-20261110-1955.tar.zst
(8,099,357 bytes; SHA-256 df5881aa72dc556d9fff6217fd436cec9ad9cb345ac899058a12d23bbdf40da5).
Both build scripts now copy it into source/Rust-worker-source.tar.zst, and both
installers require, verify and retain it in the installed application tree.
Existing RC output directories predate this change and must be rebuilt. The
separate 330,067,308-byte MSYS2 toolchain/wx source bundle remains outside the
runtime package. No legally reviewed source-distribution procedure or written
offer has been completed; binary redistribution remains blocked.

The bundled Binutils package has been updated to signed MSYS2 2.47-4. The
shipped linker emits PE/COFF (i386pep/i386pe), and readelf is not included, but
the BFD input target vector also recognizes ELF objects. A source-level review
of the exact signed archive confirmed that `cache_bwrite` is reachable during
ordinary PE output writes through the COFF final-link foreign-section path. The
default RBL build links compiler-generated COFF objects and does not accept
arbitrary object inputs or pass `--gc-sections -w`; this narrows exposure but
does not prove the vulnerable write-size condition unreachable. CVE-2026-90801
remains unresolved and release-blocking, not a claimed upstream fix.

**Current status: ENGINEERING TESTS PASS / GUI ACCEPTANCE SUBSTANTIAL WITH
REMAINING GAPS / RELEASE BLOCKED / UNRELEASED.** No commit, push, tag or
publication was performed.
