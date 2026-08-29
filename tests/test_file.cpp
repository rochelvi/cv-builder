// Whole-file reading and writing, and the one thing about it that could quietly
// break on a platform: a path with non-ASCII characters in it. On Windows that
// path is UTF-16 and on the Unixes it is bytes, which is exactly what
// std::filesystem::path exists to hide - but only if the standard library
// actually opens the wide name. This checks that it does.
#include <filesystem>
#include <string>
#include <vector>

#include "file.h"
#include "harness.h"
#include "model.h"

namespace {

// A directory of our own under the system temporary directory, removed again on
// the way out so a test run leaves nothing behind.
struct Sandbox {
    cvb::Path dir;

    Sandbox() {
        std::error_code ec;
        dir = std::filesystem::temp_directory_path(ec) / "cvb-tests";
        std::filesystem::create_directories(dir, ec);
    }
    ~Sandbox() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

}  // namespace

TEST(file_round_trips_text) {
    Sandbox box;
    const cvb::Path path = box.dir / "plain.txt";
    std::string error;

    CHECK(cvb::writeFile(path, std::string("hello\nworld"), error));
    std::string back;
    CHECK(cvb::readFile(path, back, error));
    CHECK_EQ(back, std::string("hello\nworld"));
}

TEST(file_round_trips_bytes) {
    Sandbox box;
    const cvb::Path path = box.dir / "bytes.bin";
    std::string error;

    // Includes a NUL and a high byte: nothing here may treat the content as
    // text, since this is the path a font file takes.
    const std::vector<uint8_t> data = {0x00, 0x01, 0xFF, 0x7F, 0x0A, 0x0D, 0x00};
    CHECK(cvb::writeFile(path, data.data(), data.size(), error));

    std::vector<uint8_t> back;
    CHECK(cvb::readFile(path, back, error));
    CHECK(back == data);
}

TEST(file_handles_an_empty_file) {
    Sandbox box;
    const cvb::Path path = box.dir / "empty.txt";
    std::string error;

    CHECK(cvb::writeFile(path, std::string(), error));
    std::string back = "not empty yet";
    CHECK(cvb::readFile(path, back, error));
    CHECK(back.empty());
}

TEST(file_reports_a_missing_file) {
    Sandbox box;
    std::string error;
    std::string back;
    CHECK(!cvb::readFile(box.dir / "nothing-here.json", back, error));
    CHECK(!error.empty());
}

TEST(file_opens_a_cyrillic_path) {
    Sandbox box;
    // Both the directory and the file name are non-ASCII, and the name carries
    // a character outside Latin-1 so a single-byte code page cannot spell it.
    const cvb::Path dir = box.dir / u8"Резюме и вёрстка";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    CHECK(!ec);

    const cvb::Path path = dir / u8"моё резюме.json";
    std::string error;
    CHECK(cvb::writeFile(path, std::string("{\"name\":\"Даниил\"}"), error));
    CHECK(std::filesystem::exists(path));

    std::string back;
    CHECK(cvb::readFile(path, back, error));
    CHECK_EQ(back, std::string("{\"name\":\"Даниил\"}"));
}

TEST(model_round_trips_through_a_cyrillic_path) {
    Sandbox box;
    const cvb::Path path = box.dir / u8"резюме.json";

    cvb::CV written = cvb::emptyCV();
    written.name = "Даниил Мишин";
    written.role = "SYSTEM ADMINISTRATOR";
    written.summary = "Первая строка\nвторая строка";

    std::string error;
    CHECK(cvb::save(path, written, error));

    cvb::CV read;
    CHECK(cvb::load(path, read, error));
    CHECK_EQ(read.name, written.name);
    CHECK_EQ(read.role, written.role);
    CHECK_EQ(read.summary, written.summary);
}
