#!/usr/bin/env python3
from __future__ import annotations

import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool


def compiler_command() -> tuple[list[str], dict[str, str] | None]:
    if os.name == "nt":
        toolchain = rbltool.find_native_toolchain()
        if toolchain:
            return [str(toolchain["gcc"])], rbltool.toolchain_env(toolchain)
    for name in ("cc", "gcc"):
        found = shutil.which(name)
        if found:
            return [found], None
    raise RuntimeError("A C compiler is required to run the Runtime ABI tests")


def verify_missing_abi_link(code: int, output: str) -> int:
    if code == 0:
        print("FAIL: generated executable linked without its Runtime ABI object", file=sys.stderr)
        return 1
    _, message, _ = rbltool._link_failure(code, output, "link failed")
    if "Runtime ABI mismatch" not in message:
        print("FAIL: missing ABI symbol did not produce an actionable linker diagnostic", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1
    print("PASS: linker rejects a Runtime without the versioned ABI symbol")
    return 0


def test_generated_program_startup() -> int:
    settings = rbltool.load_settings()
    rbltool.ensure_toolchain(settings)
    build_root = ROOT / "build"
    build_root.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="runtime-abi-startup-", dir=build_root) as temp:
        temp_dir = Path(temp)
        source = temp_dir / "abi_startup.rbl"
        assembly = temp_dir / "abi_startup.s"
        obj = temp_dir / "abi_startup.o"
        executable = temp_dir / ("abi_startup.exe" if rbltool.use_native_windows(settings) else "abi_startup")
        source.write_text(
            'func main() (\n    print("RBL main must not execute on ABI mismatch")\n)\n',
            encoding="utf-8",
        )

        compile_cmd = rbltool.compiler_cmd(source, assembly, settings)
        if rbltool.use_wsl(settings):
            code, out, err = rbltool.run_wsl(compile_cmd, settings)
        else:
            code, out, err = rbltool.run_native(compile_cmd)
        if code:
            print(out, end="")
            print(err, end="", file=sys.stderr)
            return code

        generated = assembly.read_text(encoding="utf-8")
        marker = ".Lrbl_compiler_abi_descriptor:\n"
        if marker not in generated:
            print("FAIL: compiler did not embed a Runtime ABI descriptor", file=sys.stderr)
            return 1
        prefix, descriptor = generated.split(marker, 1)
        abi_major = int(re.search(
            r"^#define RBL_RUNTIME_ABI_MAJOR (\d+)u$",
            (ROOT / "runtime" / "rbl_abi.h").read_text(encoding="utf-8"),
            re.MULTILINE,
        ).group(1))
        descriptor, replacements = re.subn(
            rf"(?m)^    \.short {abi_major}$", f"    .short {abi_major + 1}", descriptor, count=1
        )
        if replacements != 1:
            print("FAIL: could not mutate the embedded ABI major for the negative test", file=sys.stderr)
            return 1
        assembly.write_text(prefix + marker + descriptor, encoding="utf-8")
        missing_executable = executable.with_name(executable.stem + "_without_abi" + executable.suffix)

        if rbltool.use_wsl(settings):
            asm_path = shlex.quote(rbltool.wsl_path(assembly, settings))
            obj_path = shlex.quote(rbltool.wsl_path(obj, settings))
            exe_path = shlex.quote(rbltool.wsl_path(executable, settings))
            missing_exe_path = shlex.quote(rbltool.wsl_path(missing_executable, settings))
            runtime = shlex.quote(rbltool.wsl_path(rbltool.RUNTIME_OBJ, settings))
            containers = shlex.quote(rbltool.wsl_path(rbltool.CONTAINERS_OBJ, settings))
            abi = shlex.quote(rbltool.wsl_path(rbltool.RUNTIME_ABI_OBJ, settings))
            formatting = shlex.quote(rbltool.wsl_path(rbltool.RUNTIME_FORMAT_OBJ, settings))
            missing_command = (
                f"as --64 {asm_path} -o {obj_path} && "
                f"ld {obj_path} {runtime} {containers} {formatting} -o {missing_exe_path} "
                "-dynamic-linker /lib64/ld-linux-x86-64.so.2 -lc -lm"
            )
            missing_code, missing_out, missing_err = rbltool.run_wsl(missing_command, settings)
            missing_check = verify_missing_abi_link(missing_code, missing_out + missing_err)
            if missing_check:
                return missing_check
            command = (
                f"ld {obj_path} {runtime} {containers} {abi} {formatting} -o {exe_path} "
                "-dynamic-linker /lib64/ld-linux-x86-64.so.2 -lc -lm"
            )
            code, out, err = rbltool.run_wsl(command, settings)
        elif rbltool.use_native_windows(settings):
            toolchain = rbltool.find_native_toolchain()
            env = rbltool.toolchain_env(toolchain)
            code, out, err = rbltool.run_native(
                [str(toolchain["as"]), "--64", str(assembly), "-o", str(obj)], env=env
            )
            if not code:
                missing_code, missing_out, missing_err = rbltool.run_native(
                    [str(toolchain["gcc"]), str(obj), str(rbltool.WIN_RUNTIME_OBJ),
                     str(rbltool.WIN_CONTAINERS_OBJ), str(rbltool.WIN_RUNTIME_FORMAT_OBJ),
                     "-o", str(missing_executable), "-static", "-lm", "-fno-use-linker-plugin"], env=env
                )
                missing_check = verify_missing_abi_link(missing_code, missing_out + missing_err)
                if missing_check:
                    return missing_check
                code, out, err = rbltool.run_native(
                    [str(toolchain["gcc"]), str(obj), str(rbltool.WIN_RUNTIME_OBJ),
                     str(rbltool.WIN_CONTAINERS_OBJ), str(rbltool.WIN_RUNTIME_ABI_OBJ),
                     str(rbltool.WIN_RUNTIME_FORMAT_OBJ),
                     "-o", str(executable), "-static", "-lm", "-fno-use-linker-plugin"], env=env
                )
        else:
            code, out, err = rbltool.run_native(["as", "--64", str(assembly), "-o", str(obj)])
            if not code:
                missing_code, missing_out, missing_err = rbltool.run_native(
                    ["ld", str(obj), str(rbltool.RUNTIME_OBJ), str(rbltool.CONTAINERS_OBJ),
                     str(rbltool.RUNTIME_FORMAT_OBJ),
                     "-o", str(missing_executable), "-dynamic-linker", "/lib64/ld-linux-x86-64.so.2", "-lc", "-lm"]
                )
                missing_check = verify_missing_abi_link(missing_code, missing_out + missing_err)
                if missing_check:
                    return missing_check
                code, out, err = rbltool.run_native(
                    ["ld", str(obj), str(rbltool.RUNTIME_OBJ), str(rbltool.CONTAINERS_OBJ),
                     str(rbltool.RUNTIME_ABI_OBJ), str(rbltool.RUNTIME_FORMAT_OBJ), "-o", str(executable),
                     "-dynamic-linker", "/lib64/ld-linux-x86-64.so.2", "-lc", "-lm"]
                )
        if code:
            print(out, end="")
            print(err, end="", file=sys.stderr)
            return code

        rc, stdout, stderr = rbltool.run_built(executable, settings=settings)
        if rc != 78 or "RBL Runtime ABI mismatch:" not in stderr or stdout:
            print("FAIL: generated executable did not reject an incompatible embedded ABI", file=sys.stderr)
            print(f"exit={rc}\nstdout={stdout}\nstderr={stderr}", file=sys.stderr)
            return 1
        print("PASS: generated executable checks ABI before entering RBL main")
    return 0


def test_fast_call_stack_alignment() -> int:
    """Instrument fast-function entries to enforce the SysV 8-mod-16 entry rule."""
    settings = rbltool.load_settings()
    rbltool.ensure_toolchain(settings)
    build_root = ROOT / "build"
    build_root.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="runtime-abi-stack-", dir=build_root) as temp:
        temp_dir = Path(temp)
        source = temp_dir / "abi_stack_alignment.rbl"
        assembly = temp_dir / "abi_stack_alignment.s"
        obj = temp_dir / "abi_stack_alignment.o"
        executable = temp_dir / ("abi_stack_alignment.exe" if rbltool.use_native_windows(settings) else "abi_stack_alignment")
        source.write_text(
            "func identity(x int) int (\n"
            "    return x\n"
            ")\n"
            "func add_right_call(x int) int (\n"
            "    return x + identity(2)\n"
            ")\n"
            "func main() (\n"
            "    print(add_right_call(40))\n"
            "    if (identity(1) < identity(2) and identity(2) < identity(3)) (\n"
            "        print(\"boolean calls aligned\")\n"
            "    )\n"
            ")\n",
            encoding="utf-8",
        )

        compile_cmd = rbltool.compiler_cmd(source, assembly, settings)
        if rbltool.use_wsl(settings):
            code, out, err = rbltool.run_wsl(compile_cmd, settings)
        else:
            code, out, err = rbltool.run_native(compile_cmd)
        if code:
            print(out, end="")
            print(err, end="", file=sys.stderr)
            return code

        generated = assembly.read_text(encoding="utf-8")
        counter = 0

        def add_alignment_guard(match: re.Match[str]) -> str:
            nonlocal counter
            label = match.group(1)
            guard = counter
            counter += 1
            return (
                f"{label}\n"
                "    mov r10,rsp\n"
                "    and r10,15\n"
                "    cmp r10,8\n"
                f"    je .Lrbl_abi_stack_ok_{guard}\n"
                "    mov rax,-999\n"
                "    ret\n"
                f".Lrbl_abi_stack_ok_{guard}:\n"
            )

        generated, replacements = re.subn(r"(?m)^(fast_fn_\d+:)$", add_alignment_guard, generated)
        if replacements < 2:
            print("FAIL: stack-alignment probe did not generate both fast functions", file=sys.stderr)
            return 1
        assembly.write_text(generated, encoding="utf-8")

        if rbltool.use_wsl(settings):
            asm_path = shlex.quote(rbltool.wsl_path(assembly, settings))
            obj_path = shlex.quote(rbltool.wsl_path(obj, settings))
            exe_path = shlex.quote(rbltool.wsl_path(executable, settings))
            runtime = shlex.quote(rbltool.wsl_path(rbltool.RUNTIME_OBJ, settings))
            containers = shlex.quote(rbltool.wsl_path(rbltool.CONTAINERS_OBJ, settings))
            abi = shlex.quote(rbltool.wsl_path(rbltool.RUNTIME_ABI_OBJ, settings))
            formatting = shlex.quote(rbltool.wsl_path(rbltool.RUNTIME_FORMAT_OBJ, settings))
            command = (
                f"as --64 {asm_path} -o {obj_path} && "
                f"ld {obj_path} {runtime} {containers} {abi} {formatting} -o {exe_path} "
                "-dynamic-linker /lib64/ld-linux-x86-64.so.2 -lc -lm"
            )
            code, out, err = rbltool.run_wsl(command, settings)
        elif rbltool.use_native_windows(settings):
            toolchain = rbltool.find_native_toolchain()
            env = rbltool.toolchain_env(toolchain)
            code, out, err = rbltool.run_native(
                [str(toolchain["as"]), "--64", str(assembly), "-o", str(obj)], env=env
            )
            if not code:
                code, out, err = rbltool.run_native(
                    [str(toolchain["gcc"]), str(obj), str(rbltool.WIN_RUNTIME_OBJ),
                     str(rbltool.WIN_CONTAINERS_OBJ), str(rbltool.WIN_RUNTIME_ABI_OBJ),
                     str(rbltool.WIN_RUNTIME_FORMAT_OBJ),
                     "-o", str(executable), "-static", "-lm", "-fno-use-linker-plugin"], env=env
                )
        else:
            code, out, err = rbltool.run_native(["as", "--64", str(assembly), "-o", str(obj)])
            if not code:
                code, out, err = rbltool.run_native(
                    ["ld", str(obj), str(rbltool.RUNTIME_OBJ), str(rbltool.CONTAINERS_OBJ),
                     str(rbltool.RUNTIME_ABI_OBJ), str(rbltool.RUNTIME_FORMAT_OBJ), "-o", str(executable),
                     "-dynamic-linker", "/lib64/ld-linux-x86-64.so.2", "-lc", "-lm"]
                )
        if code:
            print(out, end="")
            print(err, end="", file=sys.stderr)
            return code

        rc, stdout, stderr = rbltool.run_built(executable, settings=settings)
        expected = "42\nboolean calls aligned\n"
        if rc != 0 or stdout != expected:
            print("FAIL: generated fast calls violated SysV stack alignment", file=sys.stderr)
            print(f"exit={rc}\nexpected={expected!r}\nstdout={stdout!r}\nstderr={stderr}", file=sys.stderr)
            return 1
        print("PASS: nested fast calls enter with SysV 8-byte-mod-16 stack alignment")
    return 0


def test_tag_contract() -> int:
    header = (ROOT / "runtime" / "rbl_abi.h").read_text(encoding="utf-8")
    assembly = (ROOT / "runtime" / "rbl_runtime.s").read_text(encoding="utf-8")
    windows_runtime = (ROOT / "runtime" / "rbl_runtime_win.c").read_text(encoding="utf-8")
    abi_source = (ROOT / "runtime" / "rbl_abi.c").read_text(encoding="utf-8")
    codegen = (ROOT / "compiler" / "codegen.c").read_text(encoding="utf-8")
    major_match = re.search(r"(?m)^#define RBL_RUNTIME_ABI_MAJOR (\d+)u$", header)
    minor_match = re.search(r"(?m)^#define RBL_RUNTIME_ABI_MINOR (\d+)u$", header)
    format_feature = re.search(r"(?m)^#define RBL_RUNTIME_ABI_FEATURE_FSTRING_FORMAT \(UINT64_C\(1\) << (\d+)\)$", header)
    descriptor_symbol = re.search(r"(?m)^#define RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL (\w+)$", header)
    if not major_match or not minor_match or not descriptor_symbol or not format_feature:
        print("FAIL: Runtime ABI version, formatting feature or descriptor symbol is missing", file=sys.stderr)
        return 1
    major = int(major_match.group(1))
    if rbltool.runtime_abi_check_symbol() != f"rbl_runtime_abi_check_v{major}" or descriptor_symbol.group(1) != f"rbl_runtime_abi_v{major}":
        print("FAIL: Runtime ABI exported symbol versions do not match the ABI major", file=sys.stderr)
        return 1
    if int(minor_match.group(1)) < 2 or int(format_feature.group(1)) != 5 or "RBL_RUNTIME_ABI_FEATURES" not in codegen:
        print("FAIL: compiler does not require the Runtime ABI f-string formatting feature", file=sys.stderr)
        return 1
    names = {
        "TAG_UNIT": "RBL_TAG_UNIT",
        "TAG_INT": "RBL_TAG_INT",
        "TAG_FLOAT": "RBL_TAG_FLOAT",
        "TAG_STR": "RBL_TAG_STRING",
        "TAG_BOOL": "RBL_TAG_BOOL",
        "TAG_LIST": "RBL_TAG_LIST",
        "TAG_DICT": "RBL_TAG_DICTIONARY",
        "TAG_TUPLE": "RBL_TAG_TUPLE",
        "TAG_NULL": "RBL_TAG_NULL",
    }
    for asm_name, header_name in names.items():
        expected = re.search(rf"\b{header_name}\s*=\s*(\d+)", header)
        actual = re.search(rf"(?m)^\.equ\s+{asm_name},\s*(\d+)\s*$", assembly)
        if not expected or not actual or expected.group(1) != actual.group(1):
            print(f"FAIL: Runtime tag {asm_name} differs between ABI header and Linux assembly", file=sys.stderr)
            return 1
        win_name = re.search(rf"(?m)^\s*{asm_name}\s*=\s*{header_name}\s*,?$", windows_runtime)
        if not win_name:
            print(f"FAIL: Windows Runtime tag {asm_name} does not use {header_name}", file=sys.stderr)
            return 1
    if '"rbl_abi.h"' not in windows_runtime or "RBL_RUNTIME_ABI_FEATURES," not in abi_source:
        print("FAIL: Windows Runtime or ABI descriptor is disconnected from the canonical ABI header", file=sys.stderr)
        return 1
    if any(token not in codegen for token in ("RBL_TAG_INT", "RBL_TAG_LIST", "RBL_TAG_TUPLE")):
        print("FAIL: compiler codegen does not use canonical Runtime tag constants", file=sys.stderr)
        return 1
    if any(token in codegen for token in ("cmp rax,5", "cmp rax,7", "mov eax,1\n")):
        print("FAIL: compiler codegen contains hard-coded Runtime tag values", file=sys.stderr)
        return 1
    print("PASS: ABI symbols/features and C/compiler, Linux assembly, Windows Runtime and codegen use canonical tags")
    return 0


def main() -> int:
    tags = test_tag_contract()
    if tags:
        return tags
    settings = rbltool.load_settings()
    build_root = ROOT / "build"
    build_root.mkdir(exist_ok=True)
    bundled_native = False
    if rbltool.use_native_windows(settings):
        toolchain = rbltool.find_native_toolchain()
        bundled_native = bool(toolchain and toolchain.get("kind") == "bundled")
    if bundled_native:
        print("SKIP: standalone C ABI probe requires GCC cc1/headers, intentionally excluded from the user bundle")
    else:
        with tempfile.TemporaryDirectory(prefix="rbl-abi-test-", dir=build_root) as temp:
            executable = Path(temp) / ("runtime_abi_probe.exe" if rbltool.use_native_windows(settings) else "runtime_abi_probe")
            if rbltool.use_wsl(settings):
                exe_path = shlex.quote(rbltool.wsl_path(executable, settings))
                abi_source = shlex.quote(rbltool.wsl_path(ROOT / "runtime" / "rbl_abi.c", settings))
                probe_source = shlex.quote(rbltool.wsl_path(ROOT / "tests" / "runtime_abi_probe.c", settings))
                command = f"cc -std=c11 -Wall -Wextra -Wpedantic {abi_source} {probe_source} -o {exe_path}"
                code, out, err = rbltool.run_wsl(command, settings)
                if code:
                    print(out, end="")
                    print(err, end="", file=sys.stderr)
                    return code
                valid_code, valid_out, valid_err = rbltool.run_wsl(exe_path, settings)
                if valid_code:
                    print(valid_out, end="")
                    print(valid_err, end="", file=sys.stderr)
                    return valid_code
                print(valid_out, end="")
                mismatch_code, mismatch_out, mismatch_err = rbltool.run_wsl(f"{exe_path} --mismatch", settings)
            else:
                compiler, env = compiler_command()
                command = compiler + [
                    "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
                    str(ROOT / "runtime" / "rbl_abi.c"),
                    str(ROOT / "tests" / "runtime_abi_probe.c"),
                    "-o", str(executable),
                ]
                built = subprocess.run(command, text=True, capture_output=True, env=env,
                                       encoding="utf-8", errors="replace")
                if built.returncode:
                    print(built.stdout, end="")
                    print(built.stderr, end="", file=sys.stderr)
                    return built.returncode
                valid = subprocess.run([str(executable)], text=True, capture_output=True, env=env,
                                       encoding="utf-8", errors="replace")
                print(valid.stdout, end="")
                if valid.returncode:
                    print(valid.stderr, end="", file=sys.stderr)
                    return valid.returncode
                mismatch = subprocess.run([str(executable), "--mismatch"], text=True, capture_output=True, env=env,
                                          encoding="utf-8", errors="replace")
                mismatch_code, mismatch_out, mismatch_err = mismatch.returncode, mismatch.stdout, mismatch.stderr
            expected = "RBL Runtime ABI mismatch:"
            if mismatch_code != 78 or expected not in mismatch_err:
                print("FAIL: incompatible ABI did not produce the documented diagnostic", file=sys.stderr)
                print(f"exit={mismatch_code}\nstdout={mismatch_out}\nstderr={mismatch_err}", file=sys.stderr)
                return 1
            print("PASS: incompatible ABI diagnostic and exit code 78")

    abi_symbol = rbltool.runtime_abi_check_symbol()
    link_code, link_message, stage = rbltool._link_failure(
        1, f"undefined reference to `{abi_symbol}'", "link failed"
    )
    if link_code != 1 or stage != "link failed" or "Runtime ABI mismatch" not in link_message:
        print("FAIL: missing major-version symbol did not produce an actionable link diagnostic", file=sys.stderr)
        return 1
    print("PASS: missing major-version symbol link diagnostic")

    startup = test_generated_program_startup()
    if startup:
        return startup
    stack_alignment = test_fast_call_stack_alignment()
    if stack_alignment:
        return stack_alignment

    print("ALL RUNTIME ABI TESTS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
