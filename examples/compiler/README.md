# last verified: v0.8.0 | 2026.11.09 | COMPILER EXAMPLE

# Compiler implementation example

`lookup_demo.rbl` is a small, ordinary RBL 0.8.0 program for exploring compiler-side indexed lookup behavior.

It deliberately reuses local variable names, calls the same functions from multiple places and returns repeated string values. The compiler resolves these names through private indexes:

- the variable index is owned by `VarVec` in semantic analysis;
- the function index is built over the completed function list and preserves the existing last-definition-wins rule;
- the assembly string pool keeps its own index while retaining deterministic insertion order.

These indexes are implementation details. RBL programs do not declare, import or manipulate compiler modules, and the example does not expose hash values, bucket layout or collision handling as a language feature.

Run it from the repository root:

```bash
python3 rbl.py check examples/compiler/lookup_demo.rbl
python3 rbl.py run examples/compiler/lookup_demo.rbl
```

Expected output:

```text
record=1; category=shared category; total=72
record=2; category=shared category; total=108
record=3; category=secondary category; total=48
record=4; category=unknown category; total=12
```

The program is a readable lookup workload, not a microbenchmark. Use the reproducible scripts under `benchmarks/` to measure compiler performance or stress hash collisions.
