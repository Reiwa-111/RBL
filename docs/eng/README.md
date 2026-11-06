# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## RBL documentation

This documentation describes **RBL 0.7.2** and follows [Diátaxis](https://diataxis.fr/): tutorials, how-to guides, reference material and explanations. It documents the compiler and runtime that are actually implemented, not features that may exist in a future release.

## Start here

| Section | Purpose | Main pages |
|---|---|---|
| **Beginner course** | Follow 20 guided lessons with runnable examples | [Beginner course](../../examples/beginner/README_ENG.md) |
| **How-to** | Complete a specific practical task | [Project workflow](how-to/project-workflow.md), [Windows and WSL](how-to/windows-toolchain.md), [Testing](how-to/testing.md), [RBL Studio](how-to/ide.md), [Benchmarking](how-to/benchmarking.md) |
| **Reference** | Look up exact syntax, functions or commands | [Syntax](reference/syntax.md), [Standard library](reference/standard-library.md), [CLI](reference/cli.md), [Manifest](reference/manifest.md), [Limitations](reference/limitations.md) |
| **Explanation** | Understand implementation details and design trade-offs | [Architecture](explanation/architecture.md), [Execution model](explanation/execution-model.md), [Optimizations](explanation/optimization.md), [Performance](explanation/performance.md), [Benchmark results](explanation/performance-results.md), [Design principles](explanation/design.md) |

## Supported version and scope

- Repository version: `0.7.2`; the source of truth is [`../../VERSION`](../../VERSION).
- Targets: x86-64 Linux (ELF64) and x86-64 Windows (PE/COFF).
- User programs are compiled to machine code through direct x86-64 assembly generation.
- RBL Studio is an optional graphical IDE; the CLI and compiler can be used independently.
- Source imports, multi-file compilation and dependency management are not implemented.

If an archived example conflicts with this documentation or the current version's tests, the current source code and regression tests take precedence.

## Releases

User-visible changes follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/): [Release notes](release-notes.md). Each release uses the **Added**, **Changed** and **Fixed** sections.

## Historical material

[`archive/`](archive/) contains documents preserved from the previous documentation structure. It is historical reference material, not the normative RBL 0.7.2 specification. Use the four sections above for current information.

## Other language

The [Russian edition](../ru/README.md) contains the same documentation in Russian.
