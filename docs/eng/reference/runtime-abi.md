# last verified: v0.8.0 | 2026.11.09 | RUNTIME ABI REFERENCE RECHECK

## Runtime ABI v1

**Status:** implemented in RBL 0.8.0. The ABI version is independent of the language/compiler version in `VERSION`.

## Version policy

The current contract is **Runtime ABI 1.2.0**.

- A major version change is incompatible. A compiler built for ABI major 1 references the versioned `rbl_runtime_abi_check_v1` entry point; a Runtime without that entry point fails at link time.
- A minor version may add backward-compatible entry points or features. A Runtime with the same major and a minor version greater than or equal to the compiler's minimum is accepted.
- A patch version may correct implementation details but must not change layouts, calling conventions or observable ABI contracts. Patch level is not used for compatibility: patch differences in either direction are accepted when the major, minor and structural requirements match.
- A compiler declares the minimum minor version and required feature bits in the descriptor embedded in each generated executable.

The runtime checks the descriptor before calling the RBL `main` function. A mismatch terminates with exit code **78** and a diagnostic identifying the incompatibility. This runtime check also catches layout, pointer-width, calling-convention and feature mismatches that a version number alone cannot describe.

## Targets and calling convention

The supported targets are x86-64 Linux ELF64 and x86-64 Windows PE/COFF. Runtime helper calls use the **System V AMD64 calling convention on both targets**:

- Integer/pointer arguments are passed in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`, then on the stack according to the SysV AMD64 ABI.
- Caller-saved GPRs are `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`-`r11`; callee-saved GPRs are `rbx`, `rbp`, `r12`-`r15` and `rsp`. Vector-register preservation follows SysV AMD64 (XMM registers are caller-saved). The caller aligns the stack to 16 bytes before a call.
- Windows C runtime exports use `__attribute__((sysv_abi))`; the PE entry point itself is entered by the Windows CRT using the platform's normal convention and then switches to the RBL helper convention.
- Linux executables enter through `_start`; Windows executables enter through CRT `main`.

## Value representation

A general RBL value is a pair of 64-bit words, **16 bytes total**:

| Word | Meaning |
|---|---|
| `tag` | Runtime type tag |
| `payload` | Integer bits, IEEE-754 binary64 bits, pointer or boolean payload |

| Tag | Type |
|---:|---|
| 0 | `Unit` |
| 1 | `Int` (signed 64-bit) |
| 2 | `Float` (IEEE-754 binary64) |
| 3 | `String` (pointer to NUL-terminated character data) |
| 4 | `Bool` |
| 5 | `List` |
| 6 | `Dictionary` |
| 7 | `Tuple` |
| 8 | `null` |

The numeric tags are declared in `RBLRuntimeTag` in `runtime/rbl_abi.h`. `Bool` uses payload 0 or 1; `List`, `Dictionary` and `Tuple` payloads are Runtime object pointers. `Unit` and `null` use their tags to distinguish them from other values.

For a general tagged return, `rax` contains the tag and `rdx` the payload. C functions returning the two-word value struct use the SysV AMD64 aggregate-return convention, which produces the same register pair. Some compiler-proven integer/boolean fast paths use unboxed values and are not a different general value representation.

For a runtime helper receiving one tagged value, the common pair is `(tag, payload)` in `(rdi, rsi)`. Two tagged values are commonly passed as `(tag1, payload1, tag2, payload2)` in `(rdi, rsi, rdx, rcx)`. Helpers that process a variable number of values receive a pointer to contiguous 16-byte pairs and a count.

User-defined RBL functions use a compiler-defined argument-array convention: `rdi` points to contiguous 16-byte argument pairs and `rsi` contains the argument count. The function returns a tagged pair in `rax:rdx`. This is an internal compiler/runtime convention, not a C API for external callers.

## Descriptor and compatibility check

`runtime/rbl_abi.h` is the canonical source for the ABI descriptor layout, version, calling-convention identifier, feature bits and cross-translation-unit C declarations for the shared container Runtime. The header declares the list, tuple, dictionary, equality, membership/indexing and GC entry points used by generated code, together with the shared platform helpers used by the C Runtime objects. Linux and Windows bootstrap builds enforce missing/redundant-prototype diagnostics for these shared C objects. `runtime/rbl_abi.c` exports:

- `rbl_runtime_abi_v1`: the Runtime's descriptor;
- `rbl_runtime_abi_check_v1`: the versioned entry point called by generated programs;
- `rbl_runtime_abi_validate`: a pure descriptor compatibility validator used by tests.

The descriptor is exactly 40 bytes in ABI v1.2.0. Its field offsets are fixed and compile-time asserted:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 8 | `magic` | `0x52424c4142493031` |
| 8 | 8 | `features` | Supported feature-bit mask |
| 16 | 4 | `descriptor_size` | Descriptor size in bytes |
| 20 | 4 | `value_size` | `RBLRuntimeValue` size in bytes |
| 24 | 4 | `pointer_size` | Pointer size in bytes |
| 28 | 4 | `calling_convention` | Calling-convention identifier (`1` = SysV AMD64) |
| 32 | 2 | `major` | ABI major version |
| 34 | 2 | `minor` | ABI minor version |
| 36 | 2 | `patch` | ABI patch version |
| 38 | 2 | `reserved` | Reserved; currently zero |

`RBLRuntimeValue` is also layout-asserted: `tag` is at offset 0 and `payload` at offset 8. Compatibility requires equal major versions, a Runtime minor version at least as new as the compiler's minimum, matching value/pointer layout and calling convention, and support for every required feature. Patch level does not affect compatibility. A newer minor Runtime may extend the descriptor while retaining its existing 40-byte prefix; the size check rejects older descriptors that cannot satisfy the compiler's requirements.

The v1.2 feature bits are `TAGGED_VALUES`, `SYSV_AMD64`, `STACK_VALUE_ARRAYS`, `CONTAINERS`, `GC_MANAGED_STRINGS` and `FSTRING_FORMAT`.

| Bit | Feature | Mask |
|---:|---|---:|
| 0 | `TAGGED_VALUES` | `0x01` |
| 1 | `SYSV_AMD64` | `0x02` |
| 2 | `STACK_VALUE_ARRAYS` | `0x04` |
| 3 | `CONTAINERS` | `0x08` |
| 4 | `GC_MANAGED_STRINGS` | `0x10` |
| 5 | `FSTRING_FORMAT` | `0x20` |

The versioned checker symbol is a link-time guard for the ABI major. The descriptor handshake runs at process startup for minor-version and structural compatibility. ABI 1.2 requires `FSTRING_FORMAT` and the internal `rbl_format_value` helper used by generated f-string expressions. The build driver tracks the formatting source/header as runtime dependencies and links the formatting object into both Linux and Windows executables.

## Dynamic string ownership and lifetime

String payloads remain pointers to NUL-terminated bytes, but dynamically allocated strings are Runtime-owned objects managed by the same conservative mark-and-sweep collector as lists, tuples and dictionaries. String literals and other static Runtime constants are not registered and are never freed by the collector.

- Generated RBL code must not call `free()` on a string payload. The Runtime owns every dynamically allocated string returned as an RBL value.
- A dynamic string is live while its exact base pointer appears as a pointer-shaped machine word anywhere in the scanned stack range, or is reachable as a string-valued element/key/value in a live container. The stack scan is conservative and does not validate the word's tag. Aliases do not create separate ownership; all aliases refer to the same allocation.
- Collection is performed at compiler-emitted safe points before statements that may allocate or call code with unknown allocation behavior, and at loop-condition safe points. Proven non-allocating integer/Boolean scalar statements omit redundant safe points. At collection points, live RBL locals have been spilled into stack slots and expression temporaries from the preceding statement are no longer live. Runtime allocators do not trigger collection in the middle of an expression.
- The collector scans stack words on the current thread between its active frame and the frame pointer captured from generated `main`. A materialized pointer in a native C stack local within that range is therefore a conservative root while that frame remains active; this is not an explicit rooting guarantee. Native heap/global storage and other threads are not scanned. Native code must not retain a managed pointer only in those locations or after all corresponding RBL/stack roots cease to be live. `rbl_string_free` is an internal Runtime helper, not an application-level ownership-transfer API; the ABI provides no pinning or explicit root-handle API.
- Runtime state and collector registries are process-global and unsynchronized. The 0.8.0 Runtime supports one execution thread: native callers must invoke Runtime helpers only on the thread that entered generated RBL `main`, and must not call them concurrently from other threads. There is no thread-registration or per-thread stack-root protocol.
- The collector is conservative: an incidental pointer-shaped word on the scanned stack may keep an object alive longer than necessary. It does not move objects, so a live string's address remains stable.
- If mark-stack, survivor-vector or replacement-container-registry allocation fails, collection clears temporary marks and aborts before sweeping; the existing heap remains intact and a later safe point may retry. Failure to compact the string registry leaves its existing table valid.

The compiler and Runtime must agree on safe-point placement. ABI 1.2 requires `GC_MANAGED_STRINGS`, `FSTRING_FORMAT`, `rbl_gc_safepoint` and the internal `rbl_format_value` helper. A Runtime without these required features is rejected by the startup descriptor check before RBL `main` executes.

## Tests

Run the standalone descriptor and mismatch checks with:

```bash
python3 tests/test_runtime_abi.py
```

The suite checks descriptor compatibility, the link-time major guard, startup rejection before RBL `main`, tag parity between C/compiler and Linux assembly, and SysV stack alignment for nested fast-integer and boolean calls. The main test command (`python rbl.py test` or `make test`) includes these checks. Integration tests additionally build and run RBL programs, exercising the startup handshake and link-time ABI symbol.
