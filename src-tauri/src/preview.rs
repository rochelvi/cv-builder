//! A laid-out document in the shape the page draws it: SVG text with an x
//! position for every character.
//!
//! The browser is never asked to lay text out. Every character goes where the
//! bundled font's advance widths put it - the same widths the layout engine
//! measured with and the PDF writer embeds - so the preview cannot wrap or
//! kern differently from the exported file. Only the rasterising is the
//! browser's.

use serde::Serialize;

use cvb_core::font::FontSet;
use cvb_core::layout::Document;

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PreviewLine {
    pub x1: f64,
    pub y1: f64,
    pub x2: f64,
    pub y2: f64,
    pub width: f64,
    pub color: String,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PreviewRun {
    /// Baseline, from the top of the page, in points.
    pub y: f64,
    pub size: f64,
    pub bold: bool,
    pub color: String,
    pub text: String,
    /// One x per UTF-16 code unit of `text` - the unit SVG positions by - so
    /// the second half of a surrogate pair repeats the first one's position.
    pub xs: Vec<f64>,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PreviewPage {
    pub background: String,
    pub lines: Vec<PreviewLine>,
    pub runs: Vec<PreviewRun>,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PreviewDoc {
    pub width: f64,
    pub height: f64,
    pub pages: Vec<PreviewPage>,
}

/// Thousandths of a point: far below a pixel at any zoom, and it keeps the
/// message to the page short.
fn round(v: f64) -> f64 {
    (v * 1000.0).round() / 1000.0
}

pub fn build(doc: &Document, fonts: &FontSet) -> PreviewDoc {
    let pages = doc
        .pages
        .iter()
        .map(|page| PreviewPage {
            background: page.background.color.hex(),
            lines: page
                .lines
                .iter()
                .map(|l| PreviewLine { x1: l.x1, y1: l.y1, x2: l.x2, y2: l.y2, width: l.width, color: l.color.hex() })
                .collect(),
            runs: page
                .texts
                .iter()
                .map(|t| {
                    let font = fonts.face(t.bold);
                    let mut xs = Vec::with_capacity(t.text.len());
                    let mut x = t.x;
                    for c in t.text.chars() {
                        for _ in 0..c.len_utf16() {
                            xs.push(round(x));
                        }
                        x += font.advance(font.glyph_for(c as u32)) * t.size;
                    }
                    PreviewRun { y: t.y, size: t.size, bold: t.bold, color: t.color.hex(), text: t.text.clone(), xs }
                })
                .collect(),
        })
        .collect();
    PreviewDoc { width: doc.width, height: doc.height, pages }
}

#[cfg(test)]
mod tests {
    use super::*;
    use cvb_core::layout::layout;
    use cvb_core::model::Cv;

    #[test]
    fn every_character_gets_the_layout_engines_position() {
        let fonts = FontSet::bundled();
        let cv = Cv { name: "Ab 😀".into(), role: "РОЛЬ".into(), ..Cv::default() };
        let doc = layout(&cv, &fonts);
        let preview = build(&doc, &fonts);
        assert_eq!(preview.pages.len(), doc.pages.len());
        for (page, source) in preview.pages.iter().zip(&doc.pages) {
            assert_eq!(page.runs.len(), source.texts.len());
            for (run, text) in page.runs.iter().zip(&source.texts) {
                assert_eq!(run.xs.len(), text.text.encode_utf16().count());
                assert!((run.xs[0] - text.x).abs() < 1e-3);
                // The run ends where the layout engine measured it to end.
                let last_char = text.text.chars().last().unwrap();
                let font = fonts.face(text.bold);
                let end = run.xs.last().unwrap() + font.advance(font.glyph_for(last_char as u32)) * text.size;
                assert!((end - (text.x + font.measure(&text.text, text.size))).abs() < 1e-2);
            }
        }
        let name = &preview.pages[0].runs[0];
        assert_eq!(name.text, "Ab 😀");
        assert_eq!(name.xs[3], name.xs[4], "both halves of the surrogate pair");
    }
}
