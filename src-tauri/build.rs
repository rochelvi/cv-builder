// The application's own commands are declared here as well as in
// generate_handler!, so each one gets an allow-permission and the capability
// file can name exactly the ones the window may call. A command missing from
// this list cannot be invoked from the page at all.
const COMMANDS: &[&str] = &["app_info"];

fn main() {
    tauri_build::try_build(
        tauri_build::Attributes::new()
            .app_manifest(tauri_build::AppManifest::new().commands(COMMANDS)),
    )
    .expect("tauri build script failed");
}
