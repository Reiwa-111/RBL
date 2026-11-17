# RBL Studio wx frontend — third-party component inventory

Snapshot: 2026-11-17 (RBL Studio 0.8.1 release preparation). This is an engineering inventory, not legal advice or a
certification that binary redistribution obligations have been satisfied.

## GUI and runtime dependencies

The Windows build was assembled from MSYS2 UCRT64 packages. Package versions
below were captured with pacman -Q on 2026-11-10, except zlib: the portable
runtime deliberately overrides the host zlib DLL with the repository-pinned,
signature-verified 1.4-2 runtime.

| Component/package | Version |
|---|---:|
| `mingw-w64-ucrt-x86_64-wxwidgets3.2-common` | 3.2.11-1 |
| `mingw-w64-ucrt-x86_64-wxwidgets3.2-common-libs` | 3.2.11-1 |
| `mingw-w64-ucrt-x86_64-wxwidgets3.2-msw` | 3.2.11-1 |
| `mingw-w64-ucrt-x86_64-wxwidgets3.2-msw-libs` | 3.2.11-1 |
| `mingw-w64-ucrt-x86_64-gcc`, `...-libgcc`, `...-libstdc++` | 16.2.0-4 |
| `mingw-w64-ucrt-x86_64-crt` | 14.0.0.r426.g4564ee4b5-1 |
| `mingw-w64-ucrt-x86_64-libwinpthread` | 14.0.0.r426.g4564ee4b5-1 |
| `mingw-w64-ucrt-x86_64-winpthreads` | 14.0.0.r426.g4564ee4b5-1 |
| `mingw-w64-ucrt-x86_64-brotli` | 1.2.0-1 |
| `mingw-w64-ucrt-x86_64-jbigkit` | 2.1-6 |
| `mingw-w64-ucrt-x86_64-lerc` | 4.1.1-1 |
| `mingw-w64-ucrt-x86_64-libdeflate` | 1.26-1 |
| `mingw-w64-ucrt-x86_64-libjpeg-turbo` | 3.2.0-1 |
| `mingw-w64-ucrt-x86_64-libpng` | 1.6.59-1 |
| `mingw-w64-ucrt-x86_64-libtiff` | 4.7.2-1 |
| `mingw-w64-ucrt-x86_64-libwebp` | 1.6.0-1 |
| `mingw-w64-ucrt-x86_64-pcre2` | 10.49-1 |
| `mingw-w64-ucrt-x86_64-xz` | 5.8.4-1 |
| `mingw-w64-ucrt-x86_64-zlib` | 1.4-2 |
| `mingw-w64-ucrt-x86_64-zstd` | 1.5.7-2 |

wxWidgets is distributed under the wxWindows Library Licence 3.1. The editor
uses wxStyledTextCtrl, wxWidgets' Scintilla integration; Riverbank QScintilla is
not included. The separately applicable Scintilla/SciTE copyright and
permission notice from the exact wxWidgets 3.2.11 source archive is preserved in
[`licenses/Scintilla-LICENSE.txt`](licenses/Scintilla-LICENSE.txt). The Windows
portable build copies it to `licenses/Scintilla/License.txt` and records the
attribution in `THIRD_PARTY_NOTICES.txt`.

## Rust worker dependencies

The wx frontend distributes the Rust rbl-studio-worker as a separate process.
Its active default-feature Cargo dependency closure is documented in
RUST_DEPENDENCY_NOTICES.md: 24 Windows GNU crates and 26 Linux x86-64 crates,
28 unique crate packages. The legacy egui application is separately gated by
the opt-in Cargo feature legacy-egui; its eframe/rfd graph is not built or
shipped with the wx worker. Per-crate license and notice texts for the active
worker closure are preserved in licenses/Rust/<crate>-<version>/ and copied
into both platform build payloads.

The worker closure includes option-ext 0.2.0 (MPL-2.0), unicode-ident
(Unicode-3.0 in addition to MIT/Apache-2.0), and dependencies offering
Apache-2.0 WITH LLVM-exception and Unlicense alternatives. Their package license
and notice texts are retained under `licenses/Rust/` and are copied into build
payloads. The Rust worker source itself is in the repository under
`rbl-ide/worker/`, with dependency versions pinned by `Cargo.lock`.

A local corresponding-source bundle was assembled during the 0.8.0 audit at
`build/source-material/RBL-0.8.0-Rust-Worker-Source-Materials-20261114-release-recheck-r4.tar.zst`
(8,255,104 bytes; SHA-256
`0ddd19ba6b4c049fcbef95d32462f6f8b9ebf5788a9077f389c5a1c53ec4340c`). This is
historical verification material only: it is not tracked, not copied into current
wx build/install payloads, and not a GitHub release deliverable.

## Corresponding source and distribution status

- Exact-version source archives for the 11 wxWidgets/runtime dependency sources
  were downloaded and signature-verified as part of the historical 20-archive
  audit set. Their upstream archive hashes match package PKGBUILD SHA-256 entries.
- Source archive sizes, SHA-256 values, signature evidence and package coverage
  are recorded in [the source archive audit](../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md).
- The `libstdc++` runtime package is a GCC 16.2.0-4 split package; its source is
  part of the same upstream GCC source package. Both `libwinpthread/COPYING` and
  `winpthreads/COPYING` are copied explicitly; their contents are byte-identical
  (SHA-256 `63263614cdd29f2f93cba85e992f041b31f9fc7b4033692f31269489a8a1b177`).
- The historical Windows source-material archive contains the 20 source archives,
  detached signatures, five signed package archives and audit metadata. It is
  not part of the current repository source tree or runtime payload.
- The current 0.8.1 GitHub release model publishes the tagged repository source
  tree directly and does not assemble separate corresponding-source archives.
  This does not waive third-party obligations for bundled binaries. No formal
  legal review or redistribution certification is claimed by this inventory.

The zlib runtime DLL was upgraded from 1.3.2-2 to the signed 1.4-2 MSYS2 package
on 2026-11-10. The new DLL's imported-symbol compatibility was checked against
the bundled assembler, libpng, libtiff and wxWidgets runtime DLLs.

The separate MinGW-w64 compiler/linker bundle used for RBL user programs has its
own package/source inventory in
[`../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md`](../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md)
and [`../../toolchain/windows-x86_64/SOURCE_CODE.md`](../../toolchain/windows-x86_64/SOURCE_CODE.md).
