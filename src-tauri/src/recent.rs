//! The "recently opened" list: at most eight files, most recent first, one
//! slot per file however it was reached.
//!
//! Files that no longer exist are left out when the list is shown but not
//! erased from it: a network drive that is unplugged right now should not wipe
//! the history.

use std::path::{Path, PathBuf};

use crate::settings::{MAX_RECENT, Settings};

/// True when two paths name the same file as far as the platform cares:
/// Windows paths are case-insensitive, the others are not.
pub fn same_path(a: &Path, b: &Path) -> bool {
    if cfg!(windows) {
        a.to_string_lossy().to_lowercase() == b.to_string_lossy().to_lowercase()
    } else {
        a == b
    }
}

/// The list as offered: entries that exist, in order.
pub fn list(settings: &Settings) -> Vec<PathBuf> {
    settings
        .recent_files
        .iter()
        .map(PathBuf::from)
        .filter(|p| p.is_file())
        .take(MAX_RECENT)
        .collect()
}

/// Puts `path` at the top, removing any other spelling of it.
pub fn push(settings: &mut Settings, path: &Path) {
    settings.recent_files.retain(|item| !same_path(Path::new(item), path));
    settings.recent_files.insert(0, path.to_string_lossy().into_owned());
    settings.recent_files.truncate(MAX_RECENT);
}

pub fn clear(settings: &mut Settings) {
    settings.recent_files.clear();
}

/// The entry the page asked for by position in the list it was shown.
pub fn pick(settings: &Settings, index: usize) -> Option<PathBuf> {
    list(settings).into_iter().nth(index)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::files::tests::TempDir;

    #[test]
    fn newest_first_without_duplicates_and_bounded() {
        let dir = TempDir::new("recent");
        let mut settings = Settings::default();
        let files: Vec<PathBuf> = (0..10).map(|i| dir.0.join(format!("{i}.json"))).collect();
        for f in &files {
            std::fs::write(f, "{}").unwrap();
            push(&mut settings, f);
        }
        push(&mut settings, &files[5]);
        let shown = list(&settings);
        assert_eq!(shown.len(), MAX_RECENT);
        assert_eq!(shown[0], files[5]);
        assert_eq!(shown[1], files[9]);
        assert_eq!(shown.iter().filter(|p| **p == files[5]).count(), 1);
    }

    #[test]
    fn missing_files_are_hidden_but_kept() {
        let dir = TempDir::new("recent-gone");
        let mut settings = Settings::default();
        let here = dir.0.join("here.json");
        std::fs::write(&here, "{}").unwrap();
        push(&mut settings, &dir.0.join("gone.json"));
        push(&mut settings, &here);
        assert_eq!(list(&settings), vec![here]);
        assert_eq!(settings.recent_files.len(), 2);
    }

    /// Security: an index from the page outside the list opens nothing.
    #[test]
    fn index_out_of_range_picks_nothing() {
        let dir = TempDir::new("recent-pick");
        let mut settings = Settings::default();
        let file = dir.0.join("a.json");
        std::fs::write(&file, "{}").unwrap();
        push(&mut settings, &file);
        assert_eq!(pick(&settings, 0), Some(file));
        assert_eq!(pick(&settings, 1), None);
        assert_eq!(pick(&settings, usize::MAX), None);
    }

    #[cfg(windows)]
    #[test]
    fn case_differences_are_one_file_on_windows() {
        assert!(same_path(Path::new(r"C:\Users\A\cv.json"), Path::new(r"c:\users\a\CV.JSON")));
    }
}
