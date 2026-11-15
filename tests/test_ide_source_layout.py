from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class IdeSourceLayoutTests(unittest.TestCase):
    def test_canonical_ide_directories_exist(self):
        for relative in (
            "rbl-ide/wx-ui/CMakeLists.txt",
            "rbl-ide/wx-ui/src/main.cpp",
            "rbl-ide/worker/rbl-studio-worker.rs",
            "rbl-ide/worker/process.rs",
            "rbl-ide/worker/settings.rs",
            "rbl-ide/legacy-egui/main.rs",
            "rbl-ide/legacy-egui/app.rs",
            "rbl-ide/legacy-egui/editor.rs",
        ):
            with self.subTest(path=relative):
                self.assertTrue((ROOT / relative).is_file(), relative)

    def test_cargo_targets_point_at_canonical_sources(self):
        cargo = (ROOT / "Cargo.toml").read_text(encoding="utf-8")
        self.assertIn('path = "rbl-ide/legacy-egui/main.rs"', cargo)
        self.assertIn('path = "rbl-ide/worker/rbl-studio-worker.rs"', cargo)
        self.assertNotIn('path = "src/', cargo)

    def test_worker_modules_are_sibling_relative(self):
        worker = (ROOT / "rbl-ide/worker/rbl-studio-worker.rs").read_text(encoding="utf-8")
        self.assertIn('#[path = "process.rs"]', worker)
        self.assertIn('#[path = "settings.rs"]', worker)
        self.assertNotIn('#[path = "../process.rs"]', worker)
        self.assertNotIn('#[path = "../settings.rs"]', worker)
        legacy = (ROOT / "rbl-ide/legacy-egui/main.rs").read_text(encoding="utf-8")
        self.assertIn('#[path = "../worker/process.rs"]', legacy)
        self.assertIn('#[path = "../worker/settings.rs"]', legacy)

    def test_installers_and_cli_wrappers_prefer_app_local_python(self):
        windows_installer = (ROOT / "installer/install_windows.ps1").read_text(encoding="utf-8")
        linux_installer = (ROOT / "installer/install_linux.sh").read_text(encoding="utf-8")
        windows_cli = (ROOT / "rbl.cmd").read_text(encoding="utf-8")
        linux_cli = (ROOT / "rbl").read_text(encoding="utf-8")
        self.assertIn("runtime\\python\\python.exe", windows_installer)
        self.assertIn("runtime/python/bin/python3", linux_installer)
        self.assertIn("runtime\\python\\python.exe", windows_cli)
        self.assertIn("runtime/python/bin/python3", linux_cli)
        self.assertIn("runtime/python/python-runtime.tar.gz", linux_installer)
        self.assertIn("rbl.cmd", windows_installer)
        self.assertIn('cp -- "$ROOT/rbl"', linux_installer)

    def test_build_scripts_stage_pinned_platform_runtime(self):
        windows_build = (ROOT / "tools/build_wx_ide.ps1").read_text(encoding="utf-8")
        linux_build = (ROOT / "tools/build_wx_ide.sh").read_text(encoding="utf-8")
        self.assertIn("package_python_runtime.py", windows_build)
        self.assertIn("--platform windows", windows_build)
        self.assertIn("package_python_runtime.py", linux_build)
        self.assertIn("--platform linux", linux_build)

    def test_build_and_source_bundle_scripts_use_canonical_wx_root(self):
        scripts = (
            "tools/build_wx_ide.ps1",
            "tools/build_wx_ide.sh",
            "tools/create_rust_source_bundle.py",
            "tools/verify_rust_source_bundle.py",
        )
        for relative in scripts:
            content = (ROOT / relative).read_text(encoding="utf-8")
            with self.subTest(path=relative):
                self.assertIn("rbl-ide/wx-ui", content.replace("\\", "/"))
                self.assertNotIn("native/wx-ui", content.replace("\\", "/"))


if __name__ == "__main__":
    unittest.main()
