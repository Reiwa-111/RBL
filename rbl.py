#!/usr/bin/env python3
"""RBL command-line tool: cargo/pytest/python-like project workflow."""
from __future__ import annotations
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
import rbltool  # type: ignore

VERSION = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()


def manifest_path(cwd: Path) -> Path:
    p = cwd / 'rbl.toml'
    return p if p.exists() else ROOT / 'rbl.toml'


def read_manifest(cwd: Path) -> dict[str, str]:
    p = manifest_path(cwd)
    data: dict[str, str] = {}
    if not p.exists():
        return data
    section = ''
    for raw in p.read_text(encoding='utf-8').splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1]
            continue
        if '=' in line:
            k, v = line.split('=', 1)
            v = v.strip().strip('"')
            data[f'{section}.{k.strip()}'] = v
    return data


def resolve_source(arg: str | None, cwd: Path) -> Path:
    if arg:
        p = (cwd / arg).resolve()
    else:
        m = read_manifest(cwd)
        entry = m.get('package.entry', 'src/main.rbl')
        p = (cwd / entry).resolve()
        if not p.exists() and (cwd / 'main.rbl').exists():
            p = (cwd / 'main.rbl').resolve()
    if not p.exists():
        raise SystemExit(f'RBL source not found: {p}')
    return p


def run_tests() -> int:
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'run_tests.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_stdlib.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_build_cache.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_clean_command.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_asm_call_alignment.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_runtime_abi.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_gc_safepoint_codegen.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_string_ownership.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_string_io_gc.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_rbltool_streaming.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_bundled_toolchain.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_toolchain_manifest.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_linux_launcher.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_posix_file_metadata.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_wx_find_in_files_security.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_wx_project_tree_posix_security.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_wx_save_dirfd_security.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_wx_worker_lifecycle.py')], cwd=ROOT)
    if rc:
        return rc
    rc = subprocess.call([sys.executable, str(ROOT / 'native' / 'wx-ui' / 'tests' / 'dpi_static_test.py')], cwd=ROOT)
    if rc:
        return rc
    if os.name == 'nt':
        rc = subprocess.call([
            'powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', str(ROOT / 'tests' / 'test_windows_registry_policy.ps1'),
        ], cwd=ROOT)
        if rc:
            return rc
        rc = subprocess.call([
            'powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', str(ROOT / 'tests' / 'test_windows_save_cleanup.ps1'),
        ], cwd=ROOT)
        if rc:
            return rc
    return subprocess.call([sys.executable, str(ROOT / 'tests' / 'test_diagnostics.py')], cwd=ROOT)


def run_bench(args: list[str]) -> int:
    """Portable benchmark suite: works on both backends."""
    cmd = [sys.executable, str(ROOT / 'benchmarks' / 'run_benchmarks.py'), *args]
    return subprocess.call(cmd, cwd=ROOT)


def run_bench5way(args: list[str]) -> int:
    """Five-implementation benchmark. Needs Linux/WSL for the syscall ASM baseline."""
    cmd = [sys.executable, str(ROOT / 'benchmarks' / 'compare_all.py'), *args]
    return subprocess.call(cmd, cwd=ROOT)


def run_differential(args: list[str]) -> int:
    """Compare two backends byte for byte over the whole corpus."""
    cmd = [sys.executable, str(ROOT / 'tests' / 'differential.py'), *args]
    return subprocess.call(cmd, cwd=ROOT)


def simple_format(text: str) -> str:
    lines = []
    indent = 0
    for raw in text.splitlines():
        s = raw.rstrip()
        stripped = s.strip()
        if not stripped:
            lines.append('')
            continue
        # Canonical RBL block indentation uses parentheses. Only a leading ')' closes a block
        # and a line ending in '(' opens a block. Ordinary call/grouping parentheses do not
        # participate in indentation.
        if stripped.startswith(')'):
            indent = max(0, indent - 1)
        lines.append(' ' * 4 * indent + stripped)
        if stripped.endswith('('):
            indent += 1
    return '\n'.join(lines) + '\n'


def format_file(path: Path, check: bool) -> int:
    original = path.read_text(encoding='utf-8')
    formatted = simple_format(original)
    if check:
        if formatted != original:
            print(f'Would reformat: {path}')
            return 1
        print(f'Format OK: {path}')
        return 0
    if formatted != original:
        path.write_text(formatted, encoding='utf-8')
        print(f'Formatted: {path}')
    else:
        print(f'Already formatted: {path}')
    return 0


def generate_docs(source: Path, out: Path) -> int:
    text = source.read_text(encoding='utf-8')
    pat = re.compile(r'^\s*func\s+([\w\u0080-\uffff]+)\s*\(([^)]*)\)\s*([\w\u0080-\uffff]+)?\s*\(', re.M)
    rows = ['# RBL API', '', f'Source: `{source}`', '', '| Function | Parameters | Return |', '|---|---|---|']
    for m in pat.finditer(text):
        rows.append(f'| `{m.group(1)}` | `{m.group(2).strip()}` | `{(m.group(3) or "Unit")}` |')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text('\n'.join(rows) + '\n', encoding='utf-8')
    print(f'Generated: {out}')
    return 0


def new_project(name: str, target: Path) -> int:
    if target.exists() and any(target.iterdir()):
        raise SystemExit(f'target directory is not empty: {target}')
    (target / 'src').mkdir(parents=True, exist_ok=True)
    (target / 'tests').mkdir(exist_ok=True)
    (target / 'benchmarks').mkdir(exist_ok=True)
    (target / 'rbl.toml').write_text(
        '[package]\n'
        f'name = "{name}"\n'
        'version = "0.1.0"\n'
        'entry = "src/main.rbl"\n', encoding='utf-8')
    (target / 'src' / 'main.rbl').write_text(
        'func main() (\n'
        '    print("Hello from RBL!")\n'
        ')\n', encoding='utf-8')
    (target / 'README.md').write_text(
        f'# {name}\n\nBuilt with RBL Studio.\n\n```bash\nrbl check\nrbl run\nrbl test\n```\n', encoding='utf-8')
    print(f'Created RBL project: {target}')
    return 0


def clean(cwd: Path) -> int:
    # Build outputs are centralized beside this tool, not in the caller's
    # project directory. Preserve evidence, source archives, RC bundles,
    # visualizations and other user-owned data under build/.
    build_root = Path(rbltool.BUILD_DIR)
    cache_root = build_root / 'sources'
    if not cache_root.exists() and not cache_root.is_symlink():
        print('Nothing to clean.')
        return 0
    is_junction = getattr(Path, 'is_junction', lambda self: False)
    if (build_root.is_symlink() or is_junction(build_root)
            or cache_root.is_symlink() or is_junction(cache_root)):
        print(f'Refusing to clean a symlinked/junction build path: {cache_root}', file=sys.stderr)
        return 1
    try:
        resolved_build = build_root.resolve(strict=True)
        resolved_cache = cache_root.resolve(strict=True)
    except OSError as exc:
        print(f'Cannot safely resolve build cache: {exc}', file=sys.stderr)
        return 1
    if resolved_cache.parent != resolved_build or resolved_cache.name != 'sources':
        print(f'Refusing to clean unexpected path: {resolved_cache}', file=sys.stderr)
        return 1
    if not resolved_cache.is_dir():
        print(f'Refusing to clean non-directory build cache: {resolved_cache}', file=sys.stderr)
        return 1
    shutil.rmtree(resolved_cache)
    print(f'Removed compiler build cache: {resolved_cache}')
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(prog='rbl', description='Reiwa Bat Language toolchain')
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('version')
    sub.add_parser('doctor')
    sub.add_parser('bootstrap')
    sub.add_parser('env')
    p_new = sub.add_parser('new'); p_new.add_argument('name'); p_new.add_argument('--path', default=None)
    sub.add_parser('init')
    for name in ('check', 'build', 'run', 'asm'):
        p = sub.add_parser(name)
        p.add_argument('source', nargs='?')
    sub.add_parser('test')
    sub.add_parser('bench')
    p_bench5 = sub.add_parser('bench5way'); p_bench5.add_argument('--reps', type=int, default=10); p_bench5.add_argument('--warmups', type=int, default=2)
    p_bench3 = sub.add_parser('bench3way', help='Legacy alias for bench5way'); p_bench3.add_argument('--reps', type=int, default=10); p_bench3.add_argument('--warmups', type=int, default=2)
    p_diff = sub.add_parser('diff'); p_diff.add_argument('--a', default='wsl'); p_diff.add_argument('--b', default='native'); p_diff.add_argument('paths', nargs='*')
    p_fmt = sub.add_parser('fmt'); p_fmt.add_argument('source', nargs='?'); p_fmt.add_argument('--check', action='store_true')
    p_docs = sub.add_parser('doc'); p_docs.add_argument('source', nargs='?'); p_docs.add_argument('-o', '--output', default='target/docs.md')
    sub.add_parser('clean')
    args = ap.parse_args()
    cwd = Path.cwd()
    if args.cmd == 'version': print(f'RBL {VERSION}'); return 0
    if args.cmd == 'bootstrap':
        settings = {}
        old = rbltool.os.environ.get('RBL_REBUILD_COMPILER')
        rbltool.os.environ['RBL_REBUILD_COMPILER'] = '1'
        try:
            rbltool.ensure_toolchain(settings)
        finally:
            if old is None:
                rbltool.os.environ.pop('RBL_REBUILD_COMPILER', None)
            else:
                rbltool.os.environ['RBL_REBUILD_COMPILER'] = old
        print('RBL bootstrap complete.')
        return 0
    if args.cmd == 'doctor':
        return rbltool.main_with(['doctor']) if hasattr(rbltool, 'main_with') else subprocess.call([sys.executable, str(ROOT/'tools'/'rbltool.py'), 'doctor'])
    if args.cmd == 'env':
        print(f'RBL_ROOT={ROOT}'); print(f'PROJECT_ROOT={cwd}'); print(f'PYTHON={sys.executable}'); return 0
    if args.cmd == 'new':
        target = Path(args.path).resolve() if args.path else (cwd / args.name).resolve()
        return new_project(args.name, target)
    if args.cmd == 'init':
        mpath = cwd / 'rbl.toml'
        if mpath.exists():
            print(f'rbl.toml already exists: {mpath}')
            return 0
        (cwd / 'src').mkdir(exist_ok=True)
        (cwd / 'tests').mkdir(exist_ok=True)
        (cwd / 'benchmarks').mkdir(exist_ok=True)
        mpath.write_text('[package]\nname = "%s"\nversion = "0.1.0"\nentry = "src/main.rbl"\n' % cwd.name, encoding='utf-8')
        main = cwd / 'src' / 'main.rbl'
        if not main.exists():
            main.write_text('func main() (\n    print("Hello from RBL!")\n)\n', encoding='utf-8')
        print(f'Initialized RBL project: {cwd}')
        return 0
    if args.cmd in {'check','build','run','asm'}:
        src = resolve_source(args.source, cwd)
        settings = {}
        if args.cmd == 'check': rc, out, err = rbltool.check(src, settings)
        elif args.cmd == 'build':
            rc, out, info = rbltool.build(src, settings)
            if info: print(f'Built: {info}')
        elif args.cmd == 'run': rc, out, err = rbltool.run_program(src, settings)
        else: rc, out, err, _ = rbltool.assemble_preview(src, settings)
        if out: print(out, end='')
        if args.cmd != 'build' and 'err' in locals() and err: print(err, file=sys.stderr, end='')
        return rc
    if args.cmd == 'test': return run_tests()
    if args.cmd == 'bench': return run_bench([])
    if args.cmd in {'bench5way', 'bench3way'}:
        if args.cmd == 'bench3way':
            print("rbl bench3way is a legacy alias; use rbl bench5way", file=sys.stderr)
        return run_bench5way([f'--reps={args.reps}', f'--warmups={args.warmups}'])
    if args.cmd == 'diff': return run_differential([f'--a={args.a}', f'--b={args.b}', *args.paths])
    if args.cmd == 'fmt': return format_file(resolve_source(args.source, cwd), args.check)
    if args.cmd == 'doc': return generate_docs(resolve_source(args.source, cwd), (cwd / args.output).resolve())
    if args.cmd == 'clean': return clean(cwd)
    return 2

if __name__ == '__main__': raise SystemExit(main())
