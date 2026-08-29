// The file format, which is the one thing in the program that must not change:
// a resume written by any version has to open in every other one.
#include <string>

#include "harness.h"
#include "json.h"
#include "model.h"

namespace {

cvb::CV filled() {
    cvb::CV cv;
    cv.name = "Даниил Мишин";
    cv.role = "SYSTEM ADMINISTRATOR";
    cv.email = "mail@example.com";
    cv.location = "Москва";
    cv.website = "github.com/example";
    cv.summary = "Первый абзац.\n\nВторой абзац.";

    cvb::Job job;
    job.title = "Инженер";
    job.period = "2022 – 2025 · 3 года";
    job.company = "Компания";
    job.location = "Москва";
    job.bullets = {"Сделал одно", "Сделал другое"};
    cv.jobs.push_back(job);
    cv.volunteering.push_back(job);
    cv.studies.push_back(job);

    cvb::SkillGroup group;
    group.title = "Сети";
    group.skills = {{"VLAN", true}, {"BGP", false}};
    cv.skillGroups.push_back(group);

    cv.softSkills = {"Внимание к деталям", "Коммуникация"};
    cv.education.push_back({"CCNA", "Cisco, 2024", true});
    cv.labBullets = {"Домашний стенд на трёх узлах"};
    cv.theme.c[cvb::TR_Accent] = "#ff8c42";
    return cv;
}

}  // namespace

TEST(model_round_trips_every_field) {
    const cvb::CV before = filled();
    cvb::CV after;
    std::string error;
    CHECK(cvb::fromJson(cvb::toJson(before), after, error));

    CHECK_EQ(after.name, before.name);
    CHECK_EQ(after.role, before.role);
    CHECK_EQ(after.email, before.email);
    CHECK_EQ(after.location, before.location);
    CHECK_EQ(after.website, before.website);
    CHECK_EQ(after.summary, before.summary);

    CHECK_EQ(after.jobs.size(), before.jobs.size());
    if (!after.jobs.empty()) {
        CHECK_EQ(after.jobs[0].title, before.jobs[0].title);
        CHECK_EQ(after.jobs[0].period, before.jobs[0].period);
        CHECK_EQ(after.jobs[0].company, before.jobs[0].company);
        CHECK_EQ(after.jobs[0].location, before.jobs[0].location);
        CHECK_EQ(after.jobs[0].bullets.size(), size_t{2});
    }
    CHECK_EQ(after.volunteering.size(), before.volunteering.size());
    CHECK_EQ(after.studies.size(), before.studies.size());

    CHECK_EQ(after.skillGroups.size(), size_t{1});
    if (!after.skillGroups.empty()) {
        CHECK_EQ(after.skillGroups[0].title, std::string("Сети"));
        CHECK_EQ(after.skillGroups[0].skills.size(), size_t{2});
        CHECK(after.skillGroups[0].skills[0].highlight);
        CHECK(!after.skillGroups[0].skills[1].highlight);
    }

    CHECK_EQ(after.softSkills.size(), size_t{2});
    CHECK_EQ(after.education.size(), size_t{1});
    if (!after.education.empty()) CHECK(after.education[0].highlight);
    CHECK_EQ(after.labBullets.size(), size_t{1});
    CHECK_EQ(after.theme.c[cvb::TR_Accent], std::string("#ff8c42"));

    // Serialising twice must produce the same bytes: the writer keeps key order,
    // so a saved file does not churn in version control just from being opened.
    CHECK_EQ(cvb::toJson(after), cvb::toJson(before));
}

TEST(model_reads_a_file_without_a_sections_block) {
    // What versions before the running order was editable wrote. Such a file has
    // to open, get the default order, and keep the two headings that used to live
    // under their own keys.
    const std::string legacy =
        "{\"name\":\"A\",\"volunteer_title\":\"Моё волонтёрство\","
        "\"lab_title\":\"Моя лаборатория\",\"jobs\":[]}";

    cvb::CV cv;
    std::string error;
    CHECK(cvb::fromJson(legacy, cv, error));
    CHECK_EQ(cv.sections.size(), size_t{cvb::kSectionCount});

    for (const cvb::SectionRef& ref : cv.sections) {
        CHECK(ref.enabled);
        if (ref.id == "volunteering") CHECK_EQ(ref.label, std::string("Моё волонтёрство"));
        if (ref.id == "lab") CHECK_EQ(ref.label, std::string("Моя лаборатория"));
        // Back when this was the only education block it said so, and a file from
        // then keeps its wording rather than being retitled.
        if (ref.id == "education") CHECK_EQ(ref.label, std::string("Education & Certifications"));
    }
}

TEST(model_repairs_a_damaged_sections_block) {
    // A section this build does not know, one listed twice, and one simply
    // missing. Reading must not lose anything or draw anything twice.
    const std::string damaged =
        "{\"sections\":["
        "{\"id\":\"jobs\",\"order\":1},"
        "{\"id\":\"jobs\",\"order\":2},"
        "{\"id\":\"from-the-future\",\"order\":3},"
        "{\"id\":\"summary\",\"enabled\":false,\"order\":0}]}";

    cvb::CV cv;
    std::string error;
    CHECK(cvb::fromJson(damaged, cv, error));
    CHECK_EQ(cv.sections.size(), size_t{cvb::kSectionCount});

    int jobs = 0;
    bool summaryEnabled = true;
    for (size_t i = 0; i < cv.sections.size(); ++i) {
        if (cv.sections[i].id == "jobs") ++jobs;
        if (cv.sections[i].id == "summary") summaryEnabled = cv.sections[i].enabled;
        CHECK(cv.sections[i].id != "from-the-future");
        // The order is renumbered as a dense run, so nothing depends on the
        // numbers a hand-edited file happened to use.
        CHECK_EQ(cv.sections[i].order, static_cast<int>(i));
    }
    CHECK_EQ(jobs, 1);
    // A section turned off keeps its content and stays in the list; only
    // orderedSections drops it.
    CHECK(!summaryEnabled);
    for (const cvb::SectionRef& ref : cvb::orderedSections(cv)) CHECK(ref.id != "summary");
}

TEST(model_tolerates_nonsense) {
    // Hand-edited files happen. Wrong types must fall back to defaults rather
    // than refuse the file or crash.
    const std::string odd =
        "{\"name\":42,\"jobs\":\"not an array\",\"skill_groups\":[{\"title\":null,"
        "\"skills\":[{\"name\":\"OK\",\"highlight\":\"yes\"}]}],\"theme\":[]}";

    cvb::CV cv;
    std::string error;
    CHECK(cvb::fromJson(odd, cv, error));
    CHECK_EQ(cv.name, std::string());
    CHECK(cv.jobs.empty());
    CHECK_EQ(cv.skillGroups.size(), size_t{1});
    if (!cv.skillGroups.empty()) {
        CHECK_EQ(cv.skillGroups[0].title, std::string());
        CHECK_EQ(cv.skillGroups[0].skills.size(), size_t{1});
        // "yes" is not a bool, so the flag keeps its default rather than becoming
        // true because the value was non-empty.
        CHECK(!cv.skillGroups[0].skills[0].highlight);
    }
    CHECK_EQ(cv.theme.c[cvb::TR_Background], cvb::Theme().c[cvb::TR_Background]);

    // Malformed JSON, on the other hand, is a real failure and is reported.
    CHECK(!cvb::fromJson("{\"name\":", cv, error));
    CHECK(!error.empty());
    CHECK(!cvb::fromJson("[1,2,3]", cv, error));
}

TEST(json_keeps_key_order_and_utf8) {
    js::Value root{js::Object{}};
    root.set("z", "первый");
    root.set("a", "второй");

    const std::string text = js::dump(root, 0);
    CHECK_EQ(text, std::string("{\"z\":\"первый\",\"a\":\"второй\"}"));

    // Round trip: parsing and dumping again must give the same bytes, non-ASCII
    // included - the writer never escapes as \uXXXX.
    js::Value parsed;
    std::string error;
    CHECK(js::parse(text, parsed, error));
    CHECK_EQ(js::dump(parsed, 0), text);
}

TEST(json_reads_escapes_and_a_bom) {
    js::Value value;
    std::string error;
    CHECK(js::parse("\xEF\xBB\xBF{\"s\":\"\\u0414\\u0430\\n\\\"x\\\"\"}", value, error));
    CHECK_EQ(value["s"].asString(), std::string("Да\n\"x\""));

    // A surrogate pair becomes one code point, four UTF-8 bytes.
    CHECK(js::parse("{\"s\":\"\\uD83D\\uDE00\"}", value, error));
    CHECK_EQ(value["s"].asString().size(), size_t{4});

    // A missing key yields a null that can be chained through safely.
    CHECK(value["nothing"]["deeper"].isNull());
    CHECK_EQ(value["nothing"].asString("fallback"), std::string("fallback"));
}
