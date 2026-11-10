# last verified: v0.8.0 | 2026.11.10 | DOCUMENTATION RELEASE READINESS

## RBL documentation

This documentation describes the **RBL 0.8.0 language, compiler, runtime and toolchain contract** and follows [Diátaxis](https://diataxis.fr/): tutorials, how-to guides, reference material and explanations. It documents implemented behavior, not proposed features.

## Start here

| Section | Purpose | Main pages |
|---|---|---|
| **Beginner course** | Follow 20 guided lessons with runnable examples | [Beginner course](../../examples/beginner/README_ENG.md) |
| **How-to** | Complete a specific practical task | [Project workflow](how-to/project-workflow.md), [Windows and WSL](how-to/windows-toolchain.md), [Testing](how-to/testing.md), [RBL Studio](how-to/ide.md), [Benchmarking](how-to/benchmarking.md) |
| **Reference** | Look up exact syntax, functions or commands | [Syntax](reference/syntax.md), [Diagnostics](reference/diagnostics.md), [Standard library](reference/standard-library.md), [CLI](reference/cli.md), [Manifest](reference/manifest.md), [Runtime ABI](reference/runtime-abi.md), [Limitations](reference/limitations.md) |
| **Explanation** | Understand implementation details and design trade-offs | [Architecture](explanation/architecture.md), [Execution model](explanation/execution-model.md), [Optimizations](explanation/optimization.md), [Performance](explanation/performance.md), [Benchmark results](explanation/performance-results.md), [Design principles](explanation/design.md), [Roadmap](explanation/roadmap.md) |

## Supported version and scope

- Repository/compiler version: `0.8.0`; the source of truth is [`../../VERSION`](../../VERSION).
- Targets: x86-64 Linux (ELF64) and x86-64 Windows (PE/COFF).
- User programs are compiled to machine code through direct x86-64 assembly generation.
- RBL Studio is an optional graphical IDE; the CLI and compiler can be used independently.
- The primary RBL Studio launcher/install path uses the native wxWidgets 3.2 + Scintilla frontend in `native/wx-ui/` with the Rust `rbl-studio-worker`. The previous Rust/egui source remains as a legacy reference; `RBL_STUDIO_ALLOW_LEGACY=1` permits fallback to a separately built executable from a developer checkout when wx is unavailable, but the legacy executable is not included in the wx installer payload. The wx application is still UNRELEASED pending acceptance and distribution checks.
- Source imports, multi-file compilation and dependency management are not implemented.

RBL 0.8.0 includes bounded f-string format specifications, Runtime ABI 1.2.0, managed dynamic strings, source-position diagnostics and the modular compiler implementation. Page-level `last verified` markers identify the most recent technical review of each page.

The bundled Windows MinGW-w64 toolchain and wx runtime have separate third-party license terms. See the [toolchain notices](../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md), [wx notices](../../native/wx-ui/THIRD_PARTY_NOTICES.md) and [corresponding-source audit](../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md). Apache-2.0 applies to RBL-authored files and does not relicense those binaries. Source materials are assembled locally, but distribution obligations remain open until the materials accompany a binary distribution or a written offer is legally reviewed.

## Releases

User-visible changes follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/): [Release notes](release-notes.md). Each release uses the **Added**, **Changed** and **Fixed** sections.

## Other language

The [Russian edition](../ru/README.md) contains the same documentation in Russian.
