//! A small TrueType reader: glyph lookup, advance widths, and subsetting.
//!
//! Everything the program does with text goes through here. The layout engine
//! measures with these advances, the PDF writer embeds a subset of this very
//! file, and the preview positions every character where these advances put it,
//! so the preview is not an approximation of the PDF.
//!
//! A port of the C++ reader that gives the same answers for every valid font
//! and differs only where the old code would have read past the end of a
//! damaged one: here every read is bounds-checked, and a font that does not
//! hold together is an error, never a crash. That matters because `cvcli
//! --fonts` reads files of the user's choosing, and the release build aborts on
//! a panic.

use std::collections::HashMap;

const fn tag(t: &[u8; 4]) -> u32 {
    u32::from_be_bytes(*t)
}

fn be16(d: &[u8], at: usize) -> Option<u16> {
    let b = d.get(at..at.checked_add(2)?)?;
    Some(u16::from_be_bytes([b[0], b[1]]))
}

fn sbe16(d: &[u8], at: usize) -> Option<i16> {
    be16(d, at).map(|v| v as i16)
}

fn be32(d: &[u8], at: usize) -> Option<u32> {
    let b = d.get(at..at.checked_add(4)?)?;
    Some(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
}

/// Sum of the data as big-endian 32-bit words, the sfnt checksum rule; a tail
/// shorter than a word counts as zero-padded.
fn checksum(data: &[u8]) -> u32 {
    let mut sum = 0u32;
    let (words, rest) = data.as_chunks::<4>();
    for word in words {
        sum = sum.wrapping_add(u32::from_be_bytes(*word));
    }
    if !rest.is_empty() {
        let mut tail = [0u8; 4];
        tail[..rest.len()].copy_from_slice(rest);
        sum = sum.wrapping_add(u32::from_be_bytes(tail));
    }
    sum
}

#[derive(Debug, Clone, Copy)]
struct Table {
    offset: u32,
    length: u32,
}

impl Table {
    fn range(self) -> std::ops::Range<usize> {
        self.offset as usize..self.offset as usize + self.length as usize
    }
}

/// How many code points a cmap may map in total. Unicode has 0x110000; a font
/// claiming more is describing ranges over and over, and walking them all is
/// how a damaged file would hold the program for minutes.
const CMAP_BUDGET: u64 = 0x110000 * 2;

#[derive(Debug, Clone, Default)]
pub struct Font {
    data: Vec<u8>,
    face_index: u32,
    tables: HashMap<u32, Table>,
    cmap: HashMap<u32, u16>,
    advances: Vec<u16>, // per glyph, font units
    loca: Vec<u32>,     // num_glyphs + 1 offsets into glyf
    units_per_em: i32,
    num_glyphs: i32,
    x_min: i32,
    y_min: i32,
    x_max: i32,
    y_max: i32,
    ascent: i32,
    descent: i32,
    cap_height: i32,
    bold: bool,
}

impl Font {
    /// Parses a face. `face_index` picks one out of a TrueType collection
    /// (.ttc); a plain .ttf has only face 0.
    pub fn from_bytes(data: Vec<u8>, face_index: u32) -> Result<Font, String> {
        let mut font = Font { data, face_index, units_per_em: 1000, ..Font::default() };
        font.parse(face_index)?;
        Ok(font)
    }

    pub fn face_index(&self) -> u32 {
        self.face_index
    }

    /// The bytes the face was parsed from.
    pub fn file_data(&self) -> &[u8] {
        &self.data
    }

    fn table(&self, t: &[u8; 4]) -> Option<Table> {
        self.tables.get(&tag(t)).copied()
    }

    fn parse(&mut self, face_index: u32) -> Result<(), String> {
        let d = &self.data;
        if d.len() < 12 {
            return Err("файл шрифта слишком мал".into());
        }
        let mut base = 0usize;
        if be32(d, 0) == Some(tag(b"ttcf")) {
            if d.len() < 16 {
                return Err("обрезанная коллекция шрифтов".into());
            }
            let faces = be32(d, 8).unwrap_or(0);
            if face_index >= faces {
                return Err("в коллекции нет такого начертания".into());
            }
            let at = 12 + 4 * face_index as usize;
            base = be32(d, at).ok_or("обрезанная коллекция шрифтов")? as usize;
            if base.checked_add(12).is_none_or(|end| end > d.len()) {
                return Err("испорченная коллекция шрифтов".into());
            }
        } else if face_index != 0 {
            return Err("в файле только одно начертание".into());
        }

        let version = be32(d, base).unwrap_or(0);
        if version != 0x0001_0000 && version != tag(b"true") {
            return Err("не TrueType (OpenType/CFF не поддерживается)".into());
        }

        let num_tables = be16(d, base + 4).unwrap_or(0) as usize;
        if d.len() < base + 12 + 16 * num_tables {
            return Err("обрезанный шрифт".into());
        }
        for i in 0..num_tables {
            let rec = base + 12 + 16 * i;
            let (Some(t), Some(offset), Some(length)) = (be32(d, rec), be32(d, rec + 8), be32(d, rec + 12))
            else {
                continue;
            };
            // A table that does not lie inside the file is ignored, as before.
            if (offset as usize) <= d.len() && (length as usize) <= d.len() - offset as usize {
                self.tables.insert(t, Table { offset, length });
            }
        }

        self.read_head()?;
        self.read_metrics()?;
        self.read_cmap();
        if self.cmap.is_empty() {
            return Err("в шрифте нет таблицы Unicode cmap".into());
        }
        Ok(())
    }

    fn read_head(&mut self) -> Result<(), String> {
        let missing = || "в шрифте нет таблиц head/maxp".to_string();
        let head = self.table(b"head").filter(|t| t.length >= 54).ok_or_else(missing)?;
        let maxp = self.table(b"maxp").filter(|t| t.length >= 6).ok_or_else(missing)?;
        let d = &self.data;
        let h = head.offset as usize;
        // Inside the file: the table records were checked when they were read.
        self.units_per_em = i32::from(be16(d, h + 18).unwrap_or(0));
        if self.units_per_em <= 0 {
            self.units_per_em = 1000;
        }
        self.x_min = i32::from(sbe16(d, h + 36).unwrap_or(0));
        self.y_min = i32::from(sbe16(d, h + 38).unwrap_or(0));
        self.x_max = i32::from(sbe16(d, h + 40).unwrap_or(0));
        self.y_max = i32::from(sbe16(d, h + 42).unwrap_or(0));
        let index_to_loc_format = sbe16(d, h + 50).unwrap_or(0);
        self.num_glyphs = i32::from(be16(d, maxp.offset as usize + 4).unwrap_or(0));
        if self.num_glyphs <= 0 {
            return Err("в шрифте нет глифов".into());
        }

        // loca/glyf are optional for measuring but required for subsetting.
        if let Some(loca) = self.table(b"loca") {
            let count = self.num_glyphs as usize + 1;
            let p = loca.offset as usize;
            if index_to_loc_format == 0 && loca.length as usize >= count * 2 {
                self.loca = (0..count).map(|i| u32::from(be16(d, p + i * 2).unwrap_or(0)) * 2).collect();
            } else if index_to_loc_format != 0 && loca.length as usize >= count * 4 {
                self.loca = (0..count).map(|i| be32(d, p + i * 4).unwrap_or(0)).collect();
            }
        }
        Ok(())
    }

    fn read_metrics(&mut self) -> Result<(), String> {
        let hhea = self.table(b"hhea").filter(|t| t.length >= 36);
        let hmtx = self.table(b"hmtx");
        let (Some(hhea), Some(hmtx)) = (hhea, hmtx) else {
            return Err("в шрифте нет таблиц hhea/hmtx".into());
        };
        let d = &self.data;
        let hh = hhea.offset as usize;
        self.ascent = i32::from(sbe16(d, hh + 4).unwrap_or(0));
        self.descent = i32::from(sbe16(d, hh + 6).unwrap_or(0));
        let number_of_h_metrics = be16(d, hh + 34).unwrap_or(0) as usize;
        if number_of_h_metrics == 0 {
            return Err("испорченная таблица hhea".into());
        }

        let hm = hmtx.offset as usize;
        let mut last = 0u16;
        self.advances = (0..self.num_glyphs as usize)
            .map(|gid| {
                if gid < number_of_h_metrics {
                    let at = gid * 4;
                    if at + 2 <= hmtx.length as usize {
                        last = be16(d, hm + at).unwrap_or(last);
                    }
                }
                last // trailing glyphs reuse the last advance
            })
            .collect();

        self.cap_height = (f64::from(self.ascent) * 0.72) as i32; // a guess when OS/2 has none
        if let Some(os2) = self.table(b"OS/2").filter(|t| t.length >= 64) {
            let o = os2.offset as usize;
            let version = be16(d, o).unwrap_or(0);
            self.bold = be16(d, o + 4).unwrap_or(0) >= 600 || (be16(d, o + 62).unwrap_or(0) & 0x20) != 0;
            if version >= 2 && os2.length >= 90 {
                let cap = i32::from(sbe16(d, o + 88).unwrap_or(0));
                if cap > 0 {
                    self.cap_height = cap;
                }
            }
        }
        Ok(())
    }

    fn read_cmap(&mut self) {
        let Some(cmap) = self.table(b"cmap").filter(|t| t.length >= 4) else { return };
        let d = &self.data;
        let base = cmap.offset as usize;
        let count = be16(d, base + 2).unwrap_or(0) as usize;

        let (mut best4, mut best12) = (0usize, 0usize);
        for i in 0..count {
            if 4 + 8 * i + 8 > cmap.length as usize {
                break;
            }
            let rec = base + 4 + 8 * i;
            let platform = be16(d, rec).unwrap_or(u16::MAX);
            let encoding = be16(d, rec + 2).unwrap_or(u16::MAX);
            let Some(offset) = be32(d, rec + 4).map(|o| base + o as usize) else { continue };
            if offset + 4 > d.len() {
                continue;
            }
            let format = be16(d, offset).unwrap_or(0);
            let unicode = (platform == 3 && (encoding == 1 || encoding == 10)) || platform == 0;
            if !unicode {
                continue;
            }
            if format == 12 && best12 == 0 {
                best12 = offset;
            }
            if format == 4 && best4 == 0 {
                best4 = offset;
            }
        }
        // Format 12 covers the astral planes; format 4 is the universal fallback.
        if best12 != 0 {
            self.parse_cmap12(best12);
        }
        if self.cmap.is_empty() && best4 != 0 {
            self.parse_cmap4(best4);
        }
    }

    fn parse_cmap4(&mut self, t: usize) {
        let d = &self.data;
        let Some(length) = be16(d, t + 2).map(usize::from) else { return };
        if t + length > d.len() || length < 16 {
            return;
        }
        let seg_count = be16(d, t + 6).unwrap_or(0) as usize / 2;
        let end_code = t + 14;
        let start_code = end_code + seg_count * 2 + 2;
        let id_delta = start_code + seg_count * 2;
        let id_range_offset = id_delta + seg_count * 2;
        if id_range_offset + seg_count * 2 - t > length {
            return;
        }
        let mut budget = CMAP_BUDGET;
        for seg in 0..seg_count {
            let first = u32::from(be16(d, start_code + seg * 2).unwrap_or(0));
            let last = u32::from(be16(d, end_code + seg * 2).unwrap_or(0));
            if first > last {
                continue;
            }
            let delta = be16(d, id_delta + seg * 2).unwrap_or(0);
            let range_offset = be16(d, id_range_offset + seg * 2).unwrap_or(0) as usize;
            for cp in first..=last {
                if budget == 0 {
                    return;
                }
                budget -= 1;
                let gid = if range_offset == 0 {
                    (cp as u16).wrapping_add(delta)
                } else {
                    let at = id_range_offset + seg * 2 + range_offset + (cp - first) as usize * 2;
                    let Some(raw) = be16(d, at) else { continue };
                    if raw != 0 { raw.wrapping_add(delta) } else { 0 }
                };
                if gid != 0 {
                    self.cmap.entry(cp).or_insert(gid);
                }
            }
        }
    }

    fn parse_cmap12(&mut self, t: usize) {
        let d = &self.data;
        if t + 16 > d.len() {
            return;
        }
        let groups = be32(d, t + 12).unwrap_or(0) as u64;
        if t as u64 + 16 + groups * 12 > d.len() as u64 {
            return;
        }
        let mut budget = CMAP_BUDGET;
        for g in 0..groups as usize {
            let rec = t + 16 + g * 12;
            let (Some(first), Some(last), Some(start_glyph)) = (be32(d, rec), be32(d, rec + 4), be32(d, rec + 8))
            else {
                return;
            };
            if last < first || last - first > 0x10FFFF {
                continue;
            }
            for cp in first..=last {
                if budget == 0 {
                    return;
                }
                budget -= 1;
                let gid = start_glyph.wrapping_add(cp - first);
                if gid != 0 && gid < self.num_glyphs as u32 {
                    self.cmap.entry(cp).or_insert(gid as u16);
                }
            }
        }
    }

    // ------------------------------------------------------------- querying

    /// Glyph id for a code point; 0 (.notdef) when the font has none.
    pub fn glyph_for(&self, code_point: u32) -> u16 {
        self.cmap.get(&code_point).copied().unwrap_or(0)
    }

    /// Advance width in em units (1.0 == the font size).
    pub fn advance(&self, glyph: u16) -> f64 {
        match self.advances.get(glyph as usize) {
            Some(&a) => f64::from(a) / f64::from(self.units_per_em),
            None => 0.0,
        }
    }

    /// Advance in font units, for a caller that sums before scaling.
    pub fn advance_units(&self, glyph: u16) -> u16 {
        self.advances.get(glyph as usize).copied().unwrap_or(0)
    }

    /// The glyph ids of a string, one per code point.
    pub fn shape(&self, text: &str) -> Vec<u16> {
        text.chars().map(|c| self.glyph_for(c as u32)).collect()
    }

    /// Width of a string set at `size` points: the advances summed in font
    /// units first, then scaled, in that order, as the C++ code did - the
    /// rounding of the result decides where lines break.
    pub fn measure(&self, text: &str, size: f64) -> f64 {
        let mut units = 0.0f64;
        for c in text.chars() {
            let gid = self.glyph_for(c as u32);
            if let Some(&a) = self.advances.get(gid as usize) {
                units += f64::from(a);
            }
        }
        units * size / f64::from(self.units_per_em)
    }

    pub fn units_per_em(&self) -> i32 {
        self.units_per_em
    }
    pub fn num_glyphs(&self) -> i32 {
        self.num_glyphs
    }
    pub fn x_min(&self) -> i32 {
        self.x_min
    }
    pub fn y_min(&self) -> i32 {
        self.y_min
    }
    pub fn x_max(&self) -> i32 {
        self.x_max
    }
    pub fn y_max(&self) -> i32 {
        self.y_max
    }
    pub fn ascent(&self) -> i32 {
        self.ascent
    }
    pub fn descent(&self) -> i32 {
        self.descent
    }
    pub fn cap_height(&self) -> i32 {
        self.cap_height
    }
    pub fn is_bold(&self) -> bool {
        self.bold
    }

    // ----------------------------------------------------------- subsetting

    fn glyph_range(&self, glyph: usize, glyf: Table) -> Option<(usize, usize)> {
        let start = *self.loca.get(glyph)? as usize;
        let end = *self.loca.get(glyph + 1)? as usize;
        if end <= start || end > glyf.length as usize {
            return None;
        }
        Some((glyf.offset as usize + start, glyf.offset as usize + end))
    }

    fn collect_components(&self, glyph: u16, used: &mut [bool], depth: u32) {
        if depth > 8 || glyph as usize + 1 >= self.loca.len() {
            return;
        }
        let Some(glyf) = self.table(b"glyf") else { return };
        let Some((start, end)) = self.glyph_range(glyph as usize, glyf) else { return };
        if end - start < 10 {
            return;
        }
        let d = &self.data;
        if sbe16(d, start).unwrap_or(0) >= 0 {
            return; // simple glyph: nothing to pull in
        }
        let size = end - start;
        let mut at = 10usize;
        loop {
            if at + 4 > size {
                return;
            }
            let (Some(flags), Some(component)) = (be16(d, start + at), be16(d, start + at + 2)) else {
                return;
            };
            at += 4;
            at += if flags & 0x0001 != 0 { 4 } else { 2 }; // arguments
            if flags & 0x0008 != 0 {
                at += 2; // simple scale
            } else if flags & 0x0040 != 0 {
                at += 4; // x and y scale
            } else if flags & 0x0080 != 0 {
                at += 8; // 2x2 transform
            }
            if let Some(slot) = used.get_mut(component as usize)
                && !*slot
            {
                *slot = true;
                self.collect_components(component, used, depth + 1);
            }
            if flags & 0x0020 == 0 {
                return; // no MORE_COMPONENTS
            }
        }
    }

    /// A valid TrueType file holding only the requested glyphs (plus the
    /// components composites need). Glyph ids are preserved, so a glyph is
    /// either present or empty and the caller's ids stay valid. Empty when the
    /// font has no glyph outlines to subset.
    pub fn subset(&self, glyphs: &[u16]) -> Vec<u8> {
        let mut result = Vec::new();
        let Some(glyf) = self.table(b"glyf") else { return result };
        let n = self.num_glyphs as usize;
        if self.loca.len() != n + 1 {
            return result;
        }

        let mut used = vec![false; n];
        used[0] = true; // .notdef must always be there
        for &gid in glyphs {
            if let Some(slot) = used.get_mut(gid as usize) {
                *slot = true;
            }
        }
        for gid in 0..n {
            if used[gid] {
                self.collect_components(gid as u16, &mut used, 0);
            }
        }

        let mut new_glyf: Vec<u8> = Vec::new();
        let mut new_loca: Vec<u8> = Vec::with_capacity((n + 1) * 4);
        for (gid, &is_used) in used.iter().enumerate() {
            new_loca.extend_from_slice(&(new_glyf.len() as u32).to_be_bytes());
            if !is_used {
                continue;
            }
            let Some((start, end)) = self.glyph_range(gid, glyf) else { continue };
            new_glyf.extend_from_slice(&self.data[start..end]);
            while !new_glyf.len().is_multiple_of(4) {
                new_glyf.push(0);
            }
        }
        new_loca.extend_from_slice(&(new_glyf.len() as u32).to_be_bytes());

        // head is copied with the long loca format forced on and the file
        // checksum cleared; it is filled in once the whole file exists.
        let Some(head) = self.table(b"head").filter(|t| t.length >= 54) else { return result };
        let mut new_head = self.data[head.range()].to_vec();
        new_head[8..12].fill(0);
        new_head[50] = 0;
        new_head[51] = 1;

        let mut out: Vec<(u32, Vec<u8>)> = vec![(tag(b"head"), new_head)];
        // "post" and "name" are dropped: a CIDFontType2 needs neither and post
        // alone is a third of the file. cvt/fpgm/prep stay so hinting works.
        for t in [b"OS/2", b"cvt ", b"fpgm", b"gasp", b"hhea", b"hmtx", b"maxp", b"prep"] {
            if let Some(table) = self.table(t) {
                out.push((tag(t), self.data[table.range()].to_vec()));
            }
        }
        out.push((tag(b"loca"), new_loca));
        out.push((tag(b"glyf"), new_glyf));
        out.sort_by_key(|(t, _)| *t);

        let count = out.len() as u16;
        let mut entry_selector = 0u16;
        while (1u32 << (entry_selector + 1)) <= u32::from(count) {
            entry_selector += 1;
        }
        let search_range = 16u16 << entry_selector;

        result.extend_from_slice(&0x0001_0000u32.to_be_bytes());
        result.extend_from_slice(&count.to_be_bytes());
        result.extend_from_slice(&search_range.to_be_bytes());
        result.extend_from_slice(&entry_selector.to_be_bytes());
        result.extend_from_slice(&(count.wrapping_mul(16).wrapping_sub(search_range)).to_be_bytes());

        let mut offset = 12 + 16 * u32::from(count);
        let mut head_offset = None;
        for (t, data) in &out {
            if *t == tag(b"head") {
                head_offset = Some(offset as usize);
            }
            result.extend_from_slice(&t.to_be_bytes());
            result.extend_from_slice(&checksum(data).to_be_bytes());
            result.extend_from_slice(&offset.to_be_bytes());
            result.extend_from_slice(&(data.len() as u32).to_be_bytes());
            offset += (data.len() as u32 + 3) & !3;
        }
        for (_, data) in &out {
            result.extend_from_slice(data);
            while !result.len().is_multiple_of(4) {
                result.push(0);
            }
        }

        // checkSumAdjustment closes the loop over the finished file.
        let adjustment = 0xB1B0_AFBAu32.wrapping_sub(checksum(&result));
        if let Some(at) = head_offset.map(|o| o + 8)
            && at + 4 <= result.len()
        {
            result[at..at + 4].copy_from_slice(&adjustment.to_be_bytes());
        }
        result
    }
}

/// The regular/bold pair the whole program measures, draws and embeds with.
#[derive(Debug, Clone)]
pub struct FontSet {
    pub regular: Font,
    pub bold: Font,
    /// What the pair is called, for reports. The bundled pair is
    /// "Liberation Sans"; a pair named on cvcli's command line is "explicit".
    pub family: String,
}

impl FontSet {
    pub fn face(&self, bold: bool) -> &Font {
        if bold { &self.bold } else { &self.regular }
    }

    /// The pair that ships inside the program, compiled in. Measuring with it is
    /// what makes a PDF exported on one system the same file as on another.
    pub fn bundled() -> FontSet {
        // Parsed from bytes that are part of this binary and covered by the
        // tests, so a failure here is a broken build, not a runtime condition.
        let face = |bytes: &'static [u8]| Font::from_bytes(bytes.to_vec(), 0).expect("bundled font is valid");
        FontSet {
            regular: face(BUNDLED_REGULAR),
            bold: face(BUNDLED_BOLD),
            family: "Liberation Sans".into(),
        }
    }
}

/// Liberation Sans, SIL OFL 1.1 - see assets/fonts/README.md for why this face.
pub const BUNDLED_REGULAR: &[u8] = include_bytes!("../../../assets/fonts/LiberationSans-Regular.ttf");
pub const BUNDLED_BOLD: &[u8] = include_bytes!("../../../assets/fonts/LiberationSans-Bold.ttf");

#[cfg(test)]
mod tests {
    use super::*;

    fn fnv(data: &[u8]) -> u64 {
        let mut v = 0xCBF2_9CE4_8422_2325u64;
        for &b in data {
            v ^= u64::from(b);
            v = v.wrapping_mul(0x0000_0100_0000_01B3);
        }
        v
    }

    fn milli_em(font: &Font, cp: char) -> i64 {
        (font.advance(font.glyph_for(cp as u32)) * 1000.0 + 0.5) as i64
    }

    /// Header metrics, the whole BMP cmap, every advance and three subsets of
    /// each face, against what the C++ reader reported.
    #[test]
    fn matches_the_cpp_reader() {
        let golden = include_str!("../../../fixtures/golden/font.txt");
        let fonts = FontSet::bundled();
        let mut sections = golden.split_inclusive('\n').peekable();
        for bold in [false, true] {
            let f = fonts.face(bold);
            let header = sections.next().unwrap().trim_end();
            let expected = format!(
                "{} upem={} glyphs={} bbox={},{},{},{} ascent={} descent={} cap={} isBold={}",
                if bold { "bold" } else { "regular" },
                f.units_per_em(),
                f.num_glyphs(),
                f.x_min(),
                f.y_min(),
                f.x_max(),
                f.y_max(),
                f.ascent(),
                f.descent(),
                f.cap_height(),
                u8::from(f.is_bold())
            );
            assert_eq!(header, expected);

            let mut mapped = HashMap::new();
            let mut advances = 0;
            let mut subsets = 0;
            while let Some(line) = sections.peek() {
                let line = line.trim_end();
                if line.starts_with("regular") || line.starts_with("bold") {
                    break;
                }
                sections.next();
                if let Some(rest) = line.strip_prefix('a') {
                    let (g, adv) = rest.split_once(' ').unwrap();
                    let g: u16 = g.parse().unwrap();
                    let adv: f64 = adv.parse().unwrap();
                    assert_eq!(f.advance(g), adv, "advance of glyph {g}");
                    advances += 1;
                } else if let Some(rest) = line.strip_prefix("subset") {
                    let n: usize = rest[..1].parse().unwrap();
                    let set: Vec<u16> = match n {
                        0 => vec![],
                        1 => vec![36, 37, 38],
                        _ => (0..f.num_glyphs()).step_by(7).map(|g| g as u16).collect(),
                    };
                    let sub = f.subset(&set);
                    assert_eq!(rest, format!("{n} size={} fnv={:016x}", sub.len(), fnv(&sub)));
                    subsets += 1;
                } else {
                    let (cp, g) = line.split_once(' ').unwrap();
                    mapped.insert(cp.parse::<u32>().unwrap(), g.parse::<u16>().unwrap());
                }
            }
            assert_eq!(advances, f.num_glyphs());
            assert_eq!(subsets, 3);
            for cp in 0..0x10000u32 {
                assert_eq!(f.glyph_for(cp), mapped.get(&cp).copied().unwrap_or(0), "cmap U+{cp:04X}");
            }
        }
    }

    #[test]
    fn advances_match_the_template_metrics() {
        let fonts = FontSet::bundled();
        let expected = [
            (' ', 278, 278), ('A', 667, 722), ('M', 833, 833), ('a', 556, 556), ('i', 222, 278),
            ('l', 222, 278), ('m', 833, 889), ('w', 722, 778), ('0', 556, 556), ('-', 333, 333),
            ('.', 278, 278), (',', 278, 278), ('/', 278, 278), ('@', 1015, 975), ('А', 667, 722),
            ('а', 556, 556), ('я', 542, 583), ('ё', 556, 556), ('·', 333, 333), ('›', 333, 333),
            ('—', 1000, 1000), ('…', 1000, 1000),
        ];
        for (cp, regular, bold) in expected {
            assert_eq!(milli_em(&fonts.regular, cp), regular, "regular {cp}");
            assert_eq!(milli_em(&fonts.bold, cp), bold, "bold {cp}");
        }
    }

    #[test]
    fn covers_what_the_template_can_set() {
        let fonts = FontSet::bundled();
        for bold in [false, true] {
            let f = fonts.face(bold);
            for range in [0x20..=0x7E, 0xC0..=0xFF, 0x410..=0x44F] {
                for cp in range {
                    assert_ne!(f.glyph_for(cp), 0, "U+{cp:04X}");
                }
            }
        }
    }

    #[test]
    fn measures_by_summing_advances() {
        let f = FontSet::bundled().regular;
        let space = f.advance(f.glyph_for(' ' as u32)) * 9.0;
        let a = f.advance(f.glyph_for('a' as u32)) * 9.0;
        assert!((f.measure("a a", 9.0) - (a + space + a)).abs() < 1e-9);
        assert_eq!(f.measure("", 9.0), 0.0);
    }

    #[test]
    fn reads_a_face_out_of_a_collection() {
        let mut ttc = b"ttcf\x00\x01\x00\x00\x00\x00\x00\x01\x00\x00\x00\x10".to_vec();
        let mut face = BUNDLED_REGULAR.to_vec();
        // Table offsets inside a collection count from the start of the file.
        let tables = u16::from_be_bytes([face[4], face[5]]) as usize;
        for i in 0..tables {
            let at = 12 + 16 * i + 8;
            let off = u32::from_be_bytes(face[at..at + 4].try_into().unwrap()) + 16;
            face[at..at + 4].copy_from_slice(&off.to_be_bytes());
        }
        ttc.extend_from_slice(&face);
        let from_ttc = Font::from_bytes(ttc.clone(), 0).unwrap();
        let plain = FontSet::bundled().regular;
        assert_eq!(from_ttc.glyph_for('Ж' as u32), plain.glyph_for('Ж' as u32));
        assert_eq!(from_ttc.advance(100), plain.advance(100));
        assert_eq!(Font::from_bytes(ttc, 1).unwrap_err(), "в коллекции нет такого начертания");
    }

    /// Security: every truncation of a real font, and a font with its tables
    /// pointed at garbage, is an error or a smaller font - never a panic.
    #[test]
    fn damaged_fonts_do_not_crash() {
        let full = BUNDLED_REGULAR;
        let mut lengths: Vec<usize> = (0..200).collect();
        lengths.extend((200..full.len()).step_by(997));
        for len in lengths {
            if let Ok(f) = Font::from_bytes(full[..len].to_vec(), 0) {
                let _ = f.measure("Привет", 9.0);
                let _ = f.subset(&[1, 2, 3, 500, 65535]);
            }
        }
        // Directory intact, every byte after it inverted.
        let mut garbage = full.to_vec();
        let directory = 12 + 16 * usize::from(u16::from_be_bytes([full[4], full[5]]));
        for b in &mut garbage[directory..] {
            *b = !*b;
        }
        if let Ok(f) = Font::from_bytes(garbage, 0) {
            let _ = f.subset(&(0..f.num_glyphs() as u16).collect::<Vec<_>>());
        }
        assert_eq!(Font::from_bytes(vec![0; 11], 0).unwrap_err(), "файл шрифта слишком мал");
        assert!(Font::from_bytes(b"OTTO\0\0\0\0\0\0\0\0".to_vec(), 0).unwrap_err().contains("CFF"));
    }

    /// Security: a cmap describing the same huge range again and again stops
    /// after a bounded amount of work instead of walking it every time.
    #[test]
    fn cmap_work_is_bounded() {
        let groups = 2000u32;
        let mut t = Vec::new();
        t.extend_from_slice(&12u16.to_be_bytes());
        t.extend_from_slice(&0u16.to_be_bytes());
        t.extend_from_slice(&(16 + 12 * groups).to_be_bytes());
        t.extend_from_slice(&0u32.to_be_bytes());
        t.extend_from_slice(&groups.to_be_bytes());
        for _ in 0..groups {
            t.extend_from_slice(&0u32.to_be_bytes());
            t.extend_from_slice(&0x10FFFFu32.to_be_bytes());
            t.extend_from_slice(&1u32.to_be_bytes());
        }
        let mut f = Font { data: t, num_glyphs: 10, ..Font::default() };
        let started = std::time::Instant::now();
        f.parse_cmap12(0);
        assert!(started.elapsed().as_secs() < 5);
        assert_eq!(f.glyph_for(0), 1);
        assert_eq!(f.glyph_for(9), 0); // gid 10 is out of range
    }
}
