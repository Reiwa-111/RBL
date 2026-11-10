#![cfg_attr(
    all(target_os = "windows", not(debug_assertions)),
    windows_subsystem = "windows"
)]

mod app;
mod editor;
mod process;
mod settings;

use std::path::{Path, PathBuf};

fn initial_file_paths(args: impl IntoIterator<Item = std::ffi::OsString>) -> Vec<PathBuf> {
    args.into_iter()
        .filter(|arg| !arg.to_string_lossy().starts_with('-'))
        .map(PathBuf::from)
        .collect()
}

fn find_repo_root(start: &Path) -> Option<PathBuf> {
    start.ancestors().find_map(|candidate| {
        (candidate.join("tools/rbltool.py").is_file() && candidate.join("VERSION").is_file())
            .then(|| candidate.to_path_buf())
    })
}

fn discover_repo_root() -> PathBuf {
    if let Ok(executable) = std::env::current_exe() {
        if let Some(parent) = executable.parent() {
            if let Some(root) = find_repo_root(parent) {
                return root;
            }
        }
    }
    if let Ok(current) = std::env::current_dir() {
        if let Some(root) = find_repo_root(&current) {
            return root;
        }
    }
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn main() -> eframe::Result<()> {
    let root = discover_repo_root();
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.iter().any(|arg| arg == "--version") {
        println!(
            "RBL Studio {} (native Rust/egui)",
            env!("CARGO_PKG_VERSION")
        );
        return Ok(());
    }
    if args.iter().any(|arg| arg == "--self-test") {
        for relative in [
            "tools/rbltool.py",
            "tests/run_tests.py",
            "benchmarks/run_benchmarks.py",
            "VERSION",
        ] {
            if !root.join(relative).is_file() {
                eprintln!("RBL Studio self-test failed: missing {relative}");
                std::process::exit(1);
            }
        }
        let lockfile = include_str!("../Cargo.lock");
        for forbidden in [
            "wry",
            "webkit2gtk",
            "webkit6",
            "cef",
            "chromium",
            "chromiumoxide",
            "headless_chrome",
            "webview2-com",
            "electron",
        ] {
            if lockfile.contains(&format!("name = \"{forbidden}\"")) {
                eprintln!("RBL Studio self-test failed: embedded browser dependency {forbidden}");
                std::process::exit(1);
            }
        }
        println!("RBL Studio native startup checks: PASS");
        return Ok(());
    }
    let initial = initial_file_paths(std::env::args_os().skip(1));

    let mut viewport = eframe::egui::ViewportBuilder::default()
        .with_title("RBL Studio")
        .with_inner_size([1440.0, 900.0])
        .with_min_inner_size([900.0, 600.0]);
    if let Ok(icon) = eframe::icon_data::from_png_bytes(include_bytes!("../assets/rbl_256.png")) {
        viewport = viewport.with_icon(std::sync::Arc::new(icon));
    }
    let options = eframe::NativeOptions {
        viewport,
        renderer: eframe::Renderer::Glow,
        ..Default::default()
    };

    eframe::run_native(
        "RBL Studio",
        options,
        Box::new(move |cc| Ok(Box::new(app::RblStudioApp::new(cc, root, initial)))),
    )
}

#[cfg(test)]
mod tests {
    use super::{find_repo_root, initial_file_paths};
    use std::path::PathBuf;

    #[test]
    fn embedded_window_icon_is_a_valid_square_png() {
        let icon = eframe::icon_data::from_png_bytes(include_bytes!("../assets/rbl_256.png"))
            .expect("RBL Studio window icon must decode");
        assert_eq!(icon.width, 256);
        assert_eq!(icon.height, 256);
        assert_eq!(icon.rgba.len(), 256 * 256 * 4);
    }

    #[test]
    fn repository_root_is_discovered_from_nested_executable_directory() {
        let temp = tempfile::tempdir().unwrap();
        let root = temp.path();
        std::fs::create_dir_all(root.join("tools")).unwrap();
        std::fs::create_dir_all(root.join("target/release")).unwrap();
        std::fs::write(root.join("tools/rbltool.py"), "").unwrap();
        std::fs::write(root.join("VERSION"), "0.8.0\n").unwrap();
        let nested = root.join("target/release");
        assert_eq!(find_repo_root(&nested), Some(root.to_path_buf()));
    }

    #[test]
    fn startup_accepts_multiple_file_manager_arguments() {
        let paths = initial_file_paths([
            std::ffi::OsString::from("--some-flag"),
            std::ffi::OsString::from("first file.rbl"),
            std::ffi::OsString::from("notes.txt"),
        ]);
        assert_eq!(
            paths,
            vec![PathBuf::from("first file.rbl"), PathBuf::from("notes.txt")]
        );
    }

    #[test]
    fn dependency_lock_has_no_embedded_browser_engine() {
        let lockfile = include_str!("../Cargo.lock");
        for forbidden in [
            "wry",
            "webkit2gtk",
            "webkit6",
            "cef",
            "chromium",
            "chromiumoxide",
            "headless_chrome",
            "webview2-com",
            "electron",
        ] {
            assert!(
                !lockfile.contains(&format!("name = \"{forbidden}\"")),
                "unexpected embedded browser engine dependency: {forbidden}"
            );
        }
    }
}
