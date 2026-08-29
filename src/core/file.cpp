#include "file.h"

#include <fstream>

namespace cvb {
namespace {

// The messages the previous, Windows-only readers and writers produced. They
// reach the user through message boxes and the status line, so they are kept
// word for word.
const char* const kCannotOpen = "не удалось открыть файл";
const char* const kReadFailed = "ошибка чтения файла";
const char* const kCannotCreate = "не удалось создать файл";
const char* const kWriteFailed = "ошибка записи файла";

// Reads the rest of the stream in one go. Asking the stream for its length
// first, rather than appending byte by byte through an iterator, keeps loading a
// megabyte-sized font a single allocation and a single read.
template <class Container>
bool readAll(std::ifstream& in, Container& out, std::string& error) {
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 0 || !in) { error = kReadFailed; return false; }

    out.resize(static_cast<size_t>(size));
    if (size == 0) return true;
    in.read(reinterpret_cast<char*>(&out[0]), size);
    if (in.gcount() != size) {
        out.clear();
        error = kReadFailed;
        return false;
    }
    return true;
}

}  // namespace

bool readFile(const Path& path, std::string& out, std::string& error) {
    out.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = kCannotOpen; return false; }
    return readAll(in, out, error);
}

bool readFile(const Path& path, std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = kCannotOpen; return false; }
    return readAll(in, out, error);
}

std::string toUtf8(const Path& path) {
    // u8string() is std::string in C++17 and std::u8string from C++20; the
    // iterator pair copies either into bytes without naming the type.
    const auto encoded = path.u8string();
    return std::string(encoded.begin(), encoded.end());
}

Path fromUtf8(const std::string& text) {
#if defined(__cpp_char8_t)
    return Path(std::u8string(text.begin(), text.end()));
#else
    // Deprecated in C++20, which is why the branch above exists, but it is the
    // only correct spelling before it.
    return std::filesystem::u8path(text);
#endif
}

bool writeFile(const Path& path, const void* data, size_t size, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = kCannotCreate; return false; }
    if (size) out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    // Closed explicitly: a write that fails while the buffer is flushed has to
    // be reported, and a destructor cannot.
    out.close();
    if (!out) { error = kWriteFailed; return false; }
    return true;
}

}  // namespace cvb
