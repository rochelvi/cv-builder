//! Turns a CV into positioned drawing primitives - the single source of truth
//! for the page design.
//!
//! Nothing here knows about PDF or about windows: it emits pages of text runs,
//! rules and fills in points, measured from the top-left corner of the sheet.
//! The PDF writer and the preview are both backends for this list, which is
//! why they cannot drift apart.
//!
//! Ported line for line from the C++ engine, down to the order in which
//! floating-point sums are taken: a baseline that differs in the last bit can
//! decide whether a block still fits on the page.

use serde::Serialize;

use crate::font::{Font, FontSet};
use crate::model::{Cv, Education, Job, SectionRef, SkillGroup, Theme, ThemeRole, default_section_label};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize)]
pub struct Rgb {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

impl Rgb {
    /// "#rrggbb", lower case.
    pub fn hex(self) -> String {
        format!("#{:02x}{:02x}{:02x}", self.r, self.g, self.b)
    }
}

/// Accepts "#rrggbb" and bare "rrggbb"; anything else yields `fallback`, so a
/// half-typed colour in the editor never breaks the render.
pub fn parse_color(text: &str, fallback: Rgb) -> Rgb {
    let bytes = text.as_bytes();
    let digits = bytes.strip_prefix(b"#").unwrap_or(bytes);
    if digits.len() != 6 {
        return fallback;
    }
    let mut v = [0u8; 6];
    for (slot, &c) in v.iter_mut().zip(digits) {
        match (c as char).to_digit(16) {
            Some(d) => *slot = d as u8,
            None => return fallback,
        }
    }
    Rgb { r: v[0] * 16 + v[1], g: v[2] * 16 + v[3], b: v[4] * 16 + v[5] }
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TextItem {
    pub x: f64,
    /// The baseline, measured down from the page top.
    pub y: f64,
    pub size: f64,
    pub bold: bool,
    pub color: Rgb,
    pub text: String,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LineItem {
    pub x1: f64,
    pub y1: f64,
    pub x2: f64,
    pub y2: f64,
    pub width: f64,
    pub color: Rgb,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RectItem {
    pub x: f64,
    pub y: f64,
    pub w: f64,
    pub h: f64,
    pub color: Rgb,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Page {
    pub background: RectItem,
    pub lines: Vec<LineItem>,
    pub texts: Vec<TextItem>,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Document {
    /// A4, in points.
    pub width: f64,
    pub height: f64,
    pub pages: Vec<Page>,
    pub title: String,
    pub author: String,
}

/// A4 at 72 dpi.
pub const PAGE_WIDTH: f64 = 595.2755905511812;
pub const PAGE_HEIGHT: f64 = 841.8897637795276;

// The template's measurements. Every gap below is a deliberate part of the
// design, not a round number waiting to be tidied up.
const LEFT: f64 = 42.85;
const RIGHT: f64 = 552.43;
const BOTTOM: f64 = 28.0;
const COLUMN_WIDTH: f64 = RIGHT - LEFT;
const COLUMN2_X: f64 = 297.6; // right column of the skills grid
const SKILL_X: f64 = 48.2;
const COLUMN_STEP: f64 = 173.85; // contacts and the education grid
const TOP_AFTER_BREAK: f64 = 52.0;

const BULLET_MARK: &str = "\u{203A}"; // ›
const MIDDOT: &str = " \u{00B7} "; // " · "
const SKILL_MARK: &str = "\u{00B7} "; // "· "

fn is_space(c: u8) -> bool {
    matches!(c, b' ' | b'\t' | b'\r' | b'\n' | b'\x0c' | b'\x0b')
}

fn blank(s: &str) -> bool {
    s.bytes().all(is_space)
}

/// ASCII-only, which is all the section titles need; Cyrillic titles are left
/// as typed.
fn upper(s: &str) -> String {
    s.to_ascii_uppercase()
}

fn split_words(text: &str) -> Vec<&str> {
    // The separators are all ASCII, so every cut falls on a character boundary.
    text.split(|c: char| c.is_ascii() && is_space(c as u8)).filter(|w| !w.is_empty()).collect()
}

/// Lines on '\n'; every '\r' anywhere is dropped, as the C++ splitter did.
fn split_lines(text: &str) -> Vec<String> {
    text.split('\n').map(|line| line.replace('\r', "")).collect()
}

struct Builder<'a> {
    cv: &'a Cv,
    fonts: &'a FontSet,
    palette: [Rgb; 8],
    doc: Document,
    y: f64, // distance from the top of the page
}

impl<'a> Builder<'a> {
    fn new(cv: &'a Cv, fonts: &'a FontSet) -> Self {
        // Resolved against the built-in theme, so a bad value falls back to the
        // role's original colour rather than to black.
        let defaults = Theme::default();
        let mut palette = [Rgb::default(); 8];
        for (i, role) in ThemeRole::ALL.into_iter().enumerate() {
            palette[i] = parse_color(cv.theme.get(role), parse_color(defaults.get(role), Rgb::default()));
        }
        let title = if cv.name.is_empty() { "CV".to_string() } else { format!("{} \u{2014} CV", cv.name) };
        Builder {
            cv,
            fonts,
            palette,
            doc: Document { width: PAGE_WIDTH, height: PAGE_HEIGHT, pages: Vec::new(), title, author: cv.name.clone() },
            y: 0.0,
        }
    }

    fn run(mut self) -> Document {
        self.new_page();
        self.header();
        for section in self.cv.ordered_sections() {
            self.draw(&section);
        }
        self.doc
    }

    /// One section of the running order; an unknown id is skipped.
    fn draw(&mut self, section: &SectionRef) {
        let title = if section.label.is_empty() {
            default_section_label(&section.id).to_string()
        } else {
            section.label.clone()
        };
        match section.id.as_str() {
            "summary" => self.summary(&title),
            "jobs" => self.experience(&title),
            "skill_groups" => self.skills(&title),
            "soft_skills" => self.soft_skills(&title),
            "studies" => self.studies(&title),
            "education" => self.education(&title),
            "volunteering" => self.volunteering(&title),
            "lab" => self.lab(&title),
            _ => {}
        }
    }

    // ---------- primitives ----------

    fn font(&self, bold: bool) -> &'a Font {
        self.fonts.face(bold)
    }

    fn color(&self, role: ThemeRole) -> Rgb {
        self.palette[role as usize]
    }

    fn new_page(&mut self) {
        let background = RectItem { x: 0.0, y: 0.0, w: PAGE_WIDTH, h: PAGE_HEIGHT, color: self.color(ThemeRole::Background) };
        self.doc.pages.push(Page { background, lines: Vec::new(), texts: Vec::new() });
    }

    fn page(&mut self) -> &mut Page {
        self.doc.pages.last_mut().expect("a page is opened before anything is drawn")
    }

    fn text(&mut self, x: f64, y: f64, s: &str, bold: bool, size: f64, role: ThemeRole) {
        if s.is_empty() {
            return;
        }
        let color = self.color(role);
        self.page().texts.push(TextItem { x, y, size, bold, color, text: s.to_string() });
    }

    fn text_right(&mut self, x: f64, y: f64, s: &str, bold: bool, size: f64, role: ThemeRole) {
        let width = self.font(bold).measure(s, size);
        self.text(x - width, y, s, bold, size, role);
    }

    fn rule(&mut self, y: f64, width: f64) {
        let color = self.color(ThemeRole::Rule);
        self.page().lines.push(LineItem { x1: LEFT, y1: y, x2: RIGHT, y2: y, width, color });
    }

    fn page_break(&mut self) {
        self.new_page();
        self.y = TOP_AFTER_BREAK;
    }

    fn room(&self) -> f64 {
        PAGE_HEIGHT - BOTTOM - self.y
    }

    fn need(&mut self, space: f64) {
        if space > self.room() {
            self.page_break();
        }
    }

    fn wrap(&self, text: &str, bold: bool, size: f64, width: f64) -> Vec<String> {
        let f = self.font(bold);
        let mut lines = Vec::new();
        let mut current = String::new();
        for word in split_words(text) {
            let candidate = if current.is_empty() { word.to_string() } else { format!("{current} {word}") };
            if current.is_empty() || f.measure(&candidate, size) <= width {
                current = candidate;
            } else {
                lines.push(std::mem::replace(&mut current, word.to_string()));
            }
        }
        if !current.is_empty() {
            lines.push(current);
        }
        if lines.is_empty() {
            lines.push(String::new());
        }
        lines
    }

    // ---------- blocks ----------

    fn header(&mut self) {
        let cv = self.cv;
        self.y = 47.0;
        self.text(LEFT, self.y, &cv.name, true, 20.0, ThemeRole::Heading);
        self.y += 16.8;
        if !cv.role.is_empty() {
            self.text(LEFT, self.y, &cv.role, false, 9.0, ThemeRole::Accent);
        }
        self.y += 18.6;
        for (i, contact) in [&cv.email, &cv.location, &cv.website].into_iter().enumerate() {
            if !contact.is_empty() {
                self.text(LEFT + i as f64 * COLUMN_STEP, self.y, contact, false, 9.0, ThemeRole::Subtle);
            }
        }
        self.y += 8.5;
        self.rule(self.y, 0.7);
    }

    fn section(&mut self, title: &str, gap: f64) {
        self.need(60.0);
        self.y += gap;
        self.text(LEFT, self.y, &upper(title), true, 8.0, ThemeRole::Accent);
        self.y += 8.25;
        self.rule(self.y, 0.5);
    }

    fn bullets(&mut self, items: &[&str], indent: f64) {
        let text_x = LEFT + indent + 5.4;
        for (i, item) in items.iter().enumerate() {
            let lines = self.wrap(item, false, 9.0, RIGHT - text_x);
            self.need(14.4 + 13.0 * (lines.len() - 1) as f64);
            self.y += if i == 0 { 11.3 } else { 14.4 };
            self.text(LEFT + indent, self.y, BULLET_MARK, false, 9.0, ThemeRole::Accent);
            for (j, line) in lines.iter().enumerate() {
                if j > 0 {
                    self.y += 13.0;
                }
                self.text(text_x, self.y, line, false, 9.0, ThemeRole::Body);
            }
        }
    }

    fn summary(&mut self, title: &str) {
        let cv = self.cv;
        if blank(&cv.summary) {
            return;
        }
        self.section(title, 15.9);
        let mut lines: Vec<String> = Vec::new();
        for paragraph in split_lines(&cv.summary) {
            if blank(&paragraph) {
                lines.push(String::new());
            } else {
                lines.extend(self.wrap(&paragraph, false, 9.5, COLUMN_WIDTH));
            }
        }
        self.y += 12.15;
        for (i, line) in lines.iter().enumerate() {
            if i > 0 {
                self.need(15.0);
                self.y += 15.0;
            }
            self.text(LEFT, self.y, line, false, 9.5, ThemeRole::Body);
        }
    }

    fn job_entries(&mut self, jobs: &[&Job]) {
        for (i, job) in jobs.iter().enumerate() {
            self.need(55.0);
            self.y += if i == 0 { 13.1 } else { 21.8 };
            self.text(LEFT - 6.0, self.y, &job.title, true, 11.0, ThemeRole::Heading);
            if !job.period.is_empty() {
                self.text_right(RIGHT, self.y, &job.period, false, 9.0, ThemeRole::Faint);
            }
            let mut subtitle = job.company.clone();
            if !job.location.is_empty() {
                subtitle = if subtitle.is_empty() { job.location.clone() } else { format!("{subtitle}{MIDDOT}{}", job.location) };
            }
            if !subtitle.is_empty() {
                self.y += 13.2;
                self.text(LEFT, self.y, &subtitle, false, 9.0, ThemeRole::Subtle);
            }
            self.y += 4.5;
            let items: Vec<&str> = job.bullets.iter().map(String::as_str).filter(|b| !blank(b)).collect();
            self.bullets(&items, 10.0);
        }
    }

    fn experience(&mut self, title: &str) {
        let cv = self.cv;
        if cv.jobs.is_empty() {
            return;
        }
        self.section(title, 19.1);
        self.job_entries(&cv.jobs.iter().collect::<Vec<_>>());
    }

    fn worth_drawing(job: &Job) -> bool {
        !job.title.is_empty() || !job.company.is_empty() || !job.bullets.is_empty()
    }

    /// The long form of education: the same shape as a job.
    fn studies(&mut self, title: &str) {
        let cv = self.cv;
        let items: Vec<&Job> = cv.studies.iter().filter(|j| Self::worth_drawing(j)).collect();
        if items.is_empty() {
            return;
        }
        self.section(title, 19.1);
        self.job_entries(&items);
    }

    fn volunteering(&mut self, title: &str) {
        let cv = self.cv;
        let items: Vec<&Job> = cv.volunteering.iter().filter(|j| Self::worth_drawing(j)).collect();
        if items.is_empty() {
            return;
        }
        self.section(title, 19.1);
        self.job_entries(&items);
    }

    /// Space one skill group takes in its column, mirroring draw_skills.
    fn group_height(group: &SkillGroup, first: bool) -> f64 {
        let count = group.skills.iter().filter(|s| !blank(&s.name)).count();
        let gap = if first { 11.3 } else { 16.9 };
        gap + if count > 0 { 14.8 + 11.0 * (count - 1) as f64 } else { 0.0 }
    }

    /// How many groups fit in `space`, filling the two columns alternately.
    // Indexed on purpose: the column is the position's parity, as in the C++.
    #[allow(clippy::needless_range_loop)]
    fn skills_that_fit(groups: &[&SkillGroup], from: usize, space: f64) -> usize {
        let mut heights = [0.0f64; 2];
        let mut counts = [0usize; 2];
        for i in from..groups.len() {
            let column = (i - from) % 2;
            let height = heights[column] + Self::group_height(groups[i], counts[column] == 0);
            if height.max(heights[1 - column]) > space {
                return i - from;
            }
            heights[column] = height;
            counts[column] += 1;
        }
        groups.len() - from
    }

    #[allow(clippy::needless_range_loop)] // two columns, indexed by number
    fn draw_skills(&mut self, groups: &[&SkillGroup], from: usize, count: usize) {
        let top = self.y;
        let mut bottom = top;
        let column_x = [SKILL_X, COLUMN2_X];
        for column in 0..2 {
            self.y = top;
            for (index, i) in (column..count).step_by(2).enumerate() {
                let group = groups[from + i];
                self.y += if index == 0 { 11.3 } else { 16.9 };
                self.text(column_x[column], self.y, &group.title, true, 9.0, ThemeRole::Heading);
                let mut drawn = 0;
                for skill in &group.skills {
                    if blank(&skill.name) {
                        continue;
                    }
                    self.y += if drawn == 0 { 14.8 } else { 11.0 };
                    let role = if skill.highlight { ThemeRole::Accent } else { ThemeRole::Body };
                    self.text(column_x[column], self.y, &format!("{SKILL_MARK}{}", skill.name), false, 8.0, role);
                    drawn += 1;
                }
            }
            bottom = bottom.max(self.y);
        }
        self.y = bottom;
    }

    fn skills(&mut self, title: &str) {
        let cv = self.cv;
        let groups: Vec<&SkillGroup> =
            cv.skill_groups.iter().filter(|g| !g.title.is_empty() || !g.skills.is_empty()).collect();
        if groups.is_empty() {
            return;
        }
        self.section(title, 19.1);
        let mut from = 0;
        while from < groups.len() {
            let mut take = Self::skills_that_fit(&groups, from, self.room());
            if take == 0 {
                // Nothing fits below the cursor - carry on overleaf.
                self.page_break();
                take = Self::skills_that_fit(&groups, from, self.room()).max(1);
            }
            self.draw_skills(&groups, from, take);
            from += take;
        }
    }

    fn soft_skills(&mut self, title: &str) {
        let cv = self.cv;
        let items: Vec<&str> = cv.soft_skills.iter().map(String::as_str).filter(|s| !blank(s)).collect();
        if items.is_empty() {
            return;
        }
        self.section(title, 22.4);

        let f = self.font(false);
        let separator_width = f.measure(MIDDOT, 8.0);

        let mut lines: Vec<Vec<&str>> = Vec::new();
        let mut used = 0.0f64;
        for &item in &items {
            let width = f.measure(item, 8.0);
            match lines.last_mut() {
                Some(line) if LEFT + used + separator_width + width <= RIGHT => {
                    line.push(item);
                    used += separator_width + width;
                }
                _ => {
                    lines.push(vec![item]);
                    used = width;
                }
            }
        }

        let line_count = lines.len();
        for (i, line) in lines.iter().enumerate() {
            self.need(12.0);
            self.y += if i == 0 { 10.3 } else { 12.0 };
            let mut x = LEFT;
            for (j, item) in line.iter().enumerate() {
                self.text(x, self.y, item, false, 8.0, ThemeRole::Accent2);
                x += f.measure(item, 8.0);
                let last = i == line_count - 1 && j == line.len() - 1;
                if !last && x + separator_width <= RIGHT {
                    self.text(x, self.y, MIDDOT, false, 8.0, ThemeRole::Accent);
                    x += separator_width;
                }
            }
        }
    }

    fn education(&mut self, title: &str) {
        let cv = self.cv;
        let items: Vec<&Education> =
            cv.education.iter().filter(|e| !e.title.is_empty() || !e.subtitle.is_empty()).collect();
        if items.is_empty() {
            return;
        }
        self.section(title, 16.6);

        let mut trailing = 0.0f64; // height the previous row's subtitles added below its baseline
        for (row, chunk) in items.chunks(3).enumerate() {
            let subtitled = chunk.iter().any(|e| !e.subtitle.is_empty());
            let extra = if subtitled { 13.0 } else { 0.0 };
            let gap = if row == 0 { 11.25 } else { 17.0 + trailing };
            self.need(gap + extra);
            self.y += gap;
            for (k, item) in chunk.iter().enumerate() {
                let x = LEFT + k as f64 * COLUMN_STEP - 6.0;
                let role = if item.highlight { ThemeRole::Accent2 } else { ThemeRole::Faint };
                self.text(x, self.y, &item.title, true, 9.0, role);
                if !item.subtitle.is_empty() {
                    self.text(x, self.y + 13.0, &item.subtitle, false, 8.0, ThemeRole::Subtle);
                }
            }
            trailing = extra;
        }
        self.y += trailing; // the last row's subtitles still sit below the cursor
    }

    fn lab(&mut self, title: &str) {
        let cv = self.cv;
        let items: Vec<&str> = cv.lab_bullets.iter().map(String::as_str).filter(|b| !blank(b)).collect();
        if items.is_empty() {
            return;
        }
        self.section(title, 17.7);
        self.bullets(&items, 10.0);
    }
}

/// Lays out a CV on A4 pages with the given fonts.
pub fn layout(cv: &Cv, fonts: &FontSet) -> Document {
    Builder::new(cv, fonts).run()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::json::{self, Value};
    use crate::model;

    const INPUTS: [&str; 7] = ["sample", "reference", "empty", "bom", "legacy", "tolerant", "long"];

    fn fixture(path: &str) -> Vec<u8> {
        let full = format!("{}/../../fixtures/{path}", env!("CARGO_MANIFEST_DIR"));
        std::fs::read(&full).unwrap_or_else(|e| panic!("{full}: {e}"))
    }

    fn num(v: &Value) -> f64 {
        match v {
            Value::Number(n) => *n,
            other => panic!("number expected, got {other:?}"),
        }
    }

    fn color(v: &Value) -> String {
        format!("#{}", v.as_str().unwrap())
    }

    /// Every page, rule and run of text, coordinates compared exactly.
    #[test]
    fn matches_the_cpp_layout() {
        let fonts = FontSet::bundled();
        for name in INPUTS {
            let cv = model::from_json(&fixture(&format!("input/{name}.json"))).unwrap();
            let doc = layout(&cv, &fonts);
            let golden = json::parse(&fixture(&format!("golden/{name}.layout.json"))).unwrap();

            assert_eq!(doc.width, num(golden.get("width")), "{name}");
            assert_eq!(doc.height, num(golden.get("height")), "{name}");
            assert_eq!(doc.title, golden.get("title").as_str().unwrap(), "{name}");
            assert_eq!(doc.author, golden.get("author").as_str().unwrap(), "{name}");
            let pages = golden.get("pages").items();
            assert_eq!(doc.pages.len(), pages.len(), "{name}: page count");

            for (p, (page, want)) in doc.pages.iter().zip(pages).enumerate() {
                let bg = want.get("background").items();
                let r = &page.background;
                assert_eq!([r.x, r.y, r.w, r.h], [num(&bg[0]), num(&bg[1]), num(&bg[2]), num(&bg[3])]);
                assert_eq!(r.color.hex(), color(&bg[4]));

                let lines = want.get("lines").items();
                assert_eq!(page.lines.len(), lines.len(), "{name} p{p}: rules");
                for (l, w) in page.lines.iter().zip(lines) {
                    let w = w.items();
                    assert_eq!(
                        [l.x1, l.y1, l.x2, l.y2, l.width],
                        [num(&w[0]), num(&w[1]), num(&w[2]), num(&w[3]), num(&w[4])],
                        "{name} p{p}: rule"
                    );
                    assert_eq!(l.color.hex(), color(&w[5]));
                }

                let texts = want.get("texts").items();
                assert_eq!(page.texts.len(), texts.len(), "{name} p{p}: texts");
                for (t, w) in page.texts.iter().zip(texts) {
                    let w = w.items();
                    assert_eq!(t.text, w[5].as_str().unwrap(), "{name} p{p}");
                    assert_eq!([t.x, t.y, t.size], [num(&w[0]), num(&w[1]), num(&w[2])], "{name} p{p}: {:?}", t.text);
                    assert_eq!(Value::Bool(t.bold), w[3]);
                    assert_eq!(t.color.hex(), color(&w[4]));
                }
            }
        }
    }

    #[test]
    fn parses_colours_tolerantly() {
        let black = Rgb::default();
        assert_eq!(parse_color("#4ade80", black), Rgb { r: 0x4a, g: 0xde, b: 0x80 });
        assert_eq!(parse_color("4ADE80", black), Rgb { r: 0x4a, g: 0xde, b: 0x80 });
        let fallback = Rgb { r: 1, g: 2, b: 3 };
        for bad in ["", "#", "#12345", "#1234567", "zzzzzz", "#ab cd1", "#ффф"] {
            assert_eq!(parse_color(bad, fallback), fallback, "{bad:?}");
        }
    }

    #[test]
    fn splits_like_the_cpp_helpers() {
        assert_eq!(split_words("  a\tb\u{b}c\u{c}d \r\n e  "), vec!["a", "b", "c", "d", "e"]);
        assert_eq!(split_words("non\u{a0}breaking"), vec!["non\u{a0}breaking"]);
        assert_eq!(split_lines("a\r\nb\rc\n"), vec!["a", "bc", ""]);
        assert!(blank(" \t\r\n"));
        assert!(!blank("\u{a0}"));
    }
}
