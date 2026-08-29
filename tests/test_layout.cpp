// The page: its size, what lands on it, and where.
//
// A4 is a property of the document and not of the operating system, so these
// numbers are the same everywhere and are checked as such. The rest guards the
// promises the layout engine makes to the two renderers above it: coordinates in
// points from the top-left corner, text positioned by its baseline, nothing
// beyond the sheet, and the same input always producing the same page.
#include <cmath>
#include <string>
#include <vector>

#include "canvas.h"
#include "document_painter.h"
#include "font.h"
#include "fonts.h"
#include "harness.h"
#include "layout.h"
#include "model.h"

namespace {

const cvb::FontSet* fonts() {
    static cvb::FontSet set;
    static bool tried = false;
    static bool ok = false;
    if (!tried) {
        tried = true;
        std::string error;
        ok = cvb::app::loadFonts(set, error);
    }
    return ok ? &set : nullptr;
}

cvb::CV sample() {
    cvb::CV cv;
    cv.name = "Даниил Мишин";
    cv.role = "SYSTEM ADMINISTRATOR";
    cv.email = "mail@example.com";
    cv.location = "Москва";
    cv.summary = "Короткий абзац о себе.";

    cvb::Job job;
    job.title = "Инженер";
    job.period = "2022 – 2025";
    job.company = "Компания";
    job.bullets = {"Первое достижение", "Второе достижение"};
    cv.jobs.push_back(job);

    cvb::SkillGroup group;
    group.title = "Сети";
    group.skills = {{"VLAN", true}, {"BGP", false}};
    cv.skillGroups.push_back(group);
    cv.softSkills = {"Внимание к деталям"};
    return cv;
}

// Records what a page asks to be drawn, which is how the preview and the printer
// see it.
struct Recorder : cvb::Canvas {
    struct Call {
        enum Kind { Rect, Line, Glyphs } kind;
        double x = 0, y = 0;
        size_t glyphs = 0;
    };
    std::vector<Call> calls;

    void fillRect(double x, double y, double, double, cvb::RGB) override {
        calls.push_back({Call::Rect, x, y, 0});
    }
    void drawLine(double x1, double y1, double, double, double, cvb::RGB) override {
        calls.push_back({Call::Line, x1, y1, 0});
    }
    void drawGlyphs(double x, double y, double, bool, const cvb::GlyphRun& run,
                    cvb::RGB) override {
        calls.push_back({Call::Glyphs, x, y, run.count});
    }
};

}  // namespace

TEST(layout_a4_is_a_property_of_the_document) {
    // 210 x 297 mm at 72 points per inch, to a thousandth of a point. Written out
    // in millimetres here so the expectation is the paper size rather than a copy
    // of the constant being tested.
    const double width = 210.0 / 25.4 * 72.0;
    const double height = 297.0 / 25.4 * 72.0;
    CHECK(std::abs(cvb::kPageWidth - width) < 0.001);
    CHECK(std::abs(cvb::kPageHeight - height) < 0.001);

    // The proportion is what makes a page look like a page at any zoom or dpi.
    CHECK(std::abs(cvb::kPageHeight / cvb::kPageWidth - std::sqrt(2.0)) < 0.001);

    const cvb::FontSet* set = fonts();
    if (!set) {
        test::skip("no font available");
        return;
    }
    const cvb::Document doc = cvb::layout(sample(), *set);
    CHECK_EQ(doc.width, cvb::kPageWidth);
    CHECK_EQ(doc.height, cvb::kPageHeight);
    for (const cvb::Page& page : doc.pages) {
        CHECK_EQ(page.background.w, cvb::kPageWidth);
        CHECK_EQ(page.background.h, cvb::kPageHeight);
    }
}

TEST(layout_is_deterministic) {
    const cvb::FontSet* set = fonts();
    if (!set) {
        test::skip("no font available");
        return;
    }
    const cvb::CV cv = sample();
    const cvb::Document first = cvb::layout(cv, *set);
    const cvb::Document second = cvb::layout(cv, *set);

    CHECK_EQ(first.pages.size(), second.pages.size());
    for (size_t p = 0; p < first.pages.size() && p < second.pages.size(); ++p) {
        CHECK_EQ(first.pages[p].texts.size(), second.pages[p].texts.size());
        for (size_t i = 0; i < first.pages[p].texts.size(); ++i) {
            CHECK_EQ(first.pages[p].texts[i].text, second.pages[p].texts[i].text);
            CHECK_EQ(first.pages[p].texts[i].x, second.pages[p].texts[i].x);
            CHECK_EQ(first.pages[p].texts[i].y, second.pages[p].texts[i].y);
        }
    }
}

TEST(layout_keeps_everything_on_the_sheet) {
    const cvb::FontSet* set = fonts();
    if (!set) {
        test::skip("no font available");
        return;
    }
    const cvb::Document doc = cvb::layout(sample(), *set);
    CHECK(!doc.pages.empty());

    for (const cvb::Page& page : doc.pages) {
        for (const cvb::TextItem& item : page.texts) {
            CHECK(item.x >= 0.0);
            CHECK(item.y > 0.0);
            CHECK(item.y <= cvb::kPageHeight);
            // The run has to end on the paper too, not merely start on it.
            const double right = item.x + set->face(item.bold).measure(item.text, item.size);
            CHECK(right <= cvb::kPageWidth);
        }
        for (const cvb::LineItem& line : page.lines) {
            CHECK(line.y1 >= 0.0);
            CHECK(line.y1 <= cvb::kPageHeight);
            CHECK(line.x2 <= cvb::kPageWidth);
        }
    }
}

TEST(layout_breaks_a_long_resume_across_pages) {
    const cvb::FontSet* set = fonts();
    if (!set) {
        test::skip("no font available");
        return;
    }
    cvb::CV cv = sample();
    CHECK_EQ(cvb::layout(cv, *set).pages.size(), size_t{1});

    // Enough jobs that one sheet cannot hold them.
    for (int i = 0; i < 20; ++i) {
        cvb::Job job;
        job.title = "Должность " + std::to_string(i);
        job.company = "Компания " + std::to_string(i);
        job.bullets = {"Пункт один", "Пункт два", "Пункт три"};
        cv.jobs.push_back(job);
    }
    const cvb::Document doc = cvb::layout(cv, *set);
    CHECK(doc.pages.size() > 1);

    // Every page is a full sheet with its own background, or the preview would
    // show a partly painted second page.
    for (const cvb::Page& page : doc.pages) CHECK_EQ(page.background.h, cvb::kPageHeight);
}

TEST(layout_disabled_sections_keep_their_content) {
    const cvb::FontSet* set = fonts();
    if (!set) {
        test::skip("no font available");
        return;
    }
    cvb::CV cv = sample();
    const size_t withSkills = cvb::layout(cv, *set).pages[0].texts.size();

    for (cvb::SectionRef& ref : cv.sections)
        if (ref.id == "skill_groups") ref.enabled = false;

    const size_t withoutSkills = cvb::layout(cv, *set).pages[0].texts.size();
    CHECK(withoutSkills < withSkills);
    // Turning a section off must not empty it: switching it back on restores the
    // page exactly.
    for (cvb::SectionRef& ref : cv.sections)
        if (ref.id == "skill_groups") ref.enabled = true;
    CHECK_EQ(cvb::layout(cv, *set).pages[0].texts.size(), withSkills);
}

TEST(painter_draws_what_the_page_holds_in_order) {
    const cvb::FontSet* set = fonts();
    if (!set) {
        test::skip("no font available");
        return;
    }
    const cvb::Document doc = cvb::layout(sample(), *set);
    Recorder recorder;
    cvb::DocumentPainter painter(*set);
    painter.paint(doc.pages[0], recorder);

    const cvb::Page& page = doc.pages[0];
    CHECK(!recorder.calls.empty());
    // Background first: the rules and the text sit on the paper, not under it.
    CHECK_EQ(static_cast<int>(recorder.calls[0].kind), static_cast<int>(Recorder::Call::Rect));

    size_t rects = 0, lines = 0, glyphRuns = 0;
    bool sawTextBeforeLine = false;
    bool sawGlyphs = false;
    for (const Recorder::Call& call : recorder.calls) {
        if (call.kind == Recorder::Call::Rect) ++rects;
        if (call.kind == Recorder::Call::Line) {
            ++lines;
            if (sawGlyphs) sawTextBeforeLine = true;
        }
        if (call.kind == Recorder::Call::Glyphs) {
            ++glyphRuns;
            sawGlyphs = true;
            // A run that reached the canvas has glyphs in it; empty strings are
            // dropped by the layout engine, not passed on.
            CHECK(call.glyphs > 0);
        }
    }
    CHECK_EQ(rects, size_t{1});
    CHECK_EQ(lines, page.lines.size());
    CHECK(!sawTextBeforeLine);

    // Every text item on the page becomes exactly one run, at its own baseline -
    // this is what keeps the preview from being an approximation of the PDF.
    CHECK_EQ(glyphRuns, page.texts.size());
    size_t at = 0;
    for (const Recorder::Call& call : recorder.calls) {
        if (call.kind != Recorder::Call::Glyphs) continue;
        CHECK_EQ(call.x, page.texts[at].x);
        CHECK_EQ(call.y, page.texts[at].y);
        // And the glyph count is what the font makes of that string, so the
        // preview draws the same ids the PDF embeds.
        std::vector<uint16_t> glyphs;
        set->face(page.texts[at].bold).shape(page.texts[at].text, glyphs);
        CHECK_EQ(call.glyphs, glyphs.size());
        ++at;
    }
}
