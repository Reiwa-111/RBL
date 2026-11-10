# RBL IR v0.1 — design proposal

**Status: PROPOSED; not frozen.** This is the candidate internal representation for v0.9.1. It does not change the RBL language, Runtime ABI or current AST-to-assembly compiler. Review the open decisions before treating it as a stable compiler contract.

## Scope

IR is the contract between the current frontend and a future backend. It must make evaluation order, control flow, runtime checks, side effects and source locations explicit enough for a verifier to reject malformed programs before code generation.

The first version represents one source file per compilation unit, matching RBL 0.8.0. It adds no imports, project modules, closures, exceptions, async execution, public bytecode format or cross-version binary IR ABI. Runtime ABI 1.2.0 is unchanged.

## Representation model

### Module, functions, locals and values

- `IRModule` owns one source-file table, constant pool and ordered function list.
- Functions retain source order and stable module-local `FunctionId`s. Among user functions, duplicate names resolve last-definition-wins; builtins resolve before user functions and therefore shadow a user function with the same spelling. Parser-reserved call forms (`clamp`, `degrees`, `radians` and struct constructors) are rewritten before ordinary function lookup; the IR records the resolved semantic operation instead of repeating name resolution in the backend.
- `LocalId` identifies a mutable function-wide local slot. `if`/`elif`/`else` and loop bodies do not create lexical scopes. Source bindings remain slots because `set`/`let`, loop-variable binding and runtime bound/type checks are observable semantics.
- `ValueId` identifies an immutable expression result. Each value has exactly one definition: an instruction result or a basic-block parameter. IDs are unique within a function.
- IR type facts are `unit`, `int`, `float`, `bool`, `string`, `list`, `dictionary`, `tuple`, `null` or `dynamic`. `dynamic` means Runtime checks remain necessary; this is not a new static type system.
- Source values retain the existing 16-byte tagged Runtime representation. Unboxed integer/Boolean registers are backend optimizations and must be observationally equivalent to tagged-value behavior.
- A declared function return type is currently a static inference hint, not a Runtime return-value check. Parameter type annotations are checked at function entry, after all call-site argument expressions have evaluated and before the callee body starts. Receiver parameters in method declarations deliberately have no Runtime type check. IR must preserve these distinctions.

### CFG and merge values

A function contains basic blocks. Each block has a unique `BlockId`, zero or more typed parameters, an ordered instruction list and exactly one terminator. There is no implicit fall-through; the entry block is explicit.

**Proposal: SSA-like immutable temporaries with block arguments, not `phi` instructions.** Each predecessor passes immutable expression values selected on that edge as arguments to the destination block. Loop back-edges may pass loop-carried temporaries the same way. Mutable source locals are shared function slots accessed through `LOAD_LOCAL`/`STORE_LOCAL`; they are not converted to SSA or passed as block arguments in v0.1. This is not full SSA: local-slot and container memory/alias effects remain explicit.

Terminators:

- `BR target(args...)` — unconditional edge.
- `COND_BR condition, true_target(true_args...), false_target(false_args...)` — conditional edge.
- `RETURN value` — return a value; a source `return` without an expression returns `unit`. Falling off the end of a function also returns `unit`.
- `UNREACHABLE` — a block that cannot be entered during valid execution, normally after a guaranteed trap.

### Instruction families

| Family | Candidate operations | Semantic contract |
|---|---|---|
| Constants | `CONST_INT`, `CONST_FLOAT`, `CONST_BOOL`, `CONST_STRING`, `CONST_NULL`, `CONST_UNIT` | Define one typed value; strings refer to the module constant pool. |
| Locals | `LOAD_LOCAL`, `CHECK_UNBOUND`, `CHECK_REBIND`, `STORE_LOCAL` | Preserve `set`/`let` ordering. `CHECK_REBIND` is fused: it requires an existing local and exact Runtime-tag equality with its current value. `set` has no type restriction. |
| Unary/binary | `UNARY_NEG`, `UNARY_NOT`, `BINARY_*`, `COMPARE_*`, `IDENTITY`, `LOGICAL_AND`, `LOGICAL_OR` | Preserve operand order and Runtime errors; `and`/`or` are eager. `==` on list/tuple/dictionary values is deep; dictionary equality is key/value based and order-insensitive. `is` compares the Runtime tag and payload identity. String `+` allocates. |
| Checks | `EXPECT_BOOL`, `EXPECT_INT`, `CHECK_USER_ARITY`, `CHECK_PARAM_TYPE` | Explicit ordered checks with source locations. User-function arity is checked at runtime before argument evaluation; typed parameter checks run in the callee after all arguments evaluate and before its body. Builtin arity errors are compiler diagnostics before execution; struct-constructor arity is a parser diagnostic. |
| Calls | `CALL_FUNCTION`, `CALL_BUILTIN`, `CALL_UNKNOWN` | Carry resolved function/builtin IDs or an unresolved function/method name, ordered args and result type. Runtime helper ABI selection belongs to the backend. |
| Containers | `LIST_NEW/PUSH/GET/SET`, `DICT_NEW/SET`, `TUPLE_NEW/PUSH`, `INDEX_GET/SET`, `CONTAINS` | Explicit allocation, mutation, bounds/type checks and missing-key `unit` behavior. `INDEX_GET` accepts integer list/tuple indices and dictionary keys; `INDEX_SET` mutates lists/dictionaries but rejects tuples. The direct `list_set` builtin also rejects tuples. `in` checks dictionary keys, scalar list/tuple elements and string substrings; nested container element membership is not recursive. |
| Strings | `STRING_CONCAT`, `STRING_METHOD`, `FORMAT_VALUE` | Preserve immutable string semantics, managed allocation and f-string format metadata. |
| Runtime | `GC_SAFEPOINT`, `TRAP` | A safepoint is an ordering barrier; a trap records failure kind/message and source location. |

The exact enum names and operand structs are verifier API details; the semantic distinctions above are the proposed contract. Builtin-name resolution precedes user-function lookup; parser rewrites for `clamp`, `degrees`, `radians` and struct constructors also precede ordinary calls. Among user functions, the last definition wins. Unknown function names and unknown namespace methods trap before evaluating their argument expressions; the namespace/method identity is retained for an unknown method. Struct construction lowers to dictionary operations; receiver methods lower to ordinary calls with the receiver first, and the receiver annotation has no Runtime type check. List/dictionary/tuple literals lower to constructor plus push/set operations. `clamp` lowers to `min(max(...))`; `degrees`/`radians` lower to float conversion and multiplication by a constant.

`switch` is parser-lowered to an ordered `if`/`elif` chain and needs no dedicated opcode. The subject must be a variable or literal and is re-read for each comparison; case expressions execute in source order and may have side effects. Comma alternatives become eager `or`; range alternatives become eager `and`. `default` becomes the final `else` even if written earlier, and there is no fallthrough.

## Evaluation order and observable behavior

IR must preserve the current execution model exactly:

1. `set name = rhs`: check that `name` is unbound **before** evaluating `rhs`.
2. `let name = rhs`: evaluate `rhs` **before** checking that `name` exists and its current Runtime tag exactly matches the new value's tag. This bound+tag check is one fused operation before the store. `set` checks only that the slot is unbound and accepts any Runtime tag.
   - `let object.field = value` and `let object[index] = value` are container mutations, not local-slot rebindings.
   - `let name += value`, `let name++` and related compound/increment forms are parsed as read-modify-write `let` operations.
3. Binary operands and function arguments evaluate left-to-right. A user-function call's runtime arity check occurs before argument evaluation; after arguments evaluate, the callee checks typed parameters at entry before executing its body. Builtin arity errors are diagnosed during compilation, and struct-constructor arity errors during parsing. An unresolved function/method name traps before any argument expression is evaluated; `CALL_UNKNOWN` therefore carries the name/location, not evaluated arguments.
4. `and` and `or` evaluate both operands; they are not short-circuit operators.
5. `if` and ternary conditions are evaluated once and checked as Boolean before selecting an edge.
6. A condition-form `for` evaluates/checks its condition on every iteration. A list/tuple iterable is evaluated once; its current length is queried on every iteration, so appending/removing elements in the body can affect the number of iterations. Element lookup occurs per iteration. Range start/end evaluate left-to-right once and are checked as integers. Exclusive `..` adjusts the upper bound by checked decrement; inclusive `..=` does not. Loop-index increments are checked for overflow, except where a proven fast path can safely omit redundant checks. A loop variable is stored only during an executed iteration: a zero-iteration loop does not create a new binding, but it preserves an earlier binding/value if the slot already existed.
7. `break`, `continue`, return and loop exits become explicit CFG edges/terminators without moving the point where subsequent code stops.
8. An expression statement still executes all side effects and can fail even when its result is discarded.

Local assignment copies a tagged value, so two locals may alias the same mutable list/dictionary/tuple payload. Container mutation is visible through every alias. No transform may fold, duplicate, hoist or reorder an operation if it changes a side effect, alias-visible mutation, allocation/lifetime root, runtime error or error order. CSE is legal only when the aliasing model proves the operation pure and unaffected by intervening writes.

## Effects, allocation and errors

Effects are derived from opcode/builtin descriptors, not trusted as arbitrary caller-supplied flags. Initial dimensions: local read/write, container read/write, I/O, blocking, clock/time, filesystem/environment state, nondeterminism, allocation, may-fail, process termination and GC barrier. Builtin descriptors must conservatively mark each operation. For example, `print`/logging perform I/O; printing containers may allocate while stringifying; `input`/file reads allocate managed strings; `write_file` and filesystem operations observe/change external state; sleep blocks; clock and random calls are observable; list/dictionary growth may allocate; string conversion/formatting allocates. `BINARY_ADD` may allocate when both operands are strings.

String concatenation/formatting (including `BINARY_ADD` for two strings), container creation/growth and allocating Runtime/library calls may allocate. The collector is conservative and non-moving; IR must retain explicit `GC_SAFEPOINT` operations at current safe-point boundaries. Safepoints occur only between statements at points where no expression temporary exists solely in a register; all live managed IR values crossing a safepoint must be materialized in scanned stack roots. The current Runtime does not collect in the middle of an expression. Placement is loop-form-specific: condition-form and list/tuple loops safepoint on each iteration; range loops retain the pre-loop statement point and rely on body-statement checks, while proven fast integer range loops may omit redundant per-iteration checks. Generated functions also perform `rbl_gc_init` at entry before arity/parameter checks; the first call records the stack boundary and later calls are no-ops. A safepoint cannot be crossed by a transform that changes live managed values or their stack roots.

Potentially failing operations retain an error contract: unary negation/not type checks, integer overflow, division by zero/overflow, range bound/increment overflow, random bound validation, sleep argument validation, local binding/type checks, arity/type checks, unknown calls/methods, container indexing/mutation, I/O and f-string formatting/type/precision errors. Current Runtime failures write to stderr and exit with code 1; there is no exception/unwinding model. The IR carries source locations for compiler/verifier diagnostics and future tooling, but Runtime 1.2.0 `rbl_fail` accepts only a message and does not report an RBL source location. A backend may choose platform-specific Runtime helpers but may not move a failure across an earlier operand side effect/error.

## Source locations

Every instruction and terminator carries `IRLocation`: source-file ID, zero-based UTF-8 byte offset, one-based line and one-based Unicode-code-point column. This matches `SourcePos`. v0.1 requires a start location, not an end span. Synthetic instructions inherit the location of the source construct; generated bookkeeping may use an explicit synthetic-location kind instead of inventing a source coordinate. This metadata does not imply that current Runtime error messages print source locations.

## Verifier contract

The verifier rejects a module before backend emission when any invariant fails:

1. Structural arrays/counts, IDs, opcode/terminator tags and source-file references are valid; function/block/local/value IDs are unique in scope.
2. Every value has exactly one definition; every operand refers to a value in the same function.
3. Instruction arity and operand/result types match the opcode contract. `dynamic` is accepted only where Runtime checking is explicitly allowed; the verifier never invents a stronger type fact.
4. Every block has exactly one final terminator; no instruction follows it.
5. Every branch target exists; edge argument counts/types match destination block parameters. Entry has no predecessors and cannot be targeted by a branch.
6. Every non-entry block has at least one predecessor and is reachable from entry. `UNREACHABLE` is valid only in a reachable block immediately after a guaranteed non-returning trap operation; it does not exempt dead blocks from reachability checks.
7. SSA-like values dominate every use. Within one block, the definition must precede the use. Edge arguments are uses at the predecessor terminator and must be available on that edge. Block parameters are defined by incoming edges and are available in the destination block and its dominated successors.
8. Calls reference valid function/builtin descriptors, have valid arity and preserve result type. Unresolved names use `CALL_UNKNOWN`, not a missing function ID.
9. Local IDs, type facts and check/store operations agree with the function local table.
10. Source locations refer to a known file and an offset/line/column inside it.
11. Effect metadata agree with opcode descriptors; a potentially failing/allocating operation cannot be marked pure/non-allocating.

Verification returns a structured error with function/block/instruction identity and the closest `IRLocation`. It does not abort the process or emit assembly. Negative tests assert the error category and location, not only a non-zero exit code.

## Test plan and acceptance

Positive fragments: constants/arithmetic, locals and binding checks, eager Boolean evaluation, nested branches, block arguments, loops/back-edges, function/builtin calls, dynamic values, containers, strings/f-strings, safepoints and source locations. Negative fragments: duplicate IDs, undefined/duplicate values, bad operand types/arity, invalid local/function/builtin IDs, missing/duplicate terminators, instructions after terminators, missing branch targets, bad edge arguments, unreachable blocks, non-dominating uses, malformed call descriptors, invalid source locations and inconsistent effect flags.

`v0.9.1` is complete when this contract is reviewed/frozen, the in-memory verifier is integrated into compiler tests, all positive fragments pass, all negative fragments are rejected with structured diagnostics, and the verifier has no target-assembly or Runtime-implementation dependency. `v0.9.2` adds AST-to-IR lowering and verifies every generated function. The current AST-to-assembly backend remains the behavioral oracle until differential parity is established.

## Open review decisions

- Confirm block arguments as the sole merge mechanism for immutable temporaries (no `phi`).
- Confirm source variables remain explicit mutable local slots in the first IR version.
- Freeze opcode inventory and builtin descriptor IDs only after checking the complete standard-library/Runtime surface.
- Decide whether effects are wholly derived from opcode descriptors or partly refined by frontend analysis; never accept unchecked effect claims.
- Decide whether `UNREACHABLE` is limited to blocks after guaranteed traps or includes other proven unreachable code.

Until reviewed, this RFC is a proposal, not a frozen IR ABI.
