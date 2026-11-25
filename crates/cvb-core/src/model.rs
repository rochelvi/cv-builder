//! The resume and its file format.
//!
//! The JSON layout is the one every earlier version wrote: same key names, same
//! order, same tolerant reading - a missing key or a value of the wrong type
//! falls back to the default, unknown keys are ignored. `to_json(from_json(x))`
//! gives back byte for byte what the C++ version saved for `x`.
//!
//! The serde derives are for the window, not for the file: the page sends and
//! receives these structures with camelCase fields. Anything that arrives that
//! way goes through [`Cv::repaired`] before it is used, the same repair a file
//! gets on reading.

use serde::{Deserialize, Serialize};

use crate::json::{self, Value};

/// Every colour the renderer uses, in the order the editor lists them.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThemeRole {
    Background,
    Rule,
    Heading,
    Body,
    Subtle,
    Faint,
    Accent,
    Accent2,
}

impl ThemeRole {
    pub const ALL: [ThemeRole; 8] = [
        ThemeRole::Background,
        ThemeRole::Rule,
        ThemeRole::Heading,
        ThemeRole::Body,
        ThemeRole::Subtle,
        ThemeRole::Faint,
        ThemeRole::Accent,
        ThemeRole::Accent2,
    ];

    /// The key in the file's "theme" object.
    pub fn key(self) -> &'static str {
        match self {
            ThemeRole::Background => "background",
            ThemeRole::Rule => "rule",
            ThemeRole::Heading => "heading",
            ThemeRole::Body => "body",
            ThemeRole::Subtle => "subtle",
            ThemeRole::Faint => "faint",
            ThemeRole::Accent => "accent",
            ThemeRole::Accent2 => "accent2",
        }
    }
}

/// The document's palette, as the user typed it: "#rrggbb" normally, but a
/// half-typed value is kept as it is and only the renderer falls back.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Theme {
    pub background: String,
    pub rule: String,
    pub heading: String,
    pub body: String,
    pub subtle: String,
    pub faint: String,
    pub accent: String,
    pub accent2: String,
}

impl Default for Theme {
    /// The dark original.
    fn default() -> Self {
        Theme::new([
            "#0d0f12", "#2a2f3a",
            "#e8ecf2", // name, job titles, skill group titles
            "#9aa0ae", // summary and bullet text
            "#7a8292", // contacts, company lines, subtitles
            "#4a5260", // dates, non-highlighted certifications
            "#4ade80", // section titles, bullets, highlighted skills
            "#7ab8f5", // soft skills, highlighted education
        ])
    }
}

impl Theme {
    pub fn new(c: [&str; 8]) -> Self {
        Theme {
            background: c[0].into(),
            rule: c[1].into(),
            heading: c[2].into(),
            body: c[3].into(),
            subtle: c[4].into(),
            faint: c[5].into(),
            accent: c[6].into(),
            accent2: c[7].into(),
        }
    }

    pub fn get(&self, role: ThemeRole) -> &str {
        match role {
            ThemeRole::Background => &self.background,
            ThemeRole::Rule => &self.rule,
            ThemeRole::Heading => &self.heading,
            ThemeRole::Body => &self.body,
            ThemeRole::Subtle => &self.subtle,
            ThemeRole::Faint => &self.faint,
            ThemeRole::Accent => &self.accent,
            ThemeRole::Accent2 => &self.accent2,
        }
    }

    pub fn get_mut(&mut self, role: ThemeRole) -> &mut String {
        match role {
            ThemeRole::Background => &mut self.background,
            ThemeRole::Rule => &mut self.rule,
            ThemeRole::Heading => &mut self.heading,
            ThemeRole::Body => &mut self.body,
            ThemeRole::Subtle => &mut self.subtle,
            ThemeRole::Faint => &mut self.faint,
            ThemeRole::Accent => &mut self.accent,
            ThemeRole::Accent2 => &mut self.accent2,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Preset {
    pub name: &'static str,
    pub theme: Theme,
}

/// The palettes offered in the colour editor. The first is the original.
pub fn presets() -> Vec<Preset> {
    let p = |name, c| Preset { name, theme: Theme::new(c) };
    vec![
        Preset { name: "Тёмная (оригинал)", theme: Theme::default() },
        p("Midnight blue", ["#0b1020", "#22304a", "#eaf1ff", "#a8b6cf", "#8593ad", "#4d5c78", "#5cc8ff", "#b39bff"]),
        p("Graphite orange", ["#141414", "#333333", "#f2f2f2", "#b0b0b0", "#8c8c8c", "#5a5a5a", "#ff8c42", "#ffd166"]),
        p("Paper (light)", ["#ffffff", "#d9dee5", "#111827", "#374151", "#6b7280", "#9ca3af", "#0f766e", "#1d4ed8"]),
        p("Warm cream", ["#f7f3ea", "#ded5c3", "#2b2113", "#4d4335", "#6f6553", "#9c9382", "#b4530a", "#3c6e47"]),
        p("Forest green", ["#0f1712", "#26372c", "#eaf5ec", "#a9bdae", "#87998c", "#4d5c52", "#5fd18c", "#a3e635"]),
        p("Deep plum", ["#160f1c", "#372a44", "#f1e9f7", "#bcaecb", "#9686a8", "#5c4d70", "#d18fff", "#ff8fd1"]),
        p("Slate mono", ["#15181d", "#2c313a", "#eceff3", "#a3aab6", "#7d8593", "#454b56", "#8fa3c8", "#c8b98f"]),
        p("Nordic ice", ["#e5e9f0", "#c7ceda", "#2e3440", "#4c566a", "#5e6a82", "#8c95a8", "#5e81ac", "#88c0d0"]),
        p("Solar amber", ["#1c1810", "#3a3220", "#f7ecd0", "#c9b98f", "#a3936a", "#6b5f42", "#f5a623", "#e05e3e"]),
        p("Rose gold", ["#fdf5f3", "#eaddda", "#3a1f22", "#6b4a4d", "#8f6d6f", "#b89a9c", "#c2185b", "#b06a35"]),
    ]
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Job {
    pub title: String,
    pub period: String,
    pub company: String,
    pub location: String,
    pub bullets: Vec<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Skill {
    pub name: String,
    pub highlight: bool,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SkillGroup {
    pub title: String,
    pub skills: Vec<Skill>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Education {
    pub title: String,
    pub subtitle: String,
    pub highlight: bool,
}

/// One entry of the running order: which block, its printed heading, whether it
/// is drawn, and where. The data stays in the CV either way, so turning a
/// section off never loses what is in it.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SectionRef {
    pub id: String,
    pub label: String,
    pub enabled: bool,
    pub order: i32,
}

/// Every section the renderer can draw, in the order a new CV gets them.
pub const SECTION_IDS: [&str; 8] = [
    "summary",
    "studies",
    "jobs",
    "skill_groups",
    "soft_skills",
    "education",
    "lab",
    "volunteering",
];

const SECTION_LABELS: [&str; 8] = [
    "Summary",
    "Education",
    "Experience",
    "Technical Skills",
    "Soft Skills",
    "Certifications & Courses",
    "Personal Lab",
    "Volunteer Work",
];

/// The English heading a section starts with; empty for an unknown id.
pub fn default_section_label(id: &str) -> &'static str {
    SECTION_IDS
        .iter()
        .position(|known| *known == id)
        .map_or("", |i| SECTION_LABELS[i])
}

pub fn is_known_section(id: &str) -> bool {
    SECTION_IDS.contains(&id)
}

/// The full list, enabled, in default order.
pub fn default_sections() -> Vec<SectionRef> {
    SECTION_IDS
        .iter()
        .zip(SECTION_LABELS)
        .enumerate()
        .map(|(i, (id, label))| SectionRef {
            id: (*id).into(),
            label: label.into(),
            enabled: true,
            order: i as i32,
        })
        .collect()
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Cv {
    pub name: String,
    pub role: String,
    pub email: String,
    pub location: String,
    pub website: String,
    pub summary: String,
    pub jobs: Vec<Job>,
    pub volunteering: Vec<Job>,
    pub skill_groups: Vec<SkillGroup>,
    pub soft_skills: Vec<String>,
    pub education: Vec<Education>,
    /// Studies written up the way jobs are, for people whose university years
    /// carry more than a certificate line.
    pub studies: Vec<Job>,
    pub lab_bullets: Vec<String>,
    /// Every known section exactly once, after [`Cv::repaired`] or reading.
    pub sections: Vec<SectionRef>,
    pub theme: Theme,
}

impl Default for Cv {
    fn default() -> Self {
        Cv {
            name: String::new(),
            role: String::new(),
            email: String::new(),
            location: String::new(),
            website: String::new(),
            summary: String::new(),
            jobs: Vec::new(),
            volunteering: Vec::new(),
            skill_groups: Vec::new(),
            soft_skills: Vec::new(),
            education: Vec::new(),
            studies: Vec::new(),
            lab_bullets: Vec::new(),
            sections: default_sections(),
            theme: Theme::default(),
        }
    }
}

impl Cv {
    /// A blank CV with the placeholder header the "New" command starts from.
    pub fn new_document() -> Cv {
        Cv { name: "Your Name".into(), role: "YOUR ROLE".into(), ..Cv::default() }
    }

    /// The enabled sections, sorted by `order` - what the renderer draws.
    pub fn ordered_sections(&self) -> Vec<SectionRef> {
        let mut out: Vec<SectionRef> = self.sections.iter().filter(|s| s.enabled).cloned().collect();
        out.sort_by_key(|s| s.order); // stable, as std::stable_sort was
        out
    }

    /// The same CV with its running order repaired the way reading a file
    /// repairs it: unknown and duplicate ids dropped, missing ones appended,
    /// orders renumbered. For data that did not come from a file - the page.
    pub fn repaired(mut self) -> Cv {
        let mut out: Vec<SectionRef> = Vec::new();
        for s in std::mem::take(&mut self.sections) {
            if !is_known_section(&s.id) || out.iter().any(|seen| seen.id == s.id) {
                continue;
            }
            out.push(s);
        }
        out.sort_by_key(|s| s.order);
        for fallback in default_sections() {
            if !out.iter().any(|s| s.id == fallback.id) {
                out.push(fallback);
            }
        }
        for (i, s) in out.iter_mut().enumerate() {
            s.order = i as i32;
        }
        self.sections = out;
        self
    }
}

// ------------------------------------------------------------ the file format

fn string_list(value: &Value) -> Vec<String> {
    value.items().iter().filter_map(|item| item.as_str().map(str::to_string)).collect()
}

fn read_job(v: &Value) -> Job {
    Job {
        title: v.get("title").string_or(""),
        period: v.get("period").string_or(""),
        company: v.get("company").string_or(""),
        location: v.get("location").string_or(""),
        bullets: string_list(v.get("bullets")),
    }
}

fn read_jobs(v: &Value) -> Vec<Job> {
    v.items().iter().map(read_job).collect()
}

fn write_job(job: &Job) -> Value {
    let mut v = Value::object();
    v.set("title", job.title.as_str());
    v.set("period", job.period.as_str());
    v.set("company", job.company.as_str());
    v.set("location", job.location.as_str());
    v.set("bullets", write_strings(&job.bullets));
    v
}

fn write_jobs(jobs: &[Job]) -> Value {
    Value::Array(jobs.iter().map(write_job).collect())
}

fn write_strings(items: &[String]) -> Value {
    Value::Array(items.iter().map(|s| Value::String(s.clone())).collect())
}

/// Reads the running order, repairing whatever the file leaves out.
///
/// A file written before the order was editable has no "sections" block; it
/// gets the default list, with the headings that used to live in
/// "volunteer_title" / "lab_title" carried over. A file that has one may still
/// miss a section added later; those are appended, enabled, at the end.
fn read_sections(root: &Value) -> Vec<SectionRef> {
    let mut out: Vec<SectionRef> = Vec::new();
    for item in root.get("sections").items() {
        let id = item.get("id").string_or("");
        if !is_known_section(&id) || out.iter().any(|seen| seen.id == id) {
            continue;
        }
        let label = item.get("label").string_or(default_section_label(&id));
        let enabled = item.get("enabled").bool_or(true);
        // `as` saturates where the C++ cast was undefined; within i32 the two
        // agree, truncating towards zero.
        let order = item.get("order").number_or(out.len() as f64) as i32;
        out.push(SectionRef { id, label, enabled, order });
    }

    let legacy = out.is_empty();
    out.sort_by_key(|s| s.order);

    for fallback in default_sections() {
        if !out.iter().any(|s| s.id == fallback.id) {
            out.push(fallback);
        }
    }

    if legacy {
        for s in &mut out {
            match s.id.as_str() {
                "volunteering" => s.label = root.get("volunteer_title").string_or(&s.label),
                "lab" => s.label = root.get("lab_title").string_or(&s.label),
                // Back when this was the only education block it said so.
                "education" => s.label = "Education & Certifications".into(),
                _ => {}
            }
        }
    }

    for (i, s) in out.iter_mut().enumerate() {
        s.order = i as i32;
    }
    out
}

/// Resume file bytes to a CV. Fails only on malformed JSON or a document that is
/// not an object; anything else unrecognised is left at its default.
pub fn from_json(bytes: &[u8]) -> Result<Cv, String> {
    let root = json::parse(bytes)?;
    if !root.is_object() {
        return Err("ожидался объект JSON".into());
    }

    let mut cv = Cv {
        name: root.get("name").string_or(""),
        role: root.get("role").string_or(""),
        email: root.get("email").string_or(""),
        location: root.get("location").string_or(""),
        website: root.get("website").string_or(""),
        summary: root.get("summary").string_or(""),
        jobs: read_jobs(root.get("jobs")),
        volunteering: read_jobs(root.get("volunteering")),
        ..Cv::default()
    };

    for g in root.get("skill_groups").items() {
        cv.skill_groups.push(SkillGroup {
            title: g.get("title").string_or(""),
            skills: g
                .get("skills")
                .items()
                .iter()
                .map(|s| Skill { name: s.get("name").string_or(""), highlight: s.get("highlight").bool_or(false) })
                .collect(),
        });
    }

    cv.soft_skills = string_list(root.get("soft_skills"));
    cv.education = root
        .get("education")
        .items()
        .iter()
        .map(|e| Education {
            title: e.get("title").string_or(""),
            subtitle: e.get("subtitle").string_or(""),
            highlight: e.get("highlight").bool_or(false),
        })
        .collect();
    cv.studies = read_jobs(root.get("studies"));
    cv.lab_bullets = string_list(root.get("lab_bullets"));
    cv.sections = read_sections(&root);

    let theme = root.get("theme");
    for role in ThemeRole::ALL {
        if let Some(value) = theme.get(role.key()).as_str() {
            *cv.theme.get_mut(role) = value.to_string();
        }
    }
    Ok(cv)
}

/// A CV as the file stores it: UTF-8, no BOM, two-space indent, final newline.
pub fn to_json(cv: &Cv) -> String {
    let mut root = Value::object();
    root.set("name", cv.name.as_str());
    root.set("role", cv.role.as_str());
    root.set("email", cv.email.as_str());
    root.set("location", cv.location.as_str());
    root.set("website", cv.website.as_str());
    root.set("summary", cv.summary.as_str());
    root.set("jobs", write_jobs(&cv.jobs));
    root.set("volunteering", write_jobs(&cv.volunteering));

    let groups = cv
        .skill_groups
        .iter()
        .map(|group| {
            let mut g = Value::object();
            g.set("title", group.title.as_str());
            let skills = group
                .skills
                .iter()
                .map(|skill| {
                    let mut s = Value::object();
                    s.set("name", skill.name.as_str());
                    s.set("highlight", skill.highlight);
                    s
                })
                .collect::<Vec<_>>();
            g.set("skills", skills);
            g
        })
        .collect::<Vec<_>>();
    root.set("skill_groups", groups);
    root.set("soft_skills", write_strings(&cv.soft_skills));

    let education = cv
        .education
        .iter()
        .map(|item| {
            let mut e = Value::object();
            e.set("title", item.title.as_str());
            e.set("subtitle", item.subtitle.as_str());
            e.set("highlight", item.highlight);
            e
        })
        .collect::<Vec<_>>();
    root.set("education", education);
    root.set("studies", write_jobs(&cv.studies));
    root.set("lab_bullets", write_strings(&cv.lab_bullets));

    let sections = cv
        .sections
        .iter()
        .map(|s| {
            let mut item = Value::object();
            item.set("id", s.id.as_str());
            item.set("label", s.label.as_str());
            item.set("enabled", s.enabled);
            item.set("order", f64::from(s.order));
            item
        })
        .collect::<Vec<_>>();
    root.set("sections", sections);

    let mut theme = Value::object();
    for role in ThemeRole::ALL {
        theme.set(role.key(), cv.theme.get(role));
    }
    root.set("theme", theme);

    json::dump(&root, 2)
}

#[cfg(test)]
mod tests {
    use super::*;

    const INPUTS: [&str; 7] = ["sample", "reference", "empty", "bom", "legacy", "tolerant", "long"];

    fn fixture(path: &str) -> Vec<u8> {
        let full = format!("{}/../../fixtures/{path}", env!("CARGO_MANIFEST_DIR"));
        std::fs::read(&full).unwrap_or_else(|e| panic!("{full}: {e}"))
    }

    /// What Save writes for each fixture is what the C++ version wrote.
    #[test]
    fn saving_matches_the_cpp_output() {
        for name in INPUTS {
            let cv = from_json(&fixture(&format!("input/{name}.json"))).unwrap();
            let golden = String::from_utf8(fixture(&format!("golden/{name}.saved.json"))).unwrap();
            assert_eq!(to_json(&cv), golden, "{name}");
        }
        assert_eq!(to_json(&Cv::default()).as_bytes(), fixture("golden/default.saved.json"));
        assert_eq!(to_json(&Cv::new_document()).as_bytes(), fixture("golden/new.saved.json"));
    }

    #[test]
    fn round_trips_every_field() {
        let before = from_json(&fixture("input/long.json")).unwrap();
        let after = from_json(to_json(&before).as_bytes()).unwrap();
        assert_eq!(after, before);
    }

    #[test]
    fn legacy_file_gets_its_old_headings() {
        let cv = from_json(&fixture("input/legacy.json")).unwrap();
        assert_eq!(cv.sections.len(), SECTION_IDS.len());
        let label = |id: &str| cv.sections.iter().find(|s| s.id == id).unwrap().label.clone();
        assert_eq!(label("volunteering"), "Помощь людям");
        assert_eq!(label("lab"), "Домашний стенд");
        assert_eq!(label("education"), "Education & Certifications");
        assert!(cv.sections.iter().all(|s| s.enabled));
    }

    #[test]
    fn refuses_what_is_not_an_object() {
        assert!(from_json(b"{\"name\":").is_err());
        assert_eq!(from_json(b"[1,2,3]").unwrap_err(), "ожидался объект JSON");
    }

    /// Security: a CV that arrives from the page with an invented, duplicated or
    /// missing section is repaired to exactly the known set before use.
    #[test]
    fn repairs_sections_from_outside() {
        let cv = Cv {
            sections: vec![
                SectionRef { id: "jobs".into(), label: "A".into(), enabled: true, order: 5 },
                SectionRef { id: "jobs".into(), label: "B".into(), enabled: true, order: 0 },
                SectionRef { id: "../../etc".into(), label: "x".into(), enabled: true, order: 1 },
                SectionRef { id: "summary".into(), label: "S".into(), enabled: false, order: -3 },
            ],
            ..Cv::default()
        };
        let cv = cv.repaired();
        let ids: Vec<&str> = cv.sections.iter().map(|s| s.id.as_str()).collect();
        assert_eq!(ids.len(), SECTION_IDS.len());
        assert_eq!(&ids[..2], &["summary", "jobs"]);
        assert_eq!(cv.sections[1].label, "A");
        for id in SECTION_IDS {
            assert_eq!(ids.iter().filter(|x| **x == id).count(), 1);
        }
        assert!(cv.sections.iter().enumerate().all(|(i, s)| s.order == i as i32));
        assert!(cv.ordered_sections().iter().all(|s| s.id != "summary"));
    }

    #[test]
    fn presets_start_with_the_original() {
        let all = presets();
        assert_eq!(all.len(), 11);
        assert_eq!(all[0].theme, Theme::default());
    }
}
