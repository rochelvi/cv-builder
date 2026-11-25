//! The commands the page can invoke. Thin on purpose: each one unpacks its
//! arguments, calls into a module that holds the logic, and hands the result
//! back. Nothing here decides anything.

use serde::Serialize;

use cvb_core::about;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AppInfo {
    pub product: &'static str,
    pub version: &'static str,
    pub display_version: String,
    pub copyright: &'static str,
}

#[tauri::command]
pub fn app_info() -> AppInfo {
    AppInfo {
        product: about::PRODUCT,
        version: about::VERSION,
        display_version: about::display_version(),
        copyright: about::COPYRIGHT,
    }
}
