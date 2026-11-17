# last verified: v0.8.1 | 2026.11.17 | RELEASE PREPARATION AND DOCUMENTATION STATUS

## RBL documentation

This documentation describes the **RBL 0.8.1 language, compiler, runtime and toolchain contract** and follows [Diataxis](https://diataxis.fr/): tutorials, how-to guides, reference material and explanations. It documents implemented behavior, not proposed features.

## Start here

- [0.8.x tutorials](tutorials/README.md) — ten focused lessons on 0.8.x additions and IDE workflows.

| Section | Purpose | Main pages |
|---|---|---|
| **Beginner course** | Follow 20 guided lessons with runnable examples | [Beginner course](../../examples/beginner/README_ENG.md) |
| **How-to** | Complete a specific practical task | [Project workflow](how-to/project-workflow.md), [Windows and WSL](how-to/windows-toolchain.md), [Linux packaging](how-to/linux-packaging.md), [Testing](how-to/testing.md), [RBL Studio](how-to/ide.md), [Benchmarking](how-to/benchmarking.md) |
| **Reference** | Look up exact syntax, functions or commands | [Syntax](reference/syntax.md), [Diagnostics](reference/diagnostics.md), [Standard library](reference/standard-library.md), [CLI](reference/cli.md), [Manifest](reference/manifest.md), [Runtime ABI](reference/runtime-abi.md), [Limitations](reference/limitations.md) |
| **Explanation** | Understand implementation details and design trade-offs | [Architecture](explanation/architecture.md), [Execution model](explanation/execution-model.md), [Optimizations](explanation/optimization.md), [Performance](explanation/performance.md), [Benchmark results](explanation/performance-results.md), [Design principles](explanation/design.md), [Roadmap](explanation/roadmap.md) |

## Supported version and scope

- Repository/compiler version: `0.8.1`; the source of truth is [`../../VERSION`](../../VERSION).
- Targets: x86-64 Linux (ELF64) and x86-64 Windows (PE/COFF).
- User programs are compiled to machine code through direct x86-64 assembly generation.
- RBL Studio is an optional graphical IDE; the CLI and compiler can be used independently.
- The primary RBL Studio launcher/install path uses the native wxWidgets 3.2 + Scintilla frontend in `rbl-ide/wx-ui/` with the Rust `rbl-studio-worker`. The native Linux build and shell install path were verified on Ubuntu 24.04 with system wxWidgets 3.2.4 and host GTK 3. The Flatpak payload and sandbox CLI checks passed locally; interactive GUI/portal and Flathub acceptance are not claimed. The native first-run setup wizard provides prerequisite diagnostics, per-user setup state and asynchronous Install / Repair when a complete installer payload is available. Windows mixed-DPI acceptance was confirmed by the user after live testing. RBL Studio 0.8.1 was published as [`v0.8.1`](https://github.com/Reiwa-111/RBL/releases/tag/v0.8.1) on 2026-11-17; `v0.8.0-r2` remains the previous source snapshot. The older GitHub `v0.8.0` entry remains the pre-release placeholder.
- Source imports, multi-file compilation and dependency management are not implemented.

RBL 0.8.1 retains the 0.8.0 language/compiler contract, including bounded f-string format specifications, Runtime ABI 1.2.0, managed dynamic strings, source-position diagnostics and the modular compiler implementation. Page-level `last verified` markers identify the most recent technical review of each page.

The bundled Windows MinGW-w64 toolchain, Linux LLVM toolchain, wx runtime, CPython and Rust worker have separate third-party license/source terms. See the [Windows toolchain notices](../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md), [wx notices](../../rbl-ide/wx-ui/THIRD_PARTY_NOTICES.md), [Linux packaging inventory](how-to/linux-packaging.md) and [corresponding-source audit](../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md). Apache-2.0 applies to RBL-authored files and does not relicense these binaries. The GitHub release publishes the repository source tree, which already contains the Rust worker and wxWidgets IDE sources plus the toolchain source inventory; separately assembled upstream corresponding-source archives are not release deliverables or publication gates. Third-party legal obligations still apply before redistributing bundled binaries and remain a separate review item.

## Releases

User-visible changes follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/): [Release notes](release-notes.md). Each release uses the **Added**, **Changed** and **Fixed** sections.

## Other language

The [Russian edition](../ru/README.md) contains the same documentation in Russian.
