# last verified: v0.8.0 | 2026.11.10 | SOURCE ARCHIVE AUDIT

# Windows toolchain and wx runtime — corresponding-source audit

## Scope and result

This audit covers 20 unique MSYS2 source archives corresponding to the pruned
Windows GCC/Binutils runtime and the wxWidgets GUI runtime dependency closure:
9 toolchain source families and 11 wx/runtime dependency source families.
The separately installed `mingw-w64-ucrt-x86_64-libstdc++` runtime package is a
GCC 16.2.0-4 split package and is covered by the same `mingw-w64-gcc` source
archive; it does not require a 21st unique source archive.

- All 20 source-only tarballs were downloaded and their detached .sig files
  verified successfully with the MSYS2 keyring.
- The zlib 1.4-2 binary package and the MSYS2 keyring package signatures also
  verified successfully.
- All 17 tarball-based source packages contain an upstream source archive whose
  SHA-256 matches the first corresponding sha256sums entry in its PKGBUILD.
- CRT and winpthreads contain the same git source repository at commit
  4564ee4b5063097bf747af3a3f8270a28adff820.
- The public-domain Windows default-manifest source package contains genrc.py
  whose SHA-256 matches its PKGBUILD.

The source archives and logs are retained locally under
build/toolchain-source-audit-20261110/. They are not committed into this
repository and are not included in the 43-file Windows toolchain runtime bundle.
The source-material bundle must accompany any binary distribution or be
replaced by another legally sufficient source-distribution method.

## Signature verification

All 20 source archives were signed by the MSYS2 developer key with fingerprint:

5F944B027F7FE2091985AA2EFA11531AA0AA7F57

The fingerprint matches the lazka developer key in the official
[MSYS2 keyring documentation](https://www.msys2.org/dev/keyring/), which states
that each developer key is signed by at least three MSYS2 master keys.
Verification output is retained in
build/toolchain-source-audit-20261110/signature-verification-current.log.

The current source set contains 20 exact-version source archives. The Binutils
2.47-4 source archive and its corresponding binary package were independently
verified with the same keyring; the zlib 1.4-2 binary package and the MSYS2
keyring package signatures also verify. The previous Binutils 2.47-3 source
archive is retained locally as superseded provenance, but is not part of the
current 20-package source set. This is cryptographic archive verification, not
a legal opinion.

## Exact source archives

All SHA-256 values below are for the downloaded .src.tar.zst files.

| Source package | Version | Size (bytes) | SHA-256 |
|---|---:|---:|---|
| mingw-w64-gcc | 16.2.0-4 | 107228968 | a1792a44764405bc365746e0e60f0daa122076cec0dddd35a781d141f94273cf |
| mingw-w64-binutils | 2.47-4 | 39012346 | e829fa1387f3f1ab14187c34c94e180f6d2da2b6aad8acdf6b1597abb4c0eb2f |
| mingw-w64-crt | 14.0.0.r426.g4564ee4b5-1 | 54805035 | 8edb0c86c4fe776e6bda609149ef1b3bbbad772711796bd051c0d57318589d5c |
| mingw-w64-gettext | 1.0-1 | 10267655 | cca0d0c8f60353faccc5ad40405240014e4a7ccc8b1c6ca9f4626aef3097d26e |
| mingw-w64-libiconv | 1.19-1 | 5131029 | 74428280c17094da5b702c29b2e1a0abae59556ea5dfdd65705cc8ccc1e000fb |
| mingw-w64-winpthreads | 14.0.0.r426.g4564ee4b5-1 | 54807189 | 5493832de1b7e24f09edb556ed409091b0844f9677212c4865618d6f93c508be |
| mingw-w64-zlib | 1.4-2 | 1324155 | 28415621a0f6c16bc20b0ce50cd1ae3632dd9f27bdc5776ab3ec18e8d2f00dcd |
| mingw-w64-zstd | 1.5.7-2 | 2402010 | 306797282df799e95d550e6d0bb4b0a7b848027b8e13893a24236e060837c217 |
| mingw-w64-windows-default-manifest | 20260815-1 | 1973 | bed6d206e13c6ef0149f59fb7ed0a20c8665b8f39297247aded467860830f954 |
| mingw-w64-wxwidgets3.2 | 3.2.11-1 | 27528064 | 24173e648df8d8960b03402739d7337a179dd1ccf9496791d3fd008272e6b128 |
| mingw-w64-brotli | 1.2.0-1 | 644113 | 2f0f93875aac21990d20f1eaf0ad8b970eb6f186c663c77ae47aaaf9e5711fcf |
| mingw-w64-jbigkit | 2.1-6 | 443470 | 64757867df26308c14d22b184aaecb43742c25ee8d24f18da51e3d1e09be8ac2 |
| mingw-w64-lerc | 4.1.1-1 | 4056783 | 697824f8dba0b297949b294d7f850a21dfa817802cf91f3a73b6192a7c234f46 |
| mingw-w64-libdeflate | 1.26-1 | 200156 | 8f08ba1c541d7cdffde7113e0796fa3ed5ae494702c57d86b736f82167805ea6 |
| mingw-w64-libjpeg-turbo | 3.2.0-1 | 2536411 | 98bfbef730e79ca74c88842eb27730db11429dbae4d95e0f5d7859e316026a74 |
| mingw-w64-libpng | 1.6.59-1 | 1611403 | 2b836f8565a3889ed9bb86f9724074ae274f109e2bcd882dd1f080ea4da8a41a |
| mingw-w64-libtiff | 4.7.2-1 | 4077229 | 11f3bdc23a154a5cea2f8fffeab3e2a118bd773d2769e1dcea926ff02f2cf3e2 |
| mingw-w64-libwebp | 1.6.0-1 | 2589761 | c8c63f98e018a7595d22e8a3e65b7caf83345fed666866a6e67d153b16f48141 |
| mingw-w64-pcre2 | 10.49-1 | 2183818 | b02f0d5070b7d7f6db5c8ba9b009f28c1a07b9b52aaf7964dc0c8b098e4088d1 |
| mingw-w64-xz | 5.8.4-1 | 1569458 | b8acbc56b2bfe08df7bffff423f3bf07ee85dbec4e07416017ab04c8afb9d13e |

## zlib 1.4-2 package replacement

The bundled zlib runtime was upgraded from 1.3.2-2 to the signed MSYS2
mingw-w64-ucrt-x86_64-zlib 1.4-2 binary package. The binary package SHA-256 is
172541cf5e2391a402f092104988699b0163129d22be1d6b4b201bfb84af6fcf.

| File | Previous | Installed |
|---|---:|---:|
| ucrt64/bin/zlib1.dll size | 127291 | 190835 |
| ucrt64/bin/zlib1.dll SHA-256 | 11cd91171765cd65f1c3362e3df67e5f5a618a6b88c8cdec899b802eaf1c7b0d | aa658771fb152b095eda1614d8bde798efb92324b6f4a0c99c743cd96b67ac90 |

The package PKGINFO reports version 1.4-2 and Zlib license metadata. Its
PKGBUILD, patches and upstream zlib-1.4.tar.xz are present in the signed source
archive; the upstream source hash matches PKGBUILD. A PE import scan confirmed
that all zlib symbols imported by the bundled assembler, libpng, libtiff and
wxWidgets runtime DLLs are present in the new zlib1.dll. The previous DLL is
backed up at build/backups/toolchain-zlib-pre-1.4-2-20261110/.

## Binutils 2.47-4 package replacement

The bundled Binutils runtime was updated from the signed 2.47-3 package to
MSYS2's signed 2.47-4 package. The binary package SHA-256 is
c18a40b871bfddb5da030f015003479ca85625afde1cb89f7313945e6151397d; the source
archive SHA-256 is
e829fa1387f3f1ab14187c34c94e180f6d2da2b6aad8acdf6b1597abb4c0eb2f.
The 2.47-4 PKGBUILD's upstream binutils-2.47.tar.bz2 hash matches the embedded
source. Its detached source and binary package signatures both verify against
the retained MSYS2 keyring.

| File | Previous 2.47-3 | Installed 2.47-4 |
|---|---:|---:|
| bin/as.exe size | 1972691 | 1968486 |
| bin/as.exe SHA-256 | f81ccfe9d5d7eac346387ed37c4673ad3b41ab2525d57747f2c0e641b6284a3a | a608bd29d15d7cad470567070c7362191881f238d6e8525f4c3ab908cf36fcc5 |
| x86_64-w64-mingw32/bin/ld.exe size | 1933847 | 1930154 |
| x86_64-w64-mingw32/bin/ld.exe SHA-256 | 9c44080673239aa8c541daa0f534d0f8f3d434e4db0641d2e7cbda22875942c2 | a279cc83f198a606f255a597b3a067b51f11a9bbf8fe46de2a0dc8bb71b47f4f |

The new linker reports only the i386pep and i386pe emulations. The 43-file
runtime bundle does not contain readelf or the other general-purpose Binutils
inspection tools. The old binaries are backed up at
build/backups/toolchain-binutils-pre-2.47-4-20261110/.

## Remaining release blockers

- The 20 source archives and signatures have been assembled into a 330,067,308-
  byte source-material archive at
  build/source-material/RBL-0.8.0-Windows-Source-Materials-20261110.tar.zst.
  SHA-256:
  6bc84404a077bcc438bafd0b6add5436bb8d1058f339aa5d1ebd988529c7fa57.
  The archive contains 52 members; its SHA256SUMS file has 51 records covering
  every other member (the checksum file cannot include its own hash). These
  records were rechecked after archive creation. The archive
  is local build output and has not yet been attached to a distributable binary.
- No written source offer or alternative distribution procedure has been
  legally reviewed. Keep the release manifest UNRELEASED.
- The bundled Binutils package was updated to 2.47-4. The [MSYS2 security
  index](https://www.msys2.org/security/) explicitly lists CVE-2026-90801
  against `mingw-w64-binutils 2.47-4`; the [installed UCRT64 package
  page](https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-binutils)
  confirms `mingw-w64-ucrt-x86_64-binutils 2.47-4`. The shipped linker reports only the
  `i386pep`/`i386pe` output emulations (`ld -V`), and the bundle does not include
  `readelf`. These facts exclude readelf-only findings from the shipped tool
  set, but the output-emulation list is not an input-format whitelist: the BFD
  target vector includes ELF input backends.

### CVE-2026-90801 source-level reachability review

A read-only review of the exact signed Binutils 2.47-4 source archive confirmed
that `bfd/cache.c:cache_bwrite` is reachable during ordinary PE output linking.
The linker recognizes ELF object inputs through the BFD target vector; the COFF
final-link path has a generic foreign-section copy route which reaches
`bfd_set_section_contents`/`bfd_write`, and the PE output cache uses
`cache_bwrite`. Therefore PE-only output emulations do not establish that the
cache-write path is unreachable. The review did not reproduce the CVE or prove
that a normal bounded RBL-generated object satisfies the vulnerable write-size
condition.

The current Debian tracker still marks Binutils vulnerable in bookworm, trixie,
forky and sid; the latest listed sid/forky package is `2.47.50.20261021-1` and
the fixed-version field remains `unfixed`. Red Hat's product-specific advisory
identifies a narrower affected portfolio and does not establish that the MSYS2
build is unaffected. These cross-distribution records reinforce the need to
retain the blocker, but do not independently prove reachability in this RBL
workflow.

The default RBL invocation assembles one compiler-generated COFF object and
links it with the bundled runtime objects; it does not accept arbitrary
user-supplied object files or pass `--gc-sections -w`. This narrows the normal
workflow exposure, but is not proof that the bundled linker is unaffected.
Keep CVE-2026-90801 **UNRESOLVED / RELEASE BLOCKING** pending a fixed linker or a
source-level proof that the exact vulnerable condition cannot occur in the
supported workflow. The [Debian tracker](https://security-tracker.debian.org/tracker/CVE-2026-90801)
still lists the current 2.47.50 snapshot as vulnerable/unfixed, while
[Ubuntu's advisory](https://ubuntu.com/security/CVE-2026-90801) lists Noble as
“Needs evaluation” (Medium, CVSS 5.3). See also the package's live
[MSYS2 vulnerability inventory](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-binutils).

This document records engineering evidence only. It is not legal advice or a
certification of redistribution compliance.

## CVE status refresh — 2026-11-10

The external status was rechecked after the source-level review:

- The MSYS2 security inventory lists the exact `mingw-w64-binutils` package
  version `2.47-4` with `CVE-2026-90801` (medium); the entry does not show a
  fixed-in version.
- The Debian OSV import lists Binutils `2.47-4` among affected versions for
  Debian 12 and 13. This is ecosystem evidence, not a direct assessment of the
  MSYS2 build or the RBL-generated object workflow.
- Red Hat Bugzilla entry 2533176 remains `NEW` with no fixed-in version.
- Ubuntu's advisory continues to show Noble (24.04) as `Needs evaluation`.
- The upstream Sourceware Bugzilla page could not be retrieved in this check
  (access denied by its anti-bot layer); no upstream fix or resolution was
  confirmed from that endpoint.

Status references checked: MSYS2 package inventory
https://packages.msys2.org/security; Debian OSV import
https://osv.dev/vulnerability/DEBIAN-CVE-2026-90801; Red Hat Bugzilla
https://bugzilla.redhat.com/show_bug.cgi?id=2533176; Ubuntu advisory
https://ubuntu.com/security/CVE-2026-90801; upstream report
https://sourceware.org/bugzilla/show_bug.cgi?id=34442.

These records do not establish exploitability in the normal RBL workflow, but
they provide no basis to close the finding either. Combined with the BFD
reachability analysis above, `CVE-2026-90801` remains **UNRESOLVED / RELEASE
BLOCKING** pending a fixed linker or a defensible source-level proof that the
vulnerable condition cannot occur in the supported workflow.
