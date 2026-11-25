//! Native dialogs and the other things a document flow asks of the desktop.
//!
//! Behind a trait so the flows in document.rs can be tested without a window:
//! the tests answer the questions themselves.

use std::path::{Path, PathBuf};

use tauri::{AppHandle, WebviewWindow};
use tauri_plugin_dialog::{DialogExt, MessageDialogButtons, MessageDialogKind, MessageDialogResult};
use tauri_plugin_opener::OpenerExt;

use cvb_core::about::PRODUCT;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SaveChoice {
    Save,
    Discard,
    Cancel,
}

pub trait Ui {
    /// "Save changes before continuing?" - Yes / No / Cancel.
    fn ask_save_changes(&self) -> SaveChoice;
    /// Whether to restore what a crashed session left; `what` names it.
    fn ask_restore(&self, what: &str) -> bool;
    fn show_error(&self, text: &str);
    fn pick_resume_to_open(&self, start: Option<&Path>) -> Option<PathBuf>;
    fn pick_resume_to_save(&self, start: Option<&Path>, name: &str) -> Option<PathBuf>;
    fn pick_pdf_to_save(&self, start: Option<&Path>, name: &str) -> Option<PathBuf>;
    /// Hands a finished file to the system's viewer.
    fn open_externally(&self, path: &Path);
    fn set_title(&self, title: &str);
}

/// The real thing: dialogs parented to the editor window.
pub struct NativeUi {
    pub app: AppHandle,
    pub window: WebviewWindow,
}

impl NativeUi {
    fn ask(&self, text: &str, buttons: MessageDialogButtons, kind: MessageDialogKind) -> MessageDialogResult {
        self.app
            .dialog()
            .message(text)
            .title(PRODUCT)
            .kind(kind)
            .buttons(buttons)
            .parent(&self.window)
            .blocking_show_with_result()
    }

    fn file_dialog(&self, title: &str, start: Option<&Path>) -> tauri_plugin_dialog::FileDialogBuilder<tauri::Wry> {
        let mut dialog = self.app.dialog().file().set_title(title).set_parent(&self.window);
        if let Some(dir) = start {
            dialog = dialog.set_directory(dir);
        }
        dialog
    }
}

fn into_path(chosen: Option<tauri_plugin_dialog::FilePath>) -> Option<PathBuf> {
    chosen.and_then(|p| p.into_path().ok())
}

impl Ui for NativeUi {
    fn ask_save_changes(&self) -> SaveChoice {
        match self.ask(
            "Сохранить изменения перед тем, как продолжить?",
            MessageDialogButtons::YesNoCancel,
            MessageDialogKind::Warning,
        ) {
            MessageDialogResult::Yes => SaveChoice::Save,
            MessageDialogResult::No => SaveChoice::Discard,
            _ => SaveChoice::Cancel,
        }
    }

    fn ask_restore(&self, what: &str) -> bool {
        let text = format!("Прошлый сеанс завершился, не сохранив изменения.\n\nВосстановить {what}?");
        self.ask(&text, MessageDialogButtons::YesNo, MessageDialogKind::Info) == MessageDialogResult::Yes
    }

    fn show_error(&self, text: &str) {
        self.ask(text, MessageDialogButtons::Ok, MessageDialogKind::Error);
    }

    fn pick_resume_to_open(&self, start: Option<&Path>) -> Option<PathBuf> {
        into_path(
            self.file_dialog("Открыть резюме", start)
                .add_filter("Резюме", &["json"])
                .add_filter("Все файлы", &["*"])
                .blocking_pick_file(),
        )
    }

    fn pick_resume_to_save(&self, start: Option<&Path>, name: &str) -> Option<PathBuf> {
        into_path(
            self.file_dialog("Сохранить резюме", start)
                .set_file_name(name)
                .add_filter("Резюме", &["json"])
                .blocking_save_file(),
        )
    }

    fn pick_pdf_to_save(&self, start: Option<&Path>, name: &str) -> Option<PathBuf> {
        into_path(
            self.file_dialog("Экспорт PDF", start)
                .set_file_name(name)
                .add_filter("PDF", &["pdf"])
                .blocking_save_file(),
        )
    }

    fn open_externally(&self, path: &Path) {
        let _ = self.app.opener().open_path(path.to_string_lossy(), None::<&str>);
    }

    fn set_title(&self, title: &str) {
        let _ = self.window.set_title(title);
    }
}
