from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipIf(os.name == "nt", "Linux launcher symlink behavior is POSIX-only")
class LinuxLauncherTests(unittest.TestCase):
    def test_symlink_launcher_resolves_install_root_and_preserves_arguments(self) -> None:
        with tempfile.TemporaryDirectory(prefix="rbl launcher symlink ") as temporary:
            base = Path(temporary)
            install = base / "installation with spaces"
            bin_dir = base / "user bin"
            install.mkdir()
            bin_dir.mkdir()

            launcher = install / "RBLStudio.sh"
            shutil.copy2(ROOT / "RBLStudio.sh", launcher)
            gui = install / "rbl-studio-wx"
            gui.write_text(
                "#!/usr/bin/env bash\n"
                "printf 'root=%s\\nworker=%s\\ncwd=%s\\narg1=%s\\narg2=%s\\n' "
                '"$RBL_STUDIO_ROOT" "$RBL_STUDIO_WORKER" "$PWD" "$1" "$2"\n',
                encoding="utf-8",
            )
            gui.chmod(0o755)
            worker = install / "rbl-studio-worker"
            worker.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            worker.chmod(0o755)

            link = bin_dir / "rblstudio"
            link.symlink_to(Path("..") / install.name / launcher.name)
            args = ["argument with spaces", 'embedded "quote"']
            completed = subprocess.run(
                ["bash", str(link), *args],
                cwd=base,
                text=True,
                capture_output=True,
                check=True,
            )

            values = dict(line.split("=", 1) for line in completed.stdout.splitlines())
            self.assertEqual(values["root"], str(install))
            self.assertEqual(values["worker"], str(worker))
            self.assertEqual(values["cwd"], str(install))
            self.assertEqual(values["arg1"], args[0])
            self.assertEqual(values["arg2"], args[1])

    def test_relative_build_pointer_and_empty_quoted_unicode_arguments(self) -> None:
        with tempfile.TemporaryDirectory(prefix="rbl launcher pointer ") as temporary:
            base = Path(temporary)
            install = base / "installation with spaces"
            bin_dir = base / "user bin"
            build = install / "build output with spaces"
            (install / "build").mkdir(parents=True)
            bin_dir.mkdir()
            build.mkdir()

            launcher = install / "RBLStudio.sh"
            shutil.copy2(ROOT / "RBLStudio.sh", launcher)
            (install / "build" / "wx-ui-current-linux.path").write_text(
                "build output with spaces\n", encoding="utf-8"
            )
            gui = build / "rbl-studio-wx"
            gui.write_text(
                "#!/usr/bin/env bash\n"
                "printf 'root=%s\\nworker=%s\\ncwd=%s\\nargv=' "
                '"$RBL_STUDIO_ROOT" "$RBL_STUDIO_WORKER" "$PWD"\n'
                "printf '%s\\0' \"$@\"\n",
                encoding="utf-8",
            )
            gui.chmod(0o755)
            worker = build / "rbl-studio-worker"
            worker.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            worker.chmod(0o755)

            link = bin_dir / "rblstudio"
            link.symlink_to(Path("..") / install.name / launcher.name)
            args = ["", "two words", 'embedded "quote"', "trailing\\", "кириллица"]
            completed = subprocess.run(
                ["bash", str(link), *args], cwd=base, text=True,
                capture_output=True, check=True,
            )
            header, argument_data = completed.stdout.split("argv=", 1)
            values = dict(line.split("=", 1) for line in header.splitlines())
            forwarded = argument_data.split("\0")[:-1]
            self.assertEqual(values["root"], str(install))
            self.assertEqual(values["worker"], str(worker))
            self.assertEqual(values["cwd"], str(install))
            self.assertEqual(forwarded, args)


if __name__ == "__main__":
    unittest.main()
