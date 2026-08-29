// A small TrueType reader: glyph lookup, advance widths, and subsetting.
//
// Everything the app does with text goes through here, and that is deliberate.
// The layout engine measures with these advances, the PDF writer embeds a
// subset of this very file, and the on-screen preview draws the same glyph ids
// with the same advances - so the preview is not an approximation of the PDF,
// it is the same run of glyphs at a different scale.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "file.h"

namespace cvb {

class Font {
public:
    // `faceIndex` picks a face out of a TrueType collection (.ttc), where
    // several faces share one file - which is how macOS ships most of its own.
    // A plain .ttf has exactly face 0.
    bool loadFromFile(const Path& path, int faceIndex, std::string& error);
    bool loadFromFile(const Path& path, std::string& error) {
        return loadFromFile(path, 0, error);
    }
    // For a face that is not a file of its own - one read out of an archive, or
    // compiled into the program.
    bool loadFromMemory(std::vector<uint8_t> data, int faceIndex, std::string& error);

    bool valid() const { return !data_.empty(); }

    // Where the face came from, and which face in it. A screen backend hands
    // both to the system text engine so it rasterises the very file the metrics
    // were measured from. Empty path for a face loaded from memory.
    const Path& path() const { return path_; }
    int faceIndex() const { return faceIndex_; }

    // The bytes the face was parsed from, for a backend that has to hand the same
    // file to a system text engine and would rather not read it again.
    const std::vector<uint8_t>& fileData() const { return data_; }

    // Glyph id for a code point, 0 (.notdef) when the font has no such glyph.
    uint16_t glyphFor(uint32_t codePoint) const;

    // Advance width in em units (1.0 == the font size).
    double advance(uint16_t glyph) const;

    // Splits UTF-8 into glyph ids; `codePoints` (optional) receives the
    // matching code points, which the PDF writer needs for its ToUnicode map.
    void shape(const std::string& utf8, std::vector<uint16_t>& glyphs,
               std::vector<uint32_t>* codePoints = nullptr) const;

    // Width of a UTF-8 string set at `size` points.
    double measure(const std::string& utf8, double size) const;

    // A valid TrueType file containing only the requested glyphs (plus the
    // components composite glyphs need). Glyph ids are preserved, so the
    // caller's ids stay correct against the subset.
    std::vector<uint8_t> subset(const std::vector<uint16_t>& glyphs) const;

    int unitsPerEm() const { return unitsPerEm_; }
    int numGlyphs() const { return numGlyphs_; }
    // Font-unit metrics for the PDF font descriptor.
    int xMin() const { return xMin_; }
    int yMin() const { return yMin_; }
    int xMax() const { return xMax_; }
    int yMax() const { return yMax_; }
    int ascent() const { return ascent_; }
    int descent() const { return descent_; }
    int capHeight() const { return capHeight_; }
    bool isBold() const { return bold_; }

private:
    struct Table { uint32_t offset = 0, length = 0; };

    const Table* table(const char* tag) const;
    bool parse(int faceIndex, std::string& error);
    bool readHead(std::string& error);
    bool readMetrics(std::string& error);
    void readCmap();
    void parseCmap4(uint32_t offset);
    void parseCmap12(uint32_t offset);
    void collectComponents(uint16_t glyph, std::vector<bool>& used, int depth) const;

    Path path_;
    int faceIndex_ = 0;
    std::vector<uint8_t> data_;
    std::unordered_map<uint32_t, Table> tables_;
    std::unordered_map<uint32_t, uint16_t> cmap_;
    std::vector<uint16_t> advances_;  // per glyph, font units
    std::vector<uint32_t> loca_;      // numGlyphs + 1 offsets into glyf

    int unitsPerEm_ = 1000;
    int numGlyphs_ = 0;
    int xMin_ = 0, yMin_ = 0, xMax_ = 0, yMax_ = 0;
    int ascent_ = 0, descent_ = 0, capHeight_ = 0;
    bool bold_ = false;
};

// One regular/bold pair worth trying, as offered by the bundled assets or by
// the platform's font list.
struct FontChoice {
    Path regular;
    Path bold;
    int regularFace = 0;
    int boldFace = 0;
    // The name the system knows this face by, UTF-8. The layout engine, the
    // preview and the PDF writer all work from the parsed file and never need
    // it; a backend that asks the operating system for a face by name does.
    std::string family;
};

// The regular/bold pair the whole app measures, draws and embeds with.
class FontSet {
public:
    // Tries the candidates in order and keeps the first pair that parses, so the
    // caller expresses its preference as the order of the list. Fails only when
    // none of them can be read.
    bool load(const std::vector<FontChoice>& candidates, std::string& error);

    const Font& face(bool bold) const { return bold ? bold_ : regular_; }
    const Font& regular() const { return regular_; }
    const Font& bold() const { return bold_; }
    bool valid() const { return regular_.valid() && bold_.valid(); }

    const std::string& family() const { return family_; }

private:
    Font regular_;
    Font bold_;
    std::string family_;
};

}  // namespace cvb
