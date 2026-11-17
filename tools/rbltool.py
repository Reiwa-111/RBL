#!/usr/bin/env python3
"""RBL Studio toolchain driver.

Two user-program backends produce the same generated assembly:

  linux   : RBL -> x86-64 ASM (ELF) -> Clang integrated assembler -> ld.lld + libc/libm -> ELF
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
import argparse, codecs, hashlib, json, os, platform, re, shlex, shutil, subprocess, sys, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IDE_CONFIG = Path(os.environ.get("APPDATA", Path.home() / ".config")) / "RBLStudio" / "settings.json" if os.name == "nt" else Path.home()/".config"/"RBLStudio"/"settings.json"
LINUX_BIN_DIR = ROOT / "bin" / "linux-x86_64"
LINUX_TOOLCHAIN_ROOT = Path(os.environ.get("RBL_LINUX_TOOLCHAIN_ROOT", ROOT / "toolchain" / "linux-x86_64")).expanduser()
LINUX_TOOLCHAIN_BIN = LINUX_TOOLCHAIN_ROOT / "bin"
LINUX_TOOLCHAIN_LIB = LINUX_TOOLCHAIN_ROOT / "lib"
LINUX_CLANG = LINUX_TOOLCHAIN_BIN / "clang"
LINUX_LLD = LINUX_TOOLCHAIN_BIN / "ld.lld"
LINUX_SYSTEM_LIB_DIRS = (
    "/lib/x86_64-linux-gnu",
    "/usr/lib/x86_64-linux-gnu",
    "/lib64",
    "/usr/lib64",
    "/lib",
    "/usr/lib",
)
WIN_BIN_DIR = ROOT / "bin" / "win-x86_64"
COMPILER_SOURCES = [ROOT / "compiler" / name for name in (
    "common.c", "lexer.c", "ast.c", "parser.c", "sema.c", "codegen.c", "driver.c"
)]
RUNTIME_ABI_HEADER = ROOT / "runtime" / "rbl_abi.h"
COMPILER_HEADERS = [ROOT / "compiler" / "rblc_internal.h", RUNTIME_ABI_HEADER]
COMPILER_DEPENDENCIES = [*COMPILER_SOURCES, *COMPILER_HEADERS, ROOT / "VERSION"]
RUNTIME_SRC = ROOT / "runtime" / "rbl_runtime.s"
RUNTIME_WIN_SRC = ROOT / "runtime" / "rbl_runtime_win.c"
# Containers are written once in C and linked into both targets.
CONTAINERS_SRC = ROOT / "runtime" / "rbl_containers.c"
RUNTIME_ABI_SRC = ROOT / "runtime" / "rbl_abi.c"
RUNTIME_FORMAT_SRC = ROOT / "runtime" / "rbl_format.c"
if os.environ.get("RBL_BUILD_DIR"):
    BUILD_DIR = Path(os.environ["RBL_BUILD_DIR"]).expanduser()
elif os.environ.get("FLATPAK_ID"):
    BUILD_DIR = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "RBLStudio" / "build"
else:
    BUILD_DIR = ROOT / "build"

# Disposable artifacts are split by purpose so cleaning one area cannot destroy
# the other:
#   CACHE_DIR  - compiled user programs and downloaded upstream sources
#                (safe to delete, expensive to rebuild);
#   OUTPUT_DIR - IDE, worker, bootstrap and packaging outputs produced from the
#                current checkout (safe to delete, cheap to rebuild).
# `bin/` holds the ready-to-use product objects shipped with the project.
if os.environ.get("RBL_CACHE_DIR"):
    CACHE_DIR = Path(os.environ["RBL_CACHE_DIR"]).expanduser()
else:
    CACHE_DIR = BUILD_DIR / "cache"

if os.environ.get("RBL_OUTPUT_DIR"):
    OUTPUT_DIR = Path(os.environ["RBL_OUTPUT_DIR"]).expanduser()
else:
    OUTPUT_DIR = BUILD_DIR / "output"

# Backwards-compatible names: the Linux backend is the historical default.
BIN_DIR = LINUX_BIN_DIR
COMPILER_BIN = LINUX_BIN_DIR / "rblc-asm"
RUNTIME_OBJ = LINUX_BIN_DIR / "rbl_runtime.o"
CONTAINERS_OBJ = LINUX_BIN_DIR / "rbl_containers.o"
RUNTIME_ABI_OBJ = LINUX_BIN_DIR / "rbl_abi.o"
RUNTIME_FORMAT_OBJ = LINUX_BIN_DIR / "rbl_format.o"
WIN_COMPILER_BIN = WIN_BIN_DIR / "rblc-asm.exe"
WIN_RUNTIME_OBJ = WIN_BIN_DIR / "rbl_runtime_win.o"
WIN_CONTAINERS_OBJ = WIN_BIN_DIR / "rbl_containers_win.o"
WIN_RUNTIME_ABI_OBJ = WIN_BIN_DIR / "rbl_abi_win.o"
WIN_RUNTIME_FORMAT_OBJ = WIN_BIN_DIR / "rbl_format_win.o"

# A generated program that regresses into an endless loop must fail the suite
# instead of blocking it forever. 124 matches the shell's timeout exit status.
PROGRAM_TIMEOUT = float(os.environ.get("RBL_TIMEOUT", "180"))
TIMEOUT_RC = 124
STREAM_CAPTURE_LIMIT_BYTES = 1024 * 1024

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


def windows_short_path(path: str | Path) -> str:
    """Return an ASCII 8.3 path for native Windows GNU tools when possible.

    MinGW's argv conversion can replace non-ASCII characters before GNU as/ld
    sees them. Python and Win32 accept Unicode paths, but the bundled GNU tools
    may not. Keep Python-side paths canonical; shorten only paths crossing the
    native-tool process boundary.
    """
    value = os.path.abspath(os.fspath(path))
    if not is_windows() or value.isascii():
        return value
    try:
        import ctypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        get_short_path = kernel32.GetShortPathNameW
        get_short_path.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32]
        get_short_path.restype = ctypes.c_uint32
        required = get_short_path(value, None, 0)
        if required:
            buffer = ctypes.create_unicode_buffer(required + 1)
            copied = get_short_path(value, buffer, len(buffer))
            if copied and buffer.value.isascii():
                return buffer.value
    except (AttributeError, OSError, ValueError):
        pass
    raise RuntimeError(
        "The native Windows GNU toolchain cannot safely receive this non-ASCII path, "
        f"and Windows did not provide an ASCII 8.3 alias: {value!r}. "
        "Enable short file names for this volume, move the project/installation to an ASCII path, "
        "or select the WSL backend with RBL_TOOLCHAIN=wsl."
    )


def windows_native_path_argument(value):
    """Shorten absolute path arguments passed to native Windows tools."""
    if not is_windows() or not isinstance(value, (str, os.PathLike)):
        return value
    text = os.fspath(value)
    if not os.path.isabs(text):
        return value
    if os.path.exists(text):
        return windows_short_path(text)
    parent, leaf = os.path.split(os.path.abspath(text))
    if not leaf.isascii():
        raise RuntimeError(
            f"Native Windows build output filename must be ASCII when its parent path is non-ASCII: {text!r}"
        )
    return os.path.join(windows_short_path(parent), leaf)


def windows_link_search_args(tc):
    """Provide short, explicit MinGW linker/startup/library search prefixes."""
    root = Path(tc["bin"]).parent
    gcc_root = root / "lib" / "gcc" / "x86_64-w64-mingw32"
    version_dirs = sorted((p for p in gcc_root.iterdir() if p.is_dir()), reverse=True) if gcc_root.is_dir() else []
    program_dirs = [Path(tc["bin"]), root / "x86_64-w64-mingw32" / "bin", root / "lib", *version_dirs]
    library_dirs = [*version_dirs, root / "x86_64-w64-mingw32" / "lib", root / "lib"]
    args = []
    for directory in program_dirs:
        if directory.is_dir():
            args.append("-B" + windows_short_path(directory) + os.sep)
    for directory in library_dirs:
        if directory.is_dir():
            args.append("-L" + windows_short_path(directory))
    return args


def have(cmd: str) -> bool:
    return shutil.which(cmd) is not None


def linux_assembler_label() -> str:
    return str(LINUX_CLANG) if LINUX_CLANG.is_file() else "as"


def linux_linker_label() -> str:
    return str(LINUX_LLD) if LINUX_LLD.is_file() else "ld"


def _linux_tool_env() -> dict[str, str]:
    env = os.environ.copy()
    if LINUX_TOOLCHAIN_LIB.is_dir():
        current = env.get("LD_LIBRARY_PATH", "")
        env["LD_LIBRARY_PATH"] = str(LINUX_TOOLCHAIN_LIB) + (os.pathsep + current if current else "")
    return env


def _linux_tool_env_wsl(settings) -> str:
    if not LINUX_TOOLCHAIN_LIB.is_dir():
        return ""
    lib = shlex.quote(wsl_path(LINUX_TOOLCHAIN_LIB, settings))
    return f"LD_LIBRARY_PATH={lib}:${{LD_LIBRARY_PATH:-}} "


def _linux_assemble_native(source: Path, output: Path):
    if LINUX_CLANG.is_file():
        return run_native([
            str(LINUX_CLANG), "-target", "x86_64-pc-linux-gnu", "-c", "-x", "assembler",
            str(source), "-o", str(output),
        ], env=_linux_tool_env())
    if os.environ.get("RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN") == "1":
        raise RuntimeError(f"Required app-local Clang assembler is missing: {LINUX_CLANG}")
    if not have("as"):
        raise RuntimeError("No Linux assembler is available (bundled Clang integrated assembler or system GNU as).")
    return run_native(["as", "--64", str(source), "-o", str(output)], env=_linux_tool_env())


def _linux_link_native(obj: Path, exe: Path):
    linker = str(LINUX_LLD) if LINUX_LLD.is_file() else "ld"
    if linker == "ld" and os.environ.get("RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN") == "1":
        raise RuntimeError(f"Required app-local LLD linker is missing: {LINUX_LLD}")
    if linker == "ld" and not have("ld"):
        raise RuntimeError("No Linux linker is available (bundled ld.lld or system GNU ld).")
    library_search = [arg for directory in LINUX_SYSTEM_LIB_DIRS for arg in ("-L", directory)]
    return run_native([
        linker, *library_search, str(obj), str(RUNTIME_OBJ), str(CONTAINERS_OBJ),
        str(RUNTIME_ABI_OBJ), str(RUNTIME_FORMAT_OBJ), "-o", str(exe),
        "-dynamic-linker", "/lib64/ld-linux-x86-64.so.2", "-l:libc.so.6", "-l:libm.so.6",
    ], env=_linux_tool_env())


def _linux_assemble_wsl(source: Path, output: Path, settings) -> str:
    src = shlex.quote(wsl_path(source, settings))
    out = shlex.quote(wsl_path(output, settings))
    if LINUX_CLANG.is_file():
        tool = shlex.quote(wsl_path(LINUX_CLANG, settings))
        return _linux_tool_env_wsl(settings) + f"{tool} -target x86_64-pc-linux-gnu -c -x assembler {src} -o {out}"
    if os.environ.get("RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN") == "1":
        raise RuntimeError(f"Required app-local Clang assembler is missing: {LINUX_CLANG}")
    return _linux_tool_env_wsl(settings) + f"as --64 {src} -o {out}"


def _linux_link_wsl(obj: Path, exe: Path, settings) -> str:
    if not LINUX_LLD.is_file() and os.environ.get("RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN") == "1":
        raise RuntimeError(f"Required app-local LLD linker is missing: {LINUX_LLD}")
    linker = shlex.quote(wsl_path(LINUX_LLD, settings)) if LINUX_LLD.is_file() else "ld"
    objp = shlex.quote(wsl_path(obj, settings))
    rt = shlex.quote(wsl_path(RUNTIME_OBJ, settings))
    ct = shlex.quote(wsl_path(CONTAINERS_OBJ, settings))
    at = shlex.quote(wsl_path(RUNTIME_ABI_OBJ, settings))
    ft = shlex.quote(wsl_path(RUNTIME_FORMAT_OBJ, settings))
    exep = shlex.quote(wsl_path(exe, settings))
    library_search = " ".join(f"-L{shlex.quote(directory)}" for directory in LINUX_SYSTEM_LIB_DIRS)
    return (
        _linux_tool_env_wsl(settings) + f"{linker} {library_search} {objp} {rt} {ct} {at} {ft} -o {exep} "
        "-dynamic-linker /lib64/ld-linux-x86-64.so.2 -l:libc.so.6 -l:libm.so.6"
    )


# --------------------------------------------------------------------------
# Native Windows (MinGW-w64) toolchain discovery
# --------------------------------------------------------------------------

def _mingw_candidates():
    cands = []
    # An explicit developer override is useful for `python rbl.py bootstrap`,
    # because the portable runtime bundle intentionally omits GCC's C frontend.
    env = os.environ.get("RBL_MINGW")
    if env:
        cands.append(Path(env))
    # A release bundle keeps the Windows compiler toolchain beside the IDE.
    # This is deliberately checked before machine-wide installations so a
    # portable RBL copy remains deterministic on developer machines too.
    bundled = ROOT / "toolchain" / "windows-x86_64" / "msys64" / "ucrt64" / "bin"
    cands.append(bundled)
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
    bundled_bin = ROOT / "toolchain" / "windows-x86_64" / "msys64" / "ucrt64" / "bin"
    candidates = _mingw_candidates()

    # An explicit developer override remains authoritative and is trusted as
    # an external toolchain. The packaged bundle is validated independently.
    explicit = os.environ.get("RBL_MINGW")
    if explicit:
        override = Path(explicit)
        try:
            override_is_bundled = override.resolve() == bundled_bin.resolve()
        except (OSError, RuntimeError):
            override_is_bundled = override == bundled_bin
        if not override_is_bundled and override.is_dir():
            gcc, asm = override / "gcc.exe", override / "as.exe"
            if gcc.is_file() and asm.is_file():
                return {"bin": override, "gcc": gcc, "as": asm,
                        "nm": override / "nm.exe" if (override / "nm.exe").is_file() else None,
                        "name": f"{override.parent.name}/{override.name}", "kind": "external"}

    bundled_root = bundled_bin.parent
    manifest_path = bundled_root / "rbl-toolchain-manifest.json"
    bundle_present = (
        bundled_root.exists() or bundled_root.is_symlink()
        or manifest_path.exists() or manifest_path.is_symlink()
    )
    if bundle_present:
        try:
            if not bundled_bin.is_dir():
                raise RuntimeError(f"bundled toolchain directory is missing or invalid: {bundled_bin}")
            if not manifest_path.is_file():
                raise RuntimeError(f"bundled toolchain manifest is missing or invalid: {manifest_path}")
            # The app-local CPython uses python314._pth, so the script's
            # directory is intentionally absent from sys.path. Load this one
            # trusted sibling by its resolved path without widening imports.
            import importlib.util

            verifier_path = Path(__file__).resolve().parent / "package_windows_toolchain.py"
            spec = importlib.util.spec_from_file_location(
                "_rbl_package_windows_toolchain", verifier_path
            )
            if spec is None or spec.loader is None:
                raise RuntimeError(f"cannot load bundled toolchain verifier: {verifier_path}")
            verifier_module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(verifier_module)
            verify_manifest = verifier_module.verify_manifest

            manifest = verify_manifest(bundled_root)
            gcc_version = manifest.get("gcc_version")
            required = {
                "bin/gcc.exe", "bin/as.exe", "x86_64-w64-mingw32/bin/ld.exe",
                "lib/crt2.o", "lib/default-manifest.o",
                f"lib/gcc/x86_64-w64-mingw32/{gcc_version}/collect2.exe",
            }
            if not required.issubset(manifest["files"]):
                raise RuntimeError("required GCC, assembler, linker or startup inputs are missing")
        except (
            ImportError, OSError, RuntimeError, ValueError, KeyError, TypeError
        ) as exc:
            raise RuntimeError(f"Bundled Windows toolchain failed manifest verification: {exc}") from exc
        gcc, asm = bundled_bin / "gcc.exe", bundled_bin / "as.exe"
        return {"bin": bundled_bin, "gcc": gcc, "as": asm,
                "nm": bundled_bin / "nm.exe" if (bundled_bin / "nm.exe").is_file() else None,
                "name": "RBL bundled MinGW-w64", "kind": "bundled"}

    for d in candidates:
        if d == bundled_bin or (explicit and d == Path(explicit)):
            continue
        try:
            if not d or not d.is_dir():
                continue
        except OSError:
            continue
        gcc, asm = d / "gcc.exe", d / "as.exe"
        if gcc.is_file() and asm.is_file():
            return {"bin": d, "gcc": gcc, "as": asm,
                    "nm": d / "nm.exe" if (d / "nm.exe").is_file() else None,
                    "name": f"{d.parent.name}/{d.name}", "kind": "external"}
    gcc, asm = shutil.which("gcc"), shutil.which("as")
    if gcc and asm:
        return {"bin": Path(gcc).parent, "gcc": Path(gcc), "as": Path(asm),
                "nm": Path(shutil.which("nm")) if shutil.which("nm") else None,
                "name": "PATH", "kind": "external"}
    return None


def _hidden_subprocess_kwargs():
    """Prevent transient console windows when RBL Studio runs child tools on Windows."""
    if os.name != "nt":
        return {}
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = subprocess.SW_HIDE
    return {
        "startupinfo": si,
        "creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0),
    }


_WSL_AVAILABLE_CACHE = {}

def native_toolchain_version(tc):
    try:
        gcc = windows_short_path(tc["gcc"]) if is_windows() else str(tc["gcc"])
        p = subprocess.run([gcc, "--version"], text=True, capture_output=True,
                           encoding="utf-8", errors="replace", timeout=30,
                           env=toolchain_env(tc), **_hidden_subprocess_kwargs())
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
    key = distro
    if key in _WSL_AVAILABLE_CACHE:
        return _WSL_AVAILABLE_CACHE[key]
    try:
        p = subprocess.run(["wsl.exe", "--distribution", distro, "--exec", "true"], text=True, capture_output=True, encoding="utf-8", errors="replace", timeout=8,
                           **_hidden_subprocess_kwargs())
        ok = p.returncode == 0
    except OSError:
        ok = False
    _WSL_AVAILABLE_CACHE[key] = ok
    return ok


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
        text=True, capture_output=True, encoding="utf-8", errors="replace",
        timeout=10,
        **_hidden_subprocess_kwargs()
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
    bindir = windows_short_path(tc["bin"]) if is_windows() else str(tc["bin"])
    path = env.get("PATH", "")
    entries = [p.lower() for p in path.split(os.pathsep) if p]
    if bindir.lower() not in entries:
        env["PATH"] = (bindir + os.pathsep + path) if path else bindir
    if is_windows():
        root = Path(tc["bin"]).parent
        gcc_root = root / "lib" / "gcc"
        gcc_target = gcc_root / "x86_64-w64-mingw32"
        version_dirs = sorted((p for p in gcc_target.iterdir() if p.is_dir()), reverse=True) if gcc_target.is_dir() else []
        target_bin = root / "x86_64-w64-mingw32" / "bin"
        target_lib = root / "x86_64-w64-mingw32" / "lib"
        compiler_dirs = [*version_dirs, gcc_target, gcc_root, Path(tc["bin"]), target_bin]
        library_dirs = [*version_dirs, target_lib, root / "lib"]
        for key, directories in (("COMPILER_PATH", compiler_dirs), ("LIBRARY_PATH", library_dirs)):
            values = []
            for directory in directories:
                if directory.is_dir():
                    value = windows_short_path(directory)
                    if value not in values:
                        values.append(value)
            existing = env.get(key, "")
            env[key] = os.pathsep.join(values + ([existing] if existing else []))
        if gcc_root.is_dir():
            env["GCC_EXEC_PREFIX"] = windows_short_path(gcc_root) + os.sep
    return env


def _streaming_subprocess(cmd, cwd, input_text, env):
    """Forward child stdout/stderr as they arrive while retaining bounded diagnostics."""
    process = subprocess.Popen(
        cmd,
        cwd=str(cwd),
        stdin=subprocess.PIPE if input_text is not None else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        bufsize=-1,
        **_hidden_subprocess_kwargs(),
    )
    captured = {"stdout": [], "stderr": []}

    def pump(stream, target, key):
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        captured_bytes = 0
        while True:
            chunk = stream.read1(8192)
            if not chunk:
                break
            text = decoder.decode(chunk)
            if not text:
                continue
            try:
                target.write(text)
                target.flush()
            except (BrokenPipeError, OSError, ValueError):
                pass
            if captured_bytes < STREAM_CAPTURE_LIMIT_BYTES:
                encoded = text.encode("utf-8")
                retained = encoded[: STREAM_CAPTURE_LIMIT_BYTES - captured_bytes].decode(
                    "utf-8", "ignore"
                )
                if retained:
                    captured[key].append(retained)
                    captured_bytes += len(retained.encode("utf-8"))
        final_text = decoder.decode(b"", final=True)
        if final_text:
            try:
                target.write(final_text)
                target.flush()
            except (BrokenPipeError, OSError, ValueError):
                pass
            if captured_bytes < STREAM_CAPTURE_LIMIT_BYTES:
                retained = final_text.encode("utf-8")[: STREAM_CAPTURE_LIMIT_BYTES - captured_bytes].decode(
                    "utf-8", "ignore"
                )
                if retained:
                    captured[key].append(retained)

    stdout_thread = threading.Thread(
        target=pump, args=(process.stdout, sys.stdout, "stdout"), daemon=True
    )
    stderr_thread = threading.Thread(
        target=pump, args=(process.stderr, sys.stderr, "stderr"), daemon=True
    )
    stdout_thread.start()
    stderr_thread.start()
    input_errors = []
    input_thread = None
    if input_text is not None and process.stdin is not None:
        def write_input():
            try:
                for offset in range(0, len(input_text), 8192):
                    process.stdin.write(input_text[offset : offset + 8192].encode("utf-8"))
            except (BrokenPipeError, OSError) as error:
                input_errors.append(error)
            finally:
                try:
                    process.stdin.close()
                except OSError:
                    pass

        input_thread = threading.Thread(target=write_input, daemon=True)
        input_thread.start()

    timed_out = False
    try:
        return_code = process.wait(timeout=PROGRAM_TIMEOUT)
    except subprocess.TimeoutExpired:
        timed_out = True
        try:
            process.kill()
        except OSError:
            pass
        return_code = process.wait()
    drain_deadline = time.monotonic() + 2.0
    for reader in (input_thread, stdout_thread, stderr_thread):
        if reader is not None:
            reader.join(timeout=max(0.0, drain_deadline - time.monotonic()))
    stdout = "".join(captured["stdout"])
    stderr = "".join(captured["stderr"])
    if (input_thread is not None and input_thread.is_alive()) or stdout_thread.is_alive() or stderr_thread.is_alive():
        warning = (
            "RBL: child process retained an inherited standard stream; "
            "remaining output was detached so the IDE can finish process-tree cleanup.\n"
        )
        try:
            sys.stderr.write(warning)
            sys.stderr.flush()
        except (BrokenPipeError, OSError, ValueError):
            pass
        stderr += warning
    if input_errors:
        stderr += f"RBL: failed to forward subprocess stdin: {input_errors[0]}\n"
    if timed_out:
        stderr += f"RBL: command timed out after {PROGRAM_TIMEOUT:.0f}s: {cmd}"
        return TIMEOUT_RC, stdout, stderr
    return return_code, stdout, stderr


def run_native(cmd, cwd=ROOT, capture=True, input_text=None, env=None):
    if is_windows():
        cmd = [windows_native_path_argument(arg) for arg in cmd]
        cwd = windows_short_path(cwd) if cwd is not None else None
    try:
        if capture and os.environ.get("RBL_STREAM_OUTPUT") == "1":
            return _streaming_subprocess(cmd, cwd, input_text, env)
        p = subprocess.run(cmd, cwd=str(cwd), text=True, capture_output=capture,
                           input=input_text, env=env,
                           encoding="utf-8", errors="replace", timeout=PROGRAM_TIMEOUT,
                           **_hidden_subprocess_kwargs())
    except subprocess.TimeoutExpired:
        return TIMEOUT_RC, "", f"RBL: command timed out after {PROGRAM_TIMEOUT:.0f}s: {cmd}"
    if capture:
        return p.returncode, p.stdout, p.stderr
    return p.returncode, "", ""


def run_wsl(cmd, settings=None, capture=True, input_text=None, cwd=None):
    settings = settings or load_settings()
    distro = wsl_distro(settings)
    working_dir = Path(cwd) if cwd is not None else ROOT
    root = wsl_path(working_dir, settings)
    shell = f"cd {shlex.quote(root)} && {cmd}"
    try:
        if capture and os.environ.get("RBL_STREAM_OUTPUT") == "1":
            return _streaming_subprocess(
                ["wsl.exe", "--distribution", distro, "--exec", "bash", "-lc", shell],
                working_dir,
                input_text,
                None,
            )
        p = subprocess.run(
            ["wsl.exe", "--distribution", distro, "--exec", "bash", "-lc", shell],
            text=True, capture_output=capture, input=input_text,
            encoding="utf-8", errors="replace",
            timeout=PROGRAM_TIMEOUT,
            **_hidden_subprocess_kwargs(),
        )
    except subprocess.TimeoutExpired:
        return TIMEOUT_RC, "", f"RBL: command timed out after {PROGRAM_TIMEOUT:.0f}s: {cmd}"
    if capture:
        return p.returncode, p.stdout, p.stderr
    return p.returncode, "", ""


def run_built(exe, stdin_text=None, settings=None, cwd=None):
    """Run an already built program through the active backend.

    The executable is a Linux ELF under WSL/native Linux and a PE image under
    the native Windows backend, so callers must not spawn it directly.
    """
    settings = settings or load_settings()
    exe = Path(exe)
    working_dir = Path(cwd) if cwd is not None else ROOT
    if use_wsl(settings):
        return run_wsl(shlex.quote(wsl_path(exe, settings)), settings,
                       input_text=stdin_text, cwd=working_dir)
    return run_native([str(exe)], cwd=working_dir, input_text=stdin_text)


def run_tool(cmd, settings=None):
    settings = settings or load_settings()
    if use_wsl(settings):
        return run_wsl(cmd, settings)
    return run_native(shlex.split(cmd))


def ensure_dir(p: Path):
    p.mkdir(parents=True, exist_ok=True)


def build_output_dir(source: Path, settings=None) -> Path:
    """Return a stable, collision-resistant output directory for one source path."""
    resolved = Path(source).resolve()
    identity = hashlib.sha256(os.path.normcase(str(resolved)).encode("utf-8")).hexdigest()[:12]
    backend = "windows" if use_native_windows(settings or load_settings()) else "linux"
    stem = native_build_stem(resolved, settings) if backend == "windows" else resolved.stem
    return CACHE_DIR / "sources" / backend / f"{stem}-{identity}"


def native_build_stem(source: Path, settings=None) -> str:
    stem = Path(source).stem
    if is_windows() and use_native_windows(settings or load_settings()) and not stem.isascii():
        safe = re.sub(r"[^A-Za-z0-9_-]+", "_", stem).strip("_")
        return (safe or "source")[:48]
    return stem


def _force_bootstrap() -> bool:
    return os.environ.get("RBL_REBUILD_COMPILER", "").lower() in {"1", "true", "yes", "on"}


def compiler_version() -> str:
    """Read the canonical repository/compiler version."""
    try:
        return (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    except (OSError, subprocess.TimeoutExpired):
        return "0.0.0"


def runtime_abi_version() -> str:
    """Read the separately versioned Runtime ABI from its canonical header."""
    try:
        source = RUNTIME_ABI_HEADER.read_text(encoding="utf-8")
        values = {}
        for part in ("MAJOR", "MINOR", "PATCH"):
            match = re.search(rf"^#define RBL_RUNTIME_ABI_{part} (\d+)u$", source, re.MULTILINE)
            if not match:
                raise ValueError(f"missing ABI {part}")
            values[part] = int(match.group(1))
        return f"{values['MAJOR']}.{values['MINOR']}.{values['PATCH']}"
    except (OSError, ValueError):
        return "unknown"


def runtime_abi_check_symbol() -> str:
    try:
        source = RUNTIME_ABI_HEADER.read_text(encoding="utf-8")
        match = re.search(r"^#define RBL_RUNTIME_ABI_CHECK_SYMBOL (\w+)$", source, re.MULTILINE)
        return match.group(1) if match else "rbl_runtime_abi_check_unknown"
    except OSError:
        return "rbl_runtime_abi_check_unknown"


def compiler_sources_for_host() -> list[str]:
    return [str(p) for p in COMPILER_SOURCES]


def compiler_version_define() -> str:
    return f'-DRBL_VERSION="{compiler_version()}"'


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
        compiler_sources = " ".join(shlex.quote(wsl_path(p, settings)) for p in COMPILER_SOURCES)
        compiler_b = shlex.quote(wsl_path(COMPILER_BIN, settings))
        runtime_s = shlex.quote(wsl_path(RUNTIME_SRC, settings))
        runtime_o = shlex.quote(wsl_path(RUNTIME_OBJ, settings))
        # Release builds ship the native compiler/runtime already. Do not make
        # normal Windows F5 depend on a host C compiler or source mtimes.
        if force_bootstrap or _stale(COMPILER_BIN, *COMPILER_DEPENDENCIES):
            version_define = shlex.quote(compiler_version_define())
            code, out, err = run_wsl(f"cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Werror=missing-prototypes -Werror=redundant-decls -Werror=implicit-function-declaration {version_define} {compiler_sources} -o {compiler_b}", settings)
            if code: raise RuntimeError(err or out)
        if force_bootstrap or _stale(RUNTIME_OBJ, RUNTIME_SRC):
            code, out, err = run_wsl(_linux_assemble_wsl(RUNTIME_SRC, RUNTIME_OBJ, settings), settings)
            if code: raise RuntimeError(err or out)
        containers_c = shlex.quote(wsl_path(CONTAINERS_SRC, settings))
        containers_o = shlex.quote(wsl_path(CONTAINERS_OBJ, settings))
        if force_bootstrap or _stale(CONTAINERS_OBJ, CONTAINERS_SRC, RUNTIME_ABI_HEADER):
            code, out, err = run_wsl(
                f"cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Wmissing-prototypes "
                f"-Wstrict-prototypes -Wredundant-decls -Werror=missing-prototypes "
                f"-Werror=redundant-decls -Werror=implicit-function-declaration "
                f"{containers_c} -c -o {containers_o}", settings)
            if code: raise RuntimeError(err or out)
        abi_c = shlex.quote(wsl_path(RUNTIME_ABI_SRC, settings))
        abi_o = shlex.quote(wsl_path(RUNTIME_ABI_OBJ, settings))
        if force_bootstrap or _stale(RUNTIME_ABI_OBJ, RUNTIME_ABI_SRC, RUNTIME_ABI_HEADER):
            code, out, err = run_wsl(
                f"cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Wmissing-prototypes "
                f"-Wstrict-prototypes -Wredundant-decls -Werror=missing-prototypes "
                f"-Werror=redundant-decls -Werror=implicit-function-declaration "
                f"{abi_c} -c -o {abi_o}", settings)
            if code: raise RuntimeError(err or out)
        format_c = shlex.quote(wsl_path(RUNTIME_FORMAT_SRC, settings))
        format_o = shlex.quote(wsl_path(RUNTIME_FORMAT_OBJ, settings))
        if force_bootstrap or _stale(RUNTIME_FORMAT_OBJ, RUNTIME_FORMAT_SRC, RUNTIME_ABI_HEADER):
            code, out, err = run_wsl(
                f"cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Wmissing-prototypes "
                f"-Wstrict-prototypes -Wredundant-decls -Werror=missing-prototypes "
                f"-Werror=redundant-decls -Werror=implicit-function-declaration "
                f"{format_c} -c -o {format_o}", settings)
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
        # The distributable Windows bundle intentionally contains only the
        # linker/assembler runtime closure, not GCC's C frontend (cc1) or
        # development headers. Never infer that a user's installed compiler
        # must be rebuilt from checkout mtimes: source archives commonly reset
        # those timestamps. Rebuilding is an explicit developer operation
        # (`python rbl.py bootstrap`) or a recovery from a missing artifact.
        if force_bootstrap or not WIN_COMPILER_BIN.is_file():
            cmd = [str(tc["gcc"]), "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                   "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wredundant-decls",
                   "-Werror=missing-prototypes", "-Werror=redundant-decls", "-Werror=implicit-function-declaration",
                   "-D__USE_MINGW_ANSI_STDIO=1", compiler_version_define(), *compiler_sources_for_host(), "-o", str(WIN_COMPILER_BIN)]
            code, out, err = run_native(cmd, env=tenv)
            if code: raise RuntimeError(f"failed to build {WIN_COMPILER_BIN.name}:\n{out}{err}")
        if force_bootstrap or not WIN_RUNTIME_OBJ.is_file():
            cmd = [str(tc["gcc"]), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-D__USE_MINGW_ANSI_STDIO=1", "-c", str(RUNTIME_WIN_SRC), "-o", str(WIN_RUNTIME_OBJ)]
            code, out, err = run_native(cmd, env=tenv)
            if code: raise RuntimeError(f"failed to build {WIN_RUNTIME_OBJ.name}:\n{out}{err}")
        if force_bootstrap or not WIN_CONTAINERS_OBJ.is_file():
            cmd = [str(tc["gcc"]), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-Wpedantic", "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wredundant-decls",
                   "-Werror=missing-prototypes", "-Werror=redundant-decls", "-Werror=implicit-function-declaration",
                   "-D__USE_MINGW_ANSI_STDIO=1", "-c", str(CONTAINERS_SRC), "-o", str(WIN_CONTAINERS_OBJ)]
            code, out, err = run_native(cmd, env=tenv)
            if code: raise RuntimeError(f"failed to build {WIN_CONTAINERS_OBJ.name}:\n{out}{err}")
        if force_bootstrap or not WIN_RUNTIME_ABI_OBJ.is_file():
            cmd = [str(tc["gcc"]), "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                   "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wredundant-decls",
                   "-Werror=missing-prototypes", "-Werror=redundant-decls", "-Werror=implicit-function-declaration",
                   "-D__USE_MINGW_ANSI_STDIO=1", "-c", str(RUNTIME_ABI_SRC), "-o", str(WIN_RUNTIME_ABI_OBJ)]
            code, out, err = run_native(cmd, env=tenv)
            if code: raise RuntimeError(f"failed to build {WIN_RUNTIME_ABI_OBJ.name}:\n{out}{err}")
        if force_bootstrap or not WIN_RUNTIME_FORMAT_OBJ.is_file():
            cmd = [str(tc["gcc"]), "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                   "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wredundant-decls",
                   "-Werror=missing-prototypes", "-Werror=redundant-decls", "-Werror=implicit-function-declaration",
                   "-D__USE_MINGW_ANSI_STDIO=1", "-c", str(RUNTIME_FORMAT_SRC), "-o", str(WIN_RUNTIME_FORMAT_OBJ)]
            code, out, err = run_native(cmd, env=tenv)
            if code: raise RuntimeError(f"failed to build {WIN_RUNTIME_FORMAT_OBJ.name}:\n{out}{err}")
        return

    # Native Linux
    ensure_dir(LINUX_BIN_DIR)
    # Release payloads ship all compiler/runtime artifacts prebuilt. Do not use
    # mtimes to trigger a developer bootstrap after extraction/install: archive
    # tools and installers may legitimately rewrite timestamps. A normal user
    # machine needs only the runtime assembler/linker for program builds, not cc.
    # Rebuilding is explicit (`RBL_REBUILD_COMPILER=1`) or a recovery from a
    # missing artifact.
    if force_bootstrap or not COMPILER_BIN.is_file():
        if not have("cc"):
            raise RuntimeError("C compiler 'cc' is needed only to bootstrap rblc-asm itself.")
        code, out, err = run_native(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                                     "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wredundant-decls",
                                     "-Werror=missing-prototypes", "-Werror=redundant-decls", "-Werror=implicit-function-declaration",
                                     compiler_version_define(), *compiler_sources_for_host(), "-o", str(COMPILER_BIN)])
        if code: raise RuntimeError(err or out)
    if force_bootstrap or not RUNTIME_OBJ.is_file():
        code, out, err = _linux_assemble_native(RUNTIME_SRC, RUNTIME_OBJ)
        if code: raise RuntimeError(err or out)
    if force_bootstrap or not CONTAINERS_OBJ.is_file():
        if not have("cc"):
            raise RuntimeError("C compiler 'cc' is needed to build the container runtime.")
        code, out, err = run_native(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                                     "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wredundant-decls",
                                     "-Werror=missing-prototypes", "-Werror=redundant-decls", "-Werror=implicit-function-declaration",
                                     "-c", str(CONTAINERS_SRC), "-o", str(CONTAINERS_OBJ)])
        if code: raise RuntimeError(err or out)
    if force_bootstrap or not RUNTIME_ABI_OBJ.is_file():
        code, out, err = run_native(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                                     "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wredundant-decls",
                                     "-Werror=missing-prototypes", "-Werror=redundant-decls", "-Werror=implicit-function-declaration",
                                     "-c", str(RUNTIME_ABI_SRC), "-o", str(RUNTIME_ABI_OBJ)])
        if code: raise RuntimeError(err or out)
    if force_bootstrap or not RUNTIME_FORMAT_OBJ.is_file():
        code, out, err = run_native(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                                     "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wredundant-decls",
                                     "-Werror=missing-prototypes", "-Werror=redundant-decls", "-Werror=implicit-function-declaration",
                                     "-c", str(RUNTIME_FORMAT_SRC), "-o", str(RUNTIME_FORMAT_OBJ)])
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


def _build_cache_valid(
    exe: Path,
    source: Path,
    dependencies: list[Path],
    required_artifacts: tuple[Path, ...] = (),
) -> bool:
    if os.environ.get("RBL_NO_BUILD_CACHE") == "1":
        return False
    try:
        artifacts = (exe, *required_artifacts)
        if any(not artifact.is_file() for artifact in artifacts):
            return False
        newest = max(
            path.stat().st_mtime_ns
            for path in (source, *dependencies)
            if path.exists()
        )
        return all(artifact.stat().st_mtime_ns >= newest for artifact in artifacts)
    except OSError:
        return False


def _link_failure(code: int, output: str, stage: str):
    """Add an actionable hint when a prebuilt Runtime lacks the ABI entry point."""
    symbol = runtime_abi_check_symbol()
    major = runtime_abi_version().split(".", 1)[0]
    if code and symbol in output and (
        "undefined reference" in output.lower()
        or "undefined symbol" in output.lower()
    ):
        output += (
            f"\nRBL Runtime ABI mismatch: this executable requires Runtime ABI major {major}, "
            f"but the linked Runtime does not provide {symbol}. "
            "Rebuild the compiler and Runtime with `python rbl.py bootstrap`.\n"
        )
    return code, output, stage


def _build_wsl(source: Path, settings):
    out_dir = build_output_dir(source, settings)
    ensure_dir(out_dir)
    asm = out_dir / (source.stem + ".s")
    obj = out_dir / (source.stem + ".o")
    exe = out_dir / source.stem
    deps = [COMPILER_BIN, RUNTIME_OBJ, CONTAINERS_OBJ, RUNTIME_ABI_OBJ, RUNTIME_FORMAT_OBJ]
    if _build_cache_valid(exe, source, deps, (asm, obj)):
        return 0, "build cache: up to date\n", str(exe)
    cs = shlex.quote(wsl_path(COMPILER_BIN, settings))
    ss = shlex.quote(wsl_path(source, settings))
    asmp = shlex.quote(wsl_path(asm, settings))
    objp = shlex.quote(wsl_path(obj, settings))
    rt = shlex.quote(wsl_path(RUNTIME_OBJ, settings))
    ct = shlex.quote(wsl_path(CONTAINERS_OBJ, settings))
    at = shlex.quote(wsl_path(RUNTIME_ABI_OBJ, settings))
    ft = shlex.quote(wsl_path(RUNTIME_FORMAT_OBJ, settings))
    exep = shlex.quote(wsl_path(exe, settings))
    command = f"{cs} {ss} -S {asmp} && " + _linux_assemble_wsl(asm, obj, settings) + " && " + _linux_link_wsl(obj, exe, settings)
    code, out, err = run_wsl(command, settings)
    if code:
        return _link_failure(code, out + err, "build failed")
    return 0, out + err, str(exe)

def _build_native_linux(source: Path, settings):
    out_dir = build_output_dir(source, settings)
    ensure_dir(out_dir)
    asm = out_dir / (source.stem + ".s")
    obj = out_dir / (source.stem + ".o")
    exe = out_dir / source.stem
    if _build_cache_valid(
        exe, source,
        [COMPILER_BIN, RUNTIME_OBJ, CONTAINERS_OBJ, RUNTIME_ABI_OBJ, RUNTIME_FORMAT_OBJ],
        (asm, obj),
    ):
        return 0, "build cache: up to date\n", str(exe)
    c1=run_native([str(COMPILER_BIN),str(source),"-S",str(asm)])
    if c1[0]:return c1
    c2=_linux_assemble_native(asm, obj)
    if c2[0]:return c2
    c3=_linux_link_native(obj, exe)
    if c3[0]:
        return _link_failure(c3[0], c1[1]+c1[2]+c2[1]+c2[2]+c3[1]+c3[2], "link failed")
    return c3[0], c1[1]+c1[2]+c2[1]+c2[2]+c3[1]+c3[2], str(exe)


def _build_native_windows(source: Path, settings):
    tc = find_native_toolchain()
    if tc is None:
        raise RuntimeError("No native Windows GNU toolchain was found (see 'rbl doctor').")
    out_dir = build_output_dir(source, settings)
    ensure_dir(out_dir)
    stem = native_build_stem(source, settings)
    asm = out_dir / (stem + ".s")
    obj = out_dir / (stem + ".o")
    exe = out_dir / (stem + ".exe")
    if _build_cache_valid(
        exe, source,
        [WIN_COMPILER_BIN, WIN_RUNTIME_OBJ, WIN_CONTAINERS_OBJ, WIN_RUNTIME_ABI_OBJ, WIN_RUNTIME_FORMAT_OBJ],
        (asm, obj),
    ):
        return 0, "build cache: up to date\n", str(exe)
    steps = [
        ([str(WIN_COMPILER_BIN), str(source), "-S", str(asm), "--target", "win"], "compile"),
        ([str(tc["as"]), "--64", str(asm), "-o", str(obj)], "assemble"),
        # RBL emits plain native objects and never produces GCC LTO objects.
        # Disabling the linker plugin avoids shipping GCC's LTO plugin/wrapper
        # (and its compression dependencies) in the minimal user toolchain.
        ([str(tc["gcc"]), *windows_link_search_args(tc), str(obj), str(WIN_RUNTIME_OBJ), str(WIN_CONTAINERS_OBJ), str(WIN_RUNTIME_ABI_OBJ), str(WIN_RUNTIME_FORMAT_OBJ), "-o", str(exe), "-static", "-lm", "-fno-use-linker-plugin"], "link"),
    ]
    allout = []
    tenv = toolchain_env(tc)
    for cmd, what in steps:
        code, out, err = run_native(cmd, env=tenv)
        allout.append(out + err)
        if code:
            if what == "link":
                return _link_failure(code, "".join(allout), "link failed")
            return code, "".join(allout), f"{what} failed"
    crt_notice = Path(tc["gcc"]).parent.parent / "share" / "licenses" / "crt" / "COPYING.MinGW-w64-runtime.txt"
    if crt_notice.is_file():
        notice_out = exe.with_name(exe.name + ".COPYING.MinGW-w64-runtime.txt")
        try:
            shutil.copy2(crt_notice, notice_out)
        except OSError as exc:
            return 1, "".join(allout) + f"\nUnable to copy MinGW-w64 CRT redistribution notice: {exc}\n", "notice copy failed"
    elif tc.get("kind") == "bundled":
        return 1, "".join(allout) + "\nBundled MinGW-w64 CRT license notice is missing; refusing to leave a distributable executable without it.\n", "notice missing"
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
        exe = build_output_dir(source, settings) / source.stem
        code, stdout, stderr = run_wsl(shlex.quote(wsl_path(exe, settings)), settings)
    else:
        exe = Path(info) if info and info != "build failed" else build_output_dir(source, settings) / native_build_stem(source, settings)
        if is_windows() and not use_wsl(settings):
            exe = exe.with_suffix(".exe") if exe.suffix != ".exe" else exe
        code, stdout, stderr = run_native([str(exe)])
    return code, stdout, stderr


def assemble_preview(source: Path, settings):
    ensure_toolchain(settings)
    out = build_output_dir(source, settings) / (native_build_stem(source, settings) + ".s")
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
            rows.append(("Win toolchain source", tc.get("kind", "external")))
            rows.append(("gcc", native_toolchain_version(tc)))
            rows.append(("assembler", str(tc["as"])))
            if tc.get("kind") == "bundled":
                rows.append(("Bootstrap C frontend", "not bundled; full MinGW-w64 required for developer rebuilds"))
        rows.append(("WSL", "available" if wsl_available(settings) else "not detected"))
        if wsl_available(settings):
            distro = wsl_distro(settings)
            rows.append(("WSL distro", distro))
            try:
                root_linux = wsl_path(ROOT, settings)
                rows.append(("WSL path", root_linux))
                code, out, err = run_wsl("command -v bash; command -v clang || command -v as; command -v ld.lld || command -v ld; test -x /bin/sh", settings)
                tools = "\n".join(x for x in (out, err) if x).strip()
                rows.append(("WSL tools", "OK" if code == 0 else f"ERROR ({code}): {tools}"))
            except Exception as exc:
                rows.append(("WSL path", f"ERROR: {exc}"))
        if not use_wsl(settings):
            rows.append(("rblc-asm.exe", "present" if WIN_COMPILER_BIN.exists() else "will bootstrap"))
            rows.append(("runtime_win.o", "present" if WIN_RUNTIME_OBJ.exists() else "will bootstrap"))
    if not is_windows():
        rows.append(("C compiler (bootstrap only)", "available" if have("cc") else "not installed; not needed for normal use"))
        require_bundled = os.environ.get("RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN") == "1"
        rows.append(("assembler", f"bundled: {LINUX_CLANG} (integrated)" if LINUX_CLANG.is_file() else ("required bundled LLVM toolchain missing" if require_bundled else ("system GNU as" if have("as") else "missing"))))
        rows.append(("linker", f"bundled: {LINUX_LLD}" if LINUX_LLD.is_file() else ("required bundled LLVM toolchain missing" if require_bundled else ("system GNU ld" if have("ld") else "missing"))))
    rows.append(("rblc-asm", "present" if COMPILER_BIN.exists() else "will bootstrap"))
    rows.append(("runtime.o", "present" if RUNTIME_OBJ.exists() else "will assemble"))
    abi_obj = WIN_RUNTIME_ABI_OBJ if use_native_windows(settings) else RUNTIME_ABI_OBJ
    abi_state = "present" if abi_obj.exists() else "will build"
    rows.append(("runtime ABI", f"{runtime_abi_version()} ({abi_state})"))
    return rows


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("command",choices=["check","build","run","asm","doctor"])
    ap.add_argument("source",nargs="?")
    args=ap.parse_args()
    settings=load_settings()
    stream_output = os.environ.get("RBL_STREAM_OUTPUT") == "1"
    try:
        if args.command=="doctor":
            print("RBL Studio toolchain doctor")
            for k,v in doctor(settings): print(f"{k:16} {v}")
            return 0
        if not args.source: ap.error("source is required")
        source=Path(args.source)
        if args.command=="check":
            code,out,err=check(source,settings)
            if not stream_output:
                print(out,end=""); print(err,end="",file=sys.stderr)
            return code
        if args.command=="build":
            code,out,info=build(source,settings)
            if not stream_output or out.startswith("build cache:"):
                print(out,end="")
            print(info,end="",file=sys.stderr)
            return code
        if args.command=="run":
            code,out,err=run_program(source,settings)
            if not stream_output:
                print(out,end=""); print(err,end="",file=sys.stderr)
            return code
        if args.command=="asm":
            code,out,err,path=assemble_preview(source,settings)
            if not stream_output:
                print(out,end=""); print(err,end="",file=sys.stderr)
            print(path)
            return code
    except Exception as e:
        print(f"RBL Studio toolchain error: {e}",file=sys.stderr); return 1
    return 0

if __name__=="__main__": raise SystemExit(main())
