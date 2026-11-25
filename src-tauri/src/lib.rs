//! CV Builder, the desktop application: plugins, shared state and every command
//! in one place. The logic lives in the modules; this file only wires them up.

mod commands;
mod dialogs;
mod document;
mod error;
mod files;
mod preview;
mod recent;
mod recovery;
mod session;
mod settings;

pub use error::{AppError, AppResult};

use std::sync::Mutex;

use serde::Serialize;
use tauri::{DragDropEvent, Emitter, Manager, WindowEvent};

use commands::AppState;
use cvb_core::font::FontSet;
use document::Workspace;

/// Sent to the page when files wait to be opened: dropped on the window, or
/// handed over by a second launch. The page asks about unsaved changes and
/// then calls `open_pending`; the paths themselves never travel to it.
#[derive(Clone, Serialize)]
struct PendingFiles {
    count: usize,
}

fn workspace(app: &tauri::AppHandle) -> std::sync::MutexGuard<'_, Workspace> {
    app.state::<AppState>().inner().workspace.lock().unwrap_or_else(|e| e.into_inner())
}

pub fn run() {
    tauri::Builder::default()
        // First, so a second launch is caught before anything else starts: it
        // hands its command line to the window already open and exits.
        .plugin(tauri_plugin_single_instance::init(|app, argv, cwd| {
            let base = std::path::PathBuf::from(cwd);
            let file = argv.iter().skip(1).find_map(|a| files::from_command_line(&base, std::ffi::OsStr::new(a)));
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.unminimize();
                let _ = window.set_focus();
                if let Some(file) = file {
                    let state = app.state::<AppState>();
                    let count = document::queue_pending(&state.workspace, vec![file]);
                    if count > 0 {
                        let _ = window.emit("open-pending", PendingFiles { count });
                    }
                }
            }
        }))
        // Both are used from Rust only. The page has no dialog or opener
        // permission: it cannot open a file dialog or launch anything itself.
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            let data_dir = app.path().app_data_dir()?;
            std::fs::create_dir_all(&data_dir)?;
            let documents = app.path().document_dir().ok();
            let mut ws = Workspace::new(data_dir, documents);

            let cwd = std::env::current_dir().unwrap_or_default();
            ws.session.startup_file =
                std::env::args_os().skip(1).find_map(|a| files::from_command_line(&cwd, &a));

            if let Some(window) = app.get_webview_window("main") {
                commands::apply_theme(&window, ws.settings.theme_mode);
            }
            app.manage(AppState { workspace: Mutex::new(ws), fonts: FontSet::bundled() });
            Ok(())
        })
        .on_window_event(|window, event| match event {
            WindowEvent::CloseRequested { api, .. } => {
                let app = window.app_handle();
                let mut ws = workspace(app);
                if ws.session.dirty {
                    // The page holds the resume; it calls close_window, which
                    // asks, saves if told to, and closes.
                    api.prevent_close();
                    let _ = window.emit("close-requested", ());
                } else {
                    let Workspace { data_dir, settings, .. } = &mut *ws;
                    recovery::clear(data_dir, settings);
                }
            }
            WindowEvent::DragDrop(DragDropEvent::Drop { paths, .. }) => {
                let state = window.app_handle().state::<AppState>();
                let count = document::queue_pending(&state.workspace, paths.clone());
                if count > 0 {
                    let _ = window.emit("open-pending", PendingFiles { count });
                }
            }
            _ => {}
        })
        .invoke_handler(tauri::generate_handler![
            commands::app_info,
            commands::startup,
            commands::set_dirty,
            commands::new_document,
            commands::open_dialog,
            commands::open_recent,
            commands::open_pending,
            commands::save,
            commands::save_as,
            commands::export_pdf,
            commands::close_window,
            commands::write_recovery,
            commands::render,
            commands::recent_files,
            commands::clear_recent,
            commands::presets,
            commands::defaults,
            commands::theme_mode,
            commands::set_theme_mode,
        ])
        .run(tauri::generate_context!())
        .expect("CV Builder could not start");
}
