# last verified: v0.8.0 | 2026.11.10 | RELEASE CANDIDATE NOTES

## RBL 0.8.0 — release candidate draft

This document describes the user-visible changes prepared for RBL 0.8.0. It remains a pre-release draft until the release is explicitly published. No publication date is assigned here.

> **IDE status as of 2026-11-10:** the primary RBL Studio frontend is C++20/wxWidgets 3.2 with Scintilla, paired with a Rust worker; the earlier Rust/egui application remains legacy source. Windows SaveDocument byte-roundtrip acceptance passes 27/27. The current-RC 7,200-second resource soak has not passed: the latest attempt ended early without a CSV/verdict. Live high-DPI/multi-monitor and interactive Linux GUI acceptance remain unverified. CVE-2026-90801 remains unresolved; the separate MSYS2 source bundle is not in the runtime payload; legal review and a written source offer are incomplete; existing RCs need rebuilding. These block binary redistribution. See the [IDE engineering audit](explanation/ide-engineering-audit.md) and [resource report](../../benchmarks/IDE_RESOURCE_USAGE.md). This is a pre-release qualification, not a publication statement.

### Added

- Bounded f-string format specifications with fill, alignment, minimum width, floating-point precision and explicit integer/float/string conversion types. The supported grammar and bounds are defined in the [syntax reference](reference/syntax.md).
- Source diagnostics with file, line, column, compiler-stage labels, source excerpts and caret positioning across lexing, parsing, semantic validation and code generation.
- Runtime-managed dynamic-string lifetime integrated with the conservative non-moving collector for strings and containers. Native C borrowing limits and collector boundaries are documented explicitly.
- Runtime ABI descriptor and compatibility checks, independently versioned from the RBL compiler/language version. The current Runtime ABI is `1.2.0`.

### Changed

- Split the compiler implementation into focused C translation units with private module interfaces. Variable, function and string-pool lookups use hash indexes while preserving deterministic source ordering and function-resolution behavior.
- Added Runtime-owned dynamic strings. RBL programs must not manually free managed string payloads; see the [execution model](explanation/execution-model.md).
- Added compiler-emitted garbage-collection safe points and a non-allocating fast-loop path that omits redundant per-iteration safe points only when the compiler proves the loop cannot allocate or mutate pointer roots.
- Extended the build system to track compiler modules, private headers, ABI sources and the version dependency, with separate object files for incremental and parallel builds.
- Centralized the shared container Runtime C entry-point declarations in `runtime/rbl_abi.h` and enabled strict missing/redundant-prototype checks in Linux/Windows bootstrap paths.
- Added the accurately named `bench5way` CLI/Makefile target for the five-implementation comparison; retained `bench3way` as a backwards-compatible alias and renamed the methodology guide.
- Promoted the C++20/wxWidgets 3.2 + Scintilla frontend to the primary RBL Studio launcher/install target, with a separate Rust `rbl-studio-worker` process. The earlier Rust/`eframe`/egui application remains as a legacy reference/fallback and is not included in the wx installer. The IDE edits supported text documents with arbitrary extensions while compiler integration remains restricted to `.rbl` files.
- Tightened editor memory accounting, page-history capacity handling and Replace All boundary checks; project-tree traversal now has a global scan limit.

### Fixed

- Improved source-position tracking for UTF-8 text, LF/CRLF/CR line endings and f-string interpolation fragments so diagnostics point into the original source file.
- Made collector allocation failures transactional: failed collection preserves the heap state and can be retried. Deterministic OOM probes cover the collector's allocation structures.
- Aligned Linux and Windows f-string formatting, including Unicode code-point width and fill/alignment edge cases.
- Corrected build dependencies so the Linux rebuild script and both platform toolchain paths include the ABI and formatting runtime objects.
- Fixed SysV AMD64 stack alignment at Linux Runtime libc/helper call sites, including error exits and shared arithmetic overflow paths; added an instrumented call-site regression harness and static control-flow verification.
- Isolated build artifacts by source identity and target backend so equal basenames and concurrent WSL/native builds cannot reuse or overwrite each other's outputs.

## Release verification record

The compiler/Runtime verification below does not constitute completed IDE acceptance. See the [IDE engineering audit](explanation/ide-engineering-audit.md) for current status and outstanding exit criteria.

- 61/61 direct compiler regression cases pass. The 63-scenario diagnostics suite contains 45 invalid-source cases, 8 valid controls, 4 expected runtime errors and 6 malformed-UTF-8 cases.
- 83 selected source cases run across Linux/WSL and native Windows: 61 successful programs and 11 expected runtime-error programs compare exit code/stdout/stderr; 7 expected-negative fixtures are rejected with matching diagnostics; 4 platform/random-output programs must build and exit successfully without output comparison. All 30 diagnostic differential cases match.
- All 114 expected-positive sources across the 53 examples and 68 compiler-test sources compile and assemble; all 7 expected-negative sources are rejected.
- All 53 example programs pass `check` on Windows native and WSL. The new A*, SIR, Monte Carlo π and least-squares regression examples also execute successfully on Windows native.
- Runtime ABI compatibility/startup/symbol and generated fast-call alignment tests, the instrumented Linux Runtime call-alignment suite (158 textual sites / 178 emitted calls in the current Runtime object), build-cache isolation, string ownership, deterministic OOM rollback/retry, native stack-borrow and repeated I/O-under-GC tests pass.
- The Linux and Windows compiler binaries and CLI report version `0.8.0`.

See [`RELEASE_MANIFEST.txt`](../../RELEASE_MANIFEST.txt), the [testing guide](how-to/testing.md), the [Runtime ABI reference](reference/runtime-abi.md) and the benchmark reports for detailed scope and reproduction commands.

## Changelog maintenance

- Record only implemented, user-visible behavior under **Added**, **Changed** or **Fixed**.
- Keep proposals and roadmap items out of the release notes until they are implemented and tested.
- Add a publication date only when the release is actually published.
