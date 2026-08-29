// The PDF, byte for byte.
//
// Two properties matter and neither is visible from looking at one file. First,
// the writer is deterministic: the same resume and the same font produce the same
// bytes, with nothing datestamped or hashed from a pointer inside. Second, that
// makes the output comparable across machines - the pinned checksum below is what
// proves a PDF exported on Linux is the same file as one exported on Windows,
// which is the whole point of shipping the font.
//
// If this test fails after a deliberate change to the writer or the template, the
// number is updated in the same commit that changes them - and that commit then
// says, out loud, that every exported resume now looks different.
#include <cstdint>
#include <string>
#include <vector>

#include "fonts.h"
#include "harness.h"
#include "layout.h"
#include "model.h"
#include "pdf.h"

namespace {

const cvb::FontSet* bundled() {
    static cvb::FontSet fonts;
    static bool tried = false;
    static bool ok = false;
    if (!tried) {
        tried = true;
        std::string error;
        ok = cvb::app::loadFonts(fonts, error) && cvb::app::usingBundledFonts(fonts);
    }
    return ok ? &fonts : nullptr;
}

// FNV-1a, 64 bit. Not a cryptographic hash and not meant to be: it is here to
// notice that bytes changed, and it fits in six lines rather than pulling in a
// dependency for a test.
uint64_t hash(const std::vector<uint8_t>& data) {
    uint64_t value = 0xCBF29CE484222325ull;
    for (uint8_t byte : data) {
        value ^= byte;
        value *= 0x100000001B3ull;
    }
    return value;
}

// The resume the pinned checksum belongs to. Written out here rather than read
// from sample_cv.json so the test does not fail because the sample was edited.
cvb::CV reference() {
    cvb::CV cv;
    cv.name = "Даниил Мишин";
    cv.role = "SYSTEM ADMINISTRATOR";
    cv.email = "mail@example.com";
    cv.location = "Москва, Россия";
    cv.website = "github.com/example";
    cv.summary = "Администратор с опытом в сетях и виртуализации.\n\nВторой абзац.";

    cvb::Job job;
    job.title = "Системный администратор";
    job.period = "2022 – 2025 · 3 года";
    job.company = "ООО «Компания»";
    job.location = "Москва";
    job.bullets = {"Собрал отказоустойчивый кластер", "Перевёл парк на централизованные политики"};
    cv.jobs.push_back(job);

    cvb::SkillGroup group;
    group.title = "Сети";
    group.skills = {{"VLAN, LACP", true}, {"BGP", false}};
    cv.skillGroups.push_back(group);

    cv.softSkills = {"Внимание к деталям", "Наставничество"};
    cv.education.push_back({"CCNA", "Cisco, 2024", true});
    cv.labBullets = {"Стенд из трёх узлов Proxmox"};
    return cv;
}

}  // namespace

TEST(pdf_is_deterministic) {
    const cvb::FontSet* fonts = bundled();
    if (!fonts) {
        test::skip("bundled font not beside the test binary");
        return;
    }
    const cvb::Document doc = cvb::layout(reference(), *fonts);

    std::string error;
    const std::vector<uint8_t> first = cvb::buildPdf(doc, *fonts, error);
    const std::vector<uint8_t> second = cvb::buildPdf(doc, *fonts, error);
    CHECK(!first.empty());
    CHECK(first == second);
}

TEST(pdf_matches_the_pinned_bytes) {
    const cvb::FontSet* fonts = bundled();
    if (!fonts) {
        test::skip("bundled font not beside the test binary");
        return;
    }
    std::string error;
    const std::vector<uint8_t> pdf = cvb::buildPdf(cvb::layout(reference(), *fonts), *fonts, error);
    CHECK(!pdf.empty());

    // Measured on Linux and on Windows, from the same source and the same bundled
    // font, and equal on both.
    CHECK_EQ(pdf.size(), size_t{80832});
    CHECK_EQ(hash(pdf), 0x7410097b186e6353ull);
}

TEST(pdf_looks_like_a_pdf) {
    const cvb::FontSet* fonts = bundled();
    if (!fonts) {
        test::skip("bundled font not beside the test binary");
        return;
    }
    std::string error;
    const std::vector<uint8_t> pdf = cvb::buildPdf(cvb::layout(reference(), *fonts), *fonts, error);
    const std::string text(pdf.begin(), pdf.end());

    CHECK_EQ(text.compare(0, 8, "%PDF-1.4"), 0);
    CHECK(text.size() > 8 && text.compare(text.size() - 6, 5, "%%EOF") == 0);

    // The pieces that make Cyrillic survive a copy out of the finished file: the
    // font is embedded as a subset, addressed by glyph id, with a map back to
    // code points.
    CHECK(text.find("/Subtype /Type0") != std::string::npos);
    CHECK(text.find("/Encoding /Identity-H") != std::string::npos);
    CHECK(text.find("/FontFile2") != std::string::npos);
    CHECK(text.find("/ToUnicode") != std::string::npos);
    CHECK(text.find("beginbfchar") != std::string::npos);

    // The page box is A4 in points, written with a decimal point whatever the
    // locale - a comma here would make the file unopenable.
    CHECK(text.find("/MediaBox [0 0 595.276 841.89]") != std::string::npos);

    // Both faces are used by this resume, and each is a separate embedded subset.
    CHECK(text.find("/F1") != std::string::npos);
    CHECK(text.find("/F2") != std::string::npos);
}

TEST(pdf_embeds_only_the_glyphs_it_needs) {
    const cvb::FontSet* fonts = bundled();
    if (!fonts) {
        test::skip("bundled font not beside the test binary");
        return;
    }
    std::string error;

    cvb::CV small;
    small.name = "A";
    for (cvb::SectionRef& ref : small.sections) ref.enabled = false;
    const std::vector<uint8_t> tiny = cvb::buildPdf(cvb::layout(small, *fonts), *fonts, error);

    const std::vector<uint8_t> full =
        cvb::buildPdf(cvb::layout(reference(), *fonts), *fonts, error);

    CHECK(!tiny.empty());
    CHECK(!full.empty());
    // The subset is the point: a one-letter resume must not carry the whole face,
    // which is some 400 kB.
    CHECK(tiny.size() < full.size());
    CHECK(tiny.size() < 100u * 1024u);
}

TEST(pdf_refuses_an_empty_document) {
    const cvb::FontSet* fonts = bundled();
    if (!fonts) {
        test::skip("bundled font not beside the test binary");
        return;
    }
    cvb::Document empty;
    std::string error;
    CHECK(cvb::buildPdf(empty, *fonts, error).empty());
    CHECK(!error.empty());
}
