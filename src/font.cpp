#include "font.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>  // _wfopen: Unicode paths

namespace cvb {
namespace {

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
int16_t sbe16(const uint8_t* p) { return static_cast<int16_t>(be16(p)); }
uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}
void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

uint32_t tagOf(const char* tag) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(tag[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(tag[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(tag[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(tag[3]));
}

// Sum of the data as big-endian 32-bit words, the sfnt checksum rule.
uint32_t checksum(const uint8_t* data, size_t length) {
    uint32_t sum = 0;
    size_t whole = length & ~static_cast<size_t>(3);
    for (size_t i = 0; i < whole; i += 4) sum += be32(data + i);
    if (whole < length) {  // the tail is treated as zero-padded
        uint8_t tail[4] = {0, 0, 0, 0};
        std::memcpy(tail, data + whole, length - whole);
        sum += be32(tail);
    }
    return sum;
}

// Decodes the next UTF-8 code point, tolerating malformed bytes by passing
// them through as U+FFFD so a stray byte never eats the rest of the string.
uint32_t nextCodePoint(const std::string& s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) {
        return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
    };
    if (c < 0x80) { ++i; return c; }
    if ((c & 0xE0) == 0xC0 && cont(1)) {
        uint32_t cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        uint32_t cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                      (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        uint32_t cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                      ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) |
                      (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

bool readWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    FILE* fh = _wfopen(path.c_str(), L"rb");
    if (!fh) return false;
    std::fseek(fh, 0, SEEK_END);
    long size = std::ftell(fh);
    std::fseek(fh, 0, SEEK_SET);
    bool ok = size > 0;
    if (ok) {
        out.resize(static_cast<size_t>(size));
        ok = std::fread(out.data(), 1, out.size(), fh) == out.size();
    }
    std::fclose(fh);
    if (!ok) out.clear();
    return ok;
}

}  // namespace

// ---------------------------------------------------------------- loading

const Font::Table* Font::table(const char* tag) const {
    auto it = tables_.find(tagOf(tag));
    return it == tables_.end() ? nullptr : &it->second;
}

bool Font::loadFromFile(const std::wstring& path, std::string& error) {
    data_.clear();
    tables_.clear();
    cmap_.clear();
    advances_.clear();
    loca_.clear();

    if (!readWholeFile(path, data_)) { error = "не удалось прочитать файл шрифта"; return false; }
    if (data_.size() < 12) { error = "файл шрифта слишком мал"; return false; }

    uint32_t version = be32(data_.data());
    if (version == tagOf("ttcf")) { error = "коллекции шрифтов (.ttc) не поддерживаются"; return false; }
    if (version != 0x00010000 && version != tagOf("true")) {
        error = "не TrueType (OpenType/CFF не поддерживается)";
        return false;
    }

    int numTables = be16(data_.data() + 4);
    if (data_.size() < static_cast<size_t>(12 + 16 * numTables)) { error = "обрезанный шрифт"; return false; }
    for (int i = 0; i < numTables; ++i) {
        const uint8_t* rec = data_.data() + 12 + 16 * i;
        Table t;
        t.offset = be32(rec + 8);
        t.length = be32(rec + 12);
        if (t.offset <= data_.size() && t.length <= data_.size() - t.offset)
            tables_[be32(rec)] = t;
    }

    if (!readHead(error)) { data_.clear(); return false; }
    if (!readMetrics(error)) { data_.clear(); return false; }
    readCmap();
    if (cmap_.empty()) { error = "в шрифте нет таблицы Unicode cmap"; data_.clear(); return false; }

    path_ = path;
    return true;
}

bool Font::readHead(std::string& error) {
    const Table* head = table("head");
    const Table* maxp = table("maxp");
    if (!head || head->length < 54 || !maxp || maxp->length < 6) {
        error = "в шрифте нет таблиц head/maxp";
        return false;
    }
    const uint8_t* h = data_.data() + head->offset;
    unitsPerEm_ = be16(h + 18);
    if (unitsPerEm_ <= 0) unitsPerEm_ = 1000;
    xMin_ = sbe16(h + 36);
    yMin_ = sbe16(h + 38);
    xMax_ = sbe16(h + 40);
    yMax_ = sbe16(h + 42);
    int indexToLocFormat = sbe16(h + 50);
    numGlyphs_ = be16(data_.data() + maxp->offset + 4);
    if (numGlyphs_ <= 0) { error = "в шрифте нет глифов"; return false; }

    // loca/glyf are optional for measuring but required for subsetting.
    const Table* loca = table("loca");
    if (loca) {
        size_t count = static_cast<size_t>(numGlyphs_) + 1;
        const uint8_t* p = data_.data() + loca->offset;
        loca_.reserve(count);
        if (indexToLocFormat == 0 && loca->length >= count * 2) {
            for (size_t i = 0; i < count; ++i) loca_.push_back(be16(p + i * 2) * 2u);
        } else if (indexToLocFormat != 0 && loca->length >= count * 4) {
            for (size_t i = 0; i < count; ++i) loca_.push_back(be32(p + i * 4));
        }
    }
    return true;
}

bool Font::readMetrics(std::string& error) {
    const Table* hhea = table("hhea");
    const Table* hmtx = table("hmtx");
    if (!hhea || hhea->length < 36 || !hmtx) { error = "в шрифте нет таблиц hhea/hmtx"; return false; }
    const uint8_t* hh = data_.data() + hhea->offset;
    ascent_ = sbe16(hh + 4);
    descent_ = sbe16(hh + 6);
    int numberOfHMetrics = be16(hh + 34);
    if (numberOfHMetrics <= 0) { error = "испорченная таблица hhea"; return false; }

    const uint8_t* hm = data_.data() + hmtx->offset;
    advances_.resize(static_cast<size_t>(numGlyphs_));
    uint16_t last = 0;
    for (int gid = 0; gid < numGlyphs_; ++gid) {
        if (gid < numberOfHMetrics) {
            size_t at = static_cast<size_t>(gid) * 4;
            if (at + 2 <= hmtx->length) last = be16(hm + at);
        }
        advances_[static_cast<size_t>(gid)] = last;  // trailing glyphs reuse the last advance
    }

    capHeight_ = static_cast<int>(ascent_ * 0.72);  // a sane guess when OS/2 has none
    const Table* os2 = table("OS/2");
    if (os2 && os2->length >= 64) {
        const uint8_t* o = data_.data() + os2->offset;
        int version = be16(o);
        bold_ = be16(o + 4) >= 600 || (be16(o + 62) & 0x20) != 0;  // usWeightClass / fsSelection
        if (version >= 2 && os2->length >= 90) {
            int cap = sbe16(o + 88);
            if (cap > 0) capHeight_ = cap;
        }
    }
    return true;
}

void Font::readCmap() {
    const Table* cmap = table("cmap");
    if (!cmap || cmap->length < 4) return;
    const uint8_t* base = data_.data() + cmap->offset;
    int count = be16(base + 2);

    uint32_t best4 = 0, best12 = 0;
    for (int i = 0; i < count; ++i) {
        const uint8_t* rec = base + 4 + 8 * i;
        if (static_cast<size_t>(4 + 8 * i + 8) > cmap->length) break;
        int platform = be16(rec);
        int encoding = be16(rec + 2);
        uint32_t offset = cmap->offset + be32(rec + 4);
        if (offset + 4 > data_.size()) continue;
        int format = be16(data_.data() + offset);
        bool unicode = (platform == 3 && (encoding == 1 || encoding == 10)) || platform == 0;
        if (!unicode) continue;
        if (format == 12 && !best12) best12 = offset;
        if (format == 4 && !best4) best4 = offset;
    }
    // Format 12 covers the astral planes; format 4 is the universal fallback.
    if (best12) parseCmap12(best12);
    if (cmap_.empty() && best4) parseCmap4(best4);
}

void Font::parseCmap4(uint32_t offset) {
    const uint8_t* t = data_.data() + offset;
    uint32_t length = be16(t + 2);
    if (offset + length > data_.size() || length < 16) return;
    int segCount = be16(t + 6) / 2;
    const uint8_t* endCode = t + 14;
    const uint8_t* startCode = endCode + segCount * 2 + 2;
    const uint8_t* idDelta = startCode + segCount * 2;
    const uint8_t* idRangeOffset = idDelta + segCount * 2;
    if (static_cast<size_t>(idRangeOffset + segCount * 2 - t) > length) return;

    for (int seg = 0; seg < segCount; ++seg) {
        uint32_t first = be16(startCode + seg * 2);
        uint32_t last = be16(endCode + seg * 2);
        if (first > last) continue;
        uint16_t delta = be16(idDelta + seg * 2);
        uint16_t rangeOffset = be16(idRangeOffset + seg * 2);
        for (uint32_t cp = first; cp <= last; ++cp) {
            uint16_t gid;
            if (rangeOffset == 0) {
                gid = static_cast<uint16_t>(cp + delta);
            } else {
                const uint8_t* at = idRangeOffset + seg * 2 + rangeOffset + (cp - first) * 2;
                if (at + 2 > data_.data() + data_.size()) continue;
                gid = be16(at);
                if (gid) gid = static_cast<uint16_t>(gid + delta);
            }
            if (gid) cmap_.emplace(cp, gid);
        }
    }
}

void Font::parseCmap12(uint32_t offset) {
    const uint8_t* t = data_.data() + offset;
    if (offset + 16 > data_.size()) return;
    uint32_t groups = be32(t + 12);
    if (offset + 16 + groups * 12ull > data_.size()) return;
    for (uint32_t g = 0; g < groups; ++g) {
        const uint8_t* rec = t + 16 + g * 12;
        uint32_t first = be32(rec), last = be32(rec + 4), startGlyph = be32(rec + 8);
        if (last < first || last - first > 0x10FFFF) continue;
        for (uint32_t cp = first; cp <= last; ++cp) {
            uint32_t gid = startGlyph + (cp - first);
            if (gid && gid < static_cast<uint32_t>(numGlyphs_))
                cmap_.emplace(cp, static_cast<uint16_t>(gid));
        }
    }
}

// ---------------------------------------------------------------- querying

uint16_t Font::glyphFor(uint32_t codePoint) const {
    auto it = cmap_.find(codePoint);
    return it == cmap_.end() ? 0 : it->second;
}

double Font::advance(uint16_t glyph) const {
    if (glyph >= advances_.size()) return 0.0;
    return static_cast<double>(advances_[glyph]) / unitsPerEm_;
}

void Font::shape(const std::string& utf8, std::vector<uint16_t>& glyphs,
                 std::vector<uint32_t>* codePoints) const {
    glyphs.clear();
    if (codePoints) codePoints->clear();
    for (size_t i = 0; i < utf8.size();) {
        uint32_t cp = nextCodePoint(utf8, i);
        glyphs.push_back(glyphFor(cp));
        if (codePoints) codePoints->push_back(cp);
    }
}

double Font::measure(const std::string& utf8, double size) const {
    double units = 0.0;
    for (size_t i = 0; i < utf8.size();) {
        uint16_t gid = glyphFor(nextCodePoint(utf8, i));
        if (gid < advances_.size()) units += advances_[gid];
    }
    return units * size / unitsPerEm_;
}

// --------------------------------------------------------------- subsetting

void Font::collectComponents(uint16_t glyph, std::vector<bool>& used, int depth) const {
    if (depth > 8 || glyph + 1u >= loca_.size()) return;
    uint32_t start = loca_[glyph], end = loca_[glyph + 1];
    const Table* glyf = table("glyf");
    if (!glyf || end <= start || end - start < 10 || end > glyf->length) return;
    const uint8_t* g = data_.data() + glyf->offset + start;
    if (sbe16(g) >= 0) return;  // simple glyph: nothing to pull in

    size_t at = 10;
    for (;;) {
        if (at + 4 > end - start) return;
        uint16_t flags = be16(g + at);
        uint16_t component = be16(g + at + 2);
        at += 4;
        at += (flags & 0x0001) ? 4 : 2;             // arguments
        if (flags & 0x0008) at += 2;                // simple scale
        else if (flags & 0x0040) at += 4;           // x and y scale
        else if (flags & 0x0080) at += 8;           // 2x2 transform
        if (component < used.size() && !used[component]) {
            used[component] = true;
            collectComponents(component, used, depth + 1);
        }
        if (!(flags & 0x0020)) return;              // MORE_COMPONENTS
    }
}

std::vector<uint8_t> Font::subset(const std::vector<uint16_t>& glyphs) const {
    std::vector<uint8_t> result;
    const Table* glyf = table("glyf");
    if (!glyf || loca_.size() != static_cast<size_t>(numGlyphs_) + 1) return result;

    // Glyph ids are left untouched, so a glyph is either present or empty. That
    // keeps every id the caller already computed valid against the subset.
    std::vector<bool> used(static_cast<size_t>(numGlyphs_), false);
    used[0] = true;  // .notdef must always be there
    for (uint16_t gid : glyphs)
        if (gid < used.size()) used[gid] = true;
    for (int gid = 0; gid < numGlyphs_; ++gid)
        if (used[static_cast<size_t>(gid)]) collectComponents(static_cast<uint16_t>(gid), used, 0);

    std::vector<uint8_t> newGlyf;
    std::vector<uint8_t> newLoca;
    newLoca.reserve((static_cast<size_t>(numGlyphs_) + 1) * 4);
    for (int gid = 0; gid < numGlyphs_; ++gid) {
        put32(newLoca, static_cast<uint32_t>(newGlyf.size()));
        if (!used[static_cast<size_t>(gid)]) continue;
        uint32_t start = loca_[static_cast<size_t>(gid)];
        uint32_t end = loca_[static_cast<size_t>(gid) + 1];
        if (end <= start || end > glyf->length) continue;
        const uint8_t* src = data_.data() + glyf->offset + start;
        newGlyf.insert(newGlyf.end(), src, src + (end - start));
        while (newGlyf.size() % 4) newGlyf.push_back(0);
    }
    put32(newLoca, static_cast<uint32_t>(newGlyf.size()));

    // head is copied with the long loca format forced on and the file checksum
    // cleared; it is filled in once the whole file exists.
    const Table* head = table("head");
    if (!head || head->length < 54) return result;
    std::vector<uint8_t> newHead(data_.data() + head->offset,
                                 data_.data() + head->offset + head->length);
    newHead[8] = newHead[9] = newHead[10] = newHead[11] = 0;
    newHead[50] = 0;
    newHead[51] = 1;

    struct Out { const char* tag; std::vector<uint8_t> data; };
    std::vector<Out> out;
    out.push_back({"head", std::move(newHead)});
    // "post" and "name" are dropped: a CIDFontType2 does not need them and post
    // alone is a third of the file. cvt /fpgm/prep stay so hinting still works.
    for (const char* tag : {"OS/2", "cvt ", "fpgm", "gasp", "hhea", "hmtx", "maxp", "prep"}) {
        if (const Table* t = table(tag))
            out.push_back({tag, std::vector<uint8_t>(data_.data() + t->offset,
                                                     data_.data() + t->offset + t->length)});
    }
    out.push_back({"loca", std::move(newLoca)});
    out.push_back({"glyf", std::move(newGlyf)});

    std::sort(out.begin(), out.end(),
              [](const Out& a, const Out& b) { return tagOf(a.tag) < tagOf(b.tag); });

    uint16_t count = static_cast<uint16_t>(out.size());
    uint16_t entrySelector = 0;
    while ((1u << (entrySelector + 1)) <= count) ++entrySelector;
    uint16_t searchRange = static_cast<uint16_t>(16u << entrySelector);

    put32(result, 0x00010000);
    put16(result, count);
    put16(result, searchRange);
    put16(result, entrySelector);
    put16(result, static_cast<uint16_t>(count * 16 - searchRange));

    uint32_t offset = static_cast<uint32_t>(12 + 16 * count);
    for (size_t i = 0; i < out.size(); ++i) {
        uint32_t padded = static_cast<uint32_t>((out[i].data.size() + 3) & ~size_t(3));
        put32(result, tagOf(out[i].tag));
        put32(result, checksum(out[i].data.data(), out[i].data.size()));
        put32(result, offset);
        put32(result, static_cast<uint32_t>(out[i].data.size()));
        offset += padded;
    }
    for (const Out& t : out) {
        result.insert(result.end(), t.data.begin(), t.data.end());
        while (result.size() % 4) result.push_back(0);
    }

    // checkSumAdjustment closes the loop over the finished file.
    uint32_t total = checksum(result.data(), result.size());
    uint32_t adjustment = 0xB1B0AFBAu - total;
    for (size_t i = 0; i < out.size(); ++i) {
        if (std::strcmp(out[i].tag, "head") != 0) continue;
        uint32_t at = be32(result.data() + 12 + 16 * i + 8) + 8;
        if (at + 4 <= result.size()) {
            result[at] = static_cast<uint8_t>(adjustment >> 24);
            result[at + 1] = static_cast<uint8_t>(adjustment >> 16);
            result[at + 2] = static_cast<uint8_t>(adjustment >> 8);
            result[at + 3] = static_cast<uint8_t>(adjustment);
        }
        break;
    }
    return result;
}

// ------------------------------------------------------------------ FontSet

bool FontSet::loadSystem(std::string& error) {
    wchar_t windir[MAX_PATH] = {0};
    UINT n = GetWindowsDirectoryW(windir, MAX_PATH);
    std::wstring fonts = (n ? std::wstring(windir, n) : std::wstring(L"C:\\Windows")) + L"\\Fonts\\";

    // Arial first: it is metrically identical to the template's Helvetica, so
    // line breaks land exactly where the original design put them.
    // Third column is the GDI family name of the same face, for backends that
    // go through the system rather than through the parsed file.
    const wchar_t* candidates[][3] = {
        {L"arial.ttf", L"arialbd.ttf", L"Arial"},
        {L"segoeui.ttf", L"segoeuib.ttf", L"Segoe UI"},
        {L"tahoma.ttf", L"tahomabd.ttf", L"Tahoma"},
        {L"verdana.ttf", L"verdanab.ttf", L"Verdana"},
        {L"calibri.ttf", L"calibrib.ttf", L"Calibri"},
    };
    std::string last = "шрифты не найдены";
    for (const auto& entry : candidates) {
        Font regular, bold;
        if (!regular.loadFromFile(fonts + entry[0], last)) continue;
        if (!bold.loadFromFile(fonts + entry[1], last)) continue;
        regular_ = std::move(regular);
        bold_ = std::move(bold);
        family_ = entry[2];
        return true;
    }
    error = "не удалось загрузить системный шрифт (" + last + ")";
    return false;
}

}  // namespace cvb
