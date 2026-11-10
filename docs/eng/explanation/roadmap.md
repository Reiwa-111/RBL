# last verified: v0.8.0 | 2026.11.10 | RELEASE READINESS RECHECK

## Purpose and status

This is the authoritative RBL development plan. It separates the implemented 0.8.0 baseline, unfinished engineering work and future proposals. It defines sequencing, dependencies and exit criteria, not calendar promises.

**Current source tree:** root VERSION, CLI and bundled compiler binaries report 0.8.0. This roadmap describes the implementation in that source tree and the remaining engineering milestones. Release publication is managed separately.

## Principles

1. **Specify before implementing.** Every capability needs grammar, semantics, evaluation order, errors and tests.
2. **Correctness before speed.** No optimization may change observable behavior, side effects or when an error occurs.
3. **One language, two backends.** Linux ELF64 and Windows PE/COFF share a frontend and should agree on deterministic behavior. Platform differences must be documented.
4. **Keep layers separate.** AST must not know about physical registers; IR must not depend on object-file format; source imports are not a plugin loader.
5. **Verify compatibility.** Refactoring must not change the value ABI, syntax or evaluation order without an explicit decision and migration note.
6. **No paper features.** Parsing or a prototype alone is not completion. Require implementation, positive and negative tests, documentation and both-platform checks.
7. **Release with evidence.** Clean builds, regression, smoke and differential tests, synchronized RU/EN docs and release notes are required.

## Current baseline — RBL 0.8.0

RBL 0.8.0 is a native, single-source-file language/toolchain targeting x86-64 Linux ELF64 and x86-64 Windows PE/COFF. One shared C frontend and code generator emit assembly directly; user RBL source is not translated to C.

The compiler is split into `common.c`, `lexer.c`, `ast.c`, `parser.c`, `sema.c`, `codegen.c` and `driver.c`, with private interfaces in `rblc_internal.h`. Variable, function and string-pool lookup use open-addressed hash indexes. The frontend retains one semantic implementation for both platform targets.

The language includes typed function parameters, recursion, conditionals, loops, `switch`, lists, dictionaries, tuples, record-like structs, strings, f-string interpolation and bounded f-string format specifications. The standard library exposes global built-ins and the `rbl.io`, `rbl.math`, `rbl.string`, `rbl.fs`, `rbl.time` and `rbl.random` namespaces.

Runtime ABI `1.2.0` describes the tagged value representation, calling convention and required runtime features. The Runtime manages dynamic strings and containers through a conservative, non-moving mark-and-sweep collector. Compiler diagnostics preserve source coordinates through lexing, parsing, semantic checks and code generation. The CLI, optional RBL Studio IDE, testing tools and benchmark suites are part of the toolchain.

The current language and runtime contract is specified in the [syntax](../reference/syntax.md), [standard-library](../reference/standard-library.md), [execution-model](execution-model.md), [Runtime ABI](../reference/runtime-abi.md) and [limitations](../reference/limitations.md) references.

## Next approved milestone

The next language/compiler work package is **v0.9.1 — IR specification and verifier**. The current compiler is still AST-to-assembly; no IR, CFG or IR backend is implemented. Start with the IR RFC/contract and malformed-IR verifier tests. Do not begin AST lowering, CFG construction or backend migration until that contract is reviewed and frozen. The v1.0 source-module system is a later milestone and likewise requires its own RFC before implementation.

## Release plan

## RBL 0.8.0 — compiler and runtime stabilization

**Goal:** stabilize the compiler/runtime architecture, introduce bounded f-string format specifications and preserve existing language behavior outside that explicit syntax addition. RBL 0.8.0 does not include the source-module system.

### 0.8.0 — compiler source modularization and lookup performance

**Status: implemented.**

- The compiler source is organized into `common.c`, `lexer.c`, `ast.c`, `parser.c`, `sema.c`, `codegen.c` and `driver.c`, with private contracts in `rblc_internal.h`.
- A single frontend and semantic contract serve both platforms; semantic fast-path eligibility is decided during semantic analysis, not by the assembly emitter.
- Variable, function and string-pool lookup use open-addressed hash indexes while preserving array order and last-definition-wins behavior.
- Makefile and Windows/WSL bootstrap paths track compiler source, header and version dependencies.
- Module-isolation measurements compare compiler-only latency; the one-TU/seven-TU builds emit byte-identical assembly on deterministic workloads.

**Completion:** full regression and negative-diagnostic suites pass; clean source builds work on both platforms; all examples compile; frontend logic is shared; CLI behavior is intact; the Runtime ABI is explicitly versioned and checked; benchmark results are reproducible.

### 0.8.0 work package - Runtime ABI and diagnostics

- The value ABI defines tags, payload, arguments, returns, calling convention and caller/callee responsibilities.
- Compiler/runtime ABI has its own version and compatibility check, independent of the language version.
- Source positions (file, UTF-8 byte offset, one-based line and column) are retained through lexer, parser, AST, semantic validation and code generation.
- Linux/Windows diagnostics use the same source-location contract apart from necessary platform-specific errors.

**Current contract:** Runtime ABI `1.2.0` includes tagged values, the System V AMD64 helper convention, stack value arrays, containers, GC-managed strings and the f-string formatting helper. The diagnostic pipeline preserves source coordinates through every compiler stage.

**Completion:** incompatible compiler/runtime pairs are rejected at link time or before entering RBL main; lexer/parser/semantic/codegen diagnostics report correct source positions and excerpts on both targets. This does not imply general compile-time type checking or source spans/ranges.

### 0.8.0 work package - string memory model

- String allocation paths cover literals, concatenation, `str()`, file I/O, formatting and container conversions.
- Static program-owned strings are distinct from dynamic heap strings.
- Root discovery, reachability and reclamation of dynamic strings are integrated with the container collector.
- Ownership and stress tests cover reachable strings, collection, mixed string/container graphs and allocation failures.
- Long-running tests exercise repeated allocation and I/O under GC pressure.

**Completion criteria:** the lifetime model of every dynamic string is documented and tested.

**Status: implemented and validated.** Native Windows and WSL suites pass, including fault-injected initial/growing/compacting registry, mark-stack/survivor/replacement-registry allocation failures, rollback/retry, native stack-borrowed roots, explicit-release idempotence, and repeated `input()`/`read_file()` under GC pressure. The differential runner checks 83 selected source cases: 61 successful programs and 11 expected runtime-error programs compare exit code/stdout/stderr, 7 expected-negative fixtures are rejected with matching diagnostics, and 4 with platform/random output must build and exit successfully without output comparison. All 30 diagnostic differential cases pass. The repository contains 121 RBL source files across `examples/` and compiler-test sources in `tests/` (53 examples and 68 test sources; one uppercase `.RBL` IDE fixture is intentionally excluded): 114 expected successful compilations and 7 expected negative cases. All 114 positive sources compile and assemble on both Windows and WSL; all 53 examples pass `check` on both targets. Conservative retention from stale stack words and the absence of a native pinning/root-handle API remain documented limitations.

### 0.8.0 work package - f-string format specifications

**Status: implemented and validated.** The syntax reference defines `[[fill]align][width][.precision][type]`, width 0..1,000,000 code points, precision 0..17, integer bases, float conversion types, `s` conversion, defaults and runtime incompatibilities. Parser diagnostics cover malformed specifications, explicit dynamic/nested-spec rejection at the beginning or after width, and limits; the shared runtime formatter uses fixed conversion formats and managed RBL string allocation. Direct regression covers bases including the signed 64-bit minimum, precision through 17 digits, width through 1,000,000, alignment, fill (including `<`, `>` and `^` as fill characters), Unicode width and explicit zero fill; the GC stress case exercises repeated formatting allocations. All 61 direct regression cases and 63 diagnostic cases pass, and the f-string case produces identical output on WSL/Linux and native Windows.

**0.8 completion state:** the modular compiler, versioned ABI, documented string lifetime model, bounded f-string format specs and cross-platform verification criteria are implemented. Remaining limitations are listed in the language reference and manifest.

### 0.8.0 release checklist

This checklist records the implementation and verification state of RBL 0.8.0. It does not authorize publication.

**Scope clarification:** 6/6 means the compiler/Runtime 0.8.0 work packages and their recorded verification are complete. It does not mean the complete desktop product is release-ready. IDE interactive acceptance and the Windows bundled-toolchain corresponding-source/source-offer review are separate open distribution gates tracked in [`RELEASE_MANIFEST.txt`](../../../RELEASE_MANIFEST.txt).

1. [x] **Architecture** — modular compiler, private module boundaries, lookup indexes, preserved semantics and reproducible compiler/build measurements.
2. [x] **Runtime ABI** — versioned descriptor, link/startup compatibility checks, value layout, calling convention and required feature bits.
3. [x] **Diagnostics** — source positions and excerpts through lexer/parser/semantic/codegen, including UTF-8 and cross-platform parity.
4. [x] **Memory** — documented dynamic-string/container ownership, deterministic collector OOM rollback/retry, native C borrow limits and repeated I/O under GC pressure.
5. [x] **F-string formatting** — bounded grammar, parser/runtime agreement, runtime type errors, managed output and boundary/differential coverage.
6. [x] **Final verification** — 61 direct regression cases pass; all 114 positive sources across the 53-example/68-test inventory compile and assemble, while 7 negative sources are rejected; the 63-scenario diagnostics suite passes; the 83-case differential runner passes with 61 successful-output comparisons, 11 expected-runtime-error comparisons, 7 expected-negative diagnostic comparisons and 4 nondeterministic build/exit comparisons, and all 30 diagnostic differential cases match. Runtime ABI, instrumented Linux Runtime call alignment, cache isolation, deterministic OOM/GC/ownership and repeated I/O-under-GC tests pass. The tracked corpus and documentation validators pass. Runtime, module-isolation and Makefile build benchmarks are recorded with environment and measurement caveats. Fresh source-only compiler/Runtime builds and execution pass in Windows and WSL sandboxes. English/Russian documentation, README, VERSION and the release-candidate manifest are synchronized.

[`RELEASE_MANIFEST.txt`](../../../RELEASE_MANIFEST.txt) is the single manifest for the RBL 0.8.0 release candidate. Its pre-release status must remain explicit until publication is authorized.

## v0.9 — RBL IR, CFG and optimizer

**Goal:** separate language semantics from machine code generation. IR must become a verifiable contract between the frontend and backend.

### 0.9.1 — IR specification and verifier

**Current artifact:** [IR v0.1 design proposal](ir-rfc.md) (proposed, not frozen). It recommends SSA-like immutable temporaries with block arguments and explicit mutable local slots; the open decisions in the RFC require review before this becomes a stable contract.

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

This is the single authoritative release roadmap. design.md records principles and links here. A task may move to “done” only after implementation and its exit criteria pass. When priorities change, update both language editions first. Candidate changes may be summarized in the release notes; a publication date is added only when the version is actually published.
