#include "numeric.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace numfmt {
namespace {

// What the locale in force puts between the integer and the fractional part:
// "." in "C", "," in most of Europe. Asked for every conversion rather than
// cached, because setlocale can be called at any moment - a toolkit does it
// while starting up, and a plug-in could do it later.
const char* decimalPoint() {
    const std::lconv* conv = std::localeconv();
    return (conv && conv->decimal_point && *conv->decimal_point) ? conv->decimal_point : ".";
}

// Rewrites whatever the locale produced as a plain '.'.
void toPlainPoint(std::string& text) {
    const char* point = decimalPoint();
    if (point[0] == '.' && point[1] == '\0') return;  // the usual case: nothing to do
    size_t at = text.find(point);
    if (at != std::string::npos) text.replace(at, std::strlen(point), ".");
}

// snprintf into a string, growing the buffer if the number needed more room
// than guessed. "%.*f" of a large double can run to several hundred digits, so
// the retry is not theoretical.
std::string format(const char* form, int precision, double value) {
    char stack[64];
    int needed = std::snprintf(stack, sizeof stack, form, precision, value);
    if (needed < 0) return std::string();
    if (static_cast<size_t>(needed) < sizeof stack) return std::string(stack, static_cast<size_t>(needed));

    std::vector<char> heap(static_cast<size_t>(needed) + 1);
    needed = std::snprintf(heap.data(), heap.size(), form, precision, value);
    if (needed < 0) return std::string();
    return std::string(heap.data(), static_cast<size_t>(needed));
}

}  // namespace

std::string fixed(double value, int decimals) {
    std::string out = format("%.*f", decimals, value);
    toPlainPoint(out);
    return out;
}

std::string shortest(double value) {
    // Whole numbers take the integer path: it has no separator to get wrong,
    // and it is the only shape the resume format actually stores.
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        char buf[32];
        int n = std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(value));
        return std::string(buf, static_cast<size_t>(n > 0 ? n : 0));
    }
    std::string out = format("%.*g", 10, value);
    toPlainPoint(out);
    return out;
}

bool parse(const std::string& text, double& out, size_t* consumed) {
    if (consumed) *consumed = 0;
    if (text.empty()) return false;

    // strtod reads the separator the locale dictates, so hand it a string
    // written that way. The input is JSON and therefore always uses '.'.
    const char* point = decimalPoint();
    std::string local = text;
    if (point[0] != '.' || point[1] != '\0') {
        size_t at = local.find('.');
        if (at != std::string::npos) local.replace(at, 1, point);
    }

    const char* begin = local.c_str();
    char* end = nullptr;
    double value = std::strtod(begin, &end);
    if (end == begin) return false;

    // Length in the caller's string, which differs from the length in `local`
    // when the separator was more than one byte wide.
    size_t used = static_cast<size_t>(end - begin);
    if (point[0] != '.' || point[1] != '\0') {
        const size_t width = std::strlen(point);
        size_t at = local.find(point);
        if (at != std::string::npos && at < used) used -= width - 1;
    }

    out = value;
    if (consumed) *consumed = used;
    return true;
}

}  // namespace numfmt
