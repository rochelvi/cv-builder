// The printer backend: the same list of primitives the preview draws and the
// PDF writer writes, put on a device context instead.
//
// Nothing here knows what a CV is. It receives a laid-out Document in points and
// paints it through the shared painter, which is why a printed page cannot
// disagree with what is on screen - the disagreement would have to come from the
// layout engine, and there is only one of those.
//
// Glyph ids, not strings. Asking GDI to set a string means asking it to choose
// glyphs and advances, and it would be entitled to choose differently from the
// font this document was measured with - or, if the face is not installed at all,
// to substitute another one silently. So the bundled font file is added to the
// process privately and the run is drawn with ETO_GLYPH_INDEX, the same glyph ids
// that go into the PDF.
#include <windows.h>

#include <commdlg.h>

#include <cmath>
#include <map>
#include <string>

#include "canvas.h"
#include "document_painter.h"
#include "ui.h"

namespace cvb {
namespace {

// Makes a font file usable by this process without installing it, and takes it
// away again afterwards. Needed because the face the document is measured with
// is the one shipped beside the program, which the system knows nothing about.
class PrivateFont {
public:
    explicit PrivateFont(const Path& path) {
        if (path.empty()) return;
        // FR_PRIVATE: visible to this process only, and gone when it exits even
        // if the removal below is missed.
        if (AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr) > 0) path_ = path;
    }
    ~PrivateFont() {
        if (!path_.empty()) RemoveFontResourceExW(path_.c_str(), FR_PRIVATE, nullptr);
    }
    PrivateFont(const PrivateFont&) = delete;
    PrivateFont& operator=(const PrivateFont&) = delete;

private:
    Path path_;
};

// A GDI font per (size, weight) actually used on the page. A CV has a handful of
// distinct sizes, so this keeps font creation to single figures instead of one
// per text run.
class FontCache {
public:
    FontCache(std::wstring family, double scale) : family_(std::move(family)), scale_(scale) {}

    ~FontCache() {
        for (auto& entry : fonts_) DeleteObject(entry.second);
    }

    HFONT get(double points, bool bold) {
        // Keyed on the rounded device height: two point sizes that land on the
        // same pixel height are the same font.
        const int height = static_cast<int>(std::lround(points * scale_));
        const Key key{height, bold};
        auto found = fonts_.find(key);
        if (found != fonts_.end()) return found->second;

        LOGFONTW lf{};
        // Negative height asks for the em size rather than the cell height,
        // which is what a typographic point size means.
        lf.lfHeight = -height;
        lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfOutPrecision = OUT_TT_PRECIS;  // the TrueType face, not a bitmap substitute
        lf.lfQuality = PROOF_QUALITY;
        wcsncpy_s(lf.lfFaceName, family_.c_str(), _TRUNCATE);
        HFONT font = CreateFontIndirectW(&lf);
        fonts_.emplace(key, font);
        return font;
    }

private:
    struct Key {
        int height;
        bool bold;
        bool operator<(const Key& other) const {
            return height != other.height ? height < other.height : bold < other.bold;
        }
    };

    std::wstring family_;
    double scale_;
    std::map<Key, HFONT> fonts_;
};

COLORREF toGdi(const RGB& c) { return RGB(c.r, c.g, c.b); }

// GDI as a Canvas. Points to device units, shifted by the printer's unprintable
// margin: the context's origin is the corner of the printable area, but the
// layout measures from the corner of the sheet.
class GdiCanvas : public Canvas {
public:
    GdiCanvas(HDC dc, double scale, int offsetX, int offsetY, FontCache& fonts)
        : dc_(dc), scale_(scale), offsetX_(offsetX), offsetY_(offsetY), fonts_(fonts) {
        SetBkMode(dc_, TRANSPARENT);
        SetTextAlign(dc_, TA_LEFT | TA_BASELINE);
    }

    void fillRect(double x, double y, double width, double height, RGB color) override {
        RECT box{X(x), Y(y), X(x + width), Y(y + height)};
        HBRUSH brush = CreateSolidBrush(toGdi(color));
        FillRect(dc_, &box, brush);
        DeleteObject(brush);
    }

    void drawLine(double x1, double y1, double x2, double y2, double width, RGB color) override {
        const int pixels = std::max(1, static_cast<int>(std::lround(width * scale_)));
        HPEN pen = CreatePen(PS_SOLID, pixels, toGdi(color));
        HGDIOBJ oldPen = SelectObject(dc_, pen);
        MoveToEx(dc_, X(x1), Y(y1), nullptr);
        LineTo(dc_, X(x2), Y(y2));
        SelectObject(dc_, oldPen);
        DeleteObject(pen);
    }

    void drawGlyphs(double x, double y, double size, bool bold, const GlyphRun& run,
                    RGB color) override {
        if (run.count == 0) return;
        HGDIOBJ oldFont = SelectObject(dc_, fonts_.get(size, bold));
        SetTextColor(dc_, toGdi(color));
        // A glyph id is 16 bits and so is a WCHAR; with ETO_GLYPH_INDEX the array
        // is read as ids rather than characters. No advances are passed: the font
        // selected here is the file the run was shaped from, so its own advances
        // are the right ones and are carried at device precision rather than
        // rounded to whole dots per glyph.
        ExtTextOutW(dc_, X(x), Y(y), ETO_GLYPH_INDEX, nullptr,
                    reinterpret_cast<const wchar_t*>(run.glyphs),
                    static_cast<UINT>(run.count), nullptr);
        SelectObject(dc_, oldFont);
    }

private:
    int X(double x) const { return static_cast<int>(std::lround(x * scale_)) - offsetX_; }
    int Y(double y) const { return static_cast<int>(std::lround(y * scale_)) - offsetY_; }

    HDC dc_;
    double scale_;
    int offsetX_;
    int offsetY_;
    FontCache& fonts_;
};

}  // namespace

PrintResult printDocument(HWND owner, const Document& doc, const FontSet& fonts,
                          const std::wstring& title, std::wstring& error) {
    if (doc.pages.empty()) {
        error = L"Нечего печатать.";
        return PrintResult::Failed;
    }
    if (!fonts.valid()) {
        error = L"Шрифты не загружены.";
        return PrintResult::Failed;
    }

    PRINTDLGW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = owner;
    dialog.Flags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    dialog.nFromPage = 1;
    dialog.nToPage = static_cast<WORD>(doc.pages.size());
    dialog.nMinPage = 1;
    dialog.nMaxPage = static_cast<WORD>(doc.pages.size());
    dialog.nCopies = 1;

    if (!PrintDlgW(&dialog)) {
        // A false return means either "the user pressed Cancel" or "the dialog
        // could not open", and only CommDlgExtendedError tells them apart.
        // Reporting both as a cancel would hide a broken printer setup behind
        // a button the user never pressed.
        const DWORD why = CommDlgExtendedError();
        if (why == 0) return PrintResult::Cancelled;
        error = L"Не удалось открыть диалог печати (код " + std::to_wstring(why) + L").";
        return PrintResult::Failed;
    }
    if (!dialog.hDC) {
        error = L"Принтер не вернул контекст устройства.";
        return PrintResult::Failed;
    }

    HDC dc = dialog.hDC;
    // Both hDevMode and hDevNames are ours to free once the dialog returns.
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);

    size_t first = 0, last = doc.pages.size() - 1;
    if (dialog.Flags & PD_PAGENUMS) {
        first = static_cast<size_t>(dialog.nFromPage) - 1;
        last = std::min<size_t>(static_cast<size_t>(dialog.nToPage) - 1, doc.pages.size() - 1);
    }

    // The faces this document was measured with, made available to GDI under
    // their own family name for as long as the job lasts. Both files are
    // registered: regular and bold are separate files, and asking for FW_BOLD of
    // a family that only has the regular installed gets a synthesised, wider
    // bold that would not match the PDF.
    PrivateFont regular(fonts.regular().path());
    PrivateFont bold(fonts.bold().path());

    const double scale = GetDeviceCaps(dc, LOGPIXELSX) / 72.0;
    const int offsetX = GetDeviceCaps(dc, PHYSICALOFFSETX);
    const int offsetY = GetDeviceCaps(dc, PHYSICALOFFSETY);
    FontCache cache(widen(fonts.family()), GetDeviceCaps(dc, LOGPIXELSY) / 72.0);

    DOCINFOW info{};
    info.cbSize = sizeof info;
    info.lpszDocName = title.c_str();
    if (StartDocW(dc, &info) <= 0) {
        error = L"Не удалось начать печать.";
        DeleteDC(dc);
        return PrintResult::Failed;
    }

    DocumentPainter painter(fonts);
    bool ok = true;
    for (size_t i = first; i <= last && ok; ++i) {
        if (StartPage(dc) <= 0) {
            ok = false;
            break;
        }
        GdiCanvas canvas(dc, scale, offsetX, offsetY, cache);
        painter.paint(doc.pages[i], canvas);
        if (EndPage(dc) <= 0) ok = false;
    }

    if (ok) {
        EndDoc(dc);
    } else {
        // Leaves nothing half-printed queued behind us.
        AbortDoc(dc);
        error = L"Печать прервана устройством.";
    }
    DeleteDC(dc);
    return ok ? PrintResult::Printed : PrintResult::Failed;
}

}  // namespace cvb
