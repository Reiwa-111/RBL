# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Release notes

These notes follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), grouping changes under **Added**, **Changed** and **Fixed**. This page lists verified user-visible changes for releases 0.7.2 and 0.7.1. The imported repository history before those releases is incomplete, so earlier releases are not reconstructed as a complete changelog.

## [0.7.2] - 2026-11-06

### Added

- Complete Russian and English documentation editions organized with Diátaxis: tutorial, how-to, reference and explanation.
- A separate detailed RBL roadmap from 0.7.2 through 1.0, including exit criteria and the source-module system as the central 1.0 goal.
- Documented five-way Linux benchmark results for RBL, C, C++, CPython and x86-64 ASM, including medians, performance ratios, environment, caveats and reproduction command.

### Changed

- The root `VERSION` file is the sole source of the repository version; `rbl.py version` and `rblc-asm --version` report `0.7.2`.
- `rbl new` and project initialization create an independent package version of `0.1.0` instead of copying the compiler version.
- The five-way benchmark passes C/C++ iteration counts at runtime and marks the call workload `noinline`, preventing `-O2` from folding the measured workload into a constant.
- Benchmark methodology now states that full process startup is timed while RBL build time is reported separately; corrected results and limitations for the three synthetic workloads are published.
- Windows runtime documentation more accurately describes the monotonic `GetTickCount64` clock, local PRNG sequence and limits of cross-platform determinism.
- The root README is now a concise entry point to the bilingual documentation; the former detailed pages are preserved in the archive.

### Fixed

- Corrected the Windows runtime diagnostic so a failure to obtain the current directory through `getcwd` is no longer reported as a clock failure.
- Updated native compiler version and diagnostic text: the stale `0.7.0` string is replaced with `0.7.2`.
- File-I/O regression/smoke cases now use the checkout's `build/` directory instead of a hard-coded `/tmp` path.

Release verification: all 59 direct regression tests and the standard-library smoke test pass; the CLI and Linux/Windows compiler binaries report `0.7.2`.

## [0.7.1] - 2026-10-05

### Added

- Added regression coverage for typed dictionary parameters and a deterministic Game of Life program.
- Expanded the direct regression suite from 57 to 59 cases.
- Updated Windows/WSL smoke tests and diagnostic tools.

### Changed

- The compiler gained direct lowering for eligible integer leaf comparisons and in-place updates to register-held integer locals, retaining overflow checks.
- The toolchain caches build outputs and avoids unnecessary compiler/runtime rebuilds when input files have not changed.
- Windows IDE/toolchain launch paths suppress transient console windows where possible.

### Fixed

- Fixed Windows runtime type reporting for `list`, `dict`, `tuple` and `null` values passed to typed function parameters.
- Fixed Windows runtime object freshness: an object is rebuilt when its source becomes newer.
- Improved reliability of Windows launch/process handling, including UTF-8 console output and rebuilding stale runtime objects.

## Changelog policy

- Add an entry under `Added`, `Changed` or `Fixed` when the user-facing contract changes.
- Describe the user-visible effect, not only the name of the modified file.
- Do not record proposals or roadmap items as shipped features.
- Do not retroactively change historical counts without a verified release diff.
- Add the next release above the previous one, using a publication date in `YYYY-MM-DD` format.

rbl v0.8 soon
