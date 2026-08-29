// The CV data model and its JSON form.
//
// The JSON layout is the one the Python version wrote, so existing cv.json
// files keep working: same key names, same order, same tolerant reading
// (missing keys fall back to defaults, unknown keys are ignored).
#pragma once

#include <string>
#include <vector>

#include "file.h"

namespace cvb {

// Every colour the renderer can use. Kept as an indexed array so the colour
// editor can walk the roles generically instead of naming eight fields.
enum ThemeRole {
    TR_Background = 0,
    TR_Rule,
    TR_Heading,
    TR_Body,
    TR_Subtle,
    TR_Faint,
    TR_Accent,
    TR_Accent2,
    TR_Count
};

extern const char* const kThemeKeys[TR_Count];  // JSON keys

struct Theme {
    std::string c[TR_Count];
    Theme();  // the dark original
    bool operator==(const Theme& other) const;
};

// The name is UTF-8: the palette is model data, and which of the front ends
// shows it - and in what string type - is not the model's business.
struct Preset {
    const char* name;
    Theme theme;
};
const std::vector<Preset>& presets();

struct Job {
    std::string title;
    std::string period;
    std::string company;
    std::string location;
    std::vector<std::string> bullets;
};

struct Skill {
    std::string name;
    bool highlight = false;
};

struct SkillGroup {
    std::string title;
    std::vector<Skill> skills;
};

struct Education {
    std::string title;
    std::string subtitle;
    bool highlight = false;
};

// One entry of the section list: which block of the CV it refers to, what
// heading it prints, whether it is drawn at all, and where it sits in the
// document. The data itself stays in the fields below - this only describes
// the running order, so turning a section off never loses what is in it.
struct SectionRef {
    std::string id;     // one of kSectionIds
    std::string label;  // heading printed on the page
    bool enabled = true;
    int order = 0;
};

// Every section the renderer knows how to draw, in the order a CV gets before
// the running order is edited.
constexpr int kSectionCount = 8;
extern const char* const kSectionIds[kSectionCount];

// The English heading a section starts with; `id` unknown yields an empty
// string, which the renderer treats as "no heading of its own".
const char* defaultSectionLabel(const std::string& id);

// The full list, enabled, in default order - what a CV without a `sections`
// block is given.
std::vector<SectionRef> defaultSections();

struct CV {
    std::string name;
    std::string role;
    std::string email;
    std::string location;
    std::string website;
    std::string summary;
    std::vector<Job> jobs;
    std::vector<Job> volunteering;
    std::vector<SkillGroup> skillGroups;
    std::vector<std::string> softSkills;
    std::vector<Education> education;
    // Studies written up the way jobs are - degree, institution, dates and
    // what was actually done there - for people whose university years carry
    // more than a certificate line.
    std::vector<Job> studies;
    std::vector<std::string> labBullets;
    // Always holds every known section exactly once: reading repairs anything
    // missing, duplicated or unknown, so the renderer and the editor can walk
    // it without checking.
    std::vector<SectionRef> sections = defaultSections();
    Theme theme;
};

// The enabled sections, sorted by `order` - what the renderer draws.
std::vector<SectionRef> orderedSections(const CV& cv);

// UTF-8 JSON text <-> CV. `fromJson` never fails on well-formed JSON: anything
// it does not recognise is left at its default.
std::string toJson(const CV& cv);
bool fromJson(const std::string& text, CV& cv, std::string& error);

// UTF-8 on disk, no BOM, on every platform - a file written on one opens on the
// others unchanged.
bool load(const Path& path, CV& cv, std::string& error);
bool save(const Path& path, const CV& cv, std::string& error);

// A blank CV with the placeholder header the "New" command starts from.
CV emptyCV();

}  // namespace cvb
