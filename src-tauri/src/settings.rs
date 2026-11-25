//! The handful of things the program remembers between runs, kept as
//! `settings.json` in the application's data directory.
//!
//! Read tolerantly - a value of the wrong type falls back to its default
//! without taking the others with it - and written atomically. Nothing secret
//! is ever stored here.
//!
//! On the first run, when there is no settings.json yet, the theme and the
//! recent-files list of an earlier version are imported once, read-only: from
//! `HKCU\Software\CV Builder` on Windows, from its settings.ini elsewhere.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::error::AppResult;
use crate::files;

pub const FILE_NAME: &str = "settings.json";

/// More than enough for a theme and eight paths; a larger file is not ours.
const MAX_SETTINGS_BYTES: u64 = 1024 * 1024;

/// How many recent files are kept, as before.
pub const MAX_RECENT: usize = 8;

/// No path is longer than this on any system the program runs on.
const MAX_PATH_CHARS: usize = 4096;

/// Which palette the interface uses. System follows the desktop's setting.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum ThemeMode {
    #[default]
    System,
    Light,
    Dark,
}

impl ThemeMode {
    /// The number earlier versions stored: 0 system, 1 light, 2 dark.
    fn from_legacy(value: i64) -> Option<ThemeMode> {
        match value {
            0 => Some(ThemeMode::System),
            1 => Some(ThemeMode::Light),
            2 => Some(ThemeMode::Dark),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Settings {
    pub theme_mode: ThemeMode,
    /// Absolute paths, most recent first.
    pub recent_files: Vec<String>,
    /// The file the crash-recovery snapshot belongs to, if it was ever saved.
    pub autosave_origin: Option<String>,
}

fn acceptable_path(text: &str) -> bool {
    !text.is_empty() && text.chars().count() <= MAX_PATH_CHARS && files::check_path(Path::new(text)).is_ok()
}

impl Settings {
    /// Parses settings.json, keeping every field that makes sense and dropping
    /// the ones that do not.
    pub fn from_json(bytes: &[u8]) -> Settings {
        let mut out = Settings::default();
        let Ok(value) = serde_json::from_slice::<serde_json::Value>(bytes) else { return out };
        if let Some(mode) = value.get("themeMode").and_then(|v| serde_json::from_value(v.clone()).ok()) {
            out.theme_mode = mode;
        }
        if let Some(items) = value.get("recentFiles").and_then(|v| v.as_array()) {
            out.recent_files = items
                .iter()
                .filter_map(|v| v.as_str())
                .filter(|s| acceptable_path(s))
                .take(MAX_RECENT)
                .map(str::to_string)
                .collect();
        }
        out.autosave_origin = value
            .get("autosaveOrigin")
            .and_then(|v| v.as_str())
            .filter(|s| acceptable_path(s))
            .map(str::to_string);
        out
    }

    pub fn to_json(&self) -> String {
        let mut text = serde_json::to_string_pretty(self).expect("settings always serialise");
        text.push('\n');
        text
    }

    /// Reads the settings from `dir`, importing an earlier version's on the
    /// very first run. Never fails: missing or broken settings are defaults.
    pub fn load(dir: &Path) -> Settings {
        let path = dir.join(FILE_NAME);
        match files::read_limited(&path, MAX_SETTINGS_BYTES) {
            Ok(bytes) => Settings::from_json(&bytes),
            Err(_) if !path.exists() => {
                let imported = legacy::import().map(Settings::from_legacy).unwrap_or_default();
                // Written straight away, so the import happens once.
                let _ = imported.save(dir);
                imported
            }
            Err(_) => Settings::default(),
        }
    }

    pub fn save(&self, dir: &Path) -> AppResult<()> {
        files::write_atomic(&dir.join(FILE_NAME), self.to_json().as_bytes())
    }

    fn from_legacy(old: legacy::Legacy) -> Settings {
        Settings {
            theme_mode: old.theme_mode.and_then(ThemeMode::from_legacy).unwrap_or_default(),
            recent_files: old.recent_files.into_iter().filter(|s| acceptable_path(s)).take(MAX_RECENT).collect(),
            autosave_origin: None,
        }
    }
}

/// What an earlier version left behind, as it stored it.
mod legacy {
    #[derive(Debug, Default, PartialEq, Eq)]
    pub struct Legacy {
        pub theme_mode: Option<i64>,
        pub recent_files: Vec<String>,
    }

    pub const MODE_VALUE: &str = "ThemeMode";
    pub const RECENT_VALUE: &str = "RecentFiles";

    /// The ini format of the Linux and macOS builds: `key=value` lines,
    /// comments and sections skipped, lists as `RecentFiles.1`, `.2`, ... up to
    /// the first gap.
    #[cfg_attr(windows, allow(dead_code))]
    pub fn parse_ini(text: &str) -> Legacy {
        let mut table = std::collections::BTreeMap::new();
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with(['#', ';', '[']) {
                continue;
            }
            if let Some((key, value)) = line.split_once('=') {
                table.insert(key.trim().to_string(), value.trim().to_string());
            }
        }
        let theme_mode = table
            .get(MODE_VALUE)
            .and_then(|v| cvb_core::numfmt::parse(v))
            .map(|(n, _)| n as i64);
        let mut recent_files = Vec::new();
        for i in 1.. {
            match table.get(&format!("{RECENT_VALUE}.{i}")) {
                Some(v) if !v.is_empty() => recent_files.push(v.clone()),
                _ => break,
            }
        }
        Legacy { theme_mode, recent_files }
    }

    #[cfg(windows)]
    pub fn import() -> Option<Legacy> {
        registry::read()
    }

    #[cfg(not(windows))]
    pub fn import() -> Option<Legacy> {
        let path = ini_path()?;
        let bytes = crate::files::read_limited(&path, 1024 * 1024).ok()?;
        Some(parse_ini(&String::from_utf8_lossy(&bytes)))
    }

    #[cfg(target_os = "macos")]
    fn ini_path() -> Option<std::path::PathBuf> {
        let home = std::env::var_os("HOME")?;
        Some(std::path::PathBuf::from(home).join("Library/Preferences/CV Builder/settings.ini"))
    }

    #[cfg(all(unix, not(target_os = "macos")))]
    fn ini_path() -> Option<std::path::PathBuf> {
        use std::path::PathBuf;
        let base = match std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from) {
            Some(xdg) if xdg.is_absolute() => xdg,
            _ => PathBuf::from(std::env::var_os("HOME")?).join(".config"),
        };
        Some(base.join("cv-builder/settings.ini"))
    }

    /// Read-only access to the key the Win32 and Qt builds wrote.
    #[cfg(windows)]
    mod registry {
        use super::{Legacy, MODE_VALUE, RECENT_VALUE};
        use windows_sys::Win32::Foundation::ERROR_SUCCESS;
        use windows_sys::Win32::System::Registry::{HKEY_CURRENT_USER, RRF_RT_REG_DWORD, RRF_RT_REG_MULTI_SZ, RegGetValueW};

        fn wide(text: &str) -> Vec<u16> {
            text.encode_utf16().chain(std::iter::once(0)).collect()
        }

        const KEY: &str = "Software\\CV Builder";

        fn read_dword(name: &str) -> Option<u32> {
            let (key, value) = (wide(KEY), wide(name));
            let mut data: u32 = 0;
            let mut size = std::mem::size_of::<u32>() as u32;
            // SAFETY: both names are NUL-terminated UTF-16, and `data`/`size`
            // describe a u32 that lives for the duration of the call.
            let status = unsafe {
                RegGetValueW(
                    HKEY_CURRENT_USER,
                    key.as_ptr(),
                    value.as_ptr(),
                    RRF_RT_REG_DWORD,
                    std::ptr::null_mut(),
                    (&mut data as *mut u32).cast(),
                    &mut size,
                )
            };
            (status == ERROR_SUCCESS).then_some(data)
        }

        fn read_multi_sz(name: &str) -> Vec<String> {
            let (key, value) = (wide(KEY), wide(name));
            let mut size: u32 = 0;
            // SAFETY: a size query - no buffer is passed, only `size` is written.
            let status = unsafe {
                RegGetValueW(
                    HKEY_CURRENT_USER,
                    key.as_ptr(),
                    value.as_ptr(),
                    RRF_RT_REG_MULTI_SZ,
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                    &mut size,
                )
            };
            // A recent-files list is a few kilobytes; refuse anything absurd.
            if status != ERROR_SUCCESS || !(2..=256 * 1024).contains(&size) {
                return Vec::new();
            }
            let mut buffer = vec![0u16; size as usize / 2 + 1];
            let mut bytes = (buffer.len() * 2) as u32;
            // SAFETY: `buffer` holds `bytes` bytes and outlives the call.
            let status = unsafe {
                RegGetValueW(
                    HKEY_CURRENT_USER,
                    key.as_ptr(),
                    value.as_ptr(),
                    RRF_RT_REG_MULTI_SZ,
                    std::ptr::null_mut(),
                    buffer.as_mut_ptr().cast(),
                    &mut bytes,
                )
            };
            if status != ERROR_SUCCESS {
                return Vec::new();
            }
            buffer
                .split(|&c| c == 0)
                .take_while(|s| !s.is_empty())
                .map(String::from_utf16_lossy)
                .collect()
        }

        pub fn read() -> Option<Legacy> {
            let theme_mode = read_dword(MODE_VALUE).map(i64::from);
            let recent_files = read_multi_sz(RECENT_VALUE);
            if theme_mode.is_none() && recent_files.is_empty() {
                return None;
            }
            Some(Legacy { theme_mode, recent_files })
        }
    }
}

/// Where a save dialog starts when the document has never been saved.
pub fn start_directory(current: Option<&Path>, documents: Option<&Path>) -> Option<PathBuf> {
    current.and_then(Path::parent).or(documents).map(Path::to_path_buf)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::files::tests::TempDir;

    fn abs(name: &str) -> String {
        std::env::temp_dir().join(name).to_string_lossy().into_owned()
    }

    #[test]
    fn round_trips() {
        let dir = TempDir::new("settings");
        let settings = Settings {
            theme_mode: ThemeMode::Dark,
            recent_files: vec![abs("a.json"), abs("б.json")],
            autosave_origin: Some(abs("a.json")),
        };
        settings.save(&dir.0).unwrap();
        assert_eq!(Settings::load(&dir.0), settings);
    }

    #[test]
    fn a_bad_field_does_not_take_the_others_with_it() {
        let a = abs("a.json");
        let text = format!(r#"{{"themeMode":"purple","recentFiles":[1,"{}",null],"autosaveOrigin":7}}"#, a.replace('\\', "\\\\"));
        let s = Settings::from_json(text.as_bytes());
        assert_eq!(s.theme_mode, ThemeMode::System);
        assert_eq!(s.recent_files, vec![a]);
        assert_eq!(s.autosave_origin, None);
        assert_eq!(Settings::from_json(b"not json"), Settings::default());
    }

    /// Security: a settings file that names relative paths, paths climbing
    /// with "..", or more entries than the list holds, is cut back on reading.
    #[test]
    fn untrusted_paths_in_settings_are_dropped() {
        let many: Vec<String> = (0..50).map(|i| abs(&format!("{i}.json"))).collect();
        let mut list: Vec<serde_json::Value> = vec!["relative.json".into(), abs("x/../y.json").into(), "".into()];
        list.extend(many.iter().cloned().map(Into::into));
        let text = serde_json::json!({ "recentFiles": list, "autosaveOrigin": "cv.json" }).to_string();
        let s = Settings::from_json(text.as_bytes());
        assert_eq!(s.recent_files, many[..MAX_RECENT].to_vec());
        assert_eq!(s.autosave_origin, None);
    }

    #[test]
    fn reads_the_old_ini_format() {
        let ini = "# CV Builder settings\n[main]\nThemeMode = 2\nRecentFiles.1=/home/u/a.json\n\
                   RecentFiles.2=/home/u/b.json\nRecentFiles.4=/home/u/gap.json\n";
        let old = legacy::parse_ini(ini);
        assert_eq!(old.theme_mode, Some(2));
        assert_eq!(old.recent_files, vec!["/home/u/a.json", "/home/u/b.json"]);
    }

    #[test]
    fn legacy_values_are_checked_like_any_other() {
        let s = Settings::from_legacy(legacy::Legacy {
            theme_mode: Some(7),
            recent_files: vec!["relative.json".into(), abs("ok.json")],
        });
        assert_eq!(s.theme_mode, ThemeMode::System);
        assert_eq!(s.recent_files, vec![abs("ok.json")]);
    }
}
