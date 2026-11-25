//! The resume itself: the model and its JSON form, the TrueType reader, the
//! layout engine and the PDF writer.
//!
//! Nothing here knows about an operating system or a window. It takes bytes and
//! returns bytes; reading and writing files, dialogs and settings belong to the
//! application. That is what lets the console renderer and the tests use it on
//! their own, and what keeps a PDF exported on one system the same file as one
//! exported on another.

pub mod about;
pub mod json;
pub mod model;
pub mod numfmt;
