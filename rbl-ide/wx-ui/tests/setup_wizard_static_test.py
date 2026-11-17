from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "main_frame.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "include" / "main_frame.hpp").read_text(encoding="utf-8")


class SetupWizardIntegrationTests(unittest.TestCase):
    def test_first_launch_setup_runs_before_worker_start(self):
        start = SOURCE.index("CallAfter([this] {")
        end = SOURCE.index("NewDocument();", start)
        startup = SOURCE[start:end]
        self.assertIn('ReadBool("Setup/Completed", false)', startup)
        self.assertIn("!RunSetupWizard(true)", startup)
        self.assertLess(startup.index("!RunSetupWizard(true)"), startup.index("StartWorker();"))
        self.assertIn("Close(true);", startup)

    def test_setup_can_be_reopened_from_settings_menu(self):
        self.assertIn('settings->Append(ID_SetupWizard, "First Launch Setup...")', SOURCE)
        self.assertIn("EVT_MENU(ID_SetupWizard, MainFrame::OnSetupWizard)", SOURCE)
        self.assertIn("void MainFrame::OnSetupWizard(wxCommandEvent&)", SOURCE)

    def test_setup_persists_user_paths_and_initial_preferences(self):
        for key in (
            '"Setup/Completed"',
            '"Setup/ApplicationRoot"',
            '"Setup/WorkerPath"',
            '"Setup/PythonPath"',
        ):
            self.assertIn(key, SOURCE)
        self.assertIn('wxSetEnv("RBL_TOOLCHAIN", toolchain_mode_)', SOURCE)
        self.assertIn("ApplyUiTheme();", SOURCE)

    def test_stale_saved_root_falls_back_to_the_executable_root(self):
        self.assertIn("const auto detected_root = application_root_;", SOURCE)
        self.assertIn("IsApplicationRoot(detected_root)", SOURCE)
        self.assertIn('wxSetEnv("RBL_STUDIO_ROOT", wxString(application_root_.wstring()))', SOURCE)

    def test_stale_saved_root_falls_back_to_the_executable_root(self):
        self.assertIn("const auto detected_root = application_root_;", SOURCE)
        self.assertIn("IsApplicationRoot(detected_root)", SOURCE)
        self.assertIn('wxSetEnv("RBL_STUDIO_ROOT", wxString(application_root_.wstring()))', SOURCE)

    def test_diagnostics_are_real_checks_with_actionable_paths(self):
        helper = (ROOT / "include" / "setup_diagnostics.hpp").read_text(encoding="utf-8")
        for token in ("IsApplicationRoot(root)", "IsRegularFile(worker)",
                      "IsRegularFile(python)", "gcc.exe", "clang", "RBL_STUDIO_WORKER", "RBL_PYTHON"):
            self.assertIn(token, helper)
        self.assertIn("Refresh checks", SOURCE)
        self.assertIn("[ERROR]", SOURCE)

    def test_installer_runs_async_with_captured_output_and_safe_user_scope_flags(self):
        self.assertIn('"Install / Repair"', SOURCE)
        self.assertIn("execute_async(arguments, wxEXEC_ASYNC, installer_process)", SOURCE)
        self.assertIn("GetInputStream()", SOURCE)
        self.assertIn("GetErrorStream()", SOURCE)
        self.assertIn('arguments.Add("-NoRegister")', SOURCE)
        self.assertIn('arguments.Add("--no-register")', SOURCE)
        self.assertIn("event.GetExitCode()", SOURCE)
        self.assertIn('"Launch installed copy"', SOURCE)
        self.assertIn('"RBLStudio.sh"', SOURCE)
        self.assertIn('wxSetEnv("RBL_STUDIO_ROOT", install_path->GetValue())', SOURCE)
        self.assertIn('wxSetEnv("RBL_STUDIO_WORKER", worker_path->GetValue())', SOURCE)
        self.assertIn('wxSetEnv("RBL_PYTHON", python_path->GetValue())', SOURCE)
        self.assertIn('arguments.Add("--build-dir")', SOURCE)
        self.assertIn('arguments.Add(executable_directory)', SOURCE)
        self.assertIn("application_root_ / \"installer/uninstall_linux.sh\"", SOURCE)
        self.assertIn("application_root_ / \"installer/uninstall_windows.ps1\"", SOURCE)
        self.assertIn("wxProcess::Kill(installer_pid, wxSIGTERM, wxKILL_CHILDREN)", SOURCE)

    def test_first_launch_requires_successful_install_or_explicit_dev_checkout(self):
        self.assertIn("first_launch && !install_succeeded && !configuration_valid && !developer_continue->GetValue()", SOURCE)
        self.assertIn("install_succeeded || configuration_valid || developer_continue->GetValue() || !first_launch", SOURCE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
