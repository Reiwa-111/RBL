# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Historical project note: the first native transition

This file preserves the intent of the early RBL Studio/native-compiler work. It is not an up-to-date implementation guide; use [`CURRENT_IMPLEMENTATION.md`](CURRENT_IMPLEMENTATION.md), [`ASM_BACKEND.md`](ASM_BACKEND.md) and [`WINDOWS.md`](WINDOWS.md) for the current architecture.

## Original goal

The original Rust implementation interpreted an AST. The native transition aimed to retain the observable language semantics while replacing AST interpretation with compiled machine code.

## Current outcome

The current compiler is a C bootstrap program that lexes and parses RBL, builds an AST, performs conservative analysis/optimizations and emits x86-64 assembly. Linux uses an assembly runtime; Windows uses a C runtime compiled with the System V calling convention; both targets share the C container runtime. User programs are not translated to C.

## Why the compiler frontend is not written in assembly

The frontend is not the performance-critical code executed by compiled RBL programs. Keeping the lexer, parser and AST implementation in C makes the bootstrap compiler easier to maintain. A future frontend rewrite or self-hosting effort can preserve the runtime ABI if the interface is deliberately maintained.

## Historical scope

The early prototype used a C-generating backend and initially relied on WSL for Windows-hosted development. Those stages were superseded. Do not use this document to infer the current build pipeline, test count or backend availability.
