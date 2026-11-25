//! The crash-recovery snapshot: written every half minute while there are
//! unsaved changes, removed as soon as the work is saved or the window closes
//! on purpose.
//!
//! The snapshot is an ordinary resume file, `autosave.json`, in the data
//! directory; which file it belongs to is kept in the settings. A snapshot
//! older than that file means the work was saved normally afterwards, and it is
//! deleted rather than offered - restoring it would talk the user into
//! overwriting good work with worse.

use std::path::{Path, PathBuf};

use cvb_core::model::{self, Cv};

use crate::error::AppResult;
use crate::files;
use crate::settings::Settings;

pub const FILE_NAME: &str = "autosave.json";

pub struct Recovery {
    pub cv: Cv,
    /// The file the work belonged to; None if it was never saved.
    pub origin: Option<PathBuf>,
}

pub fn write(dir: &Path, settings: &mut Settings, cv: &Cv, origin: Option<&Path>) -> AppResult<()> {
    files::write_atomic(&dir.join(FILE_NAME), model::to_json(cv).as_bytes())?;
    settings.autosave_origin = origin.map(|p| p.to_string_lossy().into_owned());
    settings.save(dir)
}

pub fn clear(dir: &Path, settings: &mut Settings) {
    let _ = std::fs::remove_file(dir.join(FILE_NAME));
    if settings.autosave_origin.take().is_some() {
        let _ = settings.save(dir);
    }
}

/// The snapshot left by a session that ended without saving, if there is one
/// worth offering.
pub fn find(dir: &Path, settings: &mut Settings) -> Option<Recovery> {
    let path = dir.join(FILE_NAME);
    let snapshot = std::fs::metadata(&path).and_then(|m| m.modified()).ok()?;

    // The origin came from a file on disk, so it is checked like any outside
    // path: settings.rs already dropped one that is not absolute.
    let origin = settings.autosave_origin.as_deref().map(PathBuf::from);
    if let Some(origin) = &origin
        && let Ok(saved) = std::fs::metadata(origin).and_then(|m| m.modified())
        && snapshot <= saved
    {
        clear(dir, settings);
        return None;
    }

    let bytes = files::read_resume(&path).ok()?;
    let cv = model::from_json(&bytes).ok()?;
    Some(Recovery { cv, origin })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::files::tests::TempDir;
    use std::time::{Duration, SystemTime};

    fn touch(path: &Path, when: SystemTime) {
        std::fs::File::options().write(true).open(path).unwrap().set_modified(when).unwrap();
    }

    #[test]
    fn offers_unsaved_work() {
        let dir = TempDir::new("recovery");
        let mut settings = Settings::default();
        let cv = Cv { name: "Unsaved".into(), ..Cv::default() };
        write(&dir.0, &mut settings, &cv, None).unwrap();
        let found = find(&dir.0, &mut settings).unwrap();
        assert_eq!(found.cv, cv);
        assert!(found.origin.is_none());
    }

    #[test]
    fn a_snapshot_older_than_its_file_is_dropped() {
        let dir = TempDir::new("recovery-old");
        let mut settings = Settings::default();
        let origin = dir.0.join("cv.json");
        std::fs::write(&origin, "{}").unwrap();
        write(&dir.0, &mut settings, &Cv::default(), Some(&origin)).unwrap();
        let now = SystemTime::now();
        touch(&dir.0.join(FILE_NAME), now - Duration::from_secs(60));
        touch(&origin, now);
        assert!(find(&dir.0, &mut settings).is_none());
        assert!(!dir.0.join(FILE_NAME).exists());
        assert_eq!(settings.autosave_origin, None);
    }

    #[test]
    fn a_newer_snapshot_is_offered_with_its_origin() {
        let dir = TempDir::new("recovery-new");
        let mut settings = Settings::default();
        let origin = dir.0.join("cv.json");
        std::fs::write(&origin, "{}").unwrap();
        write(&dir.0, &mut settings, &Cv::default(), Some(&origin)).unwrap();
        let now = SystemTime::now();
        touch(&origin, now - Duration::from_secs(60));
        touch(&dir.0.join(FILE_NAME), now);
        assert_eq!(find(&dir.0, &mut settings).unwrap().origin, Some(origin));
    }

    #[test]
    fn clear_removes_everything() {
        let dir = TempDir::new("recovery-clear");
        let mut settings = Settings::default();
        write(&dir.0, &mut settings, &Cv::default(), Some(&dir.0.join("x.json"))).unwrap();
        clear(&dir.0, &mut settings);
        assert!(find(&dir.0, &mut settings).is_none());
        assert_eq!(Settings::load(&dir.0).autosave_origin, None);
    }

    /// Security: a damaged snapshot is not offered, and does not crash.
    #[test]
    fn a_broken_snapshot_is_ignored() {
        let dir = TempDir::new("recovery-broken");
        let mut settings = Settings::default();
        std::fs::write(dir.0.join(FILE_NAME), "{\"name\": ").unwrap();
        assert!(find(&dir.0, &mut settings).is_none());
    }
}
