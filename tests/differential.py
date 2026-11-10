#!/usr/bin/env python3
"""Differential runner for the RBL backends.

The same RBL source is compiled and executed through two toolchains. Successful
deterministic programs compare exit code, stdout and stderr byte for byte; known
negative fixtures compare rejection status and diagnostics. Files containing
random or platform-dependent output compare successful build and exit code only.

Usage:
    python tests/differential.py                 # wsl vs native (Windows host)
    python tests/differential.py --a wsl --b native
    python tests/differential.py --a wsl --b native tests/cases/arithmetic.rbl

Every program is fed a few real input lines on stdin, so input()-reading
programs exercise the line-reading path instead of only the EOF path.
"""
from __future__ import annotations
import argparse, os, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import rbltool  # noqa: E402

# Markers of output that cannot be identical across backends by design: the random
# API uses a different generator per runtime and rbl.fs.cwd() returns a platform
# path. Programs containing them are still built and executed on both backends,
# but compared on the exit code only. Deterministic uses such as
# `rbl.time.now_ms() > 0` are deliberately not markers.
NONDET_MARKERS = (
    'random_int(', 'random_float(', 'random_bool(',
    'rbl.random.int(', 'rbl.random.float(', 'rbl.random.bool(',
    'rbl.fs.cwd(',
)

EXPECTED_NEGATIVE = {
    'tests/cases/braces_rejected.rbl',
    'tests/cases/break_outside.rbl',
    'tests/cases/continue_outside.rbl',
    'tests/cases/fstrings_bad.rbl',
    'tests/cases/fstring_spec.rbl',
    'tests/cases/literals_bad.rbl',
    'tests/cases/strings_bad_nul.rbl',
}

EXPECTED_RUNTIME_ERROR = {
    'tests/cases/param_type_error.rbl',
    'tests/cases/evaluation_timing.rbl',
    'tests/cases/error_set_twice.rbl',
    'tests/cases/error_let_unknown.rbl',
    'tests/cases/error_short_circuit.rbl',
    'tests/cases/error_randome.rbl',
    'tests/cases/loop_inc_overflow.rbl',
    'tests/cases/range_outside_for.rbl',
    'tests/cases/for_non_range.rbl',
    'tests/cases/modulo_by_zero.rbl',
    'tests/cases/modulo_float.rbl',
}


def is_nondeterministic(path) -> bool:
    try:
        text = Path(path).read_text(encoding='utf-8', errors='replace')
    except OSError:
        return False
    return any(marker in text for marker in NONDET_MARKERS)


def collect(paths):
    if paths:
        out = []
        for p in paths:
            p = Path(p)
            if not p.is_absolute():
                p = ROOT / p
            if p.is_dir():
                out.extend(sorted(p.rglob('*.rbl')))
            else:
                out.append(p)
        return out
    files = []
    files.extend(sorted((ROOT / 'tests' / 'cases').glob('*.rbl')))
    files.append(ROOT / 'tests' / 'stdlib_smoke.rbl')
    files.extend(sorted((ROOT / 'examples').glob('*.rbl')))
    files.extend(sorted((ROOT / 'benchmarks').glob('*.rbl')))
    return files


def normalize(text: str) -> str:
    """Remove host-specific paths so that only behaviour differences remain."""
    text = text.replace('\\', '/')
    root = str(ROOT).replace('\\', '/')
    markers = [root]
    if len(root) >= 3 and root[1:3] == ':/':
        drive = root[0].lower()
        markers.append('/mnt/' + drive + '/' + root[3:])
    for marker in markers:
        text = text.replace(marker, '<ROOT>')
    return text


def run_one(source: Path, mode: str):
    os.environ['RBL_TOOLCHAIN'] = mode
    code, log, info = rbltool.build(source, {})
    if code:
        return {'built': False, 'rc': code, 'out': '', 'err': normalize(log)}
    exe = Path(info)
    # Feed real lines rather than an empty stream: input()-reading programs must
    # exercise the line-reading path, not just the EOF error path. Extra lines
    # are ignored by programs that read fewer of them (and by those that read
    # none at all).
    # Examples such as game_of_life.rbl intentionally write relative files.
    # Give each backend run a private cwd so differential checks never overwrite
    # files in the repository or leave test artifacts in the user's project.
    with tempfile.TemporaryDirectory(prefix='rbl-differential-') as temp_cwd:
        # The stdlib smoke source writes its transient fixture under build/.
        (Path(temp_cwd) / 'build').mkdir()
        rc, out, err = rbltool.run_built(
            exe, stdin_text='Reiwa\n42\n3.5\n', settings={}, cwd=Path(temp_cwd)
        )
    return {'built': True, 'rc': rc, 'out': out, 'err': normalize(err)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--a', default='wsl', help='first toolchain mode (default: wsl)')
    ap.add_argument('--b', default='native', help='second toolchain mode (default: native)')
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('paths', nargs='*')
    args = ap.parse_args()

    files = collect(args.paths)
    os.environ['RBL_TOOLCHAIN'] = args.a
    a_backend = rbltool.active_backend({})
    os.environ['RBL_TOOLCHAIN'] = args.b
    b_backend = rbltool.active_backend({})
    print(f'RBL differential runner:  A={args.a}  B={args.b}  programs={len(files)}')
    print(f'  A: {a_backend}')
    print(f'  B: {b_backend}')
    diffs = []
    rc_only_count = 0
    negative_count = 0
    runtime_error_count = 0
    successful_output_count = 0
    for src in files:
        rel = src.relative_to(ROOT).as_posix()
        a = run_one(src, args.a)
        b = run_one(src, args.b)
        rc_only = is_nondeterministic(src)
        expected_negative = rel in EXPECTED_NEGATIVE
        expected_runtime_error = rel in EXPECTED_RUNTIME_ERROR
        rc_only_count += int(rc_only)
        negative_count += int(expected_negative)
        if expected_negative:
            # These fixtures must be rejected by both compilers, with matching
            # diagnostics. A crash, timeout or asymmetric acceptance is not a pass.
            same = (not a['built'] and not b['built'] and a['rc'] == 1 and b['rc'] == 1
                    and a['err'] == b['err'])
        elif expected_runtime_error:
            # These programs must compile, then fail at runtime with the same
            # observable result on both backends.
            runtime_error_count += 1
            same = (a['built'] and b['built'] and a['rc'] == 1 and b['rc'] == 1
                    and a['out'] == b['out'] and a['err'] == b['err'])
        elif rc_only:
            # Random/cwd outputs are expected to differ, but positive sources
            # must still compile and execute successfully on both backends.
            same = a['built'] and b['built'] and a['rc'] == 0 and b['rc'] == 0
        else:
            same = (a['built'] and b['built'] and a['rc'] == 0 and b['rc'] == 0 and
                    a['out'] == b['out'] and a['err'] == b['err'])
            successful_output_count += 1
        if same:
            if args.verbose or rc_only:
                print(f'[same]     {rel}  rc={a["rc"]}' + ('  (rc-only: random/platform output)' if rc_only else ''))
        else:
            diffs.append((rel, a, b))
            print(f'[DIFFER]   {rel}')
            print(f'    A({a_backend}): built={a["built"]} rc={a["rc"]} out={a["out"]!r} err={a["err"]!r}')
            print(f'    B({b_backend}): built={b["built"]} rc={b["rc"]} out={b["out"]!r} err={b["err"]!r}')
    if diffs:
        print(f'\n{len(diffs)} of {len(files)} programs differ between {args.a} and {args.b}')
        return 1
    print(
        f'\nALL {len(files)} SOURCE CASES PASS ON BOTH BACKENDS '
        f'({successful_output_count} successful-output comparisons, '
        f'{runtime_error_count} expected runtime-error comparisons, '
        f'{negative_count} expected-negative diagnostic comparisons, '
        f'{rc_only_count} build/exit-only comparisons)'
    )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
