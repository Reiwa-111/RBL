from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import rbltool


class RblToolWindowsPathTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "native Windows path conversion")
    def test_unicode_existing_and_output_paths_get_ascii_short_aliases(self):
        with tempfile.TemporaryDirectory(prefix="RBL-путь-") as temporary:
            root = Path(temporary)
            source = root / "данные.rbl"
            source.write_text("func main() (\n)\n", encoding="utf-8")
            try:
                short_source = rbltool.windows_short_path(source)
                short_output = rbltool.windows_native_path_argument(root / "generated.s")
            except RuntimeError as exc:
                self.skipTest(f"8.3 short names are disabled for this volume: {exc}")
            self.assertTrue(short_source.isascii(), short_source)
            self.assertTrue(short_output.isascii(), short_output)
            self.assertTrue(Path(short_source).is_file())
            self.assertTrue(Path(short_output).parent.is_dir())

    def test_native_build_stem_avoids_unicode_output_names(self):
        with mock.patch.object(rbltool, "is_windows", return_value=True), \
             mock.patch.object(rbltool, "use_native_windows", return_value=True):
            stem = rbltool.native_build_stem(Path("C:/workspace/пример-файл.rbl"), {})
        self.assertTrue(stem.isascii())
        self.assertTrue(stem)

    def test_windows_link_search_args_include_program_and_library_roots(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bin_dir = root / "bin"
            target_bin = root / "x86_64-w64-mingw32" / "bin"
            target_lib = root / "x86_64-w64-mingw32" / "lib"
            version_dir = root / "lib" / "gcc" / "x86_64-w64-mingw32" / "16.2.0"
            for directory in (bin_dir, target_bin, target_lib, version_dir):
                directory.mkdir(parents=True, exist_ok=True)
            with mock.patch.object(rbltool, "windows_short_path", side_effect=lambda value: str(value)):
                args = rbltool.windows_link_search_args({"bin": bin_dir})
        self.assertIn("-B" + str(bin_dir) + os.sep, args)
        self.assertIn("-B" + str(target_bin) + os.sep, args)
        self.assertIn("-B" + str(version_dir) + os.sep, args)
        self.assertIn("-L" + str(target_lib), args)
        self.assertIn("-L" + str(root / "lib"), args)

    @unittest.skipUnless(os.name == "nt", "native Windows process argument conversion")
    def test_run_native_shortens_absolute_command_paths_and_working_directory(self):
        with mock.patch.object(rbltool, "windows_native_path_argument", side_effect=lambda value: "C:\\ASCII\\" + Path(value).name), \
             mock.patch.object(rbltool, "windows_short_path", return_value="C:\\ASCII\\root"), \
             mock.patch.object(rbltool.subprocess, "run", return_value=mock.Mock(returncode=0, stdout="", stderr="")) as run:
            code, _, _ = rbltool.run_native(
                [r"C:\Users\Имя\tool.exe", "--check", r"C:\Users\Имя\source.rbl"],
                cwd=Path(r"C:\Users\Имя\workspace"),
            )
        self.assertEqual(code, 0)
        args, kwargs = run.call_args
        self.assertEqual(args[0][0], r"C:\ASCII\tool.exe")
        self.assertEqual(args[0][2], r"C:\ASCII\source.rbl")
        self.assertEqual(kwargs["cwd"], r"C:\ASCII\root")


if __name__ == "__main__":
    unittest.main(verbosity=2)
