//! The commands the page can invoke. Thin on purpose: each one checks what the
//! page sent, calls into the module that holds the logic, and hands the result
//! back.
//!
//! Every command is async, so none runs on the main thread: a native dialog
//! blocks the thread it runs on, and the window's own event loop must keep
//! turning while it is open. Flows that may show a dialog run on the blocking
//! pool.

use std::sync::Mutex;

use serde::Serialize;
use tauri::{AppHandle, Manager, State, WebviewWindow};

use cvb_core::about;
use cvb_core::font::FontSet;
use cvb_core::layout::layout;
use cvb_core::model::{self, Cv, Preset};

use crate::dialogs::NativeUi;
use crate::document::{self, Opened, Workspace};
use crate::error::{AppError, AppResult};
use crate::preview::{self, PreviewDoc};
use crate::recent;
use crate::settings::ThemeMode;

/// What every command can reach.
pub struct AppState {
    pub workspace: Mutex<Workspace>,
    pub fonts: FontSet,
}

impl AppState {
    fn workspace(&self) -> std::sync::MutexGuard<'_, Workspace> {
        self.workspace.lock().unwrap_or_else(|e| e.into_inner())
    }
}

/// Runs a flow that may open dialogs on the blocking pool, with the real UI.
async fn with_ui<T, F>(app: AppHandle, window: WebviewWindow, flow: F) -> AppResult<T>
where
    T: Send + 'static,
    F: FnOnce(&AppState, &NativeUi) -> AppResult<T> + Send + 'static,
{
    tauri::async_runtime::spawn_blocking(move || {
        let state = app.state::<AppState>();
        let ui = NativeUi { app: app.clone(), window };
        flow(&state, &ui)
    })
    .await
    .map_err(|_| AppError::io("внутренняя ошибка"))?
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AppInfo {
    pub product: &'static str,
    pub version: &'static str,
    pub display_version: String,
    pub copyright: &'static str,
}

#[tauri::command]
pub async fn app_info() -> AppInfo {
    AppInfo {
        product: about::PRODUCT,
        version: about::VERSION,
        display_version: about::display_version(),
        copyright: about::COPYRIGHT,
    }
}

#[tauri::command]
pub async fn startup(app: AppHandle, window: WebviewWindow) -> AppResult<Opened> {
    with_ui(app, window, |s, ui| Ok(document::startup(&s.workspace, ui))).await
}

#[tauri::command]
pub async fn set_dirty(app: AppHandle, window: WebviewWindow, dirty: bool) -> AppResult<()> {
    let state = app.state::<AppState>();
    document::set_dirty(&state.workspace, &NativeUi { app: app.clone(), window }, dirty);
    Ok(())
}

#[tauri::command]
pub async fn new_document(app: AppHandle, window: WebviewWindow, cv: Cv) -> AppResult<Option<Opened>> {
    let cv = document::accept(cv)?;
    with_ui(app, window, move |s, ui| document::new_document(&s.workspace, ui, &cv)).await
}

#[tauri::command]
pub async fn open_dialog(app: AppHandle, window: WebviewWindow, cv: Cv) -> AppResult<Option<Opened>> {
    let cv = document::accept(cv)?;
    with_ui(app, window, move |s, ui| document::open_dialog(&s.workspace, ui, &cv)).await
}

#[tauri::command]
pub async fn open_recent(app: AppHandle, window: WebviewWindow, index: usize, cv: Cv) -> AppResult<Option<Opened>> {
    let cv = document::accept(cv)?;
    with_ui(app, window, move |s, ui| document::open_recent(&s.workspace, ui, index, &cv)).await
}

#[tauri::command]
pub async fn open_pending(app: AppHandle, window: WebviewWindow, cv: Cv) -> AppResult<Option<Opened>> {
    let cv = document::accept(cv)?;
    with_ui(app, window, move |s, ui| document::open_pending(&s.workspace, ui, &cv)).await
}

#[tauri::command]
pub async fn save(app: AppHandle, window: WebviewWindow, cv: Cv) -> AppResult<Option<String>> {
    let cv = document::accept(cv)?;
    with_ui(app, window, move |s, ui| document::save(&s.workspace, ui, &cv)).await
}

#[tauri::command]
pub async fn save_as(app: AppHandle, window: WebviewWindow, cv: Cv) -> AppResult<Option<String>> {
    let cv = document::accept(cv)?;
    with_ui(app, window, move |s, ui| document::save_as(&s.workspace, ui, &cv)).await
}

#[tauri::command]
pub async fn export_pdf(app: AppHandle, window: WebviewWindow, cv: Cv) -> AppResult<Option<String>> {
    let cv = document::accept(cv)?;
    with_ui(app, window, move |s, ui| document::export_pdf(&s.workspace, ui, &s.fonts, &cv)).await
}

/// Closes the window after asking about unsaved changes. False if cancelled.
#[tauri::command]
pub async fn close_window(app: AppHandle, window: WebviewWindow, cv: Cv) -> AppResult<bool> {
    let cv = document::accept(cv)?;
    let target = window.clone();
    let close = with_ui(app, window, move |s, ui| document::close(&s.workspace, ui, &cv)).await?;
    if close {
        let _ = target.destroy();
    }
    Ok(close)
}

#[tauri::command]
pub async fn write_recovery(state: State<'_, AppState>, cv: Cv) -> AppResult<()> {
    let cv = document::accept(cv)?;
    document::write_recovery(&state.workspace, &cv)
}

#[tauri::command]
pub async fn render(state: State<'_, AppState>, cv: Cv) -> AppResult<PreviewDoc> {
    let cv = document::accept(cv)?;
    Ok(preview::build(&layout(&cv, &state.fonts), &state.fonts))
}

#[tauri::command]
pub async fn recent_files(state: State<'_, AppState>) -> AppResult<Vec<String>> {
    let ws = state.workspace();
    Ok(recent::list(&ws.settings).iter().map(|p| p.to_string_lossy().into_owned()).collect())
}

#[tauri::command]
pub async fn clear_recent(state: State<'_, AppState>) -> AppResult<()> {
    let mut ws = state.workspace();
    recent::clear(&mut ws.settings);
    ws.settings.save(&ws.data_dir)
}

#[tauri::command]
pub async fn presets() -> Vec<Preset> {
    model::presets()
}

/// The defaults a new section list or a new resume starts from, so the page
/// does not keep its own copy of them.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Defaults {
    pub cv: Cv,
    pub new_document: Cv,
}

#[tauri::command]
pub async fn defaults() -> Defaults {
    Defaults { cv: Cv::default(), new_document: Cv::new_document() }
}

#[tauri::command]
pub async fn theme_mode(state: State<'_, AppState>) -> AppResult<ThemeMode> {
    Ok(state.workspace().settings.theme_mode)
}

#[tauri::command]
pub async fn set_theme_mode(state: State<'_, AppState>, window: WebviewWindow, mode: ThemeMode) -> AppResult<()> {
    apply_theme(&window, mode);
    let mut ws = state.workspace();
    ws.settings.theme_mode = mode;
    ws.settings.save(&ws.data_dir)
}

/// The window frame follows the chosen palette; System leaves it to the desktop.
pub fn apply_theme(window: &WebviewWindow, mode: ThemeMode) {
    let theme = match mode {
        ThemeMode::System => None,
        ThemeMode::Light => Some(tauri::Theme::Light),
        ThemeMode::Dark => Some(tauri::Theme::Dark),
    };
    let _ = window.set_theme(theme);
}
