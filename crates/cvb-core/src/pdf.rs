//! Writes a laid-out document as a PDF 1.4 file.
//!
//! Self-contained: no PDF or compression library. Text is written as
//! Identity-H glyph runs against a subset of the bundled font embedded in the
//! file, so the result renders identically on a machine that has never heard of
//! the face, and Cyrillic survives copy-paste through the ToUnicode map.
//!
//! The output is byte for byte what the C++ writer produced - nothing is dated
//! or hashed from an address - which is what lets a test pin it.

use std::collections::{BTreeMap, BTreeSet};

use crate::font::{Font, FontSet};
use crate::layout::{Document, Rgb};
use crate::numfmt;

/// Three decimals, trailing zeros trimmed, "0" for anything that rounds away.
fn num(v: f64) -> String {
    if v.abs() < 5e-4 {
        return "0".into();
    }
    let mut s = numfmt::fixed(v, 3);
    if s.contains('.') {
        let trimmed = s.trim_end_matches('0').trim_end_matches('.').len();
        s.truncate(trimmed);
    }
    s
}

fn color_op(c: Rgb, op: &str) -> String {
    format!(
        "{} {} {} {op}\n",
        num(f64::from(c.r) / 255.0),
        num(f64::from(c.g) / 255.0),
        num(f64::from(c.b) / 255.0)
    )
}

fn push_hex16(out: &mut String, v: u16) {
    out.push_str(&format!("{v:04X}"));
}

/// A code point as UTF-16BE hex: one unit, or a surrogate pair.
fn push_code_point(out: &mut String, cp: u32) {
    if cp >= 0x10000 {
        let cp = cp - 0x10000;
        push_hex16(out, (0xD800 + (cp >> 10)) as u16);
        push_hex16(out, (0xDC00 + (cp & 0x3FF)) as u16);
    } else {
        push_hex16(out, cp as u16);
    }
}

/// A PDF text string holding arbitrary Unicode: UTF-16BE, BOM first, hex form.
fn utf16_hex_string(text: &str) -> String {
    let mut out = String::from("<FEFF");
    for unit in text.encode_utf16() {
        push_hex16(&mut out, unit);
    }
    out.push('>');
    out
}

/// Six capital letters derived from the glyph set, the subset prefix the
/// specification asks for, different when the subset differs.
fn subset_tag(glyphs: &BTreeSet<u16>, bold: bool) -> String {
    let mut hash: u32 = if bold { 0x9E37_79B9 } else { 0x85EB_CA6B };
    for &gid in glyphs {
        hash = hash.wrapping_mul(31).wrapping_add(u32::from(gid));
    }
    let mut tag = String::with_capacity(6);
    for _ in 0..6 {
        tag.push((b'A' + (hash % 26) as u8) as char);
        hash /= 26;
        hash = hash.wrapping_mul(2_654_435_761).wrapping_add(1);
    }
    tag
}

/// One face's usage across the document.
#[derive(Default)]
struct FaceUse {
    glyphs: BTreeSet<u16>,
    to_unicode: BTreeMap<u16, u32>, // glyph -> code point, for text extraction
    used: bool,
}

struct Writer<'a> {
    doc: &'a Document,
    fonts: &'a FontSet,
    uses: [FaceUse; 2],
    body: Vec<u8>,
    offsets: Vec<usize>,
    catalog: usize,
    pages_node: usize,
    info: usize,
    first_page_object: usize,
    font_object: [usize; 2],
}

impl<'a> Writer<'a> {
    fn run(mut self) -> Result<Vec<u8>, String> {
        if self.doc.pages.is_empty() {
            return Err("нечего печатать".into());
        }
        self.collect_glyphs();

        // Object numbers are fixed up front so references can be written before
        // the objects themselves exist.
        let page_count = self.doc.pages.len();
        self.catalog = 1;
        self.pages_node = 2;
        self.info = 3;
        let mut next = 4;
        for face in 0..2 {
            if self.uses[face].used {
                self.font_object[face] = next;
                next += 5; // Type0, CIDFont, descriptor, font file, ToUnicode
            }
        }
        self.first_page_object = next;
        next += page_count * 2; // page + its content stream

        self.offsets = vec![0; next];
        self.body.extend_from_slice(b"%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

        self.write_catalog(page_count);
        self.write_info();
        for face in 0..2 {
            if self.uses[face].used {
                self.write_font(face);
            }
        }
        for i in 0..page_count {
            self.write_page(i);
        }
        self.write_trailer(next);
        Ok(self.body)
    }

    // ---------- object plumbing ----------

    fn put(&mut self, text: &str) {
        self.body.extend_from_slice(text.as_bytes());
    }

    fn begin(&mut self, id: usize) {
        self.offsets[id] = self.body.len();
        self.put(&format!("{id} 0 obj\n"));
    }

    fn end(&mut self) {
        self.put("endobj\n");
    }

    fn stream(&mut self, id: usize, dict: &str, data: &[u8]) {
        self.begin(id);
        self.put(&format!("<< {dict} /Length {} >>\nstream\n", data.len()));
        self.body.extend_from_slice(data);
        self.put("\nendstream\n");
        self.end();
    }

    fn collect_glyphs(&mut self) {
        for page in &self.doc.pages {
            for item in &page.texts {
                let face = usize::from(item.bold);
                let font = self.fonts.face(item.bold);
                let usage = &mut self.uses[face];
                usage.used = true;
                for c in item.text.chars() {
                    let glyph = font.glyph_for(c as u32);
                    usage.glyphs.insert(glyph);
                    usage.to_unicode.entry(glyph).or_insert(c as u32);
                }
            }
        }
    }

    fn write_catalog(&mut self, page_count: usize) {
        self.begin(self.catalog);
        self.put(&format!("<< /Type /Catalog /Pages {} 0 R >>\n", self.pages_node));
        self.end();

        self.begin(self.pages_node);
        let mut kids = String::new();
        for i in 0..page_count {
            kids.push_str(&format!(" {} 0 R", self.first_page_object + i * 2));
        }
        self.put(&format!("<< /Type /Pages /Count {page_count} /Kids [{kids} ] >>\n"));
        self.end();
    }

    fn write_info(&mut self) {
        self.begin(self.info);
        let mut dict = format!(
            "<< /Producer {} /Creator {}",
            utf16_hex_string("CV Builder"),
            utf16_hex_string("CV Builder")
        );
        if !self.doc.title.is_empty() {
            dict.push_str(&format!(" /Title {}", utf16_hex_string(&self.doc.title)));
        }
        if !self.doc.author.is_empty() {
            dict.push_str(&format!(" /Author {}", utf16_hex_string(&self.doc.author)));
        }
        dict.push_str(" >>\n");
        self.put(&dict);
        self.end();
    }

    fn write_font(&mut self, face: usize) {
        let font: &Font = self.fonts.face(face == 1);
        let base = self.font_object[face];
        let (cid_font, descriptor, font_file, to_unicode) = (base + 1, base + 2, base + 3, base + 4);
        let name = format!(
            "{}+CVSans{}",
            subset_tag(&self.uses[face].glyphs, face == 1),
            if face == 1 { "-Bold" } else { "" }
        );
        let scale = 1000.0 / f64::from(font.units_per_em());

        self.begin(base);
        self.put(&format!(
            "<< /Type /Font /Subtype /Type0 /BaseFont /{name} /Encoding /Identity-H \
             /DescendantFonts [{cid_font} 0 R] /ToUnicode {to_unicode} 0 R >>\n"
        ));
        self.end();

        // One "gid [width]" entry per glyph: a few hundred glyphs at most, so a
        // range encoding would not pay for itself.
        let mut widths = String::new();
        for &gid in &self.uses[face].glyphs {
            widths.push_str(&format!(" {gid} [{}]", num(font.advance(gid) * 1000.0)));
        }
        self.begin(cid_font);
        self.put(&format!(
            "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /{name} \
             /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> \
             /FontDescriptor {descriptor} 0 R /CIDToGIDMap /Identity /DW 1000 /W [{widths} ] >>\n"
        ));
        self.end();

        let s = |v: i32| num(f64::from(v) * scale);
        self.begin(descriptor);
        self.put(&format!(
            "<< /Type /FontDescriptor /FontName /{name} /Flags 32 /FontBBox [{} {} {} {}] \
             /ItalicAngle 0 /Ascent {} /Descent {} /CapHeight {} /StemV {} /FontFile2 {font_file} 0 R >>\n",
            s(font.x_min()),
            s(font.y_min()),
            s(font.x_max()),
            s(font.y_max()),
            s(font.ascent()),
            s(font.descent()),
            s(font.cap_height()),
            if face == 1 { "160" } else { "80" },
        ));
        self.end();

        let glyph_list: Vec<u16> = self.uses[face].glyphs.iter().copied().collect();
        let embedded = font.subset(&glyph_list);
        self.stream(font_file, &format!("/Length1 {}", embedded.len()), &embedded);

        let cmap = build_to_unicode(&self.uses[face]);
        self.stream(to_unicode, "", cmap.as_bytes());
    }

    fn write_page(&mut self, index: usize) {
        let page = &self.doc.pages[index];
        let page_id = self.first_page_object + index * 2;
        let content_id = page_id + 1;
        let height = self.doc.height;

        // Background first, then rules, then text - the order the preview
        // paints in.
        let mut content = color_op(page.background.color, "rg");
        let bg = &page.background;
        content.push_str(&format!(
            "{} {} {} {} re f\n",
            num(bg.x),
            num(height - bg.y - bg.h),
            num(bg.w),
            num(bg.h)
        ));

        let mut stroke: Option<Rgb> = None;
        let mut line_width = -1.0f64;
        for line in &page.lines {
            if stroke != Some(line.color) {
                stroke = Some(line.color);
                content.push_str(&color_op(line.color, "RG"));
            }
            if line.width != line_width {
                line_width = line.width;
                content.push_str(&format!("{} w\n", num(line_width)));
            }
            content.push_str(&format!(
                "{} {} m {} {} l S\n",
                num(line.x1),
                num(height - line.y1),
                num(line.x2),
                num(height - line.y2)
            ));
        }

        if !page.texts.is_empty() {
            content.push_str("BT\n");
            let mut fill: Option<Rgb> = None;
            let mut current_face: Option<bool> = None;
            let mut current_size = -1.0f64;
            for item in &page.texts {
                if current_face != Some(item.bold) || item.size != current_size {
                    current_face = Some(item.bold);
                    current_size = item.size;
                    content.push_str(&format!("/F{} {} Tf\n", if item.bold { 2 } else { 1 }, num(item.size)));
                }
                if fill != Some(item.color) {
                    fill = Some(item.color);
                    content.push_str(&color_op(item.color, "rg"));
                }
                content.push_str(&format!("1 0 0 1 {} {} Tm <", num(item.x), num(height - item.y)));
                for gid in self.fonts.face(item.bold).shape(&item.text) {
                    push_hex16(&mut content, gid);
                }
                content.push_str("> Tj\n");
            }
            content.push_str("ET\n");
        }

        self.begin(page_id);
        let mut fonts = String::new();
        for face in 0..2 {
            if self.uses[face].used {
                fonts.push_str(&format!(" /F{} {} 0 R", if face == 1 { 2 } else { 1 }, self.font_object[face]));
            }
        }
        self.put(&format!(
            "<< /Type /Page /Parent {} 0 R /MediaBox [0 0 {} {}] /Resources << /Font <<{fonts} >> >> \
             /Contents {content_id} 0 R >>\n",
            self.pages_node,
            num(self.doc.width),
            num(self.doc.height),
        ));
        self.end();

        self.stream(content_id, "", content.as_bytes());
    }

    fn write_trailer(&mut self, object_count: usize) {
        let xref = self.body.len();
        let mut table = format!("xref\n0 {object_count}\n0000000000 65535 f \n");
        for i in 1..object_count {
            table.push_str(&format!("{:010} 00000 n \n", self.offsets[i]));
        }
        table.push_str(&format!(
            "trailer\n<< /Size {object_count} /Root {} 0 R /Info {} 0 R >>\nstartxref\n{xref}\n%%EOF\n",
            self.catalog, self.info
        ));
        self.put(&table);
    }
}

fn build_to_unicode(usage: &FaceUse) -> String {
    let mut map = String::from(
        "/CIDInit /ProcSet findresource begin\n\
         12 dict begin\nbegincmap\n\
         /CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n\
         /CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n\
         1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n",
    );
    let entries: Vec<(u16, u32)> = usage.to_unicode.iter().map(|(&g, &c)| (g, c)).collect();
    for chunk in entries.chunks(100) {
        map.push_str(&format!("{} beginbfchar\n", chunk.len()));
        for &(glyph, cp) in chunk {
            map.push('<');
            push_hex16(&mut map, glyph);
            map.push_str("> <");
            push_code_point(&mut map, cp);
            map.push_str(">\n");
        }
        map.push_str("endbfchar\n");
    }
    map.push_str("endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n");
    map
}

/// The finished PDF bytes. Fails only for a document without pages.
pub fn build_pdf(doc: &Document, fonts: &FontSet) -> Result<Vec<u8>, String> {
    Writer {
        doc,
        fonts,
        uses: Default::default(),
        body: Vec::new(),
        offsets: Vec::new(),
        catalog: 0,
        pages_node: 0,
        info: 0,
        first_page_object: 0,
        font_object: [0; 2],
    }
    .run()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::layout::layout;
    use crate::model::{self, Cv};

    const INPUTS: [&str; 7] = ["sample", "reference", "empty", "bom", "legacy", "tolerant", "long"];

    fn fixture(path: &str) -> Vec<u8> {
        let full = format!("{}/../../fixtures/{path}", env!("CARGO_MANIFEST_DIR"));
        std::fs::read(&full).unwrap_or_else(|e| panic!("{full}: {e}"))
    }

    fn fnv(data: &[u8]) -> u64 {
        let mut v = 0xCBF2_9CE4_8422_2325u64;
        for &b in data {
            v ^= u64::from(b);
            v = v.wrapping_mul(0x0000_0100_0000_01B3);
        }
        v
    }

    fn reference() -> Vec<u8> {
        let fonts = FontSet::bundled();
        let cv = model::from_json(&fixture("input/reference.json")).unwrap();
        build_pdf(&layout(&cv, &fonts), &fonts).unwrap()
    }

    /// Every fixture exports to exactly the bytes the C++ writer produced.
    #[test]
    fn matches_the_cpp_bytes() {
        let fonts = FontSet::bundled();
        for name in INPUTS {
            let cv = model::from_json(&fixture(&format!("input/{name}.json"))).unwrap();
            let pdf = build_pdf(&layout(&cv, &fonts), &fonts).unwrap();
            let golden = fixture(&format!("golden/{name}.pdf"));
            if pdf != golden {
                let at = pdf.iter().zip(&golden).position(|(a, b)| a != b).unwrap_or(pdf.len().min(golden.len()));
                let from = at.saturating_sub(80);
                panic!(
                    "{name}: differs at byte {at} (sizes {} vs {})\nrust: {:?}\nc++:  {:?}",
                    pdf.len(),
                    golden.len(),
                    String::from_utf8_lossy(&pdf[from..(at + 80).min(pdf.len())]),
                    String::from_utf8_lossy(&golden[from..(at + 80).min(golden.len())]),
                );
            }
        }
    }

    /// The hash the old test_pdf.cpp pinned, measured on Windows and Linux.
    /// If a deliberate change to the template or the writer moves it, move it
    /// in the same commit and say there that every exported resume changes.
    #[test]
    fn matches_the_pinned_bytes() {
        let pdf = reference();
        assert_eq!(pdf.len(), 80832);
        assert_eq!(fnv(&pdf), 0x7410_097b_186e_6353);
    }

    #[test]
    fn is_deterministic() {
        assert_eq!(reference(), reference());
    }

    #[test]
    fn looks_like_a_pdf() {
        let pdf = reference();
        let text = String::from_utf8_lossy(&pdf);
        assert!(text.starts_with("%PDF-1.4"));
        assert!(text.ends_with("%%EOF\n"));
        for needle in [
            "/Subtype /Type0",
            "/Encoding /Identity-H",
            "/FontFile2",
            "/ToUnicode",
            "beginbfchar",
            "/MediaBox [0 0 595.276 841.89]",
            "/F1",
            "/F2",
        ] {
            assert!(text.contains(needle), "{needle}");
        }
    }

    #[test]
    fn embeds_only_the_glyphs_it_needs() {
        let fonts = FontSet::bundled();
        let mut small = Cv { name: "A".into(), ..Cv::default() };
        for s in &mut small.sections {
            s.enabled = false;
        }
        let tiny = build_pdf(&layout(&small, &fonts), &fonts).unwrap();
        assert!(tiny.len() < reference().len());
        assert!(tiny.len() < 100 * 1024);
    }

    #[test]
    fn refuses_an_empty_document() {
        let fonts = FontSet::bundled();
        let empty = Document { width: 0.0, height: 0.0, pages: vec![], title: String::new(), author: String::new() };
        assert_eq!(build_pdf(&empty, &fonts).unwrap_err(), "нечего печатать");
    }

    #[test]
    fn numbers_are_trimmed() {
        assert_eq!(num(0.0), "0");
        assert_eq!(num(0.0004), "0");
        assert_eq!(num(-0.0004), "0");
        assert_eq!(num(12.0), "12");
        assert_eq!(num(841.8897637795276), "841.89");
        assert_eq!(num(0.5), "0.5");
        assert_eq!(num(-3.25), "-3.25");
    }
}
