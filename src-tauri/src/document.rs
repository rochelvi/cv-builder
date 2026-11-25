//! What the toolbar does: new, open, save, export, and what happens at start
//! and at close.
//!
//! The flows of the earlier versions, step for step and with their messages:
//! unsaved changes are asked about before anything replaces them, a crashed
//! session is offered back before the sample is loaded, a save clears the
//! recovery snapshot, an export opens in the system viewer.
//!
//! Dialogs block, so no flow holds the workspace lock while one is open: state
//! is read, the lock dropped, the question asked, and the lock taken again to
//! record the answer.

use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard};

use serde::Serialize;

use cvb_core::font::FontSet;
use cvb_core::layout::layout;
use cvb_core::model::{self, Cv};
use cvb_core::pdf::build_pdf;

use crate::dialogs::{SaveChoice, Ui};
use crate::error::{AppError, AppResult};
use crate::files;
use crate::recent;
use crate::recovery;
use crate::session::Session;
use crate::settings::{self, Settings};

/// Everything the application keeps between commands.
pub struct Workspace {
    pub data_dir: PathBuf,
    pub documents_dir: Option<PathBuf>,
    pub settings: Settings,
    pub session: Session,
}

impl Workspace {
    pub fn new(data_dir: PathBuf, documents_dir: Option<PathBuf>) -> Workspace {
        let settings = Settings::load(&data_dir);
        Workspace { data_dir, documents_dir, settings, session: Session::default() }
    }

    fn start_directory(&self) -> Option<PathBuf> {
        settings::start_directory(self.session.path.as_deref(), self.documents_dir.as_deref())
    }

    /// Records `path` at the top of the recent list and stores the list.
    fn remember(&mut self, path: &Path) {
        recent::push(&mut self.settings, path);
        let _ = self.settings.save(&self.data_dir);
    }
}

/// A document the page should now show.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Opened {
    pub cv: Cv,
    /// For the status line.
    pub status: String,
    /// True for restored work, which is unsaved by definition.
    pub dirty: bool,
}

/// The sample every earlier version started with when nothing else was open.
const SAMPLE: &[u8] = include_bytes!("../../sample_cv.json");

fn lock(ws: &Mutex<Workspace>) -> MutexGuard<'_, Workspace> {
    // A panic aborts the process in release, so a poisoned lock only happens in
    // a test that already failed; the data is still the best there is.
    ws.lock().unwrap_or_else(|e| e.into_inner())
}

/// What the page sends is checked before it is used: its size bounded and its
/// running order repaired to exactly the known sections.
pub fn accept(cv: Cv) -> AppResult<Cv> {
    if model::to_json(&cv).len() as u64 > files::MAX_RESUME_BYTES {
        return Err(AppError::validation("резюме слишком велико"));
    }
    Ok(cv.repaired())
}

fn display(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

/// Puts a document in the window: its file, a clean state, and the title.
fn adopt(ws: &Mutex<Workspace>, ui: &dyn Ui, path: Option<PathBuf>, dirty: bool) {
    let mut w = lock(ws);
    w.session.path = path;
    w.session.dirty = dirty;
    ui.set_title(&w.session.title());
}

pub fn set_dirty(ws: &Mutex<Workspace>, ui: &dyn Ui, dirty: bool) {
    let mut w = lock(ws);
    if w.session.dirty != dirty {
        w.session.dirty = dirty;
        ui.set_title(&w.session.title());
    }
}

/// Asks about unsaved changes. True means go ahead: nothing to lose, the user
/// chose not to keep it, or it was saved.
pub fn confirm_discard(ws: &Mutex<Workspace>, ui: &dyn Ui, cv: &Cv) -> AppResult<bool> {
    if !lock(ws).session.dirty {
        return Ok(true);
    }
    match ui.ask_save_changes() {
        SaveChoice::Cancel => Ok(false),
        SaveChoice::Discard => Ok(true),
        SaveChoice::Save => Ok(save(ws, ui, cv)?.is_some()),
    }
}

/// Reads a resume file and makes it the document.
pub fn open_path(ws: &Mutex<Workspace>, ui: &dyn Ui, path: &Path) -> AppResult<Opened> {
    let bytes = files::read_resume(path)?;
    let cv = model::from_json(&bytes).map_err(AppError::json)?;
    lock(ws).remember(path);
    adopt(ws, ui, Some(path.to_path_buf()), false);
    Ok(Opened { cv, status: format!("Открыто: {}", display(path)), dirty: false })
}

pub fn new_document(ws: &Mutex<Workspace>, ui: &dyn Ui, current: &Cv) -> AppResult<Option<Opened>> {
    if !confirm_discard(ws, ui, current)? {
        return Ok(None);
    }
    adopt(ws, ui, None, false);
    Ok(Some(Opened { cv: Cv::new_document(), status: "Новое резюме".into(), dirty: false }))
}

pub fn open_dialog(ws: &Mutex<Workspace>, ui: &dyn Ui, current: &Cv) -> AppResult<Option<Opened>> {
    if !confirm_discard(ws, ui, current)? {
        return Ok(None);
    }
    let start = lock(ws).start_directory();
    let Some(path) = ui.pick_resume_to_open(start.as_deref()) else { return Ok(None) };
    files::check_path(&path)?;
    open_path(ws, ui, &path).map(Some)
}

/// Opens entry `index` of the recent list as the page last showed it.
pub fn open_recent(ws: &Mutex<Workspace>, ui: &dyn Ui, index: usize, current: &Cv) -> AppResult<Option<Opened>> {
    let path = recent::pick(&lock(ws).settings, index)
        .ok_or_else(|| AppError::validation("этого файла больше нет в списке"))?;
    if !confirm_discard(ws, ui, current)? {
        return Ok(None);
    }
    open_path(ws, ui, &path).map(Some)
}

/// Opens what was dropped on the window or handed over by a second launch.
pub fn open_pending(ws: &Mutex<Workspace>, ui: &dyn Ui, current: &Cv) -> AppResult<Option<Opened>> {
    let pending = std::mem::take(&mut lock(ws).session.pending);
    let Some(first) = pending.first() else { return Ok(None) };
    if !confirm_discard(ws, ui, current)? {
        return Ok(None);
    }
    let mut opened = open_path(ws, ui, first)?;
    // Only one resume is open at a time; say so rather than quietly ignore the
    // rest of a multiple selection.
    if pending.len() > 1 {
        opened.status.push_str(&format!(" (перетащено файлов: {}, открыт первый)", pending.len()));
    }
    Ok(Some(opened))
}

/// Saves to the current file, or asks for one. None when the user cancelled.
pub fn save(ws: &Mutex<Workspace>, ui: &dyn Ui, cv: &Cv) -> AppResult<Option<String>> {
    let Some(path) = lock(ws).session.path.clone() else { return save_as(ws, ui, cv) };
    write_resume(ws, ui, cv, &path).map(Some)
}

pub fn save_as(ws: &Mutex<Workspace>, ui: &dyn Ui, cv: &Cv) -> AppResult<Option<String>> {
    let start = lock(ws).start_directory();
    let name = files::suggested_name(&cv.name, "json");
    let Some(chosen) = ui.pick_resume_to_save(start.as_deref(), &name) else { return Ok(None) };
    let path = files::with_extension(chosen, "json");
    write_resume(ws, ui, cv, &path).map(Some)
}

fn write_resume(ws: &Mutex<Workspace>, ui: &dyn Ui, cv: &Cv, path: &Path) -> AppResult<String> {
    files::write_atomic(path, model::to_json(cv).as_bytes())?;
    {
        let mut w = lock(ws);
        w.remember(path);
        // The work is on disk now, so the snapshot has nothing left to rescue
        // and must not be offered on the next start.
        let Workspace { data_dir, settings, .. } = &mut *w;
        recovery::clear(data_dir, settings);
    }
    adopt(ws, ui, Some(path.to_path_buf()), false);
    Ok(format!("Сохранено: {}", display(path)))
}

/// Writes the PDF and opens it in the system viewer. None when cancelled.
pub fn export_pdf(ws: &Mutex<Workspace>, ui: &dyn Ui, fonts: &FontSet, cv: &Cv) -> AppResult<Option<String>> {
    let start = lock(ws).start_directory();
    let name = files::suggested_name(&cv.name, "pdf");
    let Some(chosen) = ui.pick_pdf_to_save(start.as_deref(), &name) else { return Ok(None) };
    let path = files::with_extension(chosen, "pdf");
    let pdf = build_pdf(&layout(cv, fonts), fonts).map_err(AppError::pdf)?;
    files::write_atomic(&path, &pdf)?;
    ui.open_externally(&path);
    Ok(Some(format!("Экспортировано: {}", display(&path))))
}

/// The first document of a run: the file on the command line, else work a
/// crashed session left, else the sample, else a blank resume.
pub fn startup(ws: &Mutex<Workspace>, ui: &dyn Ui) -> Opened {
    let startup_file = lock(ws).session.startup_file.take();
    if let Some(path) = startup_file {
        match open_path(ws, ui, &path) {
            Ok(opened) => return opened,
            Err(e) => ui.show_error(&format!("Не удалось открыть файл:\n{e}")),
        }
    }

    // Unsaved work outranks anything else, so it is offered before the sample.
    let found = {
        let mut w = lock(ws);
        let Workspace { data_dir, settings, .. } = &mut *w;
        recovery::find(data_dir, settings)
    };
    if let Some(found) = found {
        let what = found
            .origin
            .as_ref()
            .and_then(|p| p.file_name())
            .map_or_else(|| "несохранённое резюме".to_string(), |n| n.to_string_lossy().into_owned());
        if ui.ask_restore(&what) {
            adopt(ws, ui, found.origin, true);
            return Opened { cv: found.cv, status: "Восстановлено из автосохранения".into(), dirty: true };
        }
        let mut w = lock(ws);
        let Workspace { data_dir, settings, .. } = &mut *w;
        recovery::clear(data_dir, settings);
    }

    adopt(ws, ui, None, false);
    match model::from_json(SAMPLE) {
        Ok(cv) => Opened { cv, status: "Загружен пример sample_cv.json".into(), dirty: false },
        Err(_) => Opened { cv: Cv::new_document(), status: "Готово".into(), dirty: false },
    }
}

/// The half-minute snapshot. Only unsaved work is worth one.
pub fn write_recovery(ws: &Mutex<Workspace>, cv: &Cv) -> AppResult<()> {
    let mut w = lock(ws);
    if !w.session.dirty {
        return Ok(());
    }
    let origin = w.session.path.clone();
    let Workspace { data_dir, settings, .. } = &mut *w;
    recovery::write(data_dir, settings, cv, origin.as_deref())
}

/// Asks about unsaved changes before the window closes. True means close.
pub fn close(ws: &Mutex<Workspace>, ui: &dyn Ui, cv: &Cv) -> AppResult<bool> {
    if !confirm_discard(ws, ui, cv)? {
        return Ok(false);
    }
    // Closing on purpose - saved, or knowingly discarded - leaves nothing to
    // recover on the next start.
    let mut w = lock(ws);
    let Workspace { data_dir, settings, .. } = &mut *w;
    recovery::clear(data_dir, settings);
    Ok(true)
}

/// Accepts paths that arrived from outside - a drop, a second launch - for the
/// page to confirm. Returns how many were kept.
pub fn queue_pending(ws: &Mutex<Workspace>, paths: Vec<PathBuf>) -> usize {
    let accepted: Vec<PathBuf> = paths.into_iter().filter(|p| files::check_path(p).is_ok()).collect();
    let count = accepted.len();
    if count > 0 {
        lock(ws).session.pending = accepted;
    }
    count
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::files::tests::TempDir;
    use std::cell::{Cell, RefCell};

    /// Answers every question the way the test says and records the rest.
    #[derive(Default)]
    struct FakeUi {
        save_choice: Cell<Option<SaveChoice>>,
        restore: Cell<bool>,
        pick: RefCell<Option<PathBuf>>,
        asked_save: Cell<u32>,
        errors: RefCell<Vec<String>>,
        opened: RefCell<Vec<PathBuf>>,
        title: RefCell<String>,
    }

    impl Ui for FakeUi {
        fn ask_save_changes(&self) -> SaveChoice {
            self.asked_save.set(self.asked_save.get() + 1);
            self.save_choice.get().expect("unexpected question")
        }
        fn ask_restore(&self, _: &str) -> bool {
            self.restore.get()
        }
        fn show_error(&self, text: &str) {
            self.errors.borrow_mut().push(text.into());
        }
        fn pick_resume_to_open(&self, _: Option<&Path>) -> Option<PathBuf> {
            self.pick.borrow().clone()
        }
        fn pick_resume_to_save(&self, _: Option<&Path>, _: &str) -> Option<PathBuf> {
            self.pick.borrow().clone()
        }
        fn pick_pdf_to_save(&self, _: Option<&Path>, _: &str) -> Option<PathBuf> {
            self.pick.borrow().clone()
        }
        fn open_externally(&self, path: &Path) {
            self.opened.borrow_mut().push(path.into());
        }
        fn set_title(&self, title: &str) {
            *self.title.borrow_mut() = title.into();
        }
    }

    fn workspace(dir: &TempDir) -> Mutex<Workspace> {
        let data = dir.0.join("data");
        std::fs::create_dir_all(&data).unwrap();
        // An empty settings file, so the tests never import this machine's own.
        std::fs::write(data.join(settings::FILE_NAME), "{}").unwrap();
        Mutex::new(Workspace::new(data, Some(dir.0.clone())))
    }

    #[test]
    fn startup_falls_back_to_the_sample() {
        let dir = TempDir::new("doc-start");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        let opened = startup(&ws, &ui);
        assert_eq!(opened.status, "Загружен пример sample_cv.json");
        assert_eq!(opened.cv.role, "SYSTEM ADMINISTRATOR");
        assert_eq!(*ui.title.borrow(), "CV Builder");
    }

    #[test]
    fn startup_opens_the_command_line_file_or_reports_it() {
        let dir = TempDir::new("doc-argv");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        let file = dir.0.join("mine.json");
        std::fs::write(&file, r#"{"name":"Mine"}"#).unwrap();
        lock(&ws).session.startup_file = Some(file.clone());
        let opened = startup(&ws, &ui);
        assert_eq!(opened.cv.name, "Mine");
        assert_eq!(*ui.title.borrow(), "CV Builder — mine.json");
        assert_eq!(lock(&ws).settings.recent_files, vec![file.to_string_lossy().into_owned()]);

        lock(&ws).session.startup_file = Some(dir.0.join("broken.json"));
        let opened = startup(&ws, &ui);
        assert_eq!(ui.errors.borrow().len(), 1);
        assert!(ui.errors.borrow()[0].starts_with("Не удалось открыть файл:\n"));
        assert_eq!(opened.status, "Загружен пример sample_cv.json");
    }

    #[test]
    fn startup_offers_a_crashed_session_first() {
        let dir = TempDir::new("doc-recover");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        let lost = Cv { name: "Lost work".into(), ..Cv::default() };
        lock(&ws).session.dirty = true;
        write_recovery(&ws, &lost).unwrap();

        ui.restore.set(true);
        let opened = startup(&ws, &ui);
        assert_eq!(opened.cv.name, "Lost work");
        assert!(opened.dirty);
        assert_eq!(*ui.title.borrow(), "CV Builder *");
    }

    #[test]
    fn declining_recovery_deletes_the_snapshot() {
        let dir = TempDir::new("doc-decline");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        lock(&ws).session.dirty = true;
        write_recovery(&ws, &Cv::default()).unwrap();
        ui.restore.set(false);
        assert_eq!(startup(&ws, &ui).status, "Загружен пример sample_cv.json");
        assert!(!lock(&ws).data_dir.join(recovery::FILE_NAME).exists());
    }

    #[test]
    fn unsaved_changes_are_asked_about_and_cancel_stops() {
        let dir = TempDir::new("doc-confirm");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        let cv = Cv::default();
        assert!(new_document(&ws, &ui, &cv).unwrap().is_some());
        assert_eq!(ui.asked_save.get(), 0);

        lock(&ws).session.dirty = true;
        ui.save_choice.set(Some(SaveChoice::Cancel));
        assert!(new_document(&ws, &ui, &cv).unwrap().is_none());
        ui.save_choice.set(Some(SaveChoice::Discard));
        assert_eq!(new_document(&ws, &ui, &cv).unwrap().unwrap().cv, Cv::new_document());
        assert_eq!(ui.asked_save.get(), 2);
    }

    #[test]
    fn choosing_save_saves_first_and_a_cancelled_save_stops() {
        let dir = TempDir::new("doc-save-first");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        let cv = Cv { name: "Keep me".into(), ..Cv::default() };
        lock(&ws).session.dirty = true;
        ui.save_choice.set(Some(SaveChoice::Save));
        // No file yet, and the save dialog is cancelled: nothing happens.
        assert!(new_document(&ws, &ui, &cv).unwrap().is_none());
        // Now a file is chosen: saved, then replaced.
        let target = dir.0.join("kept");
        *ui.pick.borrow_mut() = Some(target.clone());
        assert!(new_document(&ws, &ui, &cv).unwrap().is_some());
        let saved = std::fs::read(dir.0.join("kept.json")).unwrap();
        assert_eq!(model::from_json(&saved).unwrap().name, "Keep me");
    }

    #[test]
    fn save_writes_the_file_format_and_clears_recovery() {
        let dir = TempDir::new("doc-save");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        let cv = model::from_json(SAMPLE).unwrap();
        lock(&ws).session.dirty = true;
        write_recovery(&ws, &cv).unwrap();

        *ui.pick.borrow_mut() = Some(dir.0.join("cv.json"));
        let status = save(&ws, &ui, &cv).unwrap().unwrap();
        assert!(status.starts_with("Сохранено: "));
        assert_eq!(std::fs::read_to_string(dir.0.join("cv.json")).unwrap(), model::to_json(&cv));
        assert!(!lock(&ws).data_dir.join(recovery::FILE_NAME).exists());
        assert!(!lock(&ws).session.dirty);
        // A second save goes to the same file without asking.
        *ui.pick.borrow_mut() = None;
        assert!(save(&ws, &ui, &cv).unwrap().is_some());
    }

    #[test]
    fn export_writes_the_pdf_and_opens_it() {
        let dir = TempDir::new("doc-export");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        let fonts = FontSet::bundled();
        let cv = model::from_json(SAMPLE).unwrap();
        *ui.pick.borrow_mut() = Some(dir.0.join("out"));
        let status = export_pdf(&ws, &ui, &fonts, &cv).unwrap().unwrap();
        let pdf_path = dir.0.join("out.pdf");
        assert_eq!(status, format!("Экспортировано: {}", pdf_path.display()));
        assert!(std::fs::read(&pdf_path).unwrap().starts_with(b"%PDF-1.4"));
        assert_eq!(*ui.opened.borrow(), vec![pdf_path]);
    }

    #[test]
    fn a_broken_file_is_reported_by_kind() {
        let dir = TempDir::new("doc-broken");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        let file = dir.0.join("broken.json");
        std::fs::write(&file, "[1, 2").unwrap();
        *ui.pick.borrow_mut() = Some(file);
        let err = open_dialog(&ws, &ui, &Cv::default()).unwrap_err();
        assert!(matches!(err, AppError::Json { .. }));
    }

    #[test]
    fn drops_open_the_first_and_say_how_many() {
        let dir = TempDir::new("doc-drop");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        let a = dir.0.join("a.json");
        let b = dir.0.join("b.json");
        std::fs::write(&a, r#"{"name":"A"}"#).unwrap();
        std::fs::write(&b, r#"{"name":"B"}"#).unwrap();
        assert_eq!(queue_pending(&ws, vec![a, b]), 2);
        let opened = open_pending(&ws, &ui, &Cv::default()).unwrap().unwrap();
        assert_eq!(opened.cv.name, "A");
        assert!(opened.status.ends_with(" (перетащено файлов: 2, открыт первый)"));
        // Taken: a second call opens nothing.
        assert!(open_pending(&ws, &ui, &Cv::default()).unwrap().is_none());
    }

    /// Security: dropped or handed-over paths that are relative or climb with
    /// ".." are not queued at all.
    #[test]
    fn untrusted_pending_paths_are_refused() {
        let dir = TempDir::new("doc-drop-bad");
        let ws = workspace(&dir);
        let bad = vec![PathBuf::from("relative.json"), dir.0.join("..").join("x.json")];
        assert_eq!(queue_pending(&ws, bad), 0);
        assert!(lock(&ws).session.pending.is_empty());
    }

    /// Security: a recent-list index the list does not have is refused before
    /// anything is asked or opened.
    #[test]
    fn recent_index_is_checked() {
        let dir = TempDir::new("doc-recent");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        let err = open_recent(&ws, &ui, 3, &Cv::default()).unwrap_err();
        assert!(matches!(err, AppError::Validation { .. }));
        assert_eq!(ui.asked_save.get(), 0);
    }

    /// Security: a CV from the page is size-bounded and gets a valid running
    /// order whatever it claimed.
    #[test]
    fn accept_checks_what_the_page_sends() {
        let mut cv = Cv::default();
        cv.sections.clear();
        assert_eq!(accept(cv).unwrap().sections.len(), model::SECTION_IDS.len());
        let huge = Cv { summary: "x".repeat(files::MAX_RESUME_BYTES as usize + 1), ..Cv::default() };
        assert!(matches!(accept(huge).unwrap_err(), AppError::Validation { .. }));
    }

    #[test]
    fn closing_clears_the_snapshot_unless_cancelled() {
        let dir = TempDir::new("doc-close");
        let ws = workspace(&dir);
        let ui = FakeUi::default();
        lock(&ws).session.dirty = true;
        write_recovery(&ws, &Cv::default()).unwrap();
        ui.save_choice.set(Some(SaveChoice::Cancel));
        assert!(!close(&ws, &ui, &Cv::default()).unwrap());
        assert!(lock(&ws).data_dir.join(recovery::FILE_NAME).exists());
        ui.save_choice.set(Some(SaveChoice::Discard));
        assert!(close(&ws, &ui, &Cv::default()).unwrap());
        assert!(!lock(&ws).data_dir.join(recovery::FILE_NAME).exists());
    }
}
