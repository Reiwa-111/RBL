# last verified: v0.8.0 | 2026.11.10 | WINDOWS TOOLCHAIN THIRD-PARTY NOTICES

# Third-party notices: Windows MinGW-w64 toolchain

This directory contains a pruned MSYS2 UCRT64 MinGW-w64 toolchain for RBL's
Windows x86-64 user-program assembly and static-linking path. It is not a full
MSYS2 installation and does not include the GCC C frontend or development
headers.

The exact files, SHA-256 hashes, sizes, tool versions and runtime dependency
closure are recorded in `msys64/ucrt64/rbl-toolchain-manifest.json`.
License texts for the redistributed components are included under
`msys64/ucrt64/share/licenses/`. The bundle is an extracted subset of the
following installed MSYS2 package versions (package metadata is not itself a
substitute for reviewing the license terms that apply to each redistributed
file):

| Component / package | Version | License metadata observed in the MSYS2 package database |
|---|---|---|
| `mingw-w64-ucrt-x86_64-gcc`, `...-libgcc` | 16.2.0-4 | GPL-3.0-or-later WITH GCC-exception-3.1; `COPYING3` and `COPYING.RUNTIME` included. The package metadata also lists GFDL-1.3-or-later; GCC manuals are not part of this bundle. |
| `mingw-w64-ucrt-x86_64-binutils` | 2.47-4 | GPL-3.0-or-later, GPL-2.0-or-later, LGPL-3.0-or-later and LGPL-2.0-or-later components; corresponding license texts included. |
| `mingw-w64-ucrt-x86_64-crt` | 14.0.0.r426.g4564ee4b5-1 | ZPL-2.1 plus component-specific notices for the statically linked MinGW-w64 runtime; `crt/COPYING`, `COPYING.MinGW-w64-runtime.txt` and `COPYING.MinGW-w64.txt` are included. The runtime notice explicitly requires reproducing its copyright/license notices in binary distributions. |
| `mingw-w64-ucrt-x86_64-gettext-runtime` | 1.0-1 | GPL-3.0-or-later and LGPL-2.1-or-later components; license texts included. |
| `mingw-w64-ucrt-x86_64-libiconv` | 1.19-1 | LGPL-2.1-or-later runtime; package metadata also marks GPL-3.0-or-later documentation, which is not redistributed. Runtime license texts included. |
| `mingw-w64-ucrt-x86_64-libwinpthread`, `...-winpthreads` | 14.0.0.r426.g4564ee4b5-1 | MIT and BSD-3-Clause-Clear; both runtime-library and winpthreads license texts included. |
| `mingw-w64-ucrt-x86_64-zlib` | 1.4-2 | Zlib license; license text included. |
| `mingw-w64-ucrt-x86_64-zstd` | 1.5.7-2 | BSD-3-Clause OR GPL-2.0-or-later; the included `zstd/LICENSE` is the BSD option. |
| `mingw-w64-ucrt-x86_64-windows-default-manifest` | 20260815-1 | Public domain; package metadata marks it custom: Public Domain. |

The MinGW-w64 headers package is a build-time dependency of the installed CRT
package, but headers are not copied into this bundle. The redistributed CRT
objects are covered by the CRT package's ZPL-2.1 license and the component
notices in `COPYING.MinGW-w64-runtime.txt`; the latter includes BSD-style,
permissive and LGPL notices and itself states binary-notice requirements. The
additional `COPYING.MinGW-w64.txt` file is retained as upstream context; its
text says it applies to MinGW-w64 itself rather than binaries built with it
unless specific parts are enabled (for example profiling). The default
manifest object is supplied by the public-domain package listed above.

The machine-readable rbl-toolchain-manifest.json records package IDs, versions
and MSYS2 source-only archive URLs. The exact source archives and signatures have
been collected and verified in the local audit bundle, but are not included in
the 43-file runtime bundle; see [SOURCE_CODE.md](SOURCE_CODE.md) and
[SOURCE_ARCHIVE_AUDIT.md](SOURCE_ARCHIVE_AUDIT.md) for evidence and remaining
distribution requirements.

The RBL repository's Apache-2.0 license applies to RBL-authored project files;
it does not relicense this toolchain. Before publishing binary distributions,
maintainers must preserve the corresponding source code and notices for the
exact packaged versions and verify all applicable redistribution/source-offer
obligations, including any required offer or written materials. The bundle
manifest is an inventory, not a substitute for that compliance review. This
notice is an engineering inventory, not legal advice or a certification of
redistribution compliance.

Upstream project/source locations:

- MSYS2 MinGW packages: <https://github.com/msys2/MINGW-packages>
- GCC: <https://gcc.gnu.org/>
- GNU Binutils: <https://sourceware.org/binutils/>
- gettext: <https://www.gnu.org/software/gettext/>
- GNU libiconv: <https://www.gnu.org/software/libiconv/>
- zlib: <https://zlib.net/>
- zstd: <https://github.com/facebook/zstd>

## Security inventory

- zlib was updated from the bundled 1.3.2-2 runtime to the signed MSYS2 1.4-2
  package on 2026-11-10. The binary package signature and corresponding source
  archive signature verify with the MSYS2 keyring. The new zlib1.dll exports all
  symbols imported from zlib1.dll by the current bundled assembler, libpng,
  libtiff and wxWidgets runtime DLLs. The 1.4-2 PKGBUILD explicitly excludes
  CVE-2026-22184 because it affects the demo untgz program, which is not shipped.
- The bundled Binutils runtime was updated to the signed MSYS2 2.47-4 package
  on 2026-11-10. Its package metadata currently lists 17 CVE records. The
  shipped linker reports only the i386pep/i386pe emulations; the 43-file bundle
  contains no readelf utility. The default RBL build links compiler-generated
  PE/COFF objects and does not accept arbitrary user-supplied object files.
  The XCOFF-specific CVEs (including CVE-2026-3441/3442 and CVE-2026-4647)
  concern a format not used for PE output, and readelf-only findings do not
  match the shipped tool set. Do not generalize this to ELF-input handling:
  BFD can recognize ELF inputs even though the linker emits PE/COFF. Generic
  linker findings require individual reachability review. This is a target-scope
  assessment, not a claim that the upstream Binutils source is patched.
- CVE-2026-90801 concerns an out-of-bounds write in `bfd/cache.c:cache_bwrite`
  in GNU Binutils 2.47's `ld`. The bundled package is 2.47-4 and its executable
  reports 2.47.20260726. A read-only review of the exact signed 2.47-4 source
  archive confirmed that `cache_bwrite` is reachable during ordinary PE output
  linking: BFD's input target vector includes ELF backends, and the COFF
  final-link path has a generic foreign-section copy route that reaches
  `bfd_set_section_contents`/`bfd_write` and the PE output cache. Thus the
  `i386pep`/`i386pe` output-emulation list is not an input-format restriction.
  The review did not reproduce the CVE or prove that ordinary RBL-generated
  objects meet its vulnerable write-size condition.
  The default RBL invocation assembles one generated COFF object and links the
  bundled runtime objects; it does not accept arbitrary user-supplied object
  files or pass `--gc-sections -w`. This narrows normal workflow exposure but
  does not establish that the linker is unaffected. Keep the CVE unresolved and
  release-blocking until a fixed build or a proof excluding the vulnerable
  condition is available. Ubuntu rates the issue Medium (CVSS 5.3) and still
  lists Noble as “Needs evaluation”; Debian's tracker, updated after the
  2.47.50.20261021 snapshot, still lists 2.47.50 as unfixed.
- The signed 2.47-4 package contains the latest MSYS2 packaging revision
  inspected in this audit. It does not remove the generic CVE inventory; keep
  this scope assessment with the exact binary hashes and re-check it whenever
  the bundled linker or accepted input formats change.

Security references:

- MSYS2 Binutils 2.47-4 package metadata and vulnerability list:
  <https://packages.msys2.org/base/mingw-w64-binutils>
- MSYS2 Binutils 2.47-4 package metadata:
  <https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-binutils>
- CVE-2026-3441:
  <https://nvd.nist.gov/vuln/detail/CVE-2026-3441>
- CVE-2026-3442:
  <https://access.redhat.com/security/cve/cve-2026-3442>
- CVE-2026-90801 (Ubuntu status and severity):
  <https://ubuntu.com/security/CVE-2026-90801>
- CVE-2026-90801 (Debian fixed-version tracker):
  <https://security-tracker.debian.org/tracker/CVE-2026-90801>
- CVE-2026-90801 upstream issue:
  <https://sourceware.org/bugzilla/show_bug.cgi?id=34442>
