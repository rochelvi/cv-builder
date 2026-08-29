// The live preview: paints the laid-out document with Direct2D.
//
// It draws the same glyph ids, at the same advances, on the same coordinates
// the PDF writer uses - the font faces come from the very files the layout
// engine measured with. So this is not an approximation of the export; it is
// the export, rasterised.
// windows.h first: the other SDK headers assume its basic types exist.
#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>

#include <algorithm>

#include "ui.h"

namespace cvb {
namespace {

constexpr int kPageGap = 16;   // margin around the sheet, in pixels at 96 dpi
constexpr int kWheelStep = 60;

template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

D2D1_COLOR_F toD2D(RGB c) {
    return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f);
}

D2D1_COLOR_F toD2D(COLORREF c) {
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, 1.0f);
}

}  // namespace

struct PreviewImpl {
    HWND hwnd = nullptr;
    ID2D1Factory* factory = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* message = nullptr;
    IDWriteFontFace* faces[2] = {nullptr, nullptr};

    const FontSet* fonts = nullptr;
    Document doc;
    std::wstring error;
    int page = 0;
    int zoom = 100;
    int scrollX = 0, scrollY = 0;
    UINT dpi = 96;

    ~PreviewImpl() {
        release(brush);
        release(target);
        release(message);
        release(faces[0]);
        release(faces[1]);
        release(dwrite);
        release(factory);
    }

    double scale() const { return zoom / 100.0 * dpi / 96.0; }
    int pageWidthPx() const { return static_cast<int>(kPageWidth * scale() + 0.5); }
    int pageHeightPx() const { return static_cast<int>(kPageHeight * scale() + 0.5); }

    bool ensureFaces();
    bool ensureTarget();
    void render();
    void drawPage(const Page& sheet, D2D1_POINT_2F origin);
    void updateScrollBars();
    void clampScroll();
    void scrollBy(int dx, int dy);
};

bool PreviewImpl::ensureFaces() {
    if (!fonts || !dwrite) return false;
    if (faces[0] && faces[1]) return true;
    for (int i = 0; i < 2; ++i) {
        release(faces[i]);
        const Font& font = fonts->face(i == 1);
        IDWriteFontFile* file = nullptr;
        if (FAILED(dwrite->CreateFontFileReference(font.path().c_str(), nullptr, &file)))
            return false;
        // The face index matters for a collection, where one file holds several
        // faces; for a plain .ttf it is 0 and this is the call it always was.
        HRESULT hr = dwrite->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file,
                                            static_cast<UINT32>(font.faceIndex()),
                                            DWRITE_FONT_SIMULATIONS_NONE, &faces[i]);
        release(file);
        if (FAILED(hr)) return false;
    }
    return true;
}

bool PreviewImpl::ensureTarget() {
    if (target) return true;
    if (!factory) return false;
    RECT client{};
    GetClientRect(hwnd, &client);
    D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(client.right, 1)),
                                   static_cast<UINT32>(std::max<LONG>(client.bottom, 1)));
    // The render target works in pixels: the app already scales everything for
    // dpi itself, so a 96 dpi target keeps one D2D unit equal to one pixel.
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    if (FAILED(factory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, size),
                                               &target)))
        return false;
    target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush);
    return brush != nullptr;
}

void PreviewImpl::drawPage(const Page& sheet, D2D1_POINT_2F origin) {
    const float s = static_cast<float>(scale());
    target->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                         D2D1::Matrix3x2F::Translation(origin.x, origin.y));

    brush->SetColor(toD2D(sheet.background.color));
    target->FillRectangle(
        D2D1::RectF(0, 0, static_cast<float>(kPageWidth), static_cast<float>(kPageHeight)), brush);

    for (const LineItem& line : sheet.lines) {
        brush->SetColor(toD2D(line.color));
        target->DrawLine(D2D1::Point2F(static_cast<float>(line.x1), static_cast<float>(line.y1)),
                         D2D1::Point2F(static_cast<float>(line.x2), static_cast<float>(line.y2)),
                         brush, static_cast<float>(line.width));
    }

    std::vector<uint16_t> glyphs;
    std::vector<FLOAT> advances;
    for (const TextItem& item : sheet.texts) {
        const Font& font = fonts->face(item.bold);
        font.shape(item.text, glyphs, nullptr);
        if (glyphs.empty()) continue;
        advances.resize(glyphs.size());
        for (size_t i = 0; i < glyphs.size(); ++i)
            advances[i] = static_cast<FLOAT>(font.advance(glyphs[i]) * item.size);

        DWRITE_GLYPH_RUN run{};
        run.fontFace = faces[item.bold ? 1 : 0];
        run.fontEmSize = static_cast<FLOAT>(item.size);
        run.glyphCount = static_cast<UINT32>(glyphs.size());
        run.glyphIndices = glyphs.data();
        run.glyphAdvances = advances.data();
        brush->SetColor(toD2D(item.color));
        target->DrawGlyphRun(
            D2D1::Point2F(static_cast<float>(item.x), static_cast<float>(item.y)), &run, brush,
            DWRITE_MEASURING_MODE_NATURAL);
    }
    target->SetTransform(D2D1::Matrix3x2F::Identity());
}

void PreviewImpl::render() {
    if (!ensureTarget()) return;
    RECT client{};
    GetClientRect(hwnd, &client);

    target->BeginDraw();
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    target->Clear(toD2D(ui().previewBack));

    if (!error.empty() || doc.pages.empty() || !ensureFaces()) {
        if (message) {
            brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
            std::wstring text = error.empty() ? L"Нет страниц" : error;
            target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), message,
                              D2D1::RectF(20.0f, 20.0f, static_cast<float>(client.right) - 20.0f,
                                          static_cast<float>(client.bottom) - 20.0f),
                              brush);
        }
    } else {
        int index = std::max(0, std::min(page, static_cast<int>(doc.pages.size()) - 1));
        const int width = pageWidthPx();
        const int height = pageHeightPx();
        const int margin = scaled(kPageGap, dpi);
        float x = static_cast<float>(client.right - width) / 2.0f;
        if (width + 2 * margin > client.right) x = static_cast<float>(margin - scrollX);
        float y = static_cast<float>(margin - scrollY);

        // A soft edge so the sheet reads as paper on a desk rather than a
        // rectangle that happens to be a different colour.
        brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.25f));
        target->FillRectangle(D2D1::RectF(x + 2, y + 3, x + width + 3, y + height + 4), brush);
        drawPage(doc.pages[static_cast<size_t>(index)], D2D1::Point2F(x, y));
    }

    HRESULT hr = target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        release(brush);
        release(target);
    }
}

void PreviewImpl::clampScroll() {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int margin = scaled(kPageGap, dpi);
    int limitY = std::max(0, pageHeightPx() + 2 * margin - static_cast<int>(client.bottom));
    int limitX = std::max(0, pageWidthPx() + 2 * margin - static_cast<int>(client.right));
    scrollY = std::max(0, std::min(limitY, scrollY));
    scrollX = std::max(0, std::min(limitX, scrollX));
}

void PreviewImpl::updateScrollBars() {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int margin = scaled(kPageGap, dpi);
    SCROLLINFO info{};
    info.cbSize = sizeof info;
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, pageHeightPx() + 2 * margin - 1);
    info.nPage = static_cast<UINT>(client.bottom);
    info.nPos = scrollY;
    SetScrollInfo(hwnd, SB_VERT, &info, TRUE);

    info.nMax = std::max(0, pageWidthPx() + 2 * margin - 1);
    info.nPage = static_cast<UINT>(client.right);
    info.nPos = scrollX;
    SetScrollInfo(hwnd, SB_HORZ, &info, TRUE);
}

void PreviewImpl::scrollBy(int dx, int dy) {
    scrollX += dx;
    scrollY += dy;
    clampScroll();
    updateScrollBars();
    InvalidateRect(hwnd, nullptr, FALSE);
}

namespace {

PreviewImpl* implOf(HWND window) {
    return reinterpret_cast<PreviewImpl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

LRESULT CALLBACK previewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    PreviewImpl* impl = implOf(window);
    switch (message) {
        case WM_PAINT: {
            if (impl) impl->render();
            ValidateRect(window, nullptr);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (impl) {
                if (impl->target)
                    impl->target->Resize(D2D1::SizeU(static_cast<UINT32>(LOWORD(lParam)),
                                                     static_cast<UINT32>(HIWORD(lParam))));
                impl->clampScroll();
                impl->updateScrollBars();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_VSCROLL:
        case WM_HSCROLL: {
            if (!impl) break;
            int bar = (message == WM_VSCROLL) ? SB_VERT : SB_HORZ;
            SCROLLINFO info{};
            info.cbSize = sizeof info;
            info.fMask = SIF_ALL;
            GetScrollInfo(window, bar, &info);
            int position = info.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: position -= scaled(30, impl->dpi); break;
                case SB_LINEDOWN: position += scaled(30, impl->dpi); break;
                case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
                case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: position = info.nTrackPos; break;
                default: break;
            }
            if (bar == SB_VERT)
                impl->scrollBy(0, position - impl->scrollY);
            else
                impl->scrollBy(position - impl->scrollX, 0);
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (impl) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                impl->scrollBy(0, -delta * scaled(kWheelStep, impl->dpi) / WHEEL_DELTA);
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void registerPreviewClass(HINSTANCE instance) {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof cls;
    cls.lpfnWndProc = previewProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = L"CVBPreview";
    RegisterClassExW(&cls);
}

}  // namespace

// ------------------------------------------------------------- PreviewPane

bool PreviewPane::create(HWND parent, HINSTANCE instance) {
    registerPreviewClass(instance);
    impl_ = std::make_shared<PreviewImpl>();
    PreviewImpl& impl = *impl_;

    // __uuidof rather than the IID_ symbols: mingw's import libraries do not
    // carry the Direct2D/DirectWrite GUIDs, but the headers declare them.
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                 reinterpret_cast<void**>(&impl.factory))))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&impl.dwrite))))
        return false;
    impl.dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f,
                                  L"ru-RU", &impl.message);

    hwnd_ = CreateWindowExW(0, L"CVBPreview", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL, 0, 0, 10, 10, parent,
                            nullptr, instance, nullptr);
    if (!hwnd_) return false;
    impl.hwnd = hwnd_;
    impl.dpi = GetDpiForWindow(parent);
    if (!impl.dpi) impl.dpi = 96;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&impl));
    return true;
}

void PreviewPane::setFonts(const FontSet* fonts) {
    impl_->fonts = fonts;
    release(impl_->faces[0]);
    release(impl_->faces[1]);
}

void PreviewPane::setDocument(Document doc) {
    impl_->doc = std::move(doc);
    impl_->error.clear();
    if (impl_->page >= static_cast<int>(impl_->doc.pages.size()))
        impl_->page = std::max(0, static_cast<int>(impl_->doc.pages.size()) - 1);
    impl_->clampScroll();
    impl_->updateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (onStateChanged) onStateChanged();
}

void PreviewPane::setError(const std::wstring& text) {
    impl_->error = text;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int PreviewPane::pageCount() const {
    return std::max(1, static_cast<int>(impl_->doc.pages.size()));
}

int PreviewPane::page() const { return impl_->page; }

void PreviewPane::setPage(int index) {
    int limit = static_cast<int>(impl_->doc.pages.size()) - 1;
    impl_->page = std::max(0, std::min(limit < 0 ? 0 : limit, index));
    impl_->scrollY = 0;
    impl_->clampScroll();
    impl_->updateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (onStateChanged) onStateChanged();
}

void PreviewPane::setZoom(int percent) {
    impl_->zoom = std::max(50, std::min(300, percent));
    impl_->clampScroll();
    impl_->updateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (onStateChanged) onStateChanged();
}

int PreviewPane::zoom() const { return impl_->zoom; }

void PreviewPane::applyTheme() {
    // The desk colour is read straight from ui() while rendering, so a repaint
    // is all it takes.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

}  // namespace cvb
