#!/usr/bin/env python3
"""RBL Studio toolchain driver.

Two user-program backends produce the same generated assembly:

  linux   : RBL -> x86-64 ASM (ELF) -> GNU as --64 -> ld + libc/libm  -> ELF
  windows : RBL -> x86-64 ASM (PE)  -> GNU as --64 -> gcc + MSVCRT/UCRT -> .exe

The backend only changes the object format, the process entry/exit glue and the
runtime implementation (hand-written assembly on Linux, a C runtime compiled
with __attribute__((sysv_abi)) on Windows). Every RBL function uses the System V
convention on both targets, so one code generator serves both.

On Windows the native MinGW-w64 toolchain is preferred when it is available and
WSL is used as a fallback. Settings key ``wsl_mode`` selects the mode:

  auto    - native Windows toolchain if found, otherwise WSL   (default)
  native  - Windows toolchain only (``never``/``off`` are aliases)
  wsl     - WSL only (``always`` is an alias)

A host C compiler is only ever needed to build the bootstrap compiler itself and
the Windows runtime object.
"""
from __future__ import annotations
import argparse, json, os, platform, shlex, shutil, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IDE_CONFIG = Path(os.environ.get("APPDATA", Path.home() / ".config")) / "RBLStudio" / "settings.json" if os.name == "nt" else Path.home()/".config"/"RBLStudio"/"settings.json"
LINUX_BIN_DIR = ROOT / "bin" / "linux-x86_64"
WIN_BIN_DIR = ROOT / "bin" / "win-x86_64"
COMPILER_SRC = ROOT / "compiler" / "rblc_asm.c"
RUNTIME_SRC = ROOT / "runtime" / "rbl_runtime.s"
RUNTIME_WIN_SRC = ROOT / "runtime" / "rbl_runtime_win.c"
# Containers are written once in C and linked into both targets.
CONTAINERS_SRC = ROOT / "runtime" / "rbl_containers.c"
BUILD_DIR = ROOT / "build"

# Backwards-compatible names: the Linux backend is the historical default.
BIN_DIR = LINUX_BIN_DIR
COMPILER_BIN = LINUX_BIN_DIR / "rblc-asm"
RUNTIME_OBJ = LINUX_BIN_DIR / "rbl_runtime.o"
CONTAINERS_OBJ = LINUX_BIN_DIR / "rbl_containers.o"
WIN_COMPILER_BIN = WIN_BIN_DIR / "rblc-asm.exe"
WIN_RUNTIME_OBJ = WIN_BIN_DIR / "rbl_runtime_win.o"
WIN_CONTAINERS_OBJ = WIN_BIN_DIR / "rbl_containers_win.o"

# A generated program that regresses into an endless loop must fail the suite
# instead of blocking it forever. 124 matches the shell's timeout exit status.
PROGRAM_TIMEOUT = float(os.environ.get("RBL_TIMEOUT", "180"))
TIMEOUT_RC = 124

DEFAULT_WSL_DISTRO = "Ubuntu-24.04"


def _configure_stdio():
    """Make console output encoding-safe on Windows.

    The default console code page (cp866/cp1251/...) cannot represent arbitrary
    file paths or program output, and printing such text raises
    UnicodeEncodeError and kills the tool. Switch the console to UTF-8 and never
    fail on an unencodable character.
    """
    if os.name == "nt":
        try:
            import ctypes
            ctypes.windll.kernel32.SetConsoleOutputCP(65001)
            ctypes.windll.kernel32.SetConsoleCP(65001)
        except Exception:
            pass
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


_configure_stdio()


def load_settings():
    try:
        return json.loads(IDE_CONFIG.read_text(encoding="utf-8"))
    except Exception:
        return {}


def is_windows():
    return os.name == "nt"


def have(cmd: str) -> bool:
    return shutil.which(cmd) is not None


# --------------------------------------------------------------------------
# Native Windows (MinGW-w64) toolchain discovery
# --------------------------------------------------------------------------

def _mingw_candidates():
    cands = []
    env = os.environ.get("RBL_MINGW")
    if env:
        cands.append(Path(env))
    roots = [Path("C:/msys64"), Path("C:/msys2"), Path("C:/tools/msys64"),
             Path("C:/mingw64"), Path("C:/MinGW"), Path("C:/ProgramData/mingw64")]
    for r in roots:
        for sub in ("ucrt64", "mingw64", "clang64", "mingw32", ""):
            cands.append(r / sub / "bin" if sub else r / "bin")
    return cands


def find_native_toolchain():
    """Return a dict describing a native Windows GNU toolchain, or None."""
    if not is_windows():
        return None
    for d in _mingw_candidates():
        try:
            if not d or not d.is_dir():
                continue
        except OSError:
            continue
        gcc, asm = d / "gcc.exe", d / "as.exe"
        if gcc.is_file() and asm.is_file():
            return {"bin": d, "gcc": gcc, "as": asm,
                    "nm": d / "nm.exe" if (d / "nm.exe").is_file() else None,
                    "name": f"{d.parent.name}/{d.name}"}
    gcc, asm = shutil.which("gcc"), shutil.which("as")
    if gcc and asm:
        return {"bin": Path(gcc).parent, "gcc": Path(gcc), "as": Path(asm),
                "nm": Path(shutil.which("nm")) if shutil.which("nm") else None,
                "name": "PATH"}
    return None


def native_toolchain_version(tc):
    try:
        p = subprocess.run([str(tc["gcc"]), "--version"], text=True, capture_output=True,
                           encoding="utf-8", errors="replace", timeout=30)
        return (p.stdout or "").splitlines()[0].strip()
    except Exception as exc:                                   # pragma: no cover
        return f"unavailable ({exc})"


# --------------------------------------------------------------------------
# WSL bridge
# --------------------------------------------------------------------------

def wsl_distro(settings=None) -> str:
    settings = settings or {}
    # Environment variable wins so CI/portable installs can override the distro.
    return str(os.environ.get("RBL_WSL_DISTRO", settings.get("wsl_distro", DEFAULT_WSL_DISTRO))).strip() or DEFAULT_WSL_DISTRO


def wsl_available(settings=None) -> bool:
    if not (is_windows() and have("wsl.exe")):
        return False
    distro = wsl_distro(settings)
    try:
        p = subprocess.run(["wsl.exe", "--distribution", distro, "--exec", "true"], text=True, capture_output=True, encoding="utf-8", errors="replace")
        return p.returncode == 0
    except OSError:
        return False


# wslpath() spawns wsl.exe each time it is called (~0.3-0.5 s per call), and the
# driver converts the same handful of paths for every build and run. The mapping
# is a pure function of (distro, host path), so it is cached for the process.
_WSL_PATH_CACHE: dict = {}


def wsl_path(path: Path, settings=None) -> str:
    """Convert a Windows host path to a WSL path without backslash loss.

    wsl.exe command-line handling can strip Windows backslashes before wslpath
    sees the argument. Passing a POSIX-style Windows path (Q:/foo/bar) avoids
    that problem while preserving spaces and Unicode through subprocess argv.
    """
    distro = wsl_distro(settings)
    resolved = Path(path).resolve()
    host_path = resolved.as_posix() if is_windows() else str(resolved)
    cache_key = (distro, host_path)
    if cache_key in _WSL_PATH_CACHE:
        return _WSL_PATH_CACHE[cache_key]
    proc = subprocess.run(
        ["wsl.exe", "-d", distro, "--", "wslpath", "-a", "--", host_path],
        text=True, capture_output=True, encoding="utf-8", errors="replace"
    )
    if proc.returncode != 0:
        detail = (proc.stderr or proc.stdout or "unknown wslpath error").strip()
        raise RuntimeError(f"WSL distro '{distro}' cannot convert path '{host_path}': {detail}")
    result = proc.stdout.strip()
    if not result:
        raise RuntimeError(f"WSL distro '{distro}' returned an empty path for '{host_path}'")
    _WSL_PATH_CACHE[cache_key] = result
    return result


def toolchain_mode(settings=None) -> str:
    """Resolve the active toolchain mode.

    ``RBL_TOOLCHAIN`` overrides the saved ``wsl_mode`` setting so the same test
    suite can be pointed at both backends without touching IDE settings.
    """
    settings = settings or {}
    env = os.environ.get("RBL_TOOLCHAIN", "").strip().lower()
    if env:
        return env
    return str(settings.get("wsl_mode", "auto")).lower()


def use_wsl(settings=None) -> bool:
    """True when user programs are built and run inside WSL."""
    settings = settings or load_settings()
    if not is_windows():
        return False
    mode = toolchain_mode(settings)
    if mode in {"never", "off", "false", "native", "windows"}:
        return False
    if mode in {"wsl", "always", "true"}:
        return wsl_available(settings)
    # auto: a native Windows toolchain avoids the per-invocation WSL cost.
    return find_native_toolchain() is None and wsl_available(settings)


def use_native_windows(settings=None) -> bool:
    return is_windows() and not use_wsl(settings)


def toolchain_env(tc):
    """Environment for native Windows toolchain children.

    ``gcc.exe`` lives in the toolchain ``bin`` directory, but ``cc1.exe`` lives
    under ``lib/gcc/...`` and resolves its own DLLs (gmp, mpfr, mpc, isl, zstd)
    through ``PATH``. Without the toolchain ``bin`` on ``PATH`` the driver exits
    with status 1 and no message at all, which is why the toolchain directory is
    prepended here even though every tool is also invoked by absolute path.
    """
    env = os.environ.copy()
    if tc is None:
        return env
    bindir = str(tc["bin"])
    path = env.get("PATH", "")
    entries = [p.lower() for p in path.split(os.pathsep) if p]
    if bindir.lower() not in entries:
        env["PATH"] = (bindir + os.pathsep + path) if path else bindir
    return env


def run_native(cmd, cwd=ROOT, capture=True, input_text=None, env=None):
    try:
        p = subprocess.run(cmd, cwd=str(cwd), text=True, capture_output=capture,
                           input=input_text, env=env,
                           encoding="utf-8", errors="replace", timeout=PROGRAM_TIMEOUT)
    except subprocess.TimeoutExpired:
        return TIMEOUT_RC, "", f"RBL: command timed out after {PROGRAM_TIMEOUT:.0f}s: {cmd}"
    if capture:
        return p.returncode, p.stdout, p.stderr
    return p.returncode, "", ""


def run_wsl(cmd, settings=None, capture=True, input_text=None):
    settings = settings or load_settings()
    distro = wsl_distro(settings)
    root = wsl_path(ROOT, settings)
    shell = f"cd {shlex.quote(root)} && {cmd}"
    try:
        p = subprocess.run(
            ["wsl.exe", "--distribution", distro, "--exec", "bash", "-lc", shell],
            text=True, capture_output=capture, input=input_text,
            encoding="utf-8", errors="replace",
            timeout=PROGRAM_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        return TIMEOUT_RC, "", f"RBL: command timed out after {PROGRAM_TIMEOUT:.0f}s: {cmd}"
    if capture:
        return p.returncode, p.stdout, p.stderr
    return p.returncode, "", ""


def run_built(exe, stdin_text=None, settings=None):
    """Run an already built program through the active backend.

    The executable is a Linux ELF under WSL/native Linux and a PE image under
    the native Windows backend, so callers must not spawn it directly.
    """
    settings = settings or load_settings()
    exe = Path(exe)
    if use_wsl(settings):
        return run_wsl(shlex.quote(wsl_path(exe, settings)), settings, input_text=stdin_text)
    return run_native([str(exe)], input_text=stdin_text)


def run_tool(cmd, settings=None):
    settings = settings or load_settings()
    if use_wsl(settings):
        return run_wsl(cmd, settings)
    return run_native(shlex.split(cmd))


def ensure_dir(p: Path):
    p.mkdir(parents=True, exist_ok=True)


def _force_bootstrap() -> bool:
    return os.environ.get("RBL_REBUILD_COMPILER", "").lower() in {"1", "true", "yes", "on"}


def _stale(target: Path, *sources: Path) -> bool:
    if not target.exists():
        return True
    try:
        newest = max(s.stat().st_mtime for s in sources)
    except OSError:
        return True
    return newest > target.stat().st_mtime


def ensure_toolchain(settings):
    """Build the bootstrap compiler and the runtime object for the active backend."""
    force_bootstrap = _force_bootstrap()
    if use_wsl(settings):
        ensure_dir(LINUX_BIN_DIR)
        compiler_s = shlex.quote(wsl_path(COMPILER_SRC, settings))
        compiler_b = shlex.quote(wsl_path(COMPILER_BIN, settings))
        runtime_s = shlex.quote(wsl_path(RUNTIME_SRC, settings))
        runtime_o = shlex.quote(wsl_path(RUNTIME_OBJ, settings))
        # Release builds ship the native compiler/runtime already. Do not make
        # normal Windows F5 depend on a host C compiler or source mtimes.
        if force_bootstrap or _stale(COMPILER_BIN, COMPILER_SRC):
            code, out, err = run_wsl(f"cc -std=c11 -O2 -Wall -Wextra -Wpedantic {compiler_s} -o {compiler_b}", settings)
            if code: raise RuntimeError(err or out)
        if force_bootstrap or _stale(RUNTIME_OBJ, RUNTIME_SRC):
            code, out, err = run_wsl(f"as --64 {runtime_s} -o {runtime_o}", settings)
            if code: raise RuntimeError(err or out)
        containers_c = shlex.quote(wsl_path(CONTAINERS_SRC, settings))
        containers_o = shlex.quote(wsl_path(CONTAINERS_OBJ, settings))
        if force_bootstrap or _stale(CONTAINERS_OBJ, CONTAINERS_SRC):
            code, out, err = run_wsl(f"cc -std=c11 -O2 -Wall -Wextra {containers_c} -c -o {containers_o}", settings)
            if code: raise RuntimeError(err or out)
        return

    if use_native_windows(settings):
        tc = find_native_toolchain()
        if tc is None:
            raise RuntimeError(
                "No native Windows GNU toolchain was found.\n"
                "  Install MSYS2 with the mingw-w64 toolchain (provides gcc.exe and as.exe),\n"
                "  or set RBL_MINGW to the toolchain bin directory, or set wsl_mode to 'wsl'.\n"
                "  The compiler and the runtime are built from source on first use; no\n"
                "  prebuilt Windows binaries are required.")
        ensure_dir(WIN_BIN_DIR)
        tenv = toolchain_env(tc)
        if force_bootstrap or _stale(WIN_COMPILER_BIN, COMPILER_SRC):
            cmd = [str(tc["gcc"]), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-D__USE_MINGW_ANSI_STDIO=1", str(COMPILER_SRC), "-o", str(WIN_COMPILER_BIN)]
            code, out, err = run_native(cmd, env=tenv)
            if code: raise RuntimeError(f"failed to build {WIN_COMPILER_BIN.name}:\n{out}{err}")
        if force_bootstrap or _stale(WIN_RUNTIME_OBJ, RUNTIME_WIN_SRC):
            cmd = [str(tc["gcc"]), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-D__USE_MINGW_ANSI_STDIO=1", "-c", str(RUNTIME_WIN_SRC), "-o", str(WIN_RUNTIME_OBJ)]
            code, out, err = run_native(cmd, env=tenv)
            if code: raise RuntimeError(f"failed to build {WIN_RUNTIME_OBJ.name}:\n{out}{err}")
        if force_bootstrap or _stale(WIN_CONTAINERS_OBJ, CONTAINERS_SRC):
            cmd = [str(tc["gcc"]), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-D__USE_MINGW_ANSI_STDIO=1", "-c", str(CONTAINERS_SRC), "-o", str(WIN_CONTAINERS_OBJ)]
            code, out, err = run_native(cmd, env=tenv)
            if code: raise RuntimeError(f"failed to build {WIN_CONTAINERS_OBJ.name}:\n{out}{err}")
        return

    # Native Linux
    ensure_dir(LINUX_BIN_DIR)
    if force_bootstrap or _stale(COMPILER_BIN, COMPILER_SRC):
        if not have("cc"):
            raise RuntimeError("C compiler 'cc' is needed only to bootstrap rblc-asm itself.")
        code, out, err = run_native(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", str(COMPILER_SRC), "-o", str(COMPILER_BIN)])
        if code: raise RuntimeError(err or out)
    if force_bootstrap or _stale(RUNTIME_OBJ, RUNTIME_SRC):
        if not have("as"):
            raise RuntimeError("GNU assembler 'as' not found.")
        code, out, err = run_native(["as", "--64", str(RUNTIME_SRC), "-o", str(RUNTIME_OBJ)])
        if code: raise RuntimeError(err or out)
    if force_bootstrap or _stale(CONTAINERS_OBJ, CONTAINERS_SRC):
        if not have("cc"):
            raise RuntimeError("C compiler 'cc' is needed to build the container runtime.")
        code, out, err = run_native(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-c", str(CONTAINERS_SRC), "-o", str(CONTAINERS_OBJ)])
        if code: raise RuntimeError(err or out)


def compiler_cmd(source: Path, asm: Path, settings):
    if use_wsl(settings):
        return f"{shlex.quote(wsl_path(COMPILER_BIN, settings))} {shlex.quote(wsl_path(source, settings))} -S {shlex.quote(wsl_path(asm, settings))}"
    if use_native_windows(settings):
        return [str(WIN_COMPILER_BIN), str(source), "-S", str(asm), "--target", "win"]
    return [str(COMPILER_BIN), str(source), "-S", str(asm)]


def check(source: Path, settings):
    ensure_toolchain(settings)
    if use_wsl(settings):
        c = f"{shlex.quote(wsl_path(COMPILER_BIN, settings))} {shlex.quote(wsl_path(source, settings))} --check"
        return run_wsl(c, settings)
    if use_native_windows(settings):
        return run_native([str(WIN_COMPILER_BIN), str(source), "--check", "--target", "win"])
    return run_native([str(COMPILER_BIN), str(source), "--check"])


def _build_wsl(source: Path, settings):
    out_dir = BUILD_DIR / source.stem
    ensure_dir(out_dir)
    asm = out_dir / (source.stem + ".s")
    obj = out_dir / (source.stem + ".o")
    exe = out_dir / source.stem
    cs = shlex.quote(wsl_path(COMPILER_BIN, settings)); ss=shlex.quote(wsl_path(source, settings)); asmp=shlex.quote(wsl_path(asm, settings)); objp=shlex.quote(wsl_path(obj, settings)); rt=shlex.quote(wsl_path(RUNTIME_OBJ, settings)); ct=shlex.quote(wsl_path(CONTAINERS_OBJ, settings)); exep=shlex.quote(wsl_path(exe, settings))
    cmds = [f"{cs} {ss} -S {asmp}", f"as --64 {asmp} -o {objp}", f"ld {objp} {rt} {ct} -o {exep} -dynamic-linker /lib64/ld-linux-x86-64.so.2 -lc -lm"]
    allout=[]
    for c in cmds:
        code,o,e=run_wsl(c, settings); allout.append(o+e)
        if code:return code,"".join(allout),"build failed"
    return 0,"".join(allout),str(exe)


def _build_native_linux(source: Path, settings):
    out_dir = BUILD_DIR / source.stem
    ensure_dir(out_dir)
    asm = out_dir / (source.stem + ".s")
    obj = out_dir / (source.stem + ".o")
    exe = out_dir / source.stem
    c1=run_native([str(COMPILER_BIN),str(source),"-S",str(asm)])
    if c1[0]:return c1
    c2=run_native(["as","--64",str(asm),"-o",str(obj)])
    if c2[0]:return c2
    c3=run_native(["ld",str(obj),str(RUNTIME_OBJ),str(CONTAINERS_OBJ),"-o",str(exe),"-dynamic-linker","/lib64/ld-linux-x86-64.so.2","-lc","-lm"])
    return c3[0], c1[1]+c1[2]+c2[1]+c2[2]+c3[1]+c3[2], str(exe)


def _build_native_windows(source: Path, settings):
    tc = find_native_toolchain()
    if tc is None:
        raise RuntimeError("No native Windows GNU toolchain was found (see 'rbl doctor').")
    out_dir = BUILD_DIR / source.stem
    ensure_dir(out_dir)
    asm = out_dir / (source.stem + ".s")
    obj = out_dir / (source.stem + ".o")
    exe = out_dir / (source.stem + ".exe")
    steps = [
        ([str(WIN_COMPILER_BIN), str(source), "-S", str(asm), "--target", "win"], "compile"),
        ([str(tc["as"]), "--64", str(asm), "-o", str(obj)], "assemble"),
        ([str(tc["gcc"]), str(obj), str(WIN_RUNTIME_OBJ), str(WIN_CONTAINERS_OBJ), "-o", str(exe), "-static", "-lm"], "link"),
    ]
    allout = []
    tenv = toolchain_env(tc)
    for cmd, what in steps:
        code, out, err = run_native(cmd, env=tenv)
        allout.append(out + err)
        if code:
            return code, "".join(allout), f"{what} failed"
    return 0, "".join(allout), str(exe)


def build(source: Path, settings):
    ensure_toolchain(settings)
    source = Path(source).resolve()
    if use_wsl(settings):
        return _build_wsl(source, settings)
    if use_native_windows(settings):
        return _build_native_windows(source, settings)
    return _build_native_linux(source, settings)


def run_program(source: Path, settings):
    source = Path(source)
    code, out, info = build(source, settings)
    if code: return code, out, ""
    if use_wsl(settings):
        exe = BUILD_DIR / source.stem / source.stem
        code, stdout, stderr = run_wsl(shlex.quote(wsl_path(exe, settings)), settings)
    else:
        exe = Path(info) if info and info != "build failed" else BUILD_DIR / source.stem / source.stem
        if is_windows() and not use_wsl(settings):
            exe = exe.with_suffix(".exe") if exe.suffix != ".exe" else exe
        code, stdout, stderr = run_native([str(exe)])
    return code, stdout, stderr


def assemble_preview(source: Path, settings):
    ensure_toolchain(settings)
    out = BUILD_DIR / source.stem / (source.stem + ".s")
    ensure_dir(out.parent)
    if use_wsl(settings):
        c=f"{shlex.quote(wsl_path(COMPILER_BIN, settings))} {shlex.quote(wsl_path(source, settings))} -S {shlex.quote(wsl_path(out, settings))}"
        return run_wsl(c, settings)+(str(out),)
    if use_native_windows(settings):
        return (*run_native([str(WIN_COMPILER_BIN),str(source),"-S",str(out),"--target","win"]), str(out))
    return (*run_native([str(COMPILER_BIN),str(source),"-S",str(out)]), str(out))


def active_backend(settings=None) -> str:
    settings = settings or load_settings()
    if not is_windows():
        return "linux (native)"
    if use_wsl(settings):
        return f"linux ELF via WSL ({wsl_distro(settings)})"
    tc = find_native_toolchain()
    if tc is None:
        return "windows (no toolchain found)"
    return f"windows PE via {tc['name']}"


def doctor(settings):
    rows=[]
    rows.append(("Python",sys.version.split()[0]))
    rows.append(("OS",platform.platform()))
    rows.append(("Backend", active_backend(settings)))
    if is_windows():
        tc = find_native_toolchain()
        rows.append(("Win toolchain", f"{tc['name']} ({tc['gcc']})" if tc else "not found"))
        if tc:
            rows.append(("gcc", native_toolchain_version(tc)))
            rows.append(("assembler", str(tc["as"])))
        rows.append(("WSL", "available" if wsl_available(settings) else "not detected"))
        if wsl_available(settings):
            distro = wsl_distro(settings)
            rows.append(("WSL distro", distro))
            try:
                root_linux = wsl_path(ROOT, settings)
                rows.append(("WSL path", root_linux))
                code, out, err = run_wsl("command -v bash; command -v as; command -v ld; test -x /bin/sh", settings)
                tools = "\n".join(x for x in (out, err) if x).strip()
                rows.append(("WSL tools", "OK" if code == 0 else f"ERROR ({code}): {tools}"))
            except Exception as exc:
                rows.append(("WSL path", f"ERROR: {exc}"))
        if not use_wsl(settings):
            rows.append(("rblc-asm.exe", "present" if WIN_COMPILER_BIN.exists() else "will bootstrap"))
            rows.append(("runtime_win.o", "present" if WIN_RUNTIME_OBJ.exists() else "will bootstrap"))
    if not is_windows():
        for t in ("cc","as","ld"):
            rows.append((t, "OK" if have(t) else "missing"))
    rows.append(("rblc-asm", "present" if COMPILER_BIN.exists() else "will bootstrap"))
    rows.append(("runtime.o", "present" if RUNTIME_OBJ.exists() else "will assemble"))
    return rows


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("command",choices=["check","build","run","asm","doctor"])
    ap.add_argument("source",nargs="?")
    args=ap.parse_args()
    settings=load_settings()
    try:
        if args.command=="doctor":
            print("RBL Studio toolchain doctor")
            for k,v in doctor(settings): print(f"{k:16} {v}")
            return 0
        if not args.source: ap.error("source is required")
        source=Path(args.source)
        if args.command=="check":
            code,out,err=check(source,settings); print(out,end=""); print(err,end="",file=sys.stderr); return code
        if args.command=="build":
            code,out,info=build(source,settings); print(out,end=""); print(info,end="",file=sys.stderr); return code
        if args.command=="run":
            code,out,err=run_program(source,settings); print(out,end=""); print(err,end="",file=sys.stderr); return code
        if args.command=="asm":
            code,out,err,path=assemble_preview(source,settings); print(out,end=""); print(path); print(err,end="",file=sys.stderr); return code
    except Exception as e:
        print(f"RBL Studio toolchain error: {e}",file=sys.stderr); return 1
    return 0

if __name__=="__main__": raise SystemExit(main())
