# last verified: v0.8.0 | 2026.11.09 | DIAGNOSTICS REFERENCE RECHECK

## RBL compiler diagnostics

RBL 0.8.0 compiler diagnostics preserve source positions from lexing through parsing, semantic validation and assembly generation. A source position contains the original file, byte offset, and one-based line/column coordinates.

## Diagnostic format

```text
path/to/program.rbl:12:9: error [parser]: expected TOK_RPAREN, found TOK_EOF
  12 |     print(value
     |         ^
```

The location identifies the token or source character that caused the diagnostic. The compiler prints the corresponding source line and a caret below the position. Unexpected-EOF parser errors and unterminated quoted literals point immediately after the final source character. An unterminated f-string interpolation instead points to its opening `{`, identifying the construct that was never closed.

The stage label identifies where the error was detected:

| Stage | Responsibility |
|---|---|
| `lexer` | Invalid UTF-8/NUL input, invalid characters, numeric literals, string escapes and unterminated literals |
| `parser` | Unexpected tokens, missing delimiters, malformed expressions, f-string syntax, unknown namespaces and unknown compile-time constants |
| `semantic` | Invalid loop control flow, duplicate parameter names and a missing `main` function |
| `codegen` | Source-level constraints discovered while lowering calls and expressions, such as builtin arity |

## Position conventions

- Byte offsets are zero-based internally and refer to the original UTF-8 source buffer.
- Line and column numbers are one-based.
- Columns count Unicode code points, not UTF-8 bytes. A multibyte character therefore advances the displayed column by one.
- Columns are not grapheme-cluster counts or terminal display widths. Combining marks and wide East Asian glyphs can therefore look visually offset in some terminals even though the reported code-point column is consistent.
- A tab advances one logical source column. In the printed excerpt tabs are expanded to the next four-column tab stop so the caret remains visible and aligned.
- LF, CRLF and bare CR line endings produce the same line/column coordinates.
- An embedded NUL byte in the source file is rejected by the lexer entry path instead of silently truncating the input; the excerpt renders it visibly as `\0`.
- Malformed UTF-8 (invalid continuation bytes, overlong encodings, surrogate encodings, out-of-range code points and truncated sequences) is rejected before tokenization; invalid bytes are escaped in the excerpt.
- F-string interpolation expressions are re-lexed, but their positions are translated back to the original file and source offsets rather than reported relative to a temporary fragment.

`Expr`, `Stmt`, `Param` and `Function` nodes retain their originating `SourcePos`. Synthetic AST nodes created while desugaring syntax inherit the source position of the syntax that introduced them.

## Semantic validation

The semantic validation pass runs after parsing and before code generation. It currently checks:

- `break` and `continue` occur inside a `for` loop, including nested `if` blocks;
- a function does not declare the same parameter name more than once;
- the program contains a `main` function.

The semantic pass is not a general name or type checker. Function argument type checks and most unresolved dynamic names retain their existing runtime behavior; unknown namespaces and compile-time constants are rejected by the parser, while certain fast-path invariant failures can be reported by code generation. Runtime diagnostics are not generally mapped back to source positions.

Errors that occur before source loading (file I/O, command-line options, temporary-file creation) and internal allocation/capacity/invariant failures are infrastructure diagnostics, not source diagnostics; they may not have a source position or excerpt.

## Test commands

```bash
python3 tests/test_diagnostics.py
python3 rbl.py test
```

The 63-case suite consists of 45 invalid-source diagnostics, 8 valid controls, 4 expected runtime-error cases and 6 malformed-UTF-8 byte-sequence tests. It checks lexer, parser, semantic and code-generation locations, caret alignment in terminal cells (including wide CJK/emoji glyphs and zero-width combining marks), Unicode source columns for two-, three- and four-byte UTF-8 characters, tabs, LF/CRLF/CR and EOF positions, numeric separators, invalid Unicode escapes, embedded NUL, malformed UTF-8 classes (continuation, overlong, surrogate, out-of-range and truncated sequences), unterminated literals, multiline strings, nested f-string expressions and comments inside interpolations, format-spec grammar/limits, explicit rejection of dynamic/nested specs at the start or after width, valid format parsing including fill characters that are also alignment characters, runtime type mismatches, integer-precision errors and the invalid `s`-precision combination, parser namespace/constant errors and builtin arity. It also checks malformed UTF-8 after a valid multibyte character. Source columns remain code-point based; caret display width is an approximation of common terminal widths, not a guarantee for every grapheme/terminal. The suite runs through the active native Windows, WSL or Linux compiler backend. On Windows, `python tests/differential_diagnostics.py` compares 30 representative diagnostic and valid-source cases between WSL and native Windows after path normalization.
