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

}  // namespace cvb
