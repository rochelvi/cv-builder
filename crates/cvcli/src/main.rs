//! Console front end: renders a resume JSON to PDF, and can dump the layout
//! for comparison against a reference.
//!
//!   cvcli in.json out.pdf [--dump-ops ops.json] [--fonts regular.ttf bold.ttf]
//!   cvcli --font-report [--fonts regular.ttf bold.ttf]
//!   cvcli --version
//!
//! Same arguments, same output and same exit codes as the C++ cvcli.

use std::ffi::OsString;
use std::path::PathBuf;
use std::process::ExitCode;

use cvb_core::about;
use cvb_core::font::{Font, FontSet};
use cvb_core::layout::{Document, layout};
use cvb_core::model;
use cvb_core::numfmt;
use cvb_core::pdf::build_pdf;

const USAGE: &str = "usage: cvcli in.json out.pdf [--dump-ops ops.json]\n\
                     \x20                           [--fonts regular.ttf bold.ttf]\n\
                     \x20      cvcli --font-report [--fonts regular.ttf bold.ttf]\n\
                     \x20      cvcli --version\n";

/// A resume is a few kilobytes; this only stops a mistyped path to something
/// huge from being read into memory.
const MAX_INPUT_BYTES: u64 = 16 * 1024 * 1024;

fn escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '"' | '\\' => {
                out.push('\\');
                out.push(c);
            }
            '\n' => out.push_str("\\n"),
            c => out.push(c),
        }
    }
    out
}

/// A flat JSON list of every draw call, so a layout can be diffed line by line
/// against another renderer.
fn dump_ops(doc: &Document) -> String {
    let f3 = |v: f64| numfmt::fixed(v, 3);
    let mut out = String::from("[\n");
    let mut first = true;
    for (p, page) in doc.pages.iter().enumerate() {
        for line in &page.lines {
            out.push_str(&format!(
                "{} {{\"op\":\"rule\",\"page\":{p},\"y\":{},\"w\":{}}}",
                if first { "" } else { ",\n" },
                f3(line.y1),
                f3(line.width)
            ));
            first = false;
        }
        for item in &page.texts {
            out.push_str(&format!(
                "{} {{\"op\":\"text\",\"page\":{p},\"x\":{},\"y\":{},\"size\":{},\"bold\":{},\"color\":\"{:02x}{:02x}{:02x}\",\"s\":\"{}\"}}",
                if first { "" } else { ",\n" },
                f3(item.x),
                f3(item.y),
                numfmt::shortest(item.size),
                item.bold,
                item.color.r,
                item.color.g,
                item.color.b,
                escape(&item.text)
            ));
            first = false;
        }
    }
    out.push_str("\n]\n");
    out
}

/// Every glyph metric the layout can depend on, as text. Diff two of these and
/// you know, for every character the template can set, whether two faces lay
/// out the same.
fn font_report(fonts: &FontSet) -> String {
    // ASCII, Latin-1, Cyrillic, and the typographic characters the layout
    // engine emits itself.
    const RANGES: [(u32, u32); 8] = [
        (0x0020, 0x007E),
        (0x00A0, 0x00FF),
        (0x0400, 0x04FF),
        (0x2013, 0x2014),
        (0x2018, 0x201E),
        (0x2022, 0x2022),
        (0x2026, 0x2026),
        (0x203A, 0x203A),
    ];
    let mut out = String::new();
    for bold in [false, true] {
        let font: &Font = fonts.face(bold);
        out.push_str(&format!(
            "[{}] family={} unitsPerEm={} glyphs={} ascent={} descent={} capHeight={}\n",
            if bold { "bold" } else { "regular" },
            fonts.family,
            font.units_per_em(),
            font.num_glyphs(),
            font.ascent(),
            font.descent(),
            font.cap_height()
        ));
        for (first, last) in RANGES {
            for cp in first..=last {
                let gid = font.glyph_for(cp);
                // Per 1000 em rather than font units: two faces may disagree on
                // unitsPerEm and still set text identically.
                out.push_str(&format!(
                    "U+{cp:04X} {} {}\n",
                    if gid != 0 { "present" } else { "MISSING" },
                    numfmt::fixed(font.advance(gid) * 1000.0, 3)
                ));
            }
        }
    }
    out
}

fn read_limited(path: &PathBuf, what: &str) -> Result<Vec<u8>, String> {
    let meta = std::fs::metadata(path).map_err(|_| what.to_string())?;
    if !meta.is_file() || meta.len() > MAX_INPUT_BYTES {
        return Err(what.to_string());
    }
    std::fs::read(path).map_err(|_| what.to_string())
}

/// A pair named on the command line, both halves or neither.
fn load_explicit(regular: &PathBuf, bold: &PathBuf) -> Result<FontSet, String> {
    let face = |path: &PathBuf| -> Result<Font, String> {
        let bytes = read_limited(path, "не удалось прочитать файл шрифта")?;
        Font::from_bytes(bytes, 0)
    };
    let pair = face(regular).and_then(|r| face(bold).map(|b| (r, b)));
    match pair {
        Ok((regular, bold)) => Ok(FontSet { regular, bold, family: "explicit".into() }),
        Err(why) => Err(format!("не удалось загрузить шрифт ({why})")),
    }
}

fn run(args: &[OsString]) -> Result<(), (u8, String)> {
    let text: Vec<String> = args.iter().map(|a| a.to_string_lossy().into_owned()).collect();
    let count = args.len();

    // Answered before anything is read: "which build produced this PDF?" is
    // what a script or a bug report needs.
    if text.iter().skip(1).any(|a| a == "--version") {
        print!("{} {}\n{}\n", about::PRODUCT, about::display_version(), about::COPYRIGHT);
        return Ok(());
    }

    let mut explicit: Option<(PathBuf, PathBuf)> = None;
    let mut want_report = false;
    for i in 1..count {
        if text[i] == "--font-report" {
            want_report = true;
        }
        if text[i] == "--fonts" && i + 2 < count {
            explicit = Some((PathBuf::from(&args[i + 1]), PathBuf::from(&args[i + 2])));
        }
    }

    if !want_report && count < 3 {
        return Err((2, USAGE.into()));
    }

    let fonts = match &explicit {
        Some((regular, bold)) => load_explicit(regular, bold).map_err(|e| (1, format!("{e}\n")))?,
        None => FontSet::bundled(),
    };

    if want_report {
        print!("{}", font_report(&fonts));
        return Ok(());
    }

    let input = PathBuf::from(&args[1]);
    let output = PathBuf::from(&args[2]);

    let cv = read_limited(&input, "не удалось открыть файл")
        .and_then(|bytes| model::from_json(&bytes))
        .map_err(|e| (1, format!("не удалось загрузить {}: {e}\n", text[1])))?;

    let doc = layout(&cv, &fonts);
    let pdf = build_pdf(&doc, &fonts).map_err(|e| (1, format!("не удалось записать PDF: {e}\n")))?;
    std::fs::write(&output, pdf).map_err(|_| (1, "не удалось записать PDF: не удалось создать файл\n".into()))?;

    for i in 3..count.saturating_sub(1) {
        if text[i] == "--dump-ops" {
            // As before, a dump that cannot be written does not fail the run:
            // the PDF, which is what was asked for, is already there.
            let _ = std::fs::write(PathBuf::from(&args[i + 1]), dump_ops(&doc));
        }
    }

    println!("{}: {} страниц(ы)", text[2], doc.pages.len());
    Ok(())
}

fn main() -> ExitCode {
    let args: Vec<OsString> = std::env::args_os().collect();
    match run(&args) {
        Ok(()) => ExitCode::SUCCESS,
        Err((code, message)) => {
            eprint!("{message}");
            ExitCode::from(code)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture(path: &str) -> Vec<u8> {
        let full = format!("{}/../../fixtures/{path}", env!("CARGO_MANIFEST_DIR"));
        std::fs::read(&full).unwrap_or_else(|e| panic!("{full}: {e}"))
    }

    fn text_fixture(path: &str) -> String {
        String::from_utf8(fixture(path)).unwrap().replace("\r\n", "\n")
    }

    #[test]
    fn font_report_matches_the_cpp_cli() {
        assert_eq!(font_report(&FontSet::bundled()), text_fixture("golden/cli.font-report.txt"));
    }

    #[test]
    fn op_dump_matches_the_cpp_cli() {
        let fonts = FontSet::bundled();
        for name in ["sample", "long", "tolerant"] {
            let cv = model::from_json(&fixture(&format!("input/{name}.json"))).unwrap();
            assert_eq!(dump_ops(&layout(&cv, &fonts)), text_fixture(&format!("golden/{name}.ops.json")), "{name}");
        }
    }

    #[test]
    fn usage_is_the_old_text() {
        assert_eq!(USAGE, text_fixture("golden/cli.usage.txt"));
        let args: Vec<OsString> = vec!["cvcli".into()];
        assert_eq!(run(&args).unwrap_err().0, 2);
    }

    #[test]
    fn version_line_has_the_old_shape() {
        let golden = text_fixture("golden/cli.version.txt");
        let mut lines = golden.lines();
        assert!(lines.next().unwrap().starts_with("CV Builder v"));
        assert_eq!(lines.next().unwrap(), about::COPYRIGHT);
    }

    /// Security: --fonts pointing at a directory or at something that is not a
    /// font is reported, not crashed on.
    #[test]
    fn bad_explicit_fonts_are_errors() {
        let dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let err = load_explicit(&dir, &dir).unwrap_err();
        assert_eq!(err, "не удалось загрузить шрифт (не удалось прочитать файл шрифта)");
        let not_a_font = dir.join("Cargo.toml");
        assert!(load_explicit(&not_a_font, &not_a_font).unwrap_err().starts_with("не удалось загрузить шрифт ("));
    }

    #[test]
    fn renders_a_pdf_end_to_end() {
        let out = std::env::temp_dir().join(format!("cvcli-test-{}.pdf", std::process::id()));
        let ops = out.with_extension("ops.json");
        let input = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../fixtures/input/sample.json");
        let args: Vec<OsString> =
            vec!["cvcli".into(), input.into(), out.clone().into(), "--dump-ops".into(), ops.clone().into()];
        run(&args).unwrap();
        assert_eq!(std::fs::read(&out).unwrap(), fixture("golden/sample.pdf"));
        assert_eq!(std::fs::read_to_string(&ops).unwrap(), text_fixture("golden/sample.ops.json"));
        let _ = std::fs::remove_file(out);
        let _ = std::fs::remove_file(ops);
    }
}
