#!/usr/bin/env python3
"""Exercise the compiler CLI and Rust worker from an installed RBL Studio payload."""
from __future__ import annotations

import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path




def load_worker_acceptance():
    helper_path = Path(__file__).resolve().with_name("installed_worker_acceptance.py")
    spec = importlib.util.spec_from_file_location("rbl_installed_worker_acceptance", helper_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load installed worker acceptance helper: {helper_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.run_worker


def app_python(root: Path) -> Path:
    if os.name == "nt":
        candidate = root / "runtime" / "python" / "python.exe"
    else:
        candidate = root / "runtime" / "python" / "bin" / "python3"
    if not candidate.is_file():
        raise AssertionError(f"installed payload is missing its app-local CPython runtime: {candidate}")
    version = subprocess.run(
        [str(candidate), "-I", "-B", "-c", "import sys; print('.'.join(map(str, sys.version_info[:3])))"],
        cwd=root,
        text=True,
        capture_output=True,
        timeout=45,
        check=False,
    )
    if version.returncode or version.stdout.strip() != "3.14.10":
        raise AssertionError(f"installed app-local CPython version mismatch: {version.stdout!r} {version.stderr!r}")
    return candidate


def run_cli(root: Path, python: Path, env: dict[str, str], *args: str) -> subprocess.CompletedProcess[str]:
    command = [str(python), "-B", str(root / "rbl.py"), *args]
    result = subprocess.run(
        command,
        cwd=root,
        env=env,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        timeout=180,
        check=False,
    )
    if result.returncode:
        raise AssertionError(
            f"installed CLI command failed ({result.returncode}): {' '.join(args)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: installed_cli_acceptance.py INSTALL_ROOT")
    root = Path(sys.argv[1]).resolve(strict=True)
    wrapper = "rbl.cmd" if os.name == "nt" else "rbl"
    for required in (
        "rbl.py", wrapper, "VERSION", "tools/rbltool.py", "compiler", "runtime",
        "runtime/python/rbl-python-runtime.json", "runtime/python/UPSTREAM.md",
        "bin", "examples/test.rbl",
    ):
        if not (root / required).exists():
            raise AssertionError(f"installed payload is missing required CLI input: {required}")

    python = app_python(root)
    env = os.environ.copy()
    env.pop("RBL_PYTHON", None)
    env["RBL_TOOLCHAIN"] = "native"
    env["RBL_NO_BUILD_CACHE"] = "1"
    env["PYTHONUTF8"] = "1"
    env["PYTHONIOENCODING"] = "utf-8"
    if os.name == "nt":
        mingw = root / "toolchain" / "windows-x86_64" / "msys64" / "ucrt64" / "bin"
        if not (mingw / "gcc.exe").is_file() or not (mingw / "as.exe").is_file():
            raise AssertionError("installed payload is missing the bundled Windows GNU toolchain")
        env["RBL_MINGW"] = str(mingw)
        if shutil.which("python.exe", path=env.get("PATH", "")):
            raise AssertionError("clean-path acceptance unexpectedly found a system python.exe")
    else:
        env.pop("RBL_MINGW", None)
        if shutil.which("python3", path=env.get("PATH", "")) or shutil.which("cc", path=env.get("PATH", "")):
            raise AssertionError("clean-path acceptance unexpectedly found system Python or cc")

    expected_version = f"RBL {(root / 'VERSION').read_text(encoding='utf-8').strip()}"
    version = run_cli(root, python, env, "version")
    if version.stdout.strip() != expected_version:
        raise AssertionError(f"unexpected installed version output: {version.stdout!r}")

    doctor = run_cli(root, python, env, "doctor")
    doctor_text = doctor.stdout.lower()
    if "backend" not in doctor_text or "runtime abi" not in doctor_text:
        raise AssertionError(f"installed doctor output is incomplete:\n{doctor.stdout}\n{doctor.stderr}")

    sample = "examples/test.rbl"
    run_cli(root, python, env, "check", sample)
    execution = run_cli(root, python, env, "run", sample)
    for expected in ("10", "change number", "20", "big", "5"):
        if expected not in execution.stdout.splitlines():
            raise AssertionError(f"installed sample output is missing {expected!r}:\n{execution.stdout}")

    load_worker_acceptance()(root, env)
    run_cli(root, python, env, "clean")
    if (root / "build" / "sources").exists():
        raise AssertionError("installed CLI clean left its generated compiler cache behind")
    build_root = root / "build"
    if build_root.exists():
        if any(build_root.iterdir()):
            raise AssertionError(f"installed CLI left untracked build artifacts: {list(build_root.iterdir())!r}")
        build_root.rmdir()

    print(f"Installed CLI acceptance: PASS ({expected_version}; app-local CPython 3.14.10; doctor; check; run; worker; clean)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
