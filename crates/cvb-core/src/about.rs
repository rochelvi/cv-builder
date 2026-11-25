//! What the program calls itself. One place, so the window corner, `cvcli
//! --version` and the installer cannot disagree.

pub const PRODUCT: &str = "CV Builder";

/// The number Windows compares. Taken from the workspace manifest, which is the
/// only place it is written down.
pub const VERSION: &str = env!("CARGO_PKG_VERSION");

/// The kind of build: release, beta, alpha or rc. A label for people, not part
/// of the version number.
pub const CHANNEL: &str = "release";

pub const COPYRIGHT: &str = "© 2026 Daniil Mishin. Лицензия GPL-3.0";

/// "v3.0.0-release": what the window shows in its corner and `cvcli --version`
/// prints after the product name.
pub fn display_version() -> String {
    format!("v{VERSION}-{CHANNEL}")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn display_version_joins_number_and_channel() {
        assert_eq!(display_version(), format!("v{VERSION}-release"));
        assert!(VERSION.split('.').count() == 3, "three-part version expected");
    }
}
