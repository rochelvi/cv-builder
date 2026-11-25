// No console window behind the application in a release build on Windows.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    cv_builder_lib::run();
}
