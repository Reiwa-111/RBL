#!/usr/bin/env python3
"""Source-location and diagnostic-stage tests for the RBL compiler."""
from __future__ import annotations

import sys
import os
import shlex
import stat
import subprocess
import tempfile
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


def source_location(text: str, marker: str, occurrence: int = 0) -> tuple[int, int]:
    offset = -1
    start = 0
    for _ in range(occurrence + 1):
        offset = text.index(marker, start)
        start = offset + len(marker)
    prefix = text[:offset].replace("\r\n", "\n").replace("\r", "\n")
    return prefix.count("\n") + 1, len(prefix.rsplit("\n", 1)[-1]) + 1


def source_eof_location(text: str) -> tuple[int, int]:
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    return normalized.count("\n") + 1, len(normalized.rsplit("\n", 1)[-1]) + 1


def visual_column(text: str) -> int:
    column = 0
    for char in text:
        if char == "\t":
            column += 4 - (column % 4)
        elif unicodedata.combining(char) or char == "\u200d" or "\ufe00" <= char <= "\ufe0f":
            continue
        else:
            column += 2 if unicodedata.east_asian_width(char) in {"W", "F"} else 1
    return column


def run_case(path: Path, name: str, source: str, stage: str, marker: str,
             message: str, occurrence: int = 0, eof: bool = False,
             crlf: bool = False) -> int:
    payload = source.replace("\n", "\r\n") if crlf else source
    path.write_bytes(payload.encode("utf-8"))
    code, stdout, stderr = rbltool.check(path, rbltool.load_settings())
    if code == 0:
        print(f"FAIL: {name}: compiler accepted invalid source")
        return 1

    if eof:
        line, col = source_eof_location(source)
    else:
        line, col = source_location(source, marker, occurrence)
    location = f":{line}:{col}: error [{stage}]:"
    if location not in stderr or message not in stderr:
        print(f"FAIL: {name}: missing expected diagnostic location/message")
        print(f"  expected: {location} {message}")
        print(f"  stdout: {stdout!r}")
        print(f"  stderr: {stderr!r}")
        return 1

    normalized = source.replace("\r\n", "\n").replace("\r", "\n")
    source_lines = normalized.split("\n")
    source_line = source_lines[line - 1].expandtabs(4).replace("\0", "\\0") if line <= len(source_lines) else ""
    if source_line and source_line not in stderr:
        print(f"FAIL: {name}: source excerpt is missing")
        print(f"  expected line: {source_line!r}")
        print(f"  stderr: {stderr!r}")
        return 1
    output_lines = stderr.splitlines()
    excerpt_index = next((i for i, item in enumerate(output_lines)
                          if item.lstrip().startswith(f"{line} | ")), None)
    if excerpt_index is None or excerpt_index + 1 >= len(output_lines):
        print(f"FAIL: {name}: source excerpt/caret pair is missing")
        print(f"  stderr: {stderr!r}")
        return 1
    caret_text = output_lines[excerpt_index + 1].split("| ", 1)[-1]
    if eof:
        prefix = source_lines[line - 1] if line <= len(source_lines) else ""
    else:
        marker_offset = 0
        search_from = 0
        for _ in range(occurrence + 1):
            marker_offset = source.index(marker, search_from)
            search_from = marker_offset + len(marker)
        prefix = source[:marker_offset].replace("\r\n", "\n").replace("\r", "\n").rsplit("\n", 1)[-1]
    expected_caret = " " * visual_column(prefix) + "^"
    if caret_text != expected_caret:
        print(f"FAIL: {name}: caret is not aligned with the expected source position")
        print(f"  expected caret: {expected_caret!r}")
        print(f"  actual caret:   {caret_text!r}")
        print(f"  stderr: {stderr!r}")
        return 1

    print(f"PASS: {name} ({stage} {line}:{col})")
    return 0


def run_valid_case(path: Path, name: str, source: str) -> int:
    path.write_bytes(source.encode("utf-8"))
    code, stdout, stderr = rbltool.check(path, rbltool.load_settings())
    if code != 0:
        print(f"FAIL: {name}: valid source was rejected")
        print(f"  stdout: {stdout!r}")
        print(f"  stderr: {stderr!r}")
        return 1
    print(f"PASS: {name}")
    return 0


def run_runtime_error_case(path: Path, name: str, source: str, message: str) -> int:
    path.write_bytes(source.encode("utf-8"))
    code, stdout, stderr = rbltool.run_program(path, {})
    if code != 1 or message not in stderr:
        print(f"FAIL: {name}: expected runtime error was not reported")
        print(f"  expected: {message}")
        print(f"  exit code: {code}")
        print(f"  stdout: {stdout!r}")
        print(f"  stderr: {stderr!r}")
        return 1
    print(f"PASS: {name} (runtime error)")
    return 0


def run_raw_utf8_case(path: Path, name: str, prefix: bytes, invalid: bytes) -> int:
    path.write_bytes(b"func main() (\n    " + prefix + invalid + b"\n)\n")
    code, stdout, stderr = rbltool.check(path, rbltool.load_settings())
    prefix_text = prefix.decode("utf-8")
    expected_col = 5 + len(prefix_text)
    expected = f":2:{expected_col}: error [lexer]: source file is not valid UTF-8"
    escaped = "".join(f"\\x{byte:02X}" for byte in invalid)
    expected_excerpt = f"  2 |     {prefix_text}{escaped}"
    if code == 0 or expected not in stderr or expected_excerpt not in stderr:
        print(f"FAIL: {name}: malformed UTF-8 was not diagnosed at its byte position")
        print(f"  stdout: {stdout!r}")
        print(f"  stderr: {stderr!r}")
        return 1
    expected_caret = "    | " + " " * visual_column("    " + prefix_text) + "^"
    if expected_caret not in stderr:
        print(f"FAIL: {name}: caret is not aligned before the invalid byte")
        print(f"  stderr: {stderr!r}")
        return 1
    print(f"PASS: {name} (lexer 2:{expected_col})")
    return 0


def run_codegen_output_preservation(directory: Path) -> int:
    settings = rbltool.load_settings()
    rbltool.ensure_toolchain(settings)
    source = directory / "codegen_output_failure.rbl"
    source.write_text("func main() (\n    len(1, 2)\n)\n", encoding="utf-8")

    existing = directory / "existing-assembly.s"
    original = b"previous valid assembly must survive\n"
    existing.write_bytes(original)
    missing = directory / "new-assembly.s"

    for output in (existing, missing):
        command = rbltool.compiler_cmd(source, output, settings)
        if isinstance(command, str):
            code, stdout, stderr = rbltool.run_wsl(command, settings)
        else:
            code, stdout, stderr = rbltool.run_native(command)
        if code != 1 or "error [codegen]" not in stderr or "len() expects 1 argument" not in stderr:
            print(f"FAIL: codegen_output_preservation: expected codegen failure for {output.name}")
            print(f"  exit code: {code}; stdout: {stdout!r}; stderr: {stderr!r}")
            return 1
        if output == existing:
            if existing.read_bytes() != original:
                print("FAIL: codegen_output_preservation: failed codegen replaced an existing assembly file")
                return 1
        elif missing.exists():
            print("FAIL: codegen_output_preservation: failed codegen left a partial new assembly file")
            return 1

    valid_source = directory / "valid_output_source.rbl"
    valid_source.write_text("func main() (\n)\n", encoding="utf-8")
    expected_mode = None
    if os.name != "nt":
        existing.chmod(0o640)
        expected_mode = stat.S_IMODE(existing.stat().st_mode)
    replace_command = rbltool.compiler_cmd(valid_source, existing, settings)
    if isinstance(replace_command, str):
        replace_code, replace_stdout, replace_stderr = rbltool.run_wsl(replace_command, settings)
    else:
        replace_code, replace_stdout, replace_stderr = rbltool.run_native(replace_command)
    if replace_code != 0 or existing.read_bytes() == original or b".intel_syntax noprefix" not in existing.read_bytes():
        print("FAIL: codegen_output_preservation: successful atomic replacement did not publish the generated assembly")
        print(f"  exit code: {replace_code}; stdout: {replace_stdout!r}; stderr: {replace_stderr!r}")
        return 1
    if expected_mode == 0o640 and stat.S_IMODE(existing.stat().st_mode) != expected_mode:
        print("FAIL: codegen_output_preservation: atomic replacement did not preserve the existing POSIX mode")
        return 1
    if os.name != "nt" and expected_mode != 0o640:
        print("SKIP: codegen_output_preservation POSIX mode check (host filesystem does not expose chmod metadata)")
    if list(directory.glob("existing-assembly.s.rbltmp.*")):
        print("FAIL: codegen_output_preservation: successful replacement left a temporary sibling")
        return 1

    blocked_output = directory / "blocked-output.s"
    blocked_output.mkdir()
    command = rbltool.compiler_cmd(valid_source, blocked_output, settings)
    if isinstance(command, str):
        code, stdout, stderr = rbltool.run_wsl(command, settings)
    else:
        code, stdout, stderr = rbltool.run_native(command)
    if code == 0 or not blocked_output.is_dir() or any(blocked_output.iterdir()):
        print("FAIL: codegen_output_preservation: failed atomic promotion damaged the destination directory")
        print(f"  exit code: {code}; stdout: {stdout!r}; stderr: {stderr!r}")
        return 1
    if list(directory.glob("blocked-output.s.rbltmp.*")):
        print("FAIL: codegen_output_preservation: failed atomic promotion left a temporary sibling")
        return 1

    if os.name != "nt" or rbltool.use_wsl(settings):
        link_target = directory / "symlink-target.s"
        link_target.write_bytes(b"symlink target must remain unchanged\n")
        symlink_output = directory / "symlink-output.s"
        if os.name != "nt":
            symlink_output.symlink_to(link_target)
        if rbltool.use_wsl(settings):
            output_wsl = rbltool.wsl_path(directory, settings).rstrip("/") + "/symlink-output.s"
            target_wsl = rbltool.wsl_path(link_target, settings)
            link_code, link_stdout, link_stderr = rbltool.run_wsl(
                f"ln -s -- {shlex.quote(target_wsl)} {shlex.quote(output_wsl)}", settings
            ) if os.name == "nt" else (0, "", "")
            if link_code != 0:
                print(f"SKIP: WSL symlink-output check (host filesystem refused symlink creation: {link_stderr or link_stdout})")
                output_wsl = None
            if output_wsl is None:
                symlink_code = 0
                symlink_stdout = ""
                symlink_stderr = ""
            else:
                symlink_command = (
                    f"{shlex.quote(rbltool.wsl_path(rbltool.COMPILER_BIN, settings))} "
                    f"{shlex.quote(rbltool.wsl_path(valid_source, settings))} -S {shlex.quote(output_wsl)}"
                )
                symlink_code, symlink_stdout, symlink_stderr = rbltool.run_wsl(symlink_command, settings)
                verify_code, verify_stdout, verify_stderr = rbltool.run_wsl(
                    f"test -L {shlex.quote(output_wsl)} && cat {shlex.quote(output_wsl)}", settings
                )
                if symlink_code == 0 or "symbolic-link output path" not in symlink_stderr or verify_code != 0 or verify_stdout != "symlink target must remain unchanged\n":
                    print("FAIL: codegen_output_preservation: WSL symbolic-link output was followed or replaced")
                    print(f"  compiler: {symlink_code}; verify: {verify_code}; stdout: {verify_stdout!r}; stderr: {symlink_stderr or verify_stderr!r}")
                    return 1
        else:
            symlink_command = rbltool.compiler_cmd(valid_source, symlink_output, settings)
            symlink_code, symlink_stdout, symlink_stderr = rbltool.run_native(symlink_command)
            if symlink_code == 0 or not symlink_output.is_symlink() or link_target.read_bytes() != b"symlink target must remain unchanged\n":
                print("FAIL: codegen_output_preservation: symbolic-link output was followed or replaced")
                print(f"  exit code: {symlink_code}; stdout: {symlink_stdout!r}; stderr: {symlink_stderr!r}")
                return 1
    elif os.name == "nt":
        junction_target = directory / "junction-target"
        junction_target.mkdir()
        junction_output = directory / "junction-output.s"
        junction_result = subprocess.run(
            ["cmd.exe", "/d", "/c", "mklink", "/J", str(junction_output), str(junction_target)],
            capture_output=True,
            text=True,
            check=False,
        )
        if junction_result.returncode:
            print(f"SKIP: Windows reparse-output check (junction creation unavailable: {junction_result.stderr or junction_result.stdout})")
        else:
            try:
                junction_command = rbltool.compiler_cmd(valid_source, junction_output, settings)
                junction_code, junction_stdout, junction_stderr = rbltool.run_native(junction_command)
                if junction_code == 0 or "reparse-point output path" not in junction_stderr or not junction_output.is_dir() or any(junction_target.iterdir()):
                    print("FAIL: codegen_output_preservation: Windows junction output was replaced or written through")
                    print(f"  exit code: {junction_code}; stdout: {junction_stdout!r}; stderr: {junction_stderr!r}")
                    return 1
            finally:
                subprocess.run(["cmd.exe", "/d", "/c", "rmdir", str(junction_output)], capture_output=True, check=False)

    print("PASS: codegen_output_preservation (codegen failure preserves output; failed promotion cleans temporary sibling)")
    return 0


def main() -> int:
    build_dir = ROOT / "build"
    build_dir.mkdir(exist_ok=True)
    failures = 0
    cases = [
        ("lexer_unicode_column", 'func main() (\n    print("é") @\n)\n', "lexer", "@", "unknown symbol", 0, False, True),
        ("lexer_cjk_codepoint_column", 'func main() (\n    print("中") @\n)\n', "lexer", "@", "unknown symbol", 0, False, False),
        ("lexer_supplementary_codepoint_column", 'func main() (\n    print("😀") @\n)\n', "lexer", "@", "unknown symbol", 0, False, False),
        ("lexer_combining_mark_column", 'func main() (\n    print("é") @\n)\n', "lexer", "@", "unknown symbol", 0, False, False),
        ("lexer_escape_position", 'func main() (\n    print("a\\q")\n)\n', "lexer", "\\q", "unknown escape sequence", 0, False, False),
        ("fstring_literal_escape_position", 'func main() (\n    print(f"prefix \\q")\n)\n', "lexer", "\\q", "unknown escape sequence", 0, False, False),
        ("parser_expected_expression", "func main() (\n    let x = )\n)\n", "parser", ")", "unexpected token in expression", 1, False, False),
        ("fstring_inner_expression", 'func main() (\n    print(f"value {1 + }")\n)\n', "parser", "}", "unexpected token in expression", 0, False, False),
        ("fstring_format_unknown_type", 'func main() (\n    print(f"value {1:q}")\n)\n', "parser", "q", "unsupported f-string format type 'q'", 0, False, False),
        ("fstring_format_unknown_type_after_width", 'func main() (\n    print(f"value {1:>8q}")\n)\n', "parser", "q", "unsupported f-string format type 'q'", 0, False, False),
        ("fstring_format_dynamic_spec", 'func main() (\n    set width = 8\n    print(f"value {1:{width}}")\n)\n', "parser", "{width", "dynamic or nested f-string format specifications are not supported", 0, False, False),
        ("fstring_format_dynamic_width", 'func main() (\n    set width = 8\n    print(f"value {1:>8{width}}")\n)\n', "parser", "{width", "dynamic or nested f-string format specifications are not supported", 0, False, False),
        ("fstring_format_precision_missing", 'func main() (\n    print(f"value {1:.f}")\n)\n', "parser", ".", "f-string precision requires digits", 0, False, False),
        ("fstring_format_precision_limit", 'func main() (\n    print(f"value {1:.18f}")\n)\n', "parser", ".", "f-string precision must be between 0 and 17", 0, False, False),
        ("fstring_format_width_limit", 'func main() (\n    print(f"value {1:1000001}")\n)\n', "parser", "1000001", "f-string format width exceeds 1000000", 0, False, False),
        ("semantic_break_outside_loop", "func main() (\n    break\n)\n", "semantic", "break", "break outside of a for loop", 0, False, False),
        ("semantic_duplicate_parameter", "func pair(x int, x int) (\n)\nfunc main() (\n)\n", "semantic", "x int", "declared more than once", 1, False, False),
        ("semantic_missing_main", "func helper() (\n)\n", "semantic", "", "function main not found", 0, True, False),
        ("codegen_builtin_arity", "func main() (\n    len(1, 2)\n)\n", "codegen", "len", "len() expects 1 argument", 0, False, False),
        ("parser_crlf_eof", "func main() (\n    let x = 1\n", "parser", "", "unterminated block", 0, True, True),
        ("semantic_tab_column", "func main() (\n\tbreak\n)\n", "semantic", "break", "break outside of a for loop", 0, False, False),
        ("lexer_bare_cr_comment", "# comment\r@\nfunc main() (\n)\n", "lexer", "@", "unknown symbol", 0, False, False),
        ("lexer_numeric_separator", "func main() (\n    let x = 1__2\n)\n", "lexer", "_", "invalid digit separator", 0, False, False),
        ("lexer_multiline_string_escape", 'func main() (\n    print("""ok\n\\q""")\n)\n', "lexer", "\\q", "unknown escape sequence", 0, False, False),
        ("fstring_escaped_brace_mapping", 'func main() (\n    print(f"{{x}} \\q")\n)\n', "lexer", "\\q", "unknown escape sequence", 0, False, False),
        ("fstring_multiline_literal_escape", 'func main() (\n    print(f"""\n\\q""")\n)\n', "lexer", "\\q", "unknown escape sequence", 0, False, False),
        ("fstring_empty_expression", 'func main() (\n    print(f"{}")\n)\n', "parser", "}", "empty expression in an f-string", 0, False, False),
        ("fstring_unmatched_close", 'func main() (\n    print(f"x}")\n)\n', "parser", "}", "single '}' is not allowed", 0, False, False),
        ("fstring_nested_dict_expression", 'func main() (\n    print(f"{len({1: })}")\n)\n', "parser", "}", "unexpected token in expression", 0, False, False),
        ("fstring_multiline_inner_expression", 'func main() (\n    print(f"""value {\n1 + }\n""")\n)\n', "parser", "}", "unexpected token in expression", 0, False, False),
        ("parser_missing_list_close", "func main() (\n    let x = [1, 2\n", "parser", "", "expected TOK_RBRACKET", 0, True, False),
        ("semantic_missing_main_empty", "", "semantic", "", "function main not found", 0, True, False),
        ("codegen_abs_arity", "func main() (\n    abs()\n)\n", "codegen", "abs", "abs() expects 1 argument", 0, False, False),
        ("lexer_unterminated_string_eof", 'func main() (\n    print("unfinished', "lexer", "", "unterminated string literal", 0, True, False),
        ("lexer_unterminated_multiline_string_eof", 'func main() (\n    print("""unfinished\ntext', "lexer", "", "unterminated multi-line string literal", 0, True, False),
        ("lexer_unterminated_fstring_eof", 'func main() (\n    print(f"unfinished', "lexer", "", "unterminated f-string literal", 0, True, False),
        ("lexer_unterminated_multiline_fstring_eof", 'func main() (\n    print(f"""unfinished\ntext', "lexer", "", "unterminated multi-line f-string literal", 0, True, False),
        ("lexer_invalid_unicode_escape", 'func main() (\n    print("\\uD800")\n)\n', "lexer", "\\uD800", "invalid Unicode code point", 0, False, False),
        ("lexer_source_nul_byte", "func main() (\n    \0\n)\n", "lexer", "\0", "NUL byte is not allowed in a source file", 0, False, False),
        ("fstring_unicode_literal_position", 'func main() (\n    print(f"é \\q")\n)\n', "lexer", "\\q", "unknown escape sequence", 0, False, False),
        ("parser_missing_call_close", "func main() (\n    print(len(1)\n", "parser", "", "expected TOK_RPAREN", 0, True, False),
        ("semantic_continue_outside_loop", "func main() (\n    continue\n)\n", "semantic", "continue", "continue outside of a for loop", 0, False, False),
        ("codegen_method_builtin_arity", 'func main() (\n    rbl.math.pow(1)\n)\n', "codegen", "rbl", "rbl.math.pow() expects 2 args", 0, False, False),
        ("parser_unknown_namespace", "func main() (\n    print(foo.bar.baz())\n)\n", "parser", "foo", "unknown namespace", 0, False, False),
        ("parser_unknown_constant", "func main() (\n    print(rbl.math.tau)\n)\n", "parser", "rbl", "unknown constant", 0, False, False),
    ]

    with tempfile.TemporaryDirectory(prefix="rbl-diagnostics-") as temp:
        temp_dir = Path(temp)
        failures += run_codegen_output_preservation(temp_dir)
        for name, source, stage, marker, message, occurrence, eof, crlf in cases:
            failures += run_case(temp_dir / f"{name}.rbl", name, source, stage,
                                 marker, message, occurrence, eof, crlf)
        failures += run_valid_case(temp_dir / "semantic_nested_loop_control.rbl",
                                   "semantic_nested_loop_control",
                                   "func main() (\n    for (1..=2) (\n        if (true) (\n            continue\n        )\n    )\n)\n")
        failures += run_valid_case(temp_dir / "fstring_nested_quoted_delimiters.rbl",
                                   "fstring_nested_quoted_delimiters",
                                   'func main() (\n    print(f"""value {str("}:")}""")\n)\n')
        failures += run_valid_case(temp_dir / "fstring_hash_comment_brace.rbl",
                                   "fstring_hash_comment_brace",
                                   'func main() (\n    print(f"""value {1 # } : are part of the comment\n + 2}""")\n)\n')
        failures += run_valid_case(temp_dir / "fstring_slash_comment_brace.rbl",
                                   "fstring_slash_comment_brace",
                                   'func main() (\n    print(f"""value {1 // } : are part of the comment\n + 2}""")\n)\n')
        failures += run_valid_case(temp_dir / "fstring_nested_dict.rbl",
                                   "fstring_nested_dict",
                                   'func main() (\n    print(f"{len({1: 2})}")\n)\n')
        failures += run_valid_case(temp_dir / "fstring_nested_fstring.rbl",
                                   "fstring_nested_fstring",
                                   'func main() (\n    print(f"""outer {f"inner {1}"}""")\n)\n')
        failures += run_valid_case(temp_dir / "fstring_format_spec.rbl",
                                   "fstring_format_spec",
                                   'func main() (\n    print(f"""{255:4x} {3.14:.2f} {"é猫":>4} {"cat":界^7} {1:.17} {1:1000000}""")\n)\n')
        failures += run_valid_case(temp_dir / "fstring_alignment_fill_chars.rbl",
                                   "fstring_alignment_fill_chars",
                                   'func main() (\n    print(f"""{"cat":<<6} {"cat":>>6} {"cat":^^7}""")\n)\n')
        failures += run_runtime_error_case(
            temp_dir / "fstring_integer_type_mismatch.rbl",
            "fstring_integer_type_mismatch",
            'func main() (\n    set text = "value"\n    print(f"{text:d}")\n)\n',
            "integer f-string format type requires an int value")
        failures += run_runtime_error_case(
            temp_dir / "fstring_float_type_mismatch.rbl",
            "fstring_float_type_mismatch",
            'func main() (\n    print(f"{42:f}")\n)\n',
            "floating-point f-string format type requires a float value")
        failures += run_runtime_error_case(
            temp_dir / "fstring_integer_precision.rbl",
            "fstring_integer_precision",
            'func main() (\n    print(f"{42:.2}")\n)\n',
            "precision is only supported for floating-point f-string values")
        failures += run_runtime_error_case(
            temp_dir / "fstring_string_precision.rbl",
            "fstring_string_precision",
            'func main() (\n    print(f"{3.14:.2s}")\n)\n',
            "precision cannot be used with the string f-string format type")
        invalid_utf8_cases = [
            ("lexer_invalid_utf8_continuation", b"", b"\x80"),
            ("lexer_invalid_utf8_overlong", b"", b"\xC0\xAF"),
            ("lexer_invalid_utf8_surrogate", b"", b"\xED\xA0\x80"),
            ("lexer_invalid_utf8_out_of_range", b"", b"\xF4\x90\x80\x80"),
            ("lexer_invalid_utf8_truncated", b"", b"\xE2\x82"),
            ("lexer_invalid_utf8_after_unicode", "é ".encode("utf-8"), b"\x80"),
        ]
        for name, prefix, invalid in invalid_utf8_cases:
            failures += run_raw_utf8_case(temp_dir / f"{name}.rbl", name, prefix, invalid)

    if failures:
        print(f"\n{failures} diagnostic test(s) failed")
        return 1
    print(
        f"\nALL {len(cases) + 18} DIAGNOSTIC TESTS PASSED "
        f"({len(cases)} invalid-source, 8 valid-control, 4 runtime-error, "
        f"{len(invalid_utf8_cases)} malformed-UTF-8 cases)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
