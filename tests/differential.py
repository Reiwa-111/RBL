#!/usr/bin/env python3
"""Differential runner for the RBL backends.

The same RBL source is compiled and executed through two toolchains and the
observable behaviour is compared byte for byte: exit code, stdout and stderr.
This is the strongest check available for the two runtime implementations
(hand-written x86-64 assembly on Linux, C with __attribute__((sysv_abi)) on
Windows), because both share the frontend and the code generator.

Usage:
    python tests/differential.py                 # wsl vs native (Windows host)
    python tests/differential.py --a wsl --b native
    python tests/differential.py --a wsl --b native tests/cases/arithmetic.rbl

Every program is fed a few real input lines on stdin, so input()-reading
programs exercise the line-reading path instead of only the EOF path.
"""
from __future__ import annotations
import argparse, os, sys
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
                out.extend(sorted(p.glob('*.rbl')))
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
    for marker in (str(ROOT).replace('\\', '/'), '/mnt/q/' + str(ROOT).replace('\\', '/')[3:]):
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
    rc, out, err = rbltool.run_built(exe, stdin_text='Reiwa\n42\n3.5\n', settings={})
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
    for src in files:
        rel = src.relative_to(ROOT).as_posix()
        a = run_one(src, args.a)
        b = run_one(src, args.b)
        rc_only = is_nondeterministic(src)
        if rc_only:
            same = a['built'] == b['built'] and a['rc'] == b['rc']
        else:
            same = (a['built'] == b['built'] and a['rc'] == b['rc'] and
                    a['out'] == b['out'] and a['err'] == b['err'])
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
    print(f'\nALL {len(files)} PROGRAMS BEHAVE IDENTICALLY ON BOTH BACKENDS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
