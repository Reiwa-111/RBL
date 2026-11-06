# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Purpose and status

This is the authoritative RBL development plan from version **0.7.2** onward. It supersedes the former roadmap retained under the historical archive. It separates verified implementation, unfinished work and future proposals. It defines sequencing, dependencies and exit criteria, not calendar promises.

**Current baseline:** the root VERSION contains 0.7.2; the CLI and both Linux/Windows compiler binaries report 0.7.2. The current checkout passes 59 direct regression cases and the standard-library smoke test. This verifies the working tree; it does not prove that a v0.7.2 release tag has been published.

## Principles

1. **Specify before implementing.** Every capability needs grammar, semantics, evaluation order, errors and tests.
2. **Correctness before speed.** No optimization may change observable behavior, side effects or when an error occurs.
3. **One language, two backends.** Linux ELF64 and Windows PE/COFF share a frontend and should agree on deterministic behavior. Platform differences must be documented.
4. **Keep layers separate.** AST must not know about physical registers; IR must not depend on object-file format; source imports are not a plugin loader.
5. **Verify compatibility.** Refactoring must not change the value ABI, syntax or evaluation order without an explicit decision and migration note.
6. **No paper features.** Parsing or a prototype alone is not completion. Require implementation, positive and negative tests, documentation and both-platform checks.
7. **Release with evidence.** Clean builds, regression, smoke and differential tests, synchronized RU/EN docs and release notes are required.

## Completed history and current baseline

### v0.7.1 — released foundation

**Status: complete.** This release established direct x86-64 ASM generation from the shared C frontend for Linux ELF64 and Windows PE/COFF. User RBL code is not translated to C. Platform runtimes and a shared C container runtime are present, together with scalar fast paths and limited loop-local register allocation.

The language baseline includes functions and recursion, conditions and loops, break/continue, switch, conditional expressions, strings and f-string interpolation, lists, dictionaries, tuples, null, in/is, record-like structs and receiver-method syntax. The CLI, RBL Studio, standard library and test infrastructure are also present.

The collector uses conservative mark-and-sweep for container objects. It does not reclaim all dynamically allocated strings.

### v0.7.2 — current working baseline

**Status: changes exist in the checkout; publishing a release tag is a separate action.**

- VERSION is the single repository-version source; the CLI and both rblc-asm binaries report 0.7.2.
- Documentation is split into complete Russian and English Diataxis editions.
- Release notes follow Keep a Changelog; the 0.7.1 history remains and a 0.7.2 entry has been added.
- The five-way RBL/C/C++/CPython/ASM benchmark was corrected so compiler optimization cannot remove the C/C++ workload entirely; methodology, results and limitations are documented.
- Some Windows runtime diagnostics and platform caveats were clarified.
- Verified in the current checkout: 59/59 regression cases, standard-library smoke test and CLI/compiler version checks for both targets.

**Not included in 0.7.2:** compiler modularization, RBL IR, CFG, global register allocation, source imports or multi-file compilation.

## Release plan

## v0.8 — compiler and runtime stabilization

**Goal:** prepare the internal architecture for growth without changing language semantics. 0.8 is an engineering release, not the module-system release.

### 0.8.1 — split the compiler source

- Split the monolithic compiler source into common/diagnostics, lexer, AST, parser, semantic analysis, codegen and driver/CLI.
- Define ownership of AST nodes, tokens, strings and temporary buffers; avoid implicit shared ownership.
- Move shared structures into headers and define small internal interfaces.
- Keep one frontend and one semantic contract for both platforms.
- Do not combine a mechanical split with a parser rewrite, syntax changes or IR introduction.

**Done when:** all current tests pass without behavior changes; clean source builds work on both platforms; frontend logic is not duplicated; CLI and value ABI remain intact.

### 0.8.2 — runtime ABI and diagnostics

- Specify the value ABI: tags, payload, arguments, returns, calling convention and caller/callee responsibilities.
- Add a separate compiler/runtime ABI version and compatibility check. Language version and ABI version are distinct.
- Preserve source spans (file, line, column and range) through diagnostics.
- Align Linux/Windows diagnostics except for necessary platform-specific errors.

**Done when:** incompatible compiler/runtime objects are detected during build; frontend errors report correct source locations on both targets.

### 0.8.3 — string memory model

- Inventory string allocation paths: literals, concatenation, str(), file I/O, formatting and container conversions.
- Separate static program-owned strings from dynamic heap strings.
- Define roots, reachability and reclamation of dynamic strings in relation to the container GC.
- Verify that reachable strings are never freed and unreachable strings are not retained indefinitely.
- Add long-running stress tests for strings and mixed string/container graphs.

**Done when:** the lifetime model of every dynamic string is documented and tested. Until then, do not claim complete garbage collection.

### 0.8.4 — finish the started language surface

- Specify and implement f-string format specs: numeric precision, integer bases, width and alignment.
- Define valid type/specifier combinations, invalid-specifier behavior and Unicode/alignment rules.
- Keep Linux/Windows behavior consistent; never pass user-controlled format strings directly into an unsafe varargs call.
- Add positive and negative tests for every supported format.

**0.8 exit criteria:** modular compiler source; checked ABI; documented and tested string lifetime model; implemented and consistent f-string format specs; clean builds, regression, smoke and differential tests. Unfinished items must not be marked as shipped.

## v0.9 — RBL IR, CFG and optimizer

**Goal:** separate language semantics from machine code generation. IR must become a verifiable contract between the frontend and backend.

### 0.9.1 — IR specification and verifier

- Define values, instructions, operands/results, functions, parameters, locals and temporaries.
- Define basic blocks, terminators, control-flow edges and call representation.
- Specify side effects, runtime checks, errors and container operations explicitly.
- Add an IR verifier for instruction arity/types, value references, terminators, reachability and valid edges.
- Carry source-location metadata into IR.
- Do not mandate SSA/phi nodes before selecting and justifying a specific IR model.

**Done when:** all generated IR passes the verifier; dedicated negative tests reject malformed IR fragments.

### 0.9.2 — AST lowering and CFG

- Implement AST → IR without knowledge of registers or x86-64.
- Represent if/switch/loops through basic blocks and explicit branches.
- Preserve evaluation order, short-circuit rules and runtime error points.
- During transition, compare the IR backend with the old path on the same regression corpus.

**Done when:** every current language construct is represented correctly in CFG; tests cover edges, loops, exits and unreachable blocks.

### 0.9.3 — backend from IR

- Move x86-64 ASM codegen to consume IR while preserving the runtime value ABI.
- Keep Linux ELF64 and Windows PE/COFF as target-specific emission/link paths.
- Remove the AST-to-ASM path only after parity, clean builds and differential tests pass.

### 0.9.4 — analysis and optimizations

Implement in this order:

1. Liveness analysis for virtual values.
2. Global/whole-function register allocation, including caller/callee-saved registers and spill slots.
3. Constant folding that preserves errors and evaluation order.
4. Dead-code and dead-store elimination that accounts for side effects.
5. Common-subexpression elimination only for expressions proven pure under the aliasing model.

**0.9 exit criteria:** IR verifier and CFG tests; optimized/unoptimized execution equivalence; Linux/Windows differential parity; focused regression tests for every optimization; reproducible benchmarks on identical workloads. Speed gains do not replace correctness checks.

## v1.0 — language module system

**Goal:** move from “one .rbl file per program” to multi-file projects with explicit source modules and deterministic builds. The module system is the defining user-facing feature of 1.0.

### 1.0.1 — specify module and import semantics

Before implementation, create an RFC and freeze a testable contract:

- **Module unit:** one .rbl file with a module identity; an executable project has one entry module.
- **Import forms:** the previously discussed forms are import module, from module import name, and path import such as import "path.rbl". Aliases, re-exports and exact relative-path rules must be fixed in the RFC, not inferred from implementation.
- **Resolution:** deterministic rules relative to the importing file and project root; ambiguous matches are errors. Do not search arbitrary files through the process working directory implicitly.
- **Identity:** normalized project-relative path so one physical file is not loaded twice under different path spellings.
- **Exports and visibility:** define which declarations are externally visible, the private/public boundary, qualified names and conflict rules.
- **Graph:** every import is a dependency edge; each module is loaded, parsed and compiled once per build graph.
- **Cycles:** initial safe policy is to detect cycles and report the complete import cycle. Cycle support can be added only after initialization order and partially initialized module semantics are specified.
- **Initialization:** decide whether executable module-level expressions are permitted and when they run. Preferred baseline: declarative modules without implicit execution of arbitrary code on import.
- **Errors:** missing module/export, duplicate identity/symbol, invalid path and cycle errors include the import chain and source location.

### 1.0.2 — compile the module graph

- Separate the module resolver/loader from each file's lexer/parser.
- Build and validate the dependency graph before semantic analysis and code generation.
- Add module symbol tables and deterministic resolution of local/imported names.
- Combine modules into one program IR or coordinated IR units with one entry point.
- Support calls across modules without runtime dynamic loading.
- Preserve source locations from every file for diagnostics and debug metadata.
- Do not change the runtime value ABI solely to support source imports.

### 1.0.3 — project model and build system

- Define minimal rbl.toml fields: project name/version, entry module, source roots and target settings.
- Project version is not the RBL compiler version.
- check/build/run must operate on the complete dependency graph.
- Cache keys must include source content, dependencies, compiler version, ABI version and target.
- Changing an imported file must invalidate all dependent build outputs.
- Include local project modules in 1.0. An external registry, package downloads and semver dependency resolver are a separate post-1.0 stage.

### 1.0.4 — completion criteria

- Existing single-file programs continue to build without mandatory migration.
- Tests cover direct/transitive/repeated imports, aliases, visibility, name conflicts, missing modules/exports, cycles, relative paths and cache invalidation.
- Each module is compiled once per build.
- Linux and Windows produce equivalent behavior.
- Unchanged builds use a valid cache; dependency changes rebuild affected consumers.
- Errors identify the originating path and location, not just a generic linker failure.
- Resolution, visibility, initialization, cycle policy, project layout and migration from single-file programs are documented.
- The public module contract is secured by regression/integration tests.

**Out of scope for 1.0:** native plugin host for the IDE/compiler; external package registry; guaranteed binary ABI between independently compiled modules; async scheduler and complex closures before their memory/lifetime semantics are defined.

## After 1.0

- Package manager, registry, lockfile and reproducible external dependency acquisition.
- Native extensions/plugins with separate ABI, permissions, lifecycle, versioning and failure isolation.
- Closures/lambdas, default/variadic/named arguments and overload resolution.
- async/await, task scheduler and integration of heap-resident frames with GC.
- try/catch, raise and defer after specifying unwinding and cleanup semantics.
- Source debugger, breakpoints, variable inspection and Windows PE line tables.
- Additional architectures, a bytecode VM for debug/development and a self-hosted frontend.

## Cross-cutting workflow for every capability

1. RFC: problem, examples, grammar, semantics, errors and limitations.
2. Model: evaluation order, side effects, aliasing, ownership/lifetime and ABI.
3. Tests first: positive, negative, boundary, regression and differential tests.
4. Implement only the required frontend/IR/backend/runtime layers without duplication.
5. Validate clean builds on both platforms, regression, smoke, differential and applicable fuzz/property tests.
6. Document tutorial/how-to/reference/explanation in Russian and English.
7. Release with Added/Changed/Fixed notes, version, manifest and shipped-binary checks.

## Roadmap governance

This is the single authoritative release roadmap. design.md records principles and links here; archive/ROADMAP.md and archive/NEXT_STEPS.md are historical snapshots. A task may move to “done” only after implementation and its exit criteria pass. When priorities change, update both language editions first; update release notes only when a release actually ships.
