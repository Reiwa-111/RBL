#!/usr/bin/env python3
"""Compare compiler diagnostics emitted by the WSL and native Windows backends."""
from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


CASES = {
    "lexer_unicode": 'func main() (\n    print("é") @\n)\n',
    "lexer_cjk_unicode": 'func main() (\n    print("中") @\n)\n',
    "lexer_supplementary_unicode": 'func main() (\n    print("😀") @\n)\n',
    "lexer_combining_mark_unicode": 'func main() (\n    print("é") @\n)\n',
    "lexer_bare_cr_comment": "# comment\r@\nfunc main() (\n)\n",
    "lexer_numeric_separator": "func main() (\n    let x = 1__2\n)\n",
    "parser_unexpected_token": "func main() (\n    let x = )\n)\n",
    "parser_unknown_namespace": "func main() (\n    print(foo.bar.baz())\n)\n",
    "parser_unknown_constant": "func main() (\n    print(rbl.math.tau)\n)\n",
    "fstring_escaped_brace_mapping": 'func main() (\n    print(f"{{x}} \\q")\n)\n',
    "fstring_nested_dict": 'func main() (\n    print(f"{len({1: })}")\n)\n',
    "fstring_nested_fstring": 'func main() (\n    print(f"""outer {f"inner {1}"}""")\n)\n',
    "fstring_format_unknown_type_after_width": 'func main() (\n    print(f"value {1:>8q}")\n)\n',
    "fstring_format_dynamic_spec": 'func main() (\n    print(f"value {1:{2}}")\n)\n',
    "fstring_format_dynamic_width": 'func main() (\n    print(f"value {1:>8{2}}")\n)\n',
    "fstring_hash_comment_brace": 'func main() (\n    print(f"""{1 # } : comment\n + 2}""")\n)\n',
    "fstring_slash_comment_brace": 'func main() (\n    print(f"""{1 // } : comment\n + 2}""")\n)\n',
    "fstring_unicode_literal_position": 'func main() (\n    print(f"é \\q")\n)\n',
    "lexer_unterminated_string_eof": 'func main() (\n    print("unfinished',
    "lexer_source_nul_byte": "func main() (\n    \0\n)\n",
    "lexer_invalid_utf8": b"func main() (\n    \x80\n)\n",
    "lexer_invalid_utf8_overlong": b"func main() (\n    \xC0\xAF\n)\n",
    "lexer_invalid_utf8_surrogate": b"func main() (\n    \xED\xA0\x80\n)\n",
    "lexer_invalid_utf8_out_of_range": b"func main() (\n    \xF4\x90\x80\x80\n)\n",
    "lexer_invalid_utf8_truncated": b"func main() (\n    \xE2\x82\n)\n",
    "lexer_invalid_utf8_after_unicode": b"func main() (\n    \xC3\xA9 \x80\n)\n",
    "semantic_break": "func main() (\n    if (true) (\n        break\n    )\n)\n",
    "semantic_continue": "func main() (\n    continue\n)\n",
    "codegen_builtin_arity": "func main() (\n    len(1, 2)\n)\n",
    "codegen_method_arity": 'func main() (\n    rbl.math.pow(1)\n)\n',
}


def normalize(text: str) -> str:
    text = text.replace("\\", "/")
    root = str(ROOT).replace("\\", "/")
    text = text.replace(root, "<ROOT>")
    if len(root) > 2 and root[1:3] == ":/":
        text = text.replace("/mnt/" + root[0].lower() + root[2:], "<ROOT>")
    return text


def run_case(path: Path, source: str | bytes, mode: str):
    os.environ["RBL_TOOLCHAIN"] = mode
    path.write_bytes(source if isinstance(source, bytes) else source.encode("utf-8"))
    code, stdout, stderr = rbltool.check(path, {})
    return code, normalize(stdout), normalize(stderr)


def main() -> int:
    if os.name != "nt":
        print("This diagnostic differential test requires Windows with WSL and native MinGW-w64.")
        return 2
    if not rbltool.wsl_available({}) or rbltool.find_native_toolchain() is None:
        print("Both WSL and native MinGW-w64 must be available for this comparison.")
        return 2

    original = os.environ.get("RBL_TOOLCHAIN")
    failures = []
    try:
        build_dir = ROOT / "build"
        build_dir.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="rbl-diagnostic-diff-", dir=build_dir) as temp:
            temp_dir = Path(temp)
            for name, source in CASES.items():
                path = temp_dir / f"{name}.rbl"
                wsl = run_case(path, source, "wsl")
                native = run_case(path, source, "native")
                if wsl != native:
                    failures.append(name)
                    print(f"[DIFFER] {name}\n  WSL:    {wsl!r}\n  native: {native!r}")
                else:
                    print(f"[same] {name}: rc={wsl[0]}")
    finally:
        if original is None:
            os.environ.pop("RBL_TOOLCHAIN", None)
        else:
            os.environ["RBL_TOOLCHAIN"] = original

    if failures:
        print(f"\n{len(failures)} diagnostic cases differ")
        return 1
    print(f"\nALL {len(CASES)} DIAGNOSTIC CASES MATCH ON WSL AND NATIVE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
