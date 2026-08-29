// A test runner in a hundred lines, because the project has no third-party
// dependencies and a test framework is not a good reason to acquire the first
// one. Declare a case with TEST(name), check with CHECK / CHECK_EQ; `main` runs
// every case, or only the ones named on the command line.
#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
    std::string name;
    std::function<void()> run;
};

std::vector<Case>& cases();

struct Register {
    Register(std::string name, std::function<void()> run);
};

// Records a failure against the case being run. Never throws: a failing check
// reports and carries on, so one run tells you everything that is broken rather
// than only the first thing.
void fail(const char* file, int line, const std::string& what);

// Says why a case was not run at all - a locale the system does not have, a
// font that is not installed. A skipped case is not a pass and says so.
void skip(const std::string& why);

bool check(bool ok, const char* file, int line, const char* expression);

template <class A, class B>
bool checkEqual(const A& actual, const B& expected, const char* file, int line,
                const char* expression) {
    if (actual == expected) return true;
    std::ostringstream message;
    message << expression << "\n      actual: " << actual << "\n    expected: " << expected;
    fail(file, line, message.str());
    return false;
}

int run(int argc, char** argv);

}  // namespace test

#define TEST(name)                                                       \
    static void name();                                                  \
    static const ::test::Register test_register_##name(#name, name);      \
    static void name()

#define CHECK(condition) ::test::check((condition), __FILE__, __LINE__, #condition)
#define CHECK_EQ(actual, expected) \
    ::test::checkEqual((actual), (expected), __FILE__, __LINE__, #actual " == " #expected)
