// Whole files in and out, and the one place the program's idea of a path is
// defined.
//
// std::filesystem::path rather than a string of some width: it holds UTF-16 on
// Windows and bytes on the Unixes without the caller having to know which, it
// joins with the right separator on its own, and it takes both a std::wstring
// and a std::string, so callers on either side convert implicitly. That removes
// the two reasons the core used to need Windows: _wfopen, and paths glued
// together with a backslash.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cvb {

using Path = std::filesystem::path;

// UTF-8 text, byte for byte as stored - no BOM is added or removed here; the
// JSON parser is the one that tolerates a leading BOM.
bool readFile(const Path& path, std::string& out, std::string& error);
bool readFile(const Path& path, std::vector<uint8_t>& out, std::string& error);

bool writeFile(const Path& path, const void* data, size_t size, std::string& error);

inline bool writeFile(const Path& path, const std::string& data, std::string& error) {
    return writeFile(path, data.data(), data.size(), error);
}

// A path as UTF-8, and back. Needed wherever a path has to travel as text -
// through a settings file, through JSON, through a log line.
//
// Not simply Path(text): a std::string handed to path's constructor is read in
// the platform's *native narrow* encoding, which on Windows is a code page that
// cannot spell most paths. These two are the only conversion the program uses,
// so the mistake can only be made in one place.
std::string toUtf8(const Path& path);
Path fromUtf8(const std::string& text);

}  // namespace cvb
