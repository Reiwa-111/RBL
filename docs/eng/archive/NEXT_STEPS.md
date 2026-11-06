# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Engineering next steps

This page is a concise implementation queue for the current compiler generation. It replaces older v0.2 prototype tasks that referred to C code generation and a not-yet-existing direct assembly backend.

## Immediate maintenance

1. Keep the source language reference synchronized with parser behavior and regression cases.
2. Keep the CLI version, root `VERSION`, generated project manifests and release documentation synchronized.
3. Replace outdated comments when runtime behavior changes; in particular, distinguish container collection from string lifetime.
4. Keep Linux/Windows runtime parity tests and documented exceptions current.

## Compiler architecture

1. Split `compiler/rblc_asm.c` into modules while preserving behavior.
2. Define an explicit RBL IR and invariants.
3. Add basic blocks/control-flow representation and validation.
4. Implement liveness and global register allocation only after the IR is stable.
5. Move eligible optimizations onto the IR and add focused correctness tests.

## Language/runtime design

1. Specify complete string ownership and reclamation.
2. Define source module/import semantics independently from plugin loading.
3. Define extension ABI, isolation and versioning before implementing a native plugin host.
4. Specify errors, closures and async runtime memory requirements before implementing them.

The authoritative project priorities and limitations are in `ROADMAP.md`.
