#include "harness.h"

#include <cstdio>
#include <cstring>

namespace test {
namespace {

int gFailures = 0;
std::string gSkip;

}  // namespace

std::vector<Case>& cases() {
    static std::vector<Case> list;
    return list;
}

Register::Register(std::string name, std::function<void()> run) {
    cases().push_back(Case{std::move(name), std::move(run)});
}

void fail(const char* file, int line, const std::string& what) {
    ++gFailures;
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, what.c_str());
}

void skip(const std::string& why) { gSkip = why; }

bool check(bool ok, const char* file, int line, const char* expression) {
    if (!ok) fail(file, line, expression);
    return ok;
}

int run(int argc, char** argv) {
    int failed = 0, passed = 0, skipped = 0;
    for (const Case& item : cases()) {
        bool wanted = argc <= 1;
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], item.name.c_str()) == 0) wanted = true;
        if (!wanted) continue;

        gFailures = 0;
        gSkip.clear();
        std::printf("%-40s", item.name.c_str());
        std::fflush(stdout);
        item.run();

        if (gFailures) {
            ++failed;
            std::printf("FAILED (%d)\n", gFailures);
        } else if (!gSkip.empty()) {
            ++skipped;
            std::printf("skipped: %s\n", gSkip.c_str());
        } else {
            ++passed;
            std::printf("ok\n");
        }
    }
    std::printf("\n%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    return failed == 0 ? 0 : 1;
}

}  // namespace test

int main(int argc, char** argv) { return test::run(argc, argv); }
