//! Whole files in and out, with every path checked before it is used.
//!
//! Paths reach this module from native dialogs, from a drop on the window, from
//! the command line of a second launch, from the recent-files list and from the
//! recovery snapshot. None of those is trusted: each path must be absolute and
//! name a regular file of sensible size before a byte is read, and every write
//! goes to a temporary file beside the target that then replaces it, so a crash
//! or a full disk never leaves half a resume behind.

use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use crate::error::{AppError, AppResult};

// The messages earlier versions showed, word for word.
const CANNOT_OPEN: &str = "не удалось открыть файл";
const READ_FAILED: &str = "ошибка чтения файла";
const CANNOT_CREATE: &str = "не удалось создать файл";
const WRITE_FAILED: &str = "ошибка записи файла";

/// A resume is a few kilobytes; the largest sensible one is far below this.
/// Anything bigger is refused before it is read into memory.
pub const MAX_RESUME_BYTES: u64 = 16 * 1024 * 1024;

/// Checks that a path from outside is one this program may act on: absolute,
/// with a file name, and without `..` - a dialog or a drop never produces one,
/// so a path that has it did not come from either.
pub fn check_path(path: &Path) -> AppResult<()> {
    if !path.is_absolute() {
        return Err(AppError::validation("путь к файлу должен быть абсолютным"));
    }
    if path.components().any(|c| matches!(c, std::path::Component::ParentDir)) {
        return Err(AppError::validation("путь к файлу не должен содержать «..»"));
    }
    if path.file_name().is_none() {
        return Err(AppError::validation("в пути нет имени файла"));
    }
    Ok(())
}

/// Reads a file that has to be a regular file of at most `limit` bytes.
pub fn read_limited(path: &Path, limit: u64) -> AppResult<Vec<u8>> {
    check_path(path)?;
    let meta = fs::metadata(path).map_err(|_| AppError::io(CANNOT_OPEN))?;
    if !meta.is_file() {
        return Err(AppError::validation("это не файл"));
    }
    if meta.len() > limit {
        return Err(AppError::validation("файл слишком велик"));
    }
    let file = File::open(path).map_err(|_| AppError::io(CANNOT_OPEN))?;
    let mut out = Vec::with_capacity(meta.len() as usize);
    // take() as well as the size check: the file may grow between the two.
    file.take(limit + 1).read_to_end(&mut out).map_err(|_| AppError::io(READ_FAILED))?;
    if out.len() as u64 > limit {
        return Err(AppError::validation("файл слишком велик"));
    }
    Ok(out)
}

pub fn read_resume(path: &Path) -> AppResult<Vec<u8>> {
    read_limited(path, MAX_RESUME_BYTES)
}

static TEMP_COUNTER: AtomicU64 = AtomicU64::new(0);

/// Writes `data` to `path` atomically: a temporary file in the same directory,
/// flushed to disk, then renamed over the target. Either the old file or the
/// whole new one is there afterwards, never a mixture.
pub fn write_atomic(path: &Path, data: &[u8]) -> AppResult<()> {
    check_path(path)?;
    if path.is_dir() {
        return Err(AppError::validation("это каталог, а не файл"));
    }
    let dir = path.parent().ok_or_else(|| AppError::io(CANNOT_CREATE))?;
    let name = path.file_name().ok_or_else(|| AppError::io(CANNOT_CREATE))?;

    let unique = format!(
        ".{}.{}-{}.tmp",
        name.to_string_lossy(),
        std::process::id(),
        TEMP_COUNTER.fetch_add(1, Ordering::Relaxed)
    );
    let temp = dir.join(unique);

    let result = (|| {
        // create_new: never follow or reuse something already sitting there.
        let mut file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temp)
            .map_err(|_| AppError::io(CANNOT_CREATE))?;
        file.write_all(data).map_err(|_| AppError::io(WRITE_FAILED))?;
        file.sync_all().map_err(|_| AppError::io(WRITE_FAILED))?;
        drop(file);
        // On Windows this is MoveFileEx with REPLACE_EXISTING; on Unix rename(2).
        fs::rename(&temp, path).map_err(|_| AppError::io(WRITE_FAILED))
    })();

    if result.is_err() {
        let _ = fs::remove_file(&temp);
    }
    result
}

/// The path with `extension` added when it does not already end in it, so a
/// file saved as "cv" becomes "cv.json" and an export is always a ".pdf".
pub fn with_extension(path: PathBuf, extension: &str) -> PathBuf {
    let has = path
        .extension()
        .is_some_and(|e| e.to_string_lossy().eq_ignore_ascii_case(extension));
    if has {
        return path;
    }
    let mut text = path.into_os_string();
    text.push(".");
    text.push(extension);
    PathBuf::from(text)
}

/// The default file name for Save and Export: "Daniil Mishin" becomes
/// "Daniil_Mishin", with every character Windows forbids in a name removed.
pub fn suggested_name(person: &str, extension: &str) -> String {
    let mut base: String = person
        .chars()
        .filter(|c| !matches!(c, '\\' | '/' | ':' | '*' | '?' | '"' | '<' | '>' | '|'))
        .filter(|c| !c.is_control())
        .map(|c| if c == ' ' { '_' } else { c })
        .collect();
    // A name of dots only would be read as a directory reference.
    if base.chars().all(|c| c == '.' || c == '_') {
        base.clear();
    }
    if base.is_empty() {
        base = "cv".into();
    }
    format!("{base}.{extension}")
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// A fresh directory under the system temp dir, removed when dropped.
    pub struct TempDir(pub PathBuf);

    impl TempDir {
        pub fn new(tag: &str) -> TempDir {
            let dir = std::env::temp_dir().join(format!(
                "cvb-test-{tag}-{}-{}",
                std::process::id(),
                TEMP_COUNTER.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir_all(&dir).unwrap();
            TempDir(dir)
        }
    }

    impl Drop for TempDir {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    #[test]
    fn writes_and_replaces_atomically() {
        let dir = TempDir::new("atomic");
        let target = dir.0.join("cv.json");
        write_atomic(&target, b"first").unwrap();
        write_atomic(&target, b"second").unwrap();
        assert_eq!(fs::read(&target).unwrap(), b"second");
        // No temporary file is left beside it.
        let names: Vec<_> = fs::read_dir(&dir.0).unwrap().map(|e| e.unwrap().file_name()).collect();
        assert_eq!(names, vec![std::ffi::OsString::from("cv.json")]);
    }

    #[test]
    fn failed_write_leaves_nothing_behind() {
        let dir = TempDir::new("missing");
        let target = dir.0.join("no-such-dir").join("cv.json");
        assert_eq!(write_atomic(&target, b"x").unwrap_err(), AppError::io(CANNOT_CREATE));
        assert!(!target.exists());
    }

    /// Security: a relative path is refused for reading and writing.
    #[test]
    fn refuses_relative_paths() {
        let err = read_resume(Path::new("cv.json")).unwrap_err();
        assert!(matches!(err, AppError::Validation { .. }));
        let err = write_atomic(Path::new("out/cv.json"), b"x").unwrap_err();
        assert!(matches!(err, AppError::Validation { .. }));
    }

    /// Security: a path that climbs with ".." is refused even when absolute.
    #[test]
    fn refuses_parent_components() {
        let dir = TempDir::new("dotdot");
        let sneaky = dir.0.join("..").join("cv.json");
        assert!(matches!(read_resume(&sneaky).unwrap_err(), AppError::Validation { .. }));
        assert!(matches!(write_atomic(&sneaky, b"x").unwrap_err(), AppError::Validation { .. }));
    }

    /// Security: a directory is not read as a resume, and not written over.
    #[test]
    fn refuses_directories() {
        let dir = TempDir::new("dir");
        assert_eq!(read_resume(&dir.0).unwrap_err(), AppError::validation("это не файл"));
        assert!(matches!(write_atomic(&dir.0, b"x").unwrap_err(), AppError::Validation { .. }));
    }

    /// Security: a file over the limit is refused before it is read.
    #[test]
    fn refuses_oversized_files() {
        let dir = TempDir::new("big");
        let path = dir.0.join("big.json");
        fs::write(&path, vec![b' '; 2048]).unwrap();
        assert_eq!(read_limited(&path, 1024).unwrap_err(), AppError::validation("файл слишком велик"));
        assert_eq!(read_limited(&path, 4096).unwrap().len(), 2048);
    }

    #[test]
    fn missing_file_reports_the_old_message() {
        let dir = TempDir::new("gone");
        assert_eq!(read_resume(&dir.0.join("gone.json")).unwrap_err(), AppError::io(CANNOT_OPEN));
    }

    #[test]
    fn adds_the_extension_once() {
        assert_eq!(with_extension(PathBuf::from("/a/cv"), "json"), PathBuf::from("/a/cv.json"));
        assert_eq!(with_extension(PathBuf::from("/a/cv.JSON"), "json"), PathBuf::from("/a/cv.JSON"));
        assert_eq!(with_extension(PathBuf::from("/a/cv.json"), "pdf"), PathBuf::from("/a/cv.json.pdf"));
    }

    /// Security: the suggested file name cannot carry a path or a forbidden
    /// character, whatever the person's name field holds.
    #[test]
    fn suggested_names_are_plain_file_names() {
        assert_eq!(suggested_name("Daniil Mishin", "json"), "Daniil_Mishin.json");
        assert_eq!(suggested_name("Даниил Мишин", "pdf"), "Даниил_Мишин.pdf");
        assert_eq!(suggested_name("../../etc/passwd", "pdf"), "....etcpasswd.pdf");
        assert_eq!(suggested_name("a\\b:c*?\"<>|", "pdf"), "abc.pdf");
        assert_eq!(suggested_name("", "json"), "cv.json");
        assert_eq!(suggested_name("..", "json"), "cv.json");
        assert_eq!(suggested_name("x\ny", "json"), "xy.json");
    }
}
