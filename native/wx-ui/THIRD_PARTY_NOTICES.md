# RBL Studio wx frontend — third-party component inventory

Snapshot: 2026-11-10. This is an engineering inventory, not legal advice or a
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
Apache-2.0 WITH LLVM-exception and Unlicense alternatives. The exact
corresponding-source bundle includes worker/shared source files, Cargo.toml,
Cargo.lock, repository LICENSE, and all 28 exact active crate source archives.
Both build scripts copy it to source/Rust-worker-source.tar.zst; installers
verify the adjacent SHA-256 manifest and retain the archive in the installed
application tree. Bundle path:

build/source-material/RBL-0.8.0-Rust-Worker-Source-Materials-20261110-1955.tar.zst

SHA-256: df5881aa72dc556d9fff6217fd436cec9ad9cb345ac899058a12d23bbdf40da5.

This inventory is not legal certification. The separate MSYS2 source bundle
and corresponding distribution review remain outstanding.

## Corresponding source and distribution status

- The exact-version source archives for the 11 wxWidgets/runtime dependency
  sources were downloaded and signature-verified as part of the 20-archive
  corresponding-source set. Their embedded upstream archive hashes match the package
  PKGBUILD SHA-256 entries.
- Source archive sizes, SHA-256 values, signature evidence and the complete
  package list are recorded in
  [the source archive audit](../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md).
- The `libstdc++` runtime package is a GCC 16.2.0-4 split package; its
  corresponding source is included in the same `mingw-w64-gcc-16.2.0-4` source
  archive as the GCC and libgcc packages. Both `libwinpthread/COPYING` and
  `winpthreads/COPYING` are copied explicitly; their contents are byte-identical
  (SHA-256 `63263614cdd29f2f93cba85e992f041b31f9fc7b4033692f31269489a8a1b177`).
- A 330,067,308-byte source-material archive containing all 20 current source
  archives, signatures, package metadata and notices has been assembled locally
  at `build/source-material/RBL-0.8.0-Windows-Source-Materials-20261110.tar.zst`.
  SHA-256: `6bc84404a077bcc438bafd0b6add5436bb8d1058f339aa5d1ebd988529c7fa57`.
- The MSYS2 toolchain/wx source archive remains separate from the runtime
  payload. The Rust worker source archive is copied into newly built wx payloads
  and then into the installed application tree; existing RC directories predate
  this change and do not contain it.
- No written source offer has been completed or legally reviewed. Do not claim
  redistribution compliance or publish the portable binary until the source
  bundle is actually supplied with the binaries or an applicable offer is
  reviewed for each redistributed component.

The zlib runtime DLL was upgraded from 1.3.2-2 to the signed 1.4-2 MSYS2 package
on 2026-11-10. The new DLL's imported-symbol compatibility was checked against
the bundled assembler, libpng, libtiff and wxWidgets runtime DLLs.

The separate MinGW-w64 compiler/linker bundle used for RBL user programs has its
own package/source inventory in
[`../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md`](../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md)
and [`../../toolchain/windows-x86_64/SOURCE_CODE.md`](../../toolchain/windows-x86_64/SOURCE_CODE.md).
