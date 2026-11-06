# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Release notes

This file records notable user-visible changes in repository releases. The imported repository history is incomplete before `0.7.1`; this page therefore documents only changes that can be verified in the `0.7.1` release diff. Older feature history is summarized in the root `RELEASE_MANIFEST.txt`, not reconstructed as a complete changelog.

## [0.7.1] - 2026-10-05

### Added

- Regression coverage for typed dictionary parameters and a deterministic Game of Life program. The direct suite increased from 57 to 59 cases.
- Updated Windows/WSL smoke tests and diagnostic tooling.

### Changed

- The compiler adds direct lowering for eligible integer leaf comparisons and in-place updates to register-held integer locals, retaining overflow checks.
- The toolchain caches build outputs and avoids unnecessary compiler/runtime rebuilds when inputs have not changed.
- Windows IDE/toolchain launch paths suppress transient console windows where possible.

### Fixed

- Windows runtime type reporting for `list`, `dict`, `tuple` and `null` values passed to typed function parameters.
- Windows runtime object freshness: the corrected runtime object is rebuilt when its source is newer.
- Windows launch and process handling is more reliable, including UTF-8 console output and rebuilding stale runtime objects.

See `ROADMAP.md` for current and planned work, `RBL_QUICK_REFERENCE.md` for the implemented language surface, and the root `RELEASE_MANIFEST.txt` for the verified feature inventory of this repository snapshot.
