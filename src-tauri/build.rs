// The application's own commands are declared here as well as in
// generate_handler!, so each one gets an allow-permission and the capability
// file can name exactly the ones the window may call. A command missing from
// this list cannot be invoked from the page at all.
const COMMANDS: &[&str] = &[
    "app_info",
    "startup",
    "set_dirty",
    "new_document",
    "open_dialog",
    "open_recent",
    "open_pending",
    "save",
    "save_as",
    "export_pdf",
    "close_window",
    "write_recovery",
    "render",
    "recent_files",
    "clear_recent",
    "presets",
    "defaults",
    "theme_mode",
    "set_theme_mode",
];

fn main() {
    tauri_build::try_build(
        tauri_build::Attributes::new()
            .app_manifest(tauri_build::AppManifest::new().commands(COMMANDS)),
    )
    .expect("tauri build script failed");
}
