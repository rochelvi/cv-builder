#include "model.h"

#include <cstdio>
#include <cwchar>  // _wfopen: Unicode paths

#include "json.h"

namespace cvb {

const char* const kThemeKeys[TR_Count] = {
    "background", "rule", "heading", "body", "subtle", "faint", "accent", "accent2",
};

const wchar_t* const kThemeLabels[TR_Count] = {
    L"Фон страницы",
    L"Разделители",
    L"Заголовки (имя, должности)",
    L"Основной текст",
    L"Вторичный текст (компания, контакты)",
    L"Даты и неактивные записи",
    L"Акцент 1 (секции, маркеры)",
    L"Акцент 2 (soft skills, образование)",
};

Theme::Theme() {
    c[TR_Background] = "#0d0f12";
    c[TR_Rule] = "#2a2f3a";
    c[TR_Heading] = "#e8ecf2";  // name, job titles, skill group titles
    c[TR_Body] = "#9aa0ae";     // summary and bullet text
    c[TR_Subtle] = "#7a8292";   // contacts, company lines, subtitles
    c[TR_Faint] = "#4a5260";    // dates, non-highlighted certifications
    c[TR_Accent] = "#4ade80";   // section titles, bullets, highlighted skills
    c[TR_Accent2] = "#7ab8f5";  // soft skills, highlighted education
}

bool Theme::operator==(const Theme& other) const {
    for (int i = 0; i < TR_Count; ++i)
        if (c[i] != other.c[i]) return false;
    return true;
}

namespace {

Theme makeTheme(const char* background, const char* rule, const char* heading,
                const char* body, const char* subtle, const char* faint,
                const char* accent, const char* accent2) {
    Theme t;
    t.c[TR_Background] = background;
    t.c[TR_Rule] = rule;
    t.c[TR_Heading] = heading;
    t.c[TR_Body] = body;
    t.c[TR_Subtle] = subtle;
    t.c[TR_Faint] = faint;
    t.c[TR_Accent] = accent;
    t.c[TR_Accent2] = accent2;
    return t;
}

std::vector<std::string> stringList(const js::Value& value) {
    std::vector<std::string> out;
    if (!value.isArray()) return out;
    for (const js::Value& item : value.arr())
        if (item.type() == js::Type::String) out.push_back(item.asString());
    return out;
}

Job readJob(const js::Value& v) {
    Job job;
    job.title = v["title"].asString();
    job.period = v["period"].asString();
    job.company = v["company"].asString();
    job.location = v["location"].asString();
    job.bullets = stringList(v["bullets"]);
    return job;
}

std::vector<Job> readJobs(const js::Value& v) {
    std::vector<Job> out;
    if (!v.isArray()) return out;
    for (const js::Value& item : v.arr()) out.push_back(readJob(item));
    return out;
}

js::Value writeJob(const Job& job) {
    js::Value v{js::Object{}};
    v.set("title", job.title);
    v.set("period", job.period);
    v.set("company", job.company);
    v.set("location", job.location);
    js::Array bullets;
    for (const std::string& b : job.bullets) bullets.emplace_back(b);
    v.set("bullets", js::Value(std::move(bullets)));
    return v;
}

js::Value writeJobs(const std::vector<Job>& jobs) {
    js::Array out;
    for (const Job& job : jobs) out.push_back(writeJob(job));
    return js::Value(std::move(out));
}

js::Value writeStrings(const std::vector<std::string>& items) {
    js::Array out;
    for (const std::string& s : items) out.emplace_back(s);
    return js::Value(std::move(out));
}

bool readFile(const std::wstring& path, std::string& out, std::string& error) {
    FILE* fh = _wfopen(path.c_str(), L"rb");
    if (!fh) { error = "не удалось открыть файл"; return false; }
    out.clear();
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, fh)) > 0) out.append(buf, n);
    bool ok = std::ferror(fh) == 0;
    std::fclose(fh);
    if (!ok) error = "ошибка чтения файла";
    return ok;
}

bool writeFile(const std::wstring& path, const std::string& data, std::string& error) {
    FILE* fh = _wfopen(path.c_str(), L"wb");
    if (!fh) { error = "не удалось создать файл"; return false; }
    bool ok = std::fwrite(data.data(), 1, data.size(), fh) == data.size();
    if (std::fclose(fh) != 0) ok = false;
    if (!ok) error = "ошибка записи файла";
    return ok;
}

}  // namespace

const std::vector<Preset>& presets() {
    static const std::vector<Preset> kPresets = {
        {L"Тёмная (оригинал)", Theme()},
        {L"Midnight blue", makeTheme("#0b1020", "#22304a", "#eaf1ff", "#a8b6cf",
                                     "#8593ad", "#4d5c78", "#5cc8ff", "#b39bff")},
        {L"Graphite orange", makeTheme("#141414", "#333333", "#f2f2f2", "#b0b0b0",
                                       "#8c8c8c", "#5a5a5a", "#ff8c42", "#ffd166")},
        {L"Paper (light)", makeTheme("#ffffff", "#d9dee5", "#111827", "#374151",
                                     "#6b7280", "#9ca3af", "#0f766e", "#1d4ed8")},
        {L"Warm cream", makeTheme("#f7f3ea", "#ded5c3", "#2b2113", "#4d4335",
                                  "#6f6553", "#9c9382", "#b4530a", "#3c6e47")},
    };
    return kPresets;
}

std::string toJson(const CV& cv) {
    js::Value root{js::Object{}};
    root.set("name", cv.name);
    root.set("role", cv.role);
    root.set("email", cv.email);
    root.set("location", cv.location);
    root.set("website", cv.website);
    root.set("summary", cv.summary);
    root.set("jobs", writeJobs(cv.jobs));
    root.set("volunteer_title", cv.volunteerTitle);
    root.set("volunteering", writeJobs(cv.volunteering));

    js::Array groups;
    for (const SkillGroup& group : cv.skillGroups) {
        js::Value g{js::Object{}};
        g.set("title", group.title);
        js::Array skills;
        for (const Skill& skill : group.skills) {
            js::Value s{js::Object{}};
            s.set("name", skill.name);
            s.set("highlight", skill.highlight);
            skills.push_back(std::move(s));
        }
        g.set("skills", js::Value(std::move(skills)));
        groups.push_back(std::move(g));
    }
    root.set("skill_groups", js::Value(std::move(groups)));
    root.set("soft_skills", writeStrings(cv.softSkills));

    js::Array education;
    for (const Education& item : cv.education) {
        js::Value e{js::Object{}};
        e.set("title", item.title);
        e.set("subtitle", item.subtitle);
        e.set("highlight", item.highlight);
        education.push_back(std::move(e));
    }
    root.set("education", js::Value(std::move(education)));
    root.set("lab_title", cv.labTitle);
    root.set("lab_bullets", writeStrings(cv.labBullets));

    js::Value theme{js::Object{}};
    for (int i = 0; i < TR_Count; ++i) theme.set(kThemeKeys[i], cv.theme.c[i]);
    root.set("theme", std::move(theme));

    return js::dump(root, 2);
}

bool fromJson(const std::string& text, CV& cv, std::string& error) {
    js::Value root;
    if (!js::parse(text, root, error)) return false;
    if (!root.isObject()) { error = "ожидался объект JSON"; return false; }

    cv = CV();
    cv.name = root["name"].asString();
    cv.role = root["role"].asString();
    cv.email = root["email"].asString();
    cv.location = root["location"].asString();
    cv.website = root["website"].asString();
    cv.summary = root["summary"].asString();
    cv.jobs = readJobs(root["jobs"]);
    cv.volunteerTitle = root["volunteer_title"].asString("VOLUNTEER WORK");
    cv.volunteering = readJobs(root["volunteering"]);

    for (const js::Value& g : root["skill_groups"].arr()) {
        SkillGroup group;
        group.title = g["title"].asString();
        for (const js::Value& s : g["skills"].arr()) {
            Skill skill;
            skill.name = s["name"].asString();
            skill.highlight = s["highlight"].asBool();
            group.skills.push_back(std::move(skill));
        }
        cv.skillGroups.push_back(std::move(group));
    }

    cv.softSkills = stringList(root["soft_skills"]);
    for (const js::Value& e : root["education"].arr()) {
        Education item;
        item.title = e["title"].asString();
        item.subtitle = e["subtitle"].asString();
        item.highlight = e["highlight"].asBool();
        cv.education.push_back(std::move(item));
    }
    cv.labTitle = root["lab_title"].asString("PERSONAL LAB");
    cv.labBullets = stringList(root["lab_bullets"]);

    const js::Value& theme = root["theme"];
    for (int i = 0; i < TR_Count; ++i) {
        const js::Value& value = theme[kThemeKeys[i]];
        if (value.type() == js::Type::String) cv.theme.c[i] = value.asString();
    }
    return true;
}

bool load(const std::wstring& path, CV& cv, std::string& error) {
    std::string text;
    if (!readFile(path, text, error)) return false;
    return fromJson(text, cv, error);
}

bool save(const std::wstring& path, const CV& cv, std::string& error) {
    return writeFile(path, toJson(cv), error);
}

CV emptyCV() {
    CV cv;
    cv.name = "Your Name";
    cv.role = "YOUR ROLE";
    return cv;
}

}  // namespace cvb
