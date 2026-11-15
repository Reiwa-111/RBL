import contextlib
import io
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import rbl  # noqa: E402


class NewProjectTemplateTests(unittest.TestCase):
    def create_project(self, name: str, target: Path) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(rbl.new_project(name, target), 0)

    def test_generated_readme_uses_real_cli_entrypoint(self) -> None:
        with tempfile.TemporaryDirectory(prefix="rbl-new-project-") as tmp:
            target = Path(tmp) / "my-app_1"
            self.create_project("my-app_1", target)

            manifest = (target / "rbl.toml").read_text(encoding="utf-8")
            source = (target / "src" / "main.rbl").read_text(encoding="utf-8")
            readme = (target / "README.md").read_text(encoding="utf-8")

            self.assertIn('name = "my-app_1"\n', manifest)
            self.assertIn('entry = "src/main.rbl"\n', manifest)
            self.assertIn('print("Hello from RBL!")', source)
            self.assertIn('python3 "$RBL_STUDIO_ROOT/rbl.py" check src/main.rbl', readme)
            self.assertIn('python3 "$RBL_STUDIO_ROOT/rbl.py" run src/main.rbl', readme)
            self.assertIn('python "$env:RBL_STUDIO_ROOT\\rbl.py" check src\\main.rbl', readme)
            self.assertIn("rbl.py test` runs the RBL Studio repository regression suite", readme)
            self.assertNotRegex(readme, re.compile(r"(?m)^rbl (?:check|run|test)\b"))

    def test_invalid_names_are_rejected_before_creating_files(self) -> None:
        invalid_names = ("", "../escape", "name/child", 'bad"name', "bad\nname", "x" * 65)
        with tempfile.TemporaryDirectory(prefix="rbl-new-invalid-") as tmp:
            for index, name in enumerate(invalid_names):
                target = Path(tmp) / f"target-{index}"
                with self.subTest(name=repr(name)), self.assertRaises(SystemExit):
                    rbl.new_project(name, target)
                self.assertFalse(target.exists())

    def test_init_rejects_invalid_directory_name(self) -> None:
        with tempfile.TemporaryDirectory(prefix="rbl-init-invalid-") as tmp:
            target = Path(tmp) / "bad project name"
            target.mkdir()
            result = subprocess.run(
                [sys.executable, str(ROOT / "rbl.py"), "init"],
                cwd=target,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("project name must be", result.stderr)
            self.assertFalse((target / "rbl.toml").exists())

    def test_nonempty_target_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory(prefix="rbl-new-existing-") as tmp:
            target = Path(tmp) / "existing"
            target.mkdir()
            marker = target / "keep.txt"
            marker.write_text("preserve", encoding="utf-8")
            with self.assertRaises(SystemExit):
                rbl.new_project("demo", target)
            self.assertEqual(marker.read_text(encoding="utf-8"), "preserve")


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(NewProjectTemplateTests)
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
