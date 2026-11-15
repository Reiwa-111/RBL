# last verified: v0.8.0 | 2026.11.11 | DOCUMENTATION RELEASE STATUS

## RBL documentation

This documentation describes the **RBL 0.8.0 language, compiler, runtime and toolchain contract** and follows [Diátaxis](https://diataxis.fr/): tutorials, how-to guides, reference material and explanations. It documents implemented behavior, not proposed features.

## Start here

| Section | Purpose | Main pages |
|---|---|---|
| **Beginner course** | Follow 20 guided lessons with runnable examples | [Beginner course](../../examples/beginner/README_ENG.md) |
| **How-to** | Complete a specific practical task | [Project workflow](how-to/project-workflow.md), [Windows and WSL](how-to/windows-toolchain.md), [Linux packaging](how-to/linux-packaging.md), [Testing](how-to/testing.md), [RBL Studio](how-to/ide.md), [Benchmarking](how-to/benchmarking.md) |
| **Reference** | Look up exact syntax, functions or commands | [Syntax](reference/syntax.md), [Diagnostics](reference/diagnostics.md), [Standard library](reference/standard-library.md), [CLI](reference/cli.md), [Manifest](reference/manifest.md), [Runtime ABI](reference/runtime-abi.md), [Limitations](reference/limitations.md) |
| **Explanation** | Understand implementation details and design trade-offs | [Architecture](explanation/architecture.md), [Execution model](explanation/execution-model.md), [Optimizations](explanation/optimization.md), [Performance](explanation/performance.md), [Benchmark results](explanation/performance-results.md), [Design principles](explanation/design.md), [Roadmap](explanation/roadmap.md) |

## Supported version and scope

- Repository/compiler version: `0.8.0`; the source of truth is [`../../VERSION`](../../VERSION).
- Targets: x86-64 Linux (ELF64) and x86-64 Windows (PE/COFF).
- User programs are compiled to machine code through direct x86-64 assembly generation.
- RBL Studio is an optional graphical IDE; the CLI and compiler can be used independently.
- The primary RBL Studio launcher/install path uses the native wxWidgets 3.2 + Scintilla frontend in `rbl-ide/wx-ui/` with the Rust `rbl-studio-worker`. Linux build scripts now stage wxWidgets 3.2.4, Clang integrated assembler, LLD and CPython locally; the Flatpak manifest declares GNOME Platform 51 to supply GTK. The native Linux shell installer still uses the host GTK runtime. The wxWidgets build, native installer/CLI acceptance and portable packaging pass; the Flatpak bundle remains in progress; see [Linux packaging](how-to/linux-packaging.md). The previous Rust/egui source remains as a legacy reference; `RBL_STUDIO_ALLOW_LEGACY=1` permits fallback to a separately built executable from a developer checkout when wx is unavailable, but the legacy executable is not included in the wx installer payload. The 0.8.0 Windows payload is the repository checkout itself (GUI, Rust worker and their wx DLLs under `bin/win-x86_64/studio/`, app-local CPython under `runtime/python`, pinned toolchain under `toolchain/`) and is locally verified; it was published on 2026-11-15 as `v0.8.0-r2`. The older GitHub `v0.8.0` entry remains the pre-release placeholder with no assets. The remaining IDE/soak, pristine-VM and source-delivery/legal gates are recorded in the release manifest.
- Source imports, multi-file compilation and dependency management are not implemented.

RBL 0.8.0 includes bounded f-string format specifications, Runtime ABI 1.2.0, managed dynamic strings, source-position diagnostics and the modular compiler implementation. Page-level `last verified` markers identify the most recent technical review of each page.

The bundled Windows MinGW-w64 toolchain, Linux LLVM toolchain, wx runtime, CPython and Rust worker have separate third-party license/source terms. See the [Windows toolchain notices](../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md), [wx notices](../../rbl-ide/wx-ui/THIRD_PARTY_NOTICES.md), [Linux packaging inventory](how-to/linux-packaging.md) and [corresponding-source audit](../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md). Apache-2.0 applies to RBL-authored files and does not relicense these binaries. The checkout carries the Rust worker and wxWidgets IDE sources plus the toolchain source inventory; the large upstream corresponding-source archives are not bundled, and their exact source locations and hashes are recorded in the toolchain audit. Component-specific legal sign-off and final source delivery remain open.

## Releases

User-visible changes follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/): [Release notes](release-notes.md). Each release uses the **Added**, **Changed** and **Fixed** sections.

## Other language

The [Russian edition](../ru/README.md) contains the same documentation in Russian.
