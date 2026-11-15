#!/usr/bin/env python3
"""Verify Linux app-local LLVM selection and fail-closed packaged behavior."""
from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


class LinuxToolchainResolutionTests(unittest.TestCase):
    def test_bundled_clang_is_used_even_when_system_as_exists(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            clang = root / "bin" / "clang"
            clang.parent.mkdir()
            clang.write_text("test tool", encoding="ascii")
            source = root / "program.s"
            output = root / "program.o"
            with patch.object(rbltool, "LINUX_CLANG", clang), \
                 patch.object(rbltool, "run_native", return_value=(0, "", "")) as run, \
                 patch.object(rbltool, "have", return_value=True), \
                 patch.object(rbltool, "_linux_tool_env", return_value={}):
                code, _, _ = rbltool._linux_assemble_native(source, output)
            self.assertEqual(code, 0)
            command = run.call_args.args[0]
            self.assertEqual(command[0], str(clang))
            self.assertIn("-x", command)
            self.assertIn("assembler", command)
            self.assertNotIn("as", command)

    def test_packaged_mode_refuses_system_as_fallback(self):
        with tempfile.TemporaryDirectory() as temporary:
            missing = Path(temporary) / "missing" / "clang"
            with patch.dict(os.environ, {"RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN": "1"}), \
                 patch.object(rbltool, "LINUX_CLANG", missing), \
                 patch.object(rbltool, "have", return_value=True), \
                 patch.object(rbltool, "run_native", side_effect=AssertionError("must not invoke system as")):
                with self.assertRaisesRegex(RuntimeError, "app-local Clang assembler is missing"):
                    rbltool._linux_assemble_native(Path("program.s"), Path("program.o"))

    def test_lld_uses_runtime_shared_objects_without_dev_linker_scripts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lld = root / "bin" / "ld.lld"
            lld.parent.mkdir()
            lld.write_text("test tool", encoding="ascii")
            objects = [root / name for name in ("runtime.o", "containers.o", "abi.o", "format.o")]
            with patch.dict(os.environ, {"RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN": "1"}), \
                 patch.object(rbltool, "LINUX_LLD", lld), \
                 patch.object(rbltool, "LINUX_SYSTEM_LIB_DIRS", ("/usr/lib/x86_64-linux-gnu",)), \
                 patch.object(rbltool, "RUNTIME_OBJ", objects[0]), \
                 patch.object(rbltool, "CONTAINERS_OBJ", objects[1]), \
                 patch.object(rbltool, "RUNTIME_ABI_OBJ", objects[2]), \
                 patch.object(rbltool, "RUNTIME_FORMAT_OBJ", objects[3]), \
                 patch.object(rbltool, "run_native", return_value=(0, "", "")) as run, \
                 patch.object(rbltool, "_linux_tool_env", return_value={}):
                code, _, _ = rbltool._linux_link_native(root / "program.o", root / "program")
            self.assertEqual(code, 0)
            command = run.call_args.args[0]
            self.assertEqual(command[0], str(lld))
            self.assertIn("-l:libc.so.6", command)
            self.assertIn("-l:libm.so.6", command)
            self.assertNotIn("-lc", command)
            self.assertNotIn("-lm", command)

    def test_packaged_mode_refuses_system_ld_fallback(self):
        with tempfile.TemporaryDirectory() as temporary:
            missing = Path(temporary) / "missing" / "ld.lld"
            with patch.dict(os.environ, {"RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN": "1"}), \
                 patch.object(rbltool, "LINUX_LLD", missing), \
                 patch.object(rbltool, "have", return_value=True), \
                 patch.object(rbltool, "run_native", side_effect=AssertionError("must not invoke system ld")):
                with self.assertRaisesRegex(RuntimeError, "app-local LLD linker is missing"):
                    rbltool._linux_link_native(Path("program.o"), Path("program"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
