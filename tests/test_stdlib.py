#!/usr/bin/env python3
from __future__ import annotations
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import rbltool

ROOT = Path(__file__).resolve().parents[1]


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

    (ROOT / 'build').mkdir(parents=True, exist_ok=True)
    src_in = ROOT / 'build' / 'stdlib_input_smoke.rbl'
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
        if src_in.exists():
            src_in.unlink()
    print('RBL STDLIB + INPUT TESTS PASSED')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
