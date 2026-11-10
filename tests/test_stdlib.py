#!/usr/bin/env python3
from __future__ import annotations
import subprocess
import shutil
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import rbltool

ROOT = Path(__file__).resolve().parents[1]


def run_expected_runtime_error(path: Path, source: str, expected: str) -> bool:
    path.write_text(source, encoding='utf-8')
    build_output = rbltool.build_output_dir(path, {})
    try:
        rc, stdout, stderr = rbltool.run_program(path, {})
        if rc == 0 or expected not in stderr:
            print(f'RBL RUNTIME DIAGNOSTIC: FAIL ({path.name})')
            print('stdout:', stdout)
            print('stderr:', stderr, file=sys.stderr)
            print('expected:', expected, file=sys.stderr)
            return False
        return True
    finally:
        shutil.rmtree(build_output, ignore_errors=True)


def main() -> int:
    src = ROOT / 'tests' / 'stdlib_smoke.rbl'
    rc, out, err = rbltool.run_program(src, {})
    if rc != 0:
        print('RBL STDLIB SMOKE: FAIL')
        print(out)
        print(err, file=sys.stderr)
        return 1
    required = [
        '=== GLOBAL STDLIB ===', '=== IO / FS ===', 'RBL FILE OK',
        '=== MATH ===', '=== STRING ===', 'HELLO RBL', 'hello rbl',
        '=== TIME ===', '=== RANDOM ===', '=== PARENTHESIZED BLOCKS ===',
        'if works', 'DONE'
    ]
    if any(marker not in out for marker in required):
        print('RBL STDLIB SMOKE: FAIL')
        print(out)
        return 1

    build_root = ROOT / 'build'
    build_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='stdlib-runtime-errors-', dir=build_root) as temp:
        temp_dir = Path(temp)
        error_cases = [
            (
                'len_invalid_type.rbl',
                'func main() (\n    print(len(true))\n)\n',
                'len() expects a string, list, dictionary or tuple',
            ),
            (
                'for_invalid_iterable.rbl',
                'func main() (\n    for (value in 1) (\n        print(value)\n    )\n)\n',
                'for (x in ...) expects a range, list or tuple',
            ),
            (
                'tuple_list_set.rbl',
                'func main() (\n    set values = (1, 2)\n    list_set(values, 0, 9)\n)\n',
                'tuples cannot be modified',
            ),
        ]
        for name, source, expected in error_cases:
            if not run_expected_runtime_error(temp_dir / name, source, expected):
                return 1

    with tempfile.TemporaryDirectory(prefix='stdlib-input-smoke-', dir=build_root) as temp:
        src_in = Path(temp) / 'stdlib_input_smoke.rbl'
        build_output = rbltool.build_output_dir(src_in, {})
        src_in.write_text(
            'func main() (\n'
            '    set name = input("Name: ")\n'
            '    print(name)\n'
            ')\n', encoding='utf-8')
        try:
            build_rc, build_log, exe_path = rbltool.build(src_in, {})
            if build_rc != 0:
                print('RBL INPUT SMOKE: FAIL')
                print(build_log)
                return 1
            # The built program is a Linux ELF under WSL/native Linux and a PE image
            # under the native Windows backend, so it must be launched through the
            # active backend instead of being spawned directly.
            rc, stdout, stderr = rbltool.run_built(exe_path, stdin_text='Reiwa\n')
            if rc != 0 or not stdout.endswith('Reiwa\n'):
                print('RBL INPUT SMOKE: FAIL')
                print('stdout:', stdout)
                print('stderr:', stderr, file=sys.stderr)
                return 1
        finally:
            shutil.rmtree(build_output, ignore_errors=True)
    print('RBL STDLIB + INPUT + RUNTIME DIAGNOSTIC TESTS PASSED')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
