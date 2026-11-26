//! The document the window is editing: which file it belongs to, whether it
//! has unsaved changes, and files waiting to be opened.
//!
//! The resume itself lives in the page, which sends it along with every command
//! that needs it; what is kept here is what Rust needs on its own - for the
//! window title, for a close request, and for a drop or a second launch, which
//! arrive in Rust and are then confirmed by the page.

use std::path::PathBuf;

use cvb_core::about::PRODUCT;

#[derive(Debug, Default)]
pub struct Session {
    pub path: Option<PathBuf>,
    pub dirty: bool,
    /// Dropped on the window or passed to a second launch, waiting for the
    /// page to ask about unsaved changes. Only the first is opened.
    pub pending: Vec<PathBuf>,
    /// The file named on this launch's command line.
    pub startup_file: Option<PathBuf>,
}

impl Session {
    /// "CV Builder — cv.json *"
    pub fn title(&self) -> String {
        let mut title = PRODUCT.to_string();
        if let Some(name) = self.path.as_ref().and_then(|p| p.file_name()) {
            title.push_str(" \u{2014} ");
            title.push_str(&name.to_string_lossy());
        }
        if self.dirty {
            title.push_str(" *");
        }
        title
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn title_names_the_file_and_marks_changes() {
        let mut s = Session::default();
        assert_eq!(s.title(), "CV Builder");
        s.dirty = true;
        assert_eq!(s.title(), "CV Builder *");
        s.path = Some(std::env::temp_dir().join("Резюме.json"));
        s.dirty = false;
        assert_eq!(s.title(), "CV Builder — Резюме.json");
    }
}
