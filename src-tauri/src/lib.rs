//! CV Builder, the desktop application: plugins, shared state and every command
//! in one place. The logic lives in the modules; this file only wires them up.

mod commands;
mod error;
// Wired to the commands in the step that adds them.
#[allow(dead_code)]
mod files;

pub use error::{AppError, AppResult};

use tauri::Manager;

pub fn run() {
    tauri::Builder::default()
        // First, so a second launch is caught before anything else starts: it
        // hands its command line to the window that is already open and exits.
        .plugin(tauri_plugin_single_instance::init(|app, _argv, _cwd| {
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.unminimize();
                let _ = window.set_focus();
            }
        }))
        .invoke_handler(tauri::generate_handler![commands::app_info])
        .run(tauri::generate_context!())
        .expect("CV Builder could not start");
}
