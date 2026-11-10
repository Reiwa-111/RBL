# last verified: v0.8.0 | 2026.11.10 | WINDOWS TOOLCHAIN SOURCE INVENTORY

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
  build/toolchain-source-audit-20261110/. All 20 current detached archive
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
- The downloaded archives are not included in the 43-file runtime bundle or in
  the Git repository snapshot. A separate 330,067,308-byte source-material
  archive has been assembled locally at
  build/source-material/RBL-0.8.0-Windows-Source-Materials-20261110.tar.zst
  (SHA-256 6bc84404a077bcc438bafd0b6add5436bb8d1058f339aa5d1ebd988529c7fa57).
  It still must accompany any binary distribution (or be replaced by another
  legally sufficient source-distribution method).
- A written offer has not been completed or legally reviewed. The project must
  not claim that redistribution obligations are closed until the source bundle
  is actually supplied with the binaries or an applicable offer is reviewed.

These results establish archive integrity and source correspondence, not a legal
certification. The links above are source-location references and do not
guarantee that third-party mirrors will retain every historical archive.
