//! The one error type every command returns.
//!
//! Serialised with a `kind` tag, so the page tells errors apart by kind rather
//! than by reading the text. The text is still there, in Russian and word for
//! word what the previous versions showed, because it goes into message boxes
//! and the status line.

use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "camelCase")]
pub enum AppError {
    /// Reading or writing a file failed.
    Io { message: String },
    /// A resume file is not valid JSON, or not a JSON object.
    Json { message: String },
    /// The fonts could not be loaded or parsed.
    Font { message: String },
    /// The PDF could not be produced.
    Pdf { message: String },
    /// Something that came from outside - a path, a name, an index - was refused.
    Validation { message: String },
}

impl AppError {
    pub fn io(message: impl Into<String>) -> Self {
        AppError::Io { message: message.into() }
    }
    pub fn json(message: impl Into<String>) -> Self {
        AppError::Json { message: message.into() }
    }
    pub fn font(message: impl Into<String>) -> Self {
        AppError::Font { message: message.into() }
    }
    pub fn pdf(message: impl Into<String>) -> Self {
        AppError::Pdf { message: message.into() }
    }
    pub fn validation(message: impl Into<String>) -> Self {
        AppError::Validation { message: message.into() }
    }

    pub fn message(&self) -> &str {
        match self {
            AppError::Io { message }
            | AppError::Json { message }
            | AppError::Font { message }
            | AppError::Pdf { message }
            | AppError::Validation { message } => message,
        }
    }
}

impl std::fmt::Display for AppError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.message())
    }
}

impl std::error::Error for AppError {}

pub type AppResult<T> = Result<T, AppError>;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn serialises_with_a_kind_tag() {
        let json = serde_json::to_string(&AppError::io("не удалось открыть файл")).unwrap();
        assert_eq!(json, r#"{"kind":"io","message":"не удалось открыть файл"}"#);
        let json = serde_json::to_string(&AppError::validation("x")).unwrap();
        assert_eq!(json, r#"{"kind":"validation","message":"x"}"#);
    }
}
