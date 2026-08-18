// The CV data model and its JSON form.
//
// The JSON layout is the one the Python version wrote, so existing cv.json
// files keep working: same key names, same order, same tolerant reading
// (missing keys fall back to defaults, unknown keys are ignored).
#pragma once

#include <string>
#include <vector>

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

extern const char* const kThemeKeys[TR_Count];       // JSON keys
extern const wchar_t* const kThemeLabels[TR_Count];  // labels in the editor

struct Theme {
    std::string c[TR_Count];
    Theme();  // the dark original
    bool operator==(const Theme& other) const;
};

struct Preset {
    const wchar_t* name;
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

struct CV {
    std::string name;
    std::string role;
    std::string email;
    std::string location;
    std::string website;
    std::string summary;
    std::vector<Job> jobs;
    std::string volunteerTitle = "VOLUNTEER WORK";
    std::vector<Job> volunteering;
    std::vector<SkillGroup> skillGroups;
    std::vector<std::string> softSkills;
    std::vector<Education> education;
    std::string labTitle = "PERSONAL LAB";
    std::vector<std::string> labBullets;
    Theme theme;
};

// UTF-8 JSON text <-> CV. `fromJson` never fails on well-formed JSON: anything
// it does not recognise is left at its default.
std::string toJson(const CV& cv);
bool fromJson(const std::string& text, CV& cv, std::string& error);

// Both take UTF-16 paths and read/write UTF-8 without a BOM.
bool load(const std::wstring& path, CV& cv, std::string& error);
bool save(const std::wstring& path, const CV& cv, std::string& error);

// A blank CV with the placeholder header the "New" command starts from.
CV emptyCV();

}  // namespace cvb
