# last verified: v0.8.0 | 2026.11.09 | OPTIMIZATION GUIDE RECHECK

## Compiler-side lookup indexes

RBL 0.8.0 retains the existing AST analysis and direct assembly-generation model. It does not introduce a separate IR optimizer. The main compiler-side performance change replaces repeated linear name scans with private hash indexes.

### Hashing and collision handling

The compiler uses allocation-free FNV-1a string hashing in `common.c`. The variable index, function index and assembly string-pool index use open addressing with linear probing. Capacities are powers of two, and indexes grow before their load factor becomes high. A hash match is always confirmed with `strcmp`; hashes are not treated as unique identifiers.

The indexes store array positions rather than moving or reordering the underlying entries. This preserves deterministic insertion order and emitted labels. Function indexing deliberately overwrites an existing name's slot while walking declarations from first to last, preserving the language's existing last-definition-wins behavior.

### RBL-level example

For a small readable RBL workload that repeatedly resolves variables and functions and reuses string values, see [`examples/compiler/lookup_demo.rbl`](../../../examples/compiler/lookup_demo.rbl) and its [expected output](../../../examples/compiler/README.md). It demonstrates ordinary language usage; it is not a hash-table collision test or a performance benchmark.

### Index ownership

- `VarVec` owns its variable-name array and its index; `varvec_free` releases both.
- `StrPool` owns constant text, labels, and its index; `pool_free` releases all three.
- `Program` owns its function index after the first lookup. The parser completes the function array before semantic analysis/code generation performs lookups, so the index is built against an immutable function list. `program_index_free` releases the index after compilation.

Compiler AST and token allocations follow the compiler process lifetime. This is separate from the Runtime's managed ownership model for dynamic strings and containers.

## Existing code-generation fast paths

When analysis proves that an expression or local variable has an integer/Boolean type, the compiler can use unboxed machine values instead of the general tagged representation (`rax` tag plus `rdx` payload). Eligible integer arithmetic, comparisons, Boolean operations, selected integer-only functions and restricted integer loops can avoid some tag handling and runtime helper calls.

For a restricted class of loops, hot integer values can remain in callee-saved registers. Unsupported control flow, dynamic values and uncertain bindings fall back to ordinary tagged code generation. These are specialized local optimizations, not a global register allocator.

The compiler also omits per-statement GC safe-point calls for individual integer/Boolean scalar statements whose expressions are proven non-allocating. The same proof allows a fast-loop body to omit redundant per-iteration checks. The enclosing `for` statement still emits a safe point before entering the loop, and the next potentially allocating statement checks again. Operations that allocate, mutate pointer roots, or call code with unknown allocation behavior retain safe points.

## Compiler modularity and build performance

The same RBL 0.8.0 compiler source bodies were measured as one amalgamated translation unit and as the seven checked-in modules. Across 11 workloads, the geometric-mean modular/one-TU compile-time ratio was **1.013×** (range 0.983×–1.146×), and all 11 generated assembly hashes matched. The translation-unit split therefore remains close to parity for compiler latency and does not change output for these inputs.

Full serial C compilation measured 1,718.26 ms for seven modules versus 1,522.90 ms for one amalgamated source. The object-based Makefile reduces this cost through parallel and incremental builds: a disposable-sandbox measurement recorded `make -j6 compiler` at 797.81 ms, `-j12` at 805.88 ms, serial `-j1` at 1,772.70 ms, a one-module rebuild at 583.08 ms and a no-op build at 28.87 ms. These host-specific compiler/build measurements are not generated-program runtime results. See [`benchmarks/PERFORMANCE_AUDIT.md`](../../../benchmarks/PERFORMANCE_AUDIT.md) for the environment, method and caveats.

## What the optimizer does not do

The compiler still has no general RBL IR, CFG/data-flow pipeline, whole-function liveness analysis, global register allocator, IR-level constant folding, dead-code elimination or common-subexpression-elimination pipeline. These remain future roadmap work.
