use serde::{Deserialize, Serialize};
use std::{fs, path::PathBuf};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub theme: String,
    pub font_size: f32,
    pub tab_size: usize,
    pub auto_check: bool,
    pub auto_save: bool,
    pub show_line_numbers: bool,
    pub syntax_highlighting: bool,
    pub soft_wrap: bool,
    pub indent_guides: bool,
    pub wsl_mode: String,
    pub wsl_distro: String,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            theme: "Dark".into(),
            font_size: 14.0,
            tab_size: 4,
            auto_check: true,
            auto_save: false,
            show_line_numbers: true,
            syntax_highlighting: true,
            soft_wrap: true,
            indent_guides: true,
            wsl_mode: "auto".into(),
            wsl_distro: "Ubuntu-24.04".into(),
        }
    }
}

impl Settings {
    fn normalize(&mut self) {
        if !matches!(self.theme.as_str(), "Dark" | "Light" | "Nord" | "Monokai") {
            self.theme = "Dark".into();
        }
        if !self.font_size.is_finite() {
            self.font_size = 14.0;
        }
        self.font_size = self.font_size.clamp(9.0, 24.0);
        self.tab_size = self.tab_size.clamp(2, 8);
        if !matches!(self.wsl_mode.as_str(), "auto" | "native" | "wsl") {
            self.wsl_mode = "auto".into();
        }
        if self.wsl_distro.trim().is_empty() {
            self.wsl_distro = "Ubuntu-24.04".into();
        }
    }
}

pub fn config_path() -> PathBuf {
    #[cfg(target_os = "windows")]
    let base = std::env::var_os("APPDATA")
        .map(PathBuf::from)
        .or_else(dirs::config_dir);
    #[cfg(not(target_os = "windows"))]
    let base = dirs::config_dir();
    base.unwrap_or_else(|| PathBuf::from("."))
        .join("RBLStudio")
        .join("settings.json")
}

pub fn load() -> Settings {
    load_with_warning().0
}

pub fn load_with_warning() -> (Settings, Option<String>) {
    load_from(&config_path())
}

fn load_from(path: &std::path::Path) -> (Settings, Option<String>) {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
            return (Settings::default(), None);
        }
        Err(error) => {
            return (
                Settings::default(),
                Some(format!("Could not read settings; defaults loaded: {error}")),
            );
        }
    };
    match serde_json::from_slice::<Settings>(&bytes) {
        Ok(mut settings) => {
            settings.normalize();
            (settings, None)
        }
        Err(error) => (
            Settings::default(),
            Some(format!(
                "Settings file is invalid; defaults loaded without overwriting it: {error}"
            )),
        ),
    }
}

pub fn save(settings: &Settings) -> anyhow::Result<()> {
    save_to(&config_path(), settings)
}

fn save_to(path: &std::path::Path, settings: &Settings) -> anyhow::Result<()> {
    let parent = path
        .parent()
        .ok_or_else(|| anyhow::anyhow!("settings path has no parent"))?;
    fs::create_dir_all(parent)?;
    let bytes = serde_json::to_vec_pretty(settings)?;
    let mut temp = tempfile::NamedTempFile::new_in(parent)?;
    use std::io::Write;
    temp.write_all(&bytes)?;
    temp.persist(path).map_err(|e| e.error)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_match_the_toolchain_contract() {
        let settings = Settings::default();
        assert_eq!(settings.wsl_mode, "auto");
        assert_eq!(settings.wsl_distro, "Ubuntu-24.04");
        assert_eq!(settings.tab_size, 4);
        assert!(settings.auto_check);
        assert!(settings.syntax_highlighting);
        assert!(settings.soft_wrap);
        assert!(settings.indent_guides);
    }

    #[test]
    fn settings_round_trip_preserves_values() {
        let settings = Settings {
            theme: "Nord".into(),
            font_size: 16.0,
            tab_size: 2,
            auto_check: false,
            auto_save: true,
            show_line_numbers: false,
            syntax_highlighting: false,
            soft_wrap: false,
            indent_guides: false,
            wsl_mode: "wsl".into(),
            wsl_distro: "Ubuntu-24.04".into(),
        };
        let encoded = serde_json::to_vec(&settings).unwrap();
        let decoded: Settings = serde_json::from_slice(&encoded).unwrap();
        assert_eq!(decoded.theme, "Nord");
        assert_eq!(decoded.font_size, 16.0);
        assert_eq!(decoded.tab_size, 2);
        assert!(!decoded.auto_check);
        assert!(decoded.auto_save);
        assert!(!decoded.show_line_numbers);
        assert!(!decoded.syntax_highlighting);
        assert!(!decoded.soft_wrap);
        assert!(!decoded.indent_guides);
        assert_eq!(decoded.wsl_mode, "wsl");
    }

    #[test]
    fn invalid_settings_are_normalized_to_safe_values() {
        let mut settings = Settings {
            theme: "MissingTheme".into(),
            font_size: f32::NAN,
            tab_size: 0,
            wsl_mode: "shell-command".into(),
            wsl_distro: "  ".into(),
            ..Settings::default()
        };
        settings.normalize();
        assert_eq!(settings.theme, "Dark");
        assert_eq!(settings.font_size, 14.0);
        assert_eq!(settings.tab_size, 2);
        assert_eq!(settings.wsl_mode, "auto");
        assert_eq!(settings.wsl_distro, "Ubuntu-24.04");
    }

    #[test]
    fn settings_save_atomically_replaces_an_existing_file() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("settings.json");
        fs::write(&path, "old contents").unwrap();
        let settings = Settings {
            theme: "Nord".into(),
            ..Settings::default()
        };
        save_to(&path, &settings).unwrap();
        let saved: Settings = serde_json::from_slice(&fs::read(path).unwrap()).unwrap();
        assert_eq!(saved.theme, "Nord");
    }

    #[test]
    fn invalid_settings_are_reported_and_preserved() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("settings.json");
        let original = b"{ invalid json";
        fs::write(&path, original).unwrap();

        let (settings, warning) = load_from(&path);

        assert_eq!(settings.theme, "Dark");
        assert!(warning.is_some());
        assert_eq!(fs::read(path).unwrap(), original);
    }
}
