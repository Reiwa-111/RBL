# last verified: v0.8.0 | 2026.11.12 | WINDOWS TOOLCHAIN SOURCE INVENTORY

# Windows toolchain corresponding-source inventory

The portable runtime bundle contains 43 redistributable toolchain files and
their applicable license texts. It does **not** contain the corresponding source
archives. The exact package IDs, versions and source-only archive URLs are also
recorded in `msys64/ucrt64/rbl-toolchain-manifest.json`.

The bundled executables were directly queried on Windows: GCC reports
`16.2.0`, GNU assembler reports `2.47.20260726`, and GNU ld reports
`2.47.20260726` when the bundle's `ucrt64/bin` directory is on `PATH`. The
manifest maps these to MSYS2 package versions GCC 16.2.0-4 and Binutils
2.47-4; it also records CRT 14.0.0.r426.g4564ee4b5-1 and the other runtime
packages. The package database itself is not part of this pruned bundle, so most
package-version mappings remain the retained build inventory rather than a fresh
pacman -Q query from this runtime image. zlib was independently updated from
1.3.2-2 to signed MSYS2 package 1.4-2 on 2026-11-10; its .PKGINFO, package
signature and installed DLL hash were checked.

| MSYS2 package | Version | Source-only archive |
|---|---|---|
| `mingw-w64-ucrt-x86_64-gcc`, `...-libgcc` and `...-libstdc++` | `16.2.0-4` | [`mingw-w64-gcc-16.2.0-4.src.tar.zst`](https://mirror.msys2.org/mingw/sources/mingw-w64-gcc-16.2.0-4.src.tar.zst) |
| `mingw-w64-ucrt-x86_64-binutils` | `2.47-4` | [`mingw-w64-binutils-2.47-4.src.tar.zst`](https://mirror.msys2.org/mingw/sources/mingw-w64-binutils-2.47-4.src.tar.zst) |
| `mingw-w64-ucrt-x86_64-crt` | `14.0.0.r426.g4564ee4b5-1` | [`mingw-w64-crt-14.0.0.r426.g4564ee4b5-1.src.tar.zst`](https://mirror.msys2.org/mingw/sources/mingw-w64-crt-14.0.0.r426.g4564ee4b5-1.src.tar.zst) |
| `mingw-w64-ucrt-x86_64-gettext-runtime` | `1.0-1` | [`mingw-w64-gettext-1.0-1.src.tar.zst`](https://mirror.msys2.org/mingw/sources/mingw-w64-gettext-1.0-1.src.tar.zst) |
| `mingw-w64-ucrt-x86_64-libiconv` | `1.19-1` | [`mingw-w64-libiconv-1.19-1.src.tar.zst`](https://mirror.msys2.org/mingw/sources/mingw-w64-libiconv-1.19-1.src.tar.zst) |
| `mingw-w64-ucrt-x86_64-libwinpthread` and `...-winpthreads` | `14.0.0.r426.g4564ee4b5-1` | [`mingw-w64-winpthreads-14.0.0.r426.g4564ee4b5-1.src.tar.zst`](https://mirror.msys2.org/mingw/sources/mingw-w64-winpthreads-14.0.0.r426.g4564ee4b5-1.src.tar.zst) |
| `mingw-w64-ucrt-x86_64-zlib` | `1.4-2` | [`mingw-w64-zlib-1.4-2.src.tar.zst`](https://mirror.msys2.org/mingw/sources/mingw-w64-zlib-1.4-2.src.tar.zst) |
| `mingw-w64-ucrt-x86_64-zstd` | `1.5.7-2` | [`mingw-w64-zstd-1.5.7-2.src.tar.zst`](https://mirror.msys2.org/mingw/sources/mingw-w64-zstd-1.5.7-2.src.tar.zst) |
| `mingw-w64-ucrt-x86_64-windows-default-manifest` | `20260815-1` | [`mingw-w64-windows-default-manifest-20260815-1.src.tar.zst`](https://mirror.msys2.org/mingw/sources/mingw-w64-windows-default-manifest-20260815-1.src.tar.zst) |

## Distribution status

- Exact-version source-only archives for the 9 compiler/toolchain package sources
  and 11 wx/runtime dependency sources were downloaded to
  build/toolchain-source-audit-20261111/. All 20 current detached archive
  signatures verify with the MSYS2 keyring; the 1.4-2 zlib binary package and
  the keyring package signatures also verify. Binutils 2.47-4 source and binary
  package signatures were independently verified. The signer fingerprint is
  5F944B027F7FE2091985AA2EFA11531AA0AA7F57; it matches the lazka developer key
  published by MSYS2.
- Upstream source archive hashes match the corresponding PKGBUILD SHA-256
  entries for all 17 tarball-based packages. The CRT and winpthreads source
  archives contain the same git repository at commit
  4564ee4b5063097bf747af3a3f8270a28adff820. The public-domain default-manifest
  archive's genrc.py hash matches its PKGBUILD.
- Exact archive sizes, SHA-256 values, signer fingerprint, package metadata and
  verification evidence are recorded in
  [SOURCE_ARCHIVE_AUDIT.md](SOURCE_ARCHIVE_AUDIT.md).
- The downloaded archives are not embedded in the 43-file runtime toolchain
  subtree or in the Git repository snapshot. The original 2026-11-10 archive
  was lost and superseded by a new verified archive assembled 2026-11-11:
  build/source-material/RBL-0.8.0-Windows-Source-Materials-20261111-final-reviewed.tar.zst
  (the archive SHA-256 is recorded in the external Gate F report and
  `bundle_sha256.txt`; it is omitted here to avoid a self-referential digest).
  Its 55 SHA256SUMS records cover every other file: 20 source archives and
  detached signatures, five signed binary packages and audit/manifest material.
- Fresh Windows build payloads now copy that exact archive to
  `source/Windows-toolchain-source.tar.zst` with a SHA-256 sidecar. The Windows
  installer requires the archive, verifies its hash before staging, installs it
  beside the application and verifies it again after installation. It must
  remain beside every binary distribution; do not detach it from the installer
  or portable package.
- No written offer is needed for a distribution that actually includes the
  complete corresponding-source archive as a sibling download/installed source
  item, but the distribution process must preserve that pairing. Any alternative
  source-offer route still requires legal review.

These results establish archive integrity and source correspondence, not a legal
certification. The current local Windows payload now contains the corresponding
source archive; the public pre-release still has no binary/source assets attached.
The links above are source-location references and do not guarantee that
third-party mirrors will retain every historical archive.
