// The printer backend: the same list of primitives the preview draws and the
// PDF writer writes, put on a device context instead.
//
// Nothing here knows what a CV is. It receives a laid-out Document in points
// and paints it, which is why a printed page cannot disagree with what is on
// screen - the disagreement would have to come from the layout engine, and
// there is only one of those.
#include <windows.h>

#include <commdlg.h>

#include <cmath>
#include <map>

#include "ui.h"

namespace cvb {
namespace {

// A GDI font per (size, weight) actually used on the page. A CV has a handful
// of distinct sizes, so this keeps font creation to single figures instead of
// one per text run.
class FontCache {
public:
    FontCache(const std::wstring& family, double scale) : family_(family), scale_(scale) {}

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

void drawPage(HDC dc, const Page& sheet, double scale, int offsetX, int offsetY,
              FontCache& fonts) {
    // Points to device units, shifted by the printer's unprintable margin: the
    // context's origin is the corner of the printable area, but the layout
    // measures from the corner of the sheet.
    auto X = [&](double x) { return static_cast<int>(std::lround(x * scale)) - offsetX; };
    auto Y = [&](double y) { return static_cast<int>(std::lround(y * scale)) - offsetY; };

    // The template's background is part of the design, so it is printed like
    // everything else. On a dark template that is a lot of toner - a fact
    // worth knowing before pressing Print, not a reason for this backend to
    // quietly render something other than the document.
    RECT page{X(0), Y(0), X(kPageWidth), Y(kPageHeight)};
    HBRUSH back = CreateSolidBrush(toGdi(sheet.background.color));
    FillRect(dc, &page, back);
    DeleteObject(back);

    for (const LineItem& line : sheet.lines) {
        const int width = std::max(1, static_cast<int>(std::lround(line.width * scale)));
        HPEN pen = CreatePen(PS_SOLID, width, toGdi(line.color));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, X(line.x1), Y(line.y1), nullptr);
        LineTo(dc, X(line.x2), Y(line.y2));
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    // Every run is positioned absolutely by the layout engine, so GDI only has
    // to place one string at a time and small differences in its own advance
    // calculation cannot accumulate across the page.
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    for (const TextItem& item : sheet.texts) {
        const std::wstring text = widen(item.text);
        if (text.empty()) continue;
        HGDIOBJ oldFont = SelectObject(dc, fonts.get(item.size, item.bold));
        SetTextColor(dc, toGdi(item.color));
        TextOutW(dc, X(item.x), Y(item.y), text.c_str(), static_cast<int>(text.size()));
        SelectObject(dc, oldFont);
    }
}

}  // namespace

PrintResult printDocument(HWND owner, const Document& doc, const FontSet& fonts,
                          const std::wstring& title, std::wstring& error) {
    if (doc.pages.empty()) {
        error = L"Нечего печатать.";
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

    const double scale = GetDeviceCaps(dc, LOGPIXELSX) / 72.0;
    const int offsetX = GetDeviceCaps(dc, PHYSICALOFFSETX);
    const int offsetY = GetDeviceCaps(dc, PHYSICALOFFSETY);
    FontCache cache(fonts.family(), GetDeviceCaps(dc, LOGPIXELSY) / 72.0);

    DOCINFOW info{};
    info.cbSize = sizeof info;
    info.lpszDocName = title.c_str();
    if (StartDocW(dc, &info) <= 0) {
        error = L"Не удалось начать печать.";
        DeleteDC(dc);
        return PrintResult::Failed;
    }

    bool ok = true;
    for (size_t i = first; i <= last && ok; ++i) {
        if (StartPage(dc) <= 0) {
            ok = false;
            break;
        }
        drawPage(dc, doc.pages[i], scale, offsetX, offsetY, cache);
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
