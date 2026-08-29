#include "pdf.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>  // _wfopen: Unicode paths
#include <map>
#include <set>

#include "numeric.h"

namespace cvb {
namespace {

// Three decimals, trailing zeros trimmed. Goes through numfmt::fixed rather than
// snprintf: a PDF written with the locale's comma in its coordinates is not a
// PDF any reader will open.
std::string num(double v) {
    if (std::fabs(v) < 5e-4) return "0";
    std::string s = numfmt::fixed(v, 3);
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        size_t last = s.find_last_not_of('0');
        if (last == dot) last = dot - 1;
        s.erase(last + 1);
    }
    return s;
}

std::string colorOp(RGB c, const char* op) {
    return num(c.r / 255.0) + " " + num(c.g / 255.0) + " " + num(c.b / 255.0) + " " + op + "\n";
}

const char* kHex = "0123456789ABCDEF";

void appendHex16(std::string& out, uint16_t v) {
    out += kHex[(v >> 12) & 0xF];
    out += kHex[(v >> 8) & 0xF];
    out += kHex[(v >> 4) & 0xF];
    out += kHex[v & 0xF];
}

// A PDF text string holding arbitrary Unicode: UTF-16BE, BOM first, hex form.
std::string utf16HexString(const std::string& utf8) {
    std::string out = "<FEFF";
    for (size_t i = 0; i < utf8.size();) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        uint32_t cp;
        if (c < 0x80) { cp = c; i += 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(utf8[i + 2]) & 0x3Fu);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<unsigned char>(utf8[i + 2]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(utf8[i + 3]) & 0x3Fu);
            i += 4;
        } else { cp = 0xFFFD; i += 1; }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            appendHex16(out, static_cast<uint16_t>(0xD800 + (cp >> 10)));
            appendHex16(out, static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            appendHex16(out, static_cast<uint16_t>(cp));
        }
    }
    out += '>';
    return out;
}

// Six uppercase letters derived from the glyph set, as the spec asks for a
// subset prefix that differs when the subset differs.
std::string subsetTag(const std::set<uint16_t>& glyphs, bool bold) {
    uint32_t hash = bold ? 0x9E3779B9u : 0x85EBCA6Bu;
    for (uint16_t gid : glyphs) hash = hash * 31u + gid;
    std::string tag;
    for (int i = 0; i < 6; ++i) {
        tag += static_cast<char>('A' + (hash % 26));
        hash /= 26;
        hash = hash * 2654435761u + 1u;
    }
    return tag;
}

// Collects one font's usage across the document.
struct FaceUse {
    std::set<uint16_t> glyphs;
    std::map<uint16_t, uint32_t> toUnicode;  // glyph -> code point, for text extraction
    bool used = false;
};

class Writer {
public:
    Writer(const Document& doc, const FontSet& fonts) : doc_(doc), fonts_(fonts) {}

    bool run(std::vector<uint8_t>& out, std::string& error) {
        if (!fonts_.valid()) { error = "шрифты не загружены"; return false; }
        if (doc_.pages.empty()) { error = "нечего печатать"; return false; }
        collectGlyphs();

        // Object numbering is fixed up front so references can be written
        // before the objects themselves exist.
        const int pageCount = static_cast<int>(doc_.pages.size());
        catalog_ = 1;
        pagesNode_ = 2;
        info_ = 3;
        int next = 4;
        firstFontObject_ = next;
        for (int face = 0; face < 2; ++face) {
            if (!use_[face].used) continue;
            fontObject_[face] = next;
            next += 5;  // Type0, CIDFont, descriptor, font file, ToUnicode
        }
        firstPageObject_ = next;
        next += pageCount * 2;  // page + its content stream

        body_.clear();
        offsets_.assign(static_cast<size_t>(next), 0);
        body_ += "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";

        writeCatalog(pageCount);
        writeInfo();
        for (int face = 0; face < 2; ++face)
            if (use_[face].used) writeFont(face);
        for (int i = 0; i < pageCount; ++i) writePage(i);

        writeTrailer(next);
        out.assign(body_.begin(), body_.end());
        return true;
    }

private:
    // ---------- object plumbing ----------
    void begin(int id) {
        offsets_[static_cast<size_t>(id)] = body_.size();
        body_ += std::to_string(id) + " 0 obj\n";
    }
    void end() { body_ += "endobj\n"; }

    void stream(int id, const std::string& dict, const std::string& data) {
        begin(id);
        body_ += "<< " + dict + " /Length " + std::to_string(data.size()) + " >>\nstream\n";
        body_ += data;
        body_ += "\nendstream\n";
        end();
    }

    void collectGlyphs() {
        std::vector<uint16_t> glyphs;
        std::vector<uint32_t> codes;
        for (const Page& page : doc_.pages) {
            for (const TextItem& item : page.texts) {
                int face = item.bold ? 1 : 0;
                fonts_.face(item.bold).shape(item.text, glyphs, &codes);
                use_[face].used = true;
                for (size_t i = 0; i < glyphs.size(); ++i) {
                    use_[face].glyphs.insert(glyphs[i]);
                    use_[face].toUnicode.emplace(glyphs[i], codes[i]);
                }
            }
        }
    }

    void writeCatalog(int pageCount) {
        begin(catalog_);
        body_ += "<< /Type /Catalog /Pages " + std::to_string(pagesNode_) + " 0 R >>\n";
        end();

        begin(pagesNode_);
        body_ += "<< /Type /Pages /Count " + std::to_string(pageCount) + " /Kids [";
        for (int i = 0; i < pageCount; ++i)
            body_ += " " + std::to_string(firstPageObject_ + i * 2) + " 0 R";
        body_ += " ] >>\n";
        end();
    }

    void writeInfo() {
        begin(info_);
        body_ += "<< /Producer " + utf16HexString("CV Builder") + " /Creator " +
                 utf16HexString("CV Builder");
        if (!doc_.title.empty()) body_ += " /Title " + utf16HexString(doc_.title);
        if (!doc_.author.empty()) body_ += " /Author " + utf16HexString(doc_.author);
        body_ += " >>\n";
        end();
    }

    void writeFont(int face) {
        const Font& font = fonts_.face(face == 1);
        const FaceUse& use = use_[face];
        const int base = fontObject_[face];
        const int cidFont = base + 1;
        const int descriptor = base + 2;
        const int fontFile = base + 3;
        const int toUnicode = base + 4;
        const std::string name = subsetTag(use.glyphs, face == 1) + "+CVSans" +
                                 (face == 1 ? "-Bold" : "");
        const double scale = 1000.0 / font.unitsPerEm();

        begin(base);
        body_ += "<< /Type /Font /Subtype /Type0 /BaseFont /" + name +
                 " /Encoding /Identity-H /DescendantFonts [" + std::to_string(cidFont) +
                 " 0 R] /ToUnicode " + std::to_string(toUnicode) + " 0 R >>\n";
        end();

        // One "gid [width]" entry per glyph: the sets are a few hundred glyphs
        // at most, so a compact range encoding would not pay for itself.
        std::string widths;
        for (uint16_t gid : use.glyphs) {
            widths += " " + std::to_string(gid) + " [" + num(font.advance(gid) * 1000.0) + "]";
        }
        begin(cidFont);
        body_ += "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" + name +
                 " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>"
                 " /FontDescriptor " + std::to_string(descriptor) +
                 " 0 R /CIDToGIDMap /Identity /DW 1000 /W [" + widths + " ] >>\n";
        end();

        begin(descriptor);
        body_ += "<< /Type /FontDescriptor /FontName /" + name +
                 " /Flags 32 /FontBBox [" + num(font.xMin() * scale) + " " +
                 num(font.yMin() * scale) + " " + num(font.xMax() * scale) + " " +
                 num(font.yMax() * scale) + "] /ItalicAngle 0 /Ascent " +
                 num(font.ascent() * scale) + " /Descent " + num(font.descent() * scale) +
                 " /CapHeight " + num(font.capHeight() * scale) + " /StemV " +
                 (face == 1 ? "160" : "80") + " /FontFile2 " + std::to_string(fontFile) +
                 " 0 R >>\n";
        end();

        std::vector<uint16_t> glyphList(use.glyphs.begin(), use.glyphs.end());
        std::vector<uint8_t> embedded = font.subset(glyphList);
        std::string data(reinterpret_cast<const char*>(embedded.data()), embedded.size());
        stream(fontFile, "/Length1 " + std::to_string(data.size()), data);

        stream(toUnicode, "", buildToUnicode(use));
    }

    static std::string buildToUnicode(const FaceUse& use) {
        std::string map =
            "/CIDInit /ProcSet findresource begin\n"
            "12 dict begin\nbegincmap\n"
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
            "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
            "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
        std::vector<std::pair<uint16_t, uint32_t>> entries(use.toUnicode.begin(), use.toUnicode.end());
        for (size_t start = 0; start < entries.size(); start += 100) {
            size_t end = std::min(start + 100, entries.size());
            map += std::to_string(end - start) + " beginbfchar\n";
            for (size_t i = start; i < end; ++i) {
                map += '<';
                appendHex16(map, entries[i].first);
                map += "> <";
                uint32_t cp = entries[i].second;
                if (cp >= 0x10000) {
                    cp -= 0x10000;
                    appendHex16(map, static_cast<uint16_t>(0xD800 + (cp >> 10)));
                    appendHex16(map, static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
                } else {
                    appendHex16(map, static_cast<uint16_t>(cp));
                }
                map += ">\n";
            }
            map += "endbfchar\n";
        }
        map += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
        return map;
    }

    void writePage(int index) {
        const Page& page = doc_.pages[static_cast<size_t>(index)];
        const int pageId = firstPageObject_ + index * 2;
        const int contentId = pageId + 1;
        const double height = doc_.height;

        std::string content;
        // Background first, then rules, then text - the same order the preview
        // paints in.
        content += colorOp(page.background.color, "rg");
        content += num(page.background.x) + " " +
                   num(height - page.background.y - page.background.h) + " " +
                   num(page.background.w) + " " + num(page.background.h) + " re f\n";

        RGB stroke{0, 0, 0};
        bool strokeSet = false;
        double lineWidth = -1;
        for (const LineItem& line : page.lines) {
            if (!strokeSet || !(line.color.r == stroke.r && line.color.g == stroke.g &&
                                line.color.b == stroke.b)) {
                stroke = line.color;
                strokeSet = true;
                content += colorOp(stroke, "RG");
            }
            if (line.width != lineWidth) {
                lineWidth = line.width;
                content += num(lineWidth) + " w\n";
            }
            content += num(line.x1) + " " + num(height - line.y1) + " m " + num(line.x2) + " " +
                       num(height - line.y2) + " l S\n";
        }

        if (!page.texts.empty()) {
            content += "BT\n";
            RGB fill{0, 0, 0};
            bool fillSet = false;
            int currentFace = -1;
            double currentSize = -1;
            std::vector<uint16_t> glyphs;
            for (const TextItem& item : page.texts) {
                int face = item.bold ? 1 : 0;
                if (face != currentFace || item.size != currentSize) {
                    currentFace = face;
                    currentSize = item.size;
                    content += std::string("/F") + (face == 1 ? "2" : "1") + " " + num(item.size) +
                               " Tf\n";
                }
                if (!fillSet || !(item.color.r == fill.r && item.color.g == fill.g &&
                                  item.color.b == fill.b)) {
                    fill = item.color;
                    fillSet = true;
                    content += colorOp(fill, "rg");
                }
                fonts_.face(item.bold).shape(item.text, glyphs, nullptr);
                content += "1 0 0 1 " + num(item.x) + " " + num(height - item.y) + " Tm <";
                for (uint16_t gid : glyphs) appendHex16(content, gid);
                content += "> Tj\n";
            }
            content += "ET\n";
        }

        begin(pageId);
        body_ += "<< /Type /Page /Parent " + std::to_string(pagesNode_) +
                 " 0 R /MediaBox [0 0 " + num(doc_.width) + " " + num(doc_.height) +
                 "] /Resources << /Font <<";
        for (int face = 0; face < 2; ++face)
            if (use_[face].used)
                body_ += std::string(" /F") + (face == 1 ? "2" : "1") + " " +
                         std::to_string(fontObject_[face]) + " 0 R";
        body_ += " >> >> /Contents " + std::to_string(contentId) + " 0 R >>\n";
        end();

        stream(contentId, "", content);
    }

    void writeTrailer(int objectCount) {
        size_t xref = body_.size();
        body_ += "xref\n0 " + std::to_string(objectCount) + "\n";
        body_ += "0000000000 65535 f \n";
        char buf[32];
        for (int i = 1; i < objectCount; ++i) {
            std::snprintf(buf, sizeof buf, "%010llu 00000 n \n",
                          static_cast<unsigned long long>(offsets_[static_cast<size_t>(i)]));
            body_ += buf;
        }
        body_ += "trailer\n<< /Size " + std::to_string(objectCount) + " /Root " +
                 std::to_string(catalog_) + " 0 R /Info " + std::to_string(info_) + " 0 R >>\n";
        body_ += "startxref\n" + std::to_string(xref) + "\n%%EOF\n";
    }

    const Document& doc_;
    const FontSet& fonts_;
    FaceUse use_[2];
    std::string body_;
    std::vector<size_t> offsets_;
    int catalog_ = 0, pagesNode_ = 0, info_ = 0;
    int firstFontObject_ = 0, firstPageObject_ = 0;
    int fontObject_[2] = {0, 0};
};

}  // namespace

std::vector<uint8_t> buildPdf(const Document& doc, const FontSet& fonts, std::string& error) {
    std::vector<uint8_t> out;
    Writer(doc, fonts).run(out, error);
    return out;
}

bool writePdf(const Document& doc, const FontSet& fonts, const std::wstring& path,
              std::string& error) {
    std::vector<uint8_t> data = buildPdf(doc, fonts, error);
    if (data.empty()) return false;
    FILE* fh = _wfopen(path.c_str(), L"wb");
    if (!fh) { error = "не удалось создать файл"; return false; }
    bool ok = std::fwrite(data.data(), 1, data.size(), fh) == data.size();
    if (std::fclose(fh) != 0) ok = false;
    if (!ok) error = "ошибка записи файла";
    return ok;
}

}  // namespace cvb
