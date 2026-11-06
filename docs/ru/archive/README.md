# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## RBL documentation

This directory describes the implementation in the repository's current `main` branch. The root `VERSION` file is the version source of truth; this documentation set targets `0.7.1`.

The documentation is grouped by reader need, following the four modes described by [Diátaxis](https://diataxis.fr/): tutorials, how-to guides, reference and explanation. The grouping is about the purpose of each page; it does not imply that every topic needs four separate documents.

## Tutorial

- [`GETTING_STARTED.md`](GETTING_STARTED.md) — prerequisites, first build, new project workflow and tests.

## How-to guides

- [`TOOLING.md`](TOOLING.md) — CLI commands and project manifests.
- [`IDE_MANUAL.md`](IDE_MANUAL.md) — RBL Studio workflows and shortcuts.
- [`WINDOWS.md`](WINDOWS.md) — native Windows toolchain and WSL selection.
- [`GUI_TROUBLESHOOTING.md`](GUI_TROUBLESHOOTING.md) — diagnose IDE startup and display problems.
- [`TESTING.md`](TESTING.md) — run regression, smoke and differential checks.

## Reference

- [`RBL_QUICK_REFERENCE.md`](RBL_QUICK_REFERENCE.md) — implemented language syntax, semantics and limitations.
- [`STDLIB.md`](STDLIB.md) — built-in functions and namespaces.

## Explanation and implementation notes

- [`CURRENT_IMPLEMENTATION.md`](CURRENT_IMPLEMENTATION.md) — current compiler and runtime architecture.
- [`ASM_BACKEND.md`](ASM_BACKEND.md) — targets, value representation and backend contracts.
- [`OPTIMIZER.md`](OPTIMIZER.md) — implemented scalar and loop optimizations.

## Project planning and design

- [`ROADMAP.md`](ROADMAP.md) — current baseline and future engineering priorities.
- [`SYNTAX_DECISIONS.md`](SYNTAX_DECISIONS.md) — proposals and design constraints, not the current language specification.
- [`NEXT_STEPS.md`](NEXT_STEPS.md) — short engineering queue; the roadmap remains the strategic source of truth.
- [`RELEASE_NOTES.md`](RELEASE_NOTES.md) — user-visible changes by repository release.

## Historical material

- [`RUSSIAN_AUDIT.md`](RUSSIAN_AUDIT.md) — original Rust interpreter and an intermediate native prototype.
- [`SYNTAX_V07.md`](SYNTAX_V07.md) — historical note about parenthesized blocks.
- [`QUESTIONS_ANSWERS_SOLUTION.md`](QUESTIONS_ANSWERS_SOLUTION.md) — early native-transition rationale.

Historical pages are retained for context and must not be used as specifications for the current compiler.

## Source-of-truth rules

1. Compiler, runtime and regression tests define the behavior that is actually implemented.
2. `RBL_QUICK_REFERENCE.md` describes the implemented language surface; `STDLIB.md` describes built-ins.
3. `ROADMAP.md` and `SYNTAX_DECISIONS.md` describe future work. A proposal is not an implemented feature.
4. Keep examples executable against the checked-in compiler. Run the relevant regression or smoke test after changing behavior.
5. Keep the root `VERSION`, CLI version and generated project manifest version synchronized.
6. Benchmark claims must state the workload and measurement scope; never present a workload-specific result as a universal language ranking.
