// Numbers must read and write the same way whatever locale the process is in.
// This is not hypothetical: a UI toolkit calls setlocale(LC_ALL, "") on
// start-up, and in a German or Russian locale the C library's own formatter
// switches to a comma. A PDF coordinate written that way makes the file
// invalid, and "1.5" read back that way becomes 1.
#include <clocale>
#include <cstdio>
#include <string>

#include "harness.h"
#include "json.h"
#include "numeric.h"

namespace {

// The first locale on this machine that writes a comma. Windows and the Unixes
// spell them differently, and a stripped container may have none at all - hence
// the list, and the honest skip when nothing matches.
const char* findCommaLocale() {
    const char* candidates[] = {
        "de_DE.UTF-8", "de_DE.utf8", "de_DE", "German_Germany.1252",
        "ru_RU.UTF-8", "ru_RU.utf8", "ru_RU", "Russian_Russia.1251",
        "fr_FR.UTF-8", "French_France.1252",
    };
    for (const char* name : candidates) {
        if (!std::setlocale(LC_ALL, name)) continue;
        // Ask the C library itself rather than trusting the name: some builds
        // accept a locale and still format in the C convention.
        char probe[16];
        std::snprintf(probe, sizeof probe, "%.1f", 1.5);
        if (std::string(probe) == "1,5") return name;
    }
    std::setlocale(LC_ALL, "C");
    return nullptr;
}

struct LocaleGuard {
    explicit LocaleGuard(const char* name) { std::setlocale(LC_ALL, name); }
    ~LocaleGuard() { std::setlocale(LC_ALL, "C"); }
};

}  // namespace

TEST(numeric_fixed_writes_a_point) {
    CHECK_EQ(numfmt::fixed(0.5, 3), std::string("0.500"));
    CHECK_EQ(numfmt::fixed(595.2755905511812, 3), std::string("595.276"));
    CHECK_EQ(numfmt::fixed(-12.0, 3), std::string("-12.000"));
    CHECK_EQ(numfmt::fixed(0.0, 3), std::string("0.000"));
}

TEST(numeric_shortest_matches_the_old_output) {
    CHECK_EQ(numfmt::shortest(0.0), std::string("0"));
    CHECK_EQ(numfmt::shortest(7.0), std::string("7"));
    CHECK_EQ(numfmt::shortest(-3.0), std::string("-3"));
    CHECK_EQ(numfmt::shortest(9.5), std::string("9.5"));
    CHECK_EQ(numfmt::shortest(8.25), std::string("8.25"));
}

TEST(numeric_parse_reads_a_point) {
    double value = 0.0;
    CHECK(numfmt::parse("1.5", value));
    CHECK_EQ(value, 1.5);
    CHECK(numfmt::parse("-0.125", value));
    CHECK_EQ(value, -0.125);
    CHECK(numfmt::parse("2e3", value));
    CHECK_EQ(value, 2000.0);
    CHECK(!numfmt::parse("", value));
    CHECK(!numfmt::parse("abc", value));
}

TEST(numeric_survives_a_comma_locale) {
    const char* name = findCommaLocale();
    if (!name) {
        test::skip("no comma-decimal locale installed");
        return;
    }
    LocaleGuard guard(name);

    // The premise of the test: the C library really is formatting with a comma
    // right now. Without this the case could pass by doing nothing.
    char probe[16];
    std::snprintf(probe, sizeof probe, "%.1f", 1.5);
    CHECK_EQ(std::string(probe), std::string("1,5"));

    CHECK_EQ(numfmt::fixed(595.2755905511812, 3), std::string("595.276"));
    CHECK_EQ(numfmt::shortest(9.5), std::string("9.5"));

    double value = 0.0;
    CHECK(numfmt::parse("1.5", value));
    CHECK_EQ(value, 1.5);
}

TEST(json_survives_a_comma_locale) {
    const char* name = findCommaLocale();
    if (!name) {
        test::skip("no comma-decimal locale installed");
        return;
    }
    LocaleGuard guard(name);

    js::Value parsed;
    std::string error;
    CHECK(js::parse("{\"order\": 3, \"size\": 9.5}", parsed, error));
    CHECK_EQ(parsed["order"].asNumber(), 3.0);
    CHECK_EQ(parsed["size"].asNumber(), 9.5);

    js::Value out{js::Object{}};
    out.set("order", 3);
    out.set("size", 9.5);
    CHECK_EQ(js::dump(out, 0), std::string("{\"order\":3,\"size\":9.5}"));
}
