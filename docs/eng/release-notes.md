# RBL 0.8.0 — release notes

Last verified: 2026-11-15

## RBL 0.8.0 — full release

RBL Studio 0.8.0 is the full 0.8 release of the RBL language, its native
compiler/runtime, and the wxWidgets IDE. The Windows x86-64 payload ships as a
ready-to-run checkout: the project root contains the GUI, the Rust worker,
app-local CPython and the assembler/linker runtime needed to build and run RBL
programs. Run `RBLStudio.cmd` (GUI) or `rbl.cmd` (CLI) directly from the project
root; no separate distribution archive is produced. The same sources build the
Linux x86-64 payload.

**Publication status:** the 0.8.0 checkout was published on 2026-11-15 as the
**`v0.8.0-r2` full release**
(`https://github.com/Reiwa-111/RBL/releases/tag/v0.8.0-r2`). No binary assets are
attached: the source checkout at that tag is the distributable payload. The
older GitHub `v0.8.0` entry published on 2026-11-10 remains the pre-release
placeholder with zero assets and still contains the damaged example; it was not
moved. The acceptance and legal gates that remain open are listed under
[Known limitations and publication gates](#known-limitations-and-publication-gates).

## Distribution layout

The 0.8.0 payload is the repository checkout root. It is a portable application
directory, not an installer database: there is no install manifest, uninstaller
or registry/XDG integration. No separate ZIP/tar.zst release archives are
produced. The earlier packaged archives (Windows 578 files / Linux 5,149 files)
were removed on 2026-11-15 when the distribution model changed; their hashes
remain in the historical audit and verification material.

### Included in the checkout

- `bin/win-x86_64/studio/rbl-studio-wx.exe` — native wxWidgets 3.2 + Scintilla
  IDE, together with the wxWidgets/MinGW runtime DLLs it loads.
- `bin/win-x86_64/studio/rbl-studio-worker.exe` — Rust compiler/tool backend.
- App-local CPython 3.14.10 under `runtime/python`, pruned to the modules the
  RBL backend and regression suite use (~9 MB; optional TLS/SQLite/zstd/decimal/
  XML extensions removed).
- The RBL compiler, runtime and CLI (`rbl`, `rbl.py`, `rbl.cmd`, `RBLStudio.cmd`,
  `RBLStudio.sh`, `RBLStudio.vbs`).
- The assembler/linker closure needed to compile user programs: GNU `as`/`ld`
  from the pinned MinGW-w64 UCRT64 bundle on Windows; Clang integrated assembler
  and LLD on Linux.
- Third-party notices under `THIRD_PARTY_NOTICES.txt` and per-component licences
  under `licenses/`, `runtime/python/licenses/` and `toolchain/*/`.

### Windows quick start

1. Open the repository checkout; there is no archive to extract.
2. Start `RBLStudio.cmd` from the project root (or
   `bin\win-x86_64\studio\rbl-studio-wx.exe` directly).
3. The launcher sets the application root; the GUI finds the worker next to its
   executable and app-local Python under `runtime/python`. No `RBL_*`
   environment variable, Python installation, MSYS2 or compiler is required.

### Linux quick start

The ready-made Linux archive was removed on 2026-11-15 when the distribution
model changed. Build the native payload from the same sources with
`tools/build_wx_ide.sh`, then run:

```bash
./RBLStudio.sh          # GUI
./rbl version           # CLI
```

The native Linux build was produced and audited on Ubuntu 24.04. Its current
ELF floor is GLIBC 2.39 / GLIBCXX 3.4.32 (see `LINUX_ABI_AUDIT.txt`); older
distributions need a rebuild against an older sysroot. The native shell package
uses the host GTK 3/display stack. The Flatpak path supplies GTK through
`org.gnome.Platform` 51.

## Added

- Bounded f-string format specifications with fill, alignment, minimum width,
  floating-point precision and explicit integer/float/string conversion types.
  The supported grammar and bounds are defined in the
  [syntax reference](reference/syntax.md).
- Source diagnostics with file, line, column, compiler-stage labels, source
  excerpts and caret positioning across lexing, parsing, semantic validation and
  code generation.
- Runtime-managed dynamic-string lifetime integrated with the conservative
  non-moving collector for strings and containers. Native C borrowing limits and
  collector boundaries are documented explicitly.
- Runtime ABI descriptor and compatibility checks, independently versioned from
  the RBL compiler/language version. The current Runtime ABI is `1.2.0`.
- A prebuilt wxWidgets IDE package with an app-local Python backend and a
  bundled native assembler/linker closure, so installed check/build/run/format
  actions work without a developer checkout or system Python.

## Changed

- Split the compiler implementation into focused C translation units with
  private module interfaces. Variable, function and string-pool lookups use hash
  indexes while preserving deterministic source ordering and function-resolution
  behavior.
- Added Runtime-owned dynamic strings. RBL programs must not manually free
  managed string payloads; see the
  [execution model](explanation/execution-model.md).
- Added compiler-emitted garbage-collection safe points and a non-allocating
  fast-loop path that omits redundant per-iteration safe points only when the
  compiler proves the loop cannot allocate or mutate pointer roots.
- Extended the build system to track compiler modules, private headers, ABI
  sources and the version dependency, with separate object files for incremental
  and parallel builds.
- Centralized the shared container Runtime C entry-point declarations in
  `runtime/rbl_abi.h` and enabled strict missing/redundant-prototype checks in
  Linux/Windows bootstrap paths.
- Added the accurately named `bench5way` CLI/Makefile target for the
  five-implementation comparison; retained `bench3way` as a backwards-compatible
  alias and renamed the methodology guide.
- Promoted the C++20/wxWidgets 3.2 + Scintilla frontend to the primary RBL Studio
  launcher/install target, with a separate Rust `rbl-studio-worker` process. The
  earlier Rust/`eframe`/egui application remains as a legacy reference/fallback
  and is not included in the wx installer. The IDE edits supported text
  documents with arbitrary extensions while compiler integration remains
  restricted to `.rbl` files.
- Tightened editor memory accounting, page-history capacity handling and Replace
  All boundary checks; project-tree traversal now has a global scan limit.
- Split disposable state into `build/cache/` (compiled user programs and
  downloaded upstream sources) and `build/output/` (IDE, Cargo, bootstrap and
  packaging). `rbl.py clean`/`make clean-cache` touch only the cache;
  `make clean-build`/`make clean` touch only outputs.

## Fixed

- Improved source-position tracking for UTF-8 text, LF/CRLF/CR line endings and
  f-string interpolation fragments so diagnostics point into the original source
  file.
- Made collector allocation failures transactional: failed collection preserves
  the heap state and can be retried. Deterministic OOM probes cover the
  collector's allocation structures.
- Aligned Linux and Windows f-string formatting, including Unicode code-point
  width and fill/alignment edge cases.
- Corrected build dependencies so the Linux rebuild script and both platform
  toolchain paths include the ABI and formatting runtime objects.
- Fixed SysV AMD64 stack alignment at Linux Runtime libc/helper call sites,
  including error exits and shared arithmetic overflow paths; added an
  instrumented call-site regression harness and static control-flow
  verification.
- Isolated build artifacts by source identity and target backend so equal
  basenames and concurrent WSL/native builds cannot reuse or overwrite each
  other's outputs.
- Fixed a POSIX save-rollback gap where a symlink could replace the destination
  during the final exchange window. The displaced entry is now identified
  without following it, and rollback is allowed only while both names still
  match their observed identities. Windows orphan-temp cleanup now deletes by
  open handle.

## Verification record

All results below were re-run on 2026-11-15 from the current checkout.

- 63/63 RBL direct-assembly regression tests pass (`tests/run_tests.py`).
- 63-scenario diagnostics suite: 45 invalid sources, 8 valid controls,
  4 expected runtime errors and 6 malformed-UTF-8 cases.
- Windows wx CTest: **12/12 passed**. Linux wx CTest: **11/11 passed**.
- The checkout root was assembled from the portable installer staging tree;
  install state, build caches and CMake build directories are absent.
- Native Windows launch from the checkout root with a System32-only PATH and no
  `RBL_*` variables: `rbl.cmd version` → `RBL 0.8.0`, `rbl.cmd check` and
  `rbl.cmd run` pass; `rbl-studio-wx.exe` opens the `RBL Studio` window and
  starts the worker.
- Earlier native Linux launch from the removed `tar.zst` on ext4 under WSLg (historical evidence): the GUI
  window is alive with a worker child process.
- Linux installed CLI/worker acceptance passes in a minimal PATH containing only
  coreutils plus `bash`/`dirname` — no system `python3`, `cc`, `gcc` or `make`.
  The app-local CPython 3.14.10 backend performs `initialize`, `check`, `run`,
  worker and `clean` actions.
- Corresponding source in the checkout: the Rust worker source tree
  (`rbl-ide/worker`), the wxWidgets IDE sources (`rbl-ide/wx-ui`) and the pinned
  Windows toolchain bundle with `SOURCE_CODE.md` and the toolchain manifest. The
  large upstream corresponding-source archives (MSYS2 toolchain, CPython,
  python-build-standalone, wxWidgets) are not bundled in the checkout; their
  exact source locations and hashes are recorded in
  `toolchain/windows-x86_64/SOURCE_CODE.md`. The public source-delivery route
  still requires owner/legal approval.

## Known limitations and publication gates

These are real, measured limits of this payload, not pre-release framing:

1. **Interactive IDE acceptance is incomplete.** The full manual GUI checklist,
   live 125–200% multi-monitor DPI acceptance, all-theme visual acceptance and
   the 7,200-second resource soak have not been completed. Startup smoke and the
   static GUI suites pass.
2. **Pristine-environment acceptance is incomplete.** The packages were verified
   on the build host and in isolated directories, but not yet on a freshly
   provisioned VM with no developer caches.
3. **Legal/source-delivery sign-off is pending.** Corresponding-source archives
   are bundled and hash-verified, but the public disclosure text and written
   offer for the release page have not received owner/legal approval.
4. **Linux baseline.** The native Linux binary targets GLIBC 2.39 / GLIBCXX
   3.4.32 and uses the host GTK 3 stack. It is not a universal Linux binary.
5. **Windows save race.** `ReplaceFileW` has no destination-identity
   compare-and-swap; the remaining path-based leaf race is documented for 0.9.
   The supported workflow accepts residual CVE-2026-90801 risk only for
   compiler-generated COFF objects, with no arbitrary object input and no
   `--gc-sections`.
6. **Tag history.** The public `v0.8.0` tag still contains the damaged
   `examples/utilities/source-WUDMB5.rbl`. The working tree and both release
   archives contain the corrected 1,093-byte file. The final release must be cut
   from a new commit/tag; the existing tag must not be moved retroactively.

No publication, upload, tag movement or release-status change was performed
while preparing these notes.

## Changelog maintenance

- Record only implemented, user-visible behavior under **Added**, **Changed** or
  **Fixed**.
- Keep proposals and roadmap items out of the release notes until they are
  implemented and tested.
- Add the publication date and final asset links only after the release is
  actually published.
