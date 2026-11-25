//! Console front end: renders a resume JSON to PDF.
//!
//!   cvcli --version
//!
//! The rendering commands arrive with the core they need.

use cvb_core::about;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.iter().any(|a| a == "--version") {
        println!("{} {}", about::PRODUCT, about::display_version());
        println!("{}", about::COPYRIGHT);
        return;
    }
    eprintln!("usage: cvcli --version");
    std::process::exit(2);
}
