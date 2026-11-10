#!/usr/bin/env python3
"""Compile and assemble the complete positive/negative RBL source corpus."""
from __future__ import annotations

import shlex
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402

NEGATIVE = {
    "tests/cases/braces_rejected.rbl",
    "tests/cases/break_outside.rbl",
    "tests/cases/continue_outside.rbl",
    "tests/cases/fstrings_bad.rbl",
    "tests/cases/fstring_spec.rbl",
    "tests/cases/literals_bad.rbl",
    "tests/cases/strings_bad_nul.rbl",
}

# This uppercase-extension fixture is intentionally plain text: it exercises
# Unicode filename/content handling in the IDE's project search, not the RBL
# compiler. Keep it in the repository, but out of the source-compilation corpus.
NON_SOURCE_FIXTURES = {
    "tests/поиск_юникод.RBL",
}


def compiler_command(source: Path, args: list[str], settings: dict) -> tuple[int, str]:
    if rbltool.use_wsl(settings):
        compiler = shlex.quote(rbltool.wsl_path(rbltool.COMPILER_BIN, settings))
        source_path = shlex.quote(rbltool.wsl_path(source, settings))
        converted_args = []
        output_path = False
        for arg in args:
            if output_path:
                converted_args.append(shlex.quote(rbltool.wsl_path(Path(arg), settings)))
            else:
                converted_args.append(shlex.quote(arg))
            output_path = arg == "-S"
        command = " ".join((compiler, source_path, *converted_args, "--target", "linux"))
        code, out, err = rbltool.run_wsl(command, settings)
        return code, out + err
    if rbltool.use_native_windows(settings):
        tc = rbltool.find_native_toolchain()
        command = [str(rbltool.WIN_COMPILER_BIN), str(source), *args, "--target", "win"]
        code, out, err = rbltool.run_native(command, env=rbltool.toolchain_env(tc))
        return code, out + err
    command = [str(rbltool.COMPILER_BIN), str(source), *args, "--target", "linux"]
    code, out, err = rbltool.run_native(command)
    return code, out + err


def assemble(assembly: Path, obj: Path, settings: dict) -> tuple[int, str]:
    if rbltool.use_wsl(settings):
        asm_path = shlex.quote(rbltool.wsl_path(assembly, settings))
        obj_path = shlex.quote(rbltool.wsl_path(obj, settings))
        code, out, err = rbltool.run_wsl(f"as --64 {asm_path} -o {obj_path}", settings)
        return code, out + err
    if rbltool.use_native_windows(settings):
        tc = rbltool.find_native_toolchain()
        code, out, err = rbltool.run_native(
            [str(tc["as"]), "--64", str(assembly), "-o", str(obj)],
            env=rbltool.toolchain_env(tc),
        )
        return code, out + err
    code, out, err = rbltool.run_native(["as", "--64", str(assembly), "-o", str(obj)])
    return code, out + err


def main() -> int:
    settings = rbltool.load_settings()
    try:
        rbltool.ensure_toolchain(settings)
    except Exception as exc:
        print(f"ERROR: toolchain initialization failed: {exc}", file=sys.stderr)
        return 2

    files = sorted(
        path for path in (
            set((ROOT / "examples").rglob("*.rbl"))
            | set((ROOT / "tests").rglob("*.rbl"))
        )
        if path.relative_to(ROOT).as_posix() not in NON_SOURCE_FIXTURES
    )
    relative = {path.relative_to(ROOT).as_posix(): path for path in files}
    missing_negative = sorted(NEGATIVE - set(relative))
    if missing_negative:
        print(f"ERROR: expected negative fixtures are missing: {missing_negative}", file=sys.stderr)
        return 1

    positives = assembled = rejected = 0
    failures: list[tuple[str, str, str]] = []
    with tempfile.TemporaryDirectory(prefix="rbl-corpus-") as temp:
        output = Path(temp)
        for index, source in enumerate(files):
            rel = source.relative_to(ROOT).as_posix()
            expected_reject = rel in NEGATIVE
            check_rc, check_output = compiler_command(source, ["--check"], settings)
            if expected_reject:
                # A rejection counts only when the compiler emits a structured
                # source diagnostic. Crashes, tool-launch failures and timeouts
                # are not valid negative-test successes.
                if check_rc != 1 or "error [" not in check_output:
                    failures.append((rel, "negative fixture did not produce a compiler source diagnostic",
                                     f"exit={check_rc}\n{check_output}"))
                else:
                    rejected += 1
                continue
            if check_rc:
                failures.append((rel, "unexpected --check failure", check_output))
                continue

            assembly = output / f"{index}.s"
            obj = output / f"{index}.o"
            compile_rc, compile_output = compiler_command(source, ["-S", str(assembly)], settings)
            if compile_rc:
                failures.append((rel, "assembly generation failed", compile_output))
                continue
            assemble_rc, assemble_output = assemble(assembly, obj, settings)
            if assemble_rc:
                failures.append((rel, "assembler rejected output", assemble_output))
                continue
            positives += 1
            assembled += 1

    print(
        f"{rbltool.active_backend(settings)}: sources={len(files)} "
        f"positive={positives} assembled={assembled} expected_rejects={rejected} "
        f"failures={len(failures)}"
    )
    for rel, reason, output in failures[:30]:
        print(f"FAIL {rel}: {reason}\n{output[:1000]}")

    expected_positive = len(files) - len(NEGATIVE)
    if positives != expected_positive or assembled != expected_positive or rejected != len(NEGATIVE) or failures:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
