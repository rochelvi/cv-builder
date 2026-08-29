// Settings in a small ini file, which is what both the Unixes expect and what a
// user can read, edit and back up without a tool.
//
// The whole file is a few hundred bytes and is read and rewritten as a unit:
// there is no benefit in being cleverer, and rewriting means an entry that was
// removed is really gone rather than left behind as an empty line.
//
// Lists become numbered keys - RecentFiles.1, RecentFiles.2 - because a path may
// contain any character except a newline, so no single-line separator is safe.
#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "file.h"
#include "numeric.h"
#include "settings.h"

namespace cvb {
namespace platform {
namespace settings {
namespace {

Path settingsFile() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return Path();

#if defined(__APPLE__)
    Path dir = Path(home) / "Library" / "Preferences" / "CV Builder";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    Path base = (xdg && xdg[0] == '/') ? Path(xdg) : Path(home) / ".config";
    Path dir = base / "cv-builder";
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return Path();
    return dir / "settings.ini";
}

std::string trim(const std::string& text) {
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t a = 0, b = text.size();
    while (a < b && space(text[a])) ++a;
    while (b > a && space(text[b - 1])) --b;
    return text.substr(a, b - a);
}

// Insertion order is not kept: the file is sorted by key, which makes a diff of
// two settings files readable and the output stable between runs.
using Table = std::map<std::string, std::string>;

Table load() {
    Table table;
    const Path path = settingsFile();
    if (path.empty()) return table;

    std::string text, error;
    if (!readFile(path, text, error)) return table;

    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(start, end - start));
        start = end + 1;

        // A leading '[' is a section header and a '#' or ';' a comment. The file
        // has no sections of its own, but one added by hand should be skipped
        // rather than read as a key.
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        table[trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }
    return table;
}

void store(const Table& table) {
    const Path path = settingsFile();
    if (path.empty()) return;

    std::string text = "# CV Builder settings. Rewritten by the program; hand edits are read back.\n";
    for (const auto& entry : table) {
        if (entry.second.empty()) continue;
        text += entry.first;
        text += '=';
        text += entry.second;
        text += '\n';
    }
    std::string error;
    writeFile(path, text, error);
}

std::string listKey(const char* name, size_t index) {
    return std::string(name) + "." + std::to_string(index + 1);
}

}  // namespace

int readInt(const char* name, int fallback) {
    const Table table = load();
    auto found = table.find(name);
    if (found == table.end()) return fallback;
    double value = 0.0;
    if (!numfmt::parse(found->second, value)) return fallback;
    return static_cast<int>(value);
}

void writeInt(const char* name, int value) {
    Table table = load();
    table[name] = std::to_string(value);
    store(table);
}

std::string readString(const char* name) {
    const Table table = load();
    auto found = table.find(name);
    return found == table.end() ? std::string() : found->second;
}

void writeString(const char* name, const std::string& value) {
    Table table = load();
    if (value.empty())
        table.erase(name);
    else
        table[name] = value;
    store(table);
}

std::vector<std::string> readList(const char* name) {
    const Table table = load();
    std::vector<std::string> out;
    // Stops at the first gap: the list is always written as a run from 1, so a
    // gap means the rest is left over from something else.
    for (size_t i = 0;; ++i) {
        auto found = table.find(listKey(name, i));
        if (found == table.end() || found->second.empty()) break;
        out.push_back(found->second);
    }
    return out;
}

void writeList(const char* name, const std::vector<std::string>& values) {
    Table table = load();
    // Clear whatever the list held before, so a shorter list does not leave the
    // tail of the longer one behind.
    const std::string prefix = std::string(name) + ".";
    for (auto it = table.begin(); it != table.end();)
        it = (it->first.compare(0, prefix.size(), prefix) == 0) ? table.erase(it) : ++it;

    for (size_t i = 0; i < values.size(); ++i)
        if (!values[i].empty()) table[listKey(name, i)] = values[i];
    store(table);
}

}  // namespace settings
}  // namespace platform
}  // namespace cvb
