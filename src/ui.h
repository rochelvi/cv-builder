// Shared pieces of the Win32 front end.
//
// The window tree is deliberately flat: one form pane on the left holding all
// the editing controls, one preview pane on the right that paints the laid-out
// document with Direct2D. Cards and section frames are painted by the pane
// itself rather than being windows of their own, which keeps the handle count
// down and scrolling smooth.
#pragma once

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "font.h"
#include "layout.h"
#include "model.h"

namespace cvb {

// ------------------------------------------------------------- conversions
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& text);

// ------------------------------------------------------------------ colours
struct UiTheme {
    COLORREF window = RGB(0xF3, 0xF4, 0xF6);   // behind everything
    COLORREF pane = RGB(0xFF, 0xFF, 0xFF);     // the form surface
    COLORREF card = RGB(0xF8, 0xF9, 0xFB);     // a job / skill group card
    COLORREF cardEdge = RGB(0xDF, 0xE3, 0xE8);
    COLORREF text = RGB(0x1B, 0x1F, 0x25);
    COLORREF subtext = RGB(0x60, 0x6A, 0x78);
    COLORREF accent = RGB(0x1E, 0x6F, 0xD9);
    COLORREF previewBack = RGB(0x5A, 0x5F, 0x66);  // the desk the page sits on
};
const UiTheme& ui();

// Every hard-coded size in the UI goes through here so the app scales with the
// monitor rather than assuming 96 dpi.
int scaled(int value, UINT dpi);

// --------------------------------------------------------------- form pane
// Services the editors need from the pane that hosts them.
class FormHost {
public:
    virtual ~FormHost() = default;
    virtual HWND content() const = 0;
    virtual HFONT font() const = 0;
    virtual HFONT boldFont() const = 0;
    virtual int scale(int value) const = 0;
    // Registers a click handler for a button; the handler may add or remove
    // controls, so it runs outside the message that triggered it.
    virtual void onClick(HWND button, std::function<void()> action) = 0;
    virtual void forget(HWND control) = 0;
    virtual void changed() = 0;      // content edited: refresh the preview
    virtual void restructured() = 0; // controls added or removed: lay out again
};

// A vertical list of text fields with add / move / remove controls.
class ListEditor {
public:
    void create(FormHost& host, const wchar_t* placeholder, bool multiline = false);
    void setValues(const std::vector<std::string>& values);
    std::vector<std::string> values() const;  // blank entries are dropped
    int layout(int x, int y, int width);      // returns the height used
    void destroy();

private:
    void addRow(const std::string& text);
    void removeRow(size_t index);
    void moveRow(size_t index, int delta);

    struct Row {
        HWND edit = nullptr;
        HWND up = nullptr;
        HWND down = nullptr;
        HWND del = nullptr;
    };
    FormHost* host_ = nullptr;
    const wchar_t* placeholder_ = L"";
    bool multiline_ = false;
    std::vector<Row> rows_;
    HWND add_ = nullptr;
};

// One card in a CardList. Subclasses own their controls and know how to read
// themselves back into the model.
class Card {
public:
    virtual ~Card() = default;
    virtual void build(FormHost& host) = 0;
    virtual int layoutBody(int x, int y, int width) = 0;
    virtual void destroyBody() = 0;

    // Filled in by the CardList.
    HWND up = nullptr;
    HWND down = nullptr;
    HWND del = nullptr;
    RECT frame{};  // painted by the pane
};

class CardList {
public:
    void create(FormHost& host, const wchar_t* addLabel,
                std::function<std::unique_ptr<Card>()> factory);
    void clear();
    Card& append();                                  // a default card from the factory
    Card& appendCard(std::unique_ptr<Card> card);    // an existing one, e.g. when loading
    int layout(int x, int y, int width);
    void destroy();

    const std::vector<std::unique_ptr<Card>>& cards() const { return cards_; }
    void collectFrames(std::vector<RECT>& out) const;

private:
    void remove(size_t index);
    void move(size_t index, int delta);

    FormHost* host_ = nullptr;
    std::function<std::unique_ptr<Card>()> factory_;
    std::vector<std::unique_ptr<Card>> cards_;
    HWND add_ = nullptr;
};

// The editing pane: owns every control, hands back a CV on demand.
class FormPane {
public:
    bool create(HWND parent, HINSTANCE instance, std::function<void()> onChange);
    HWND hwnd() const { return hwnd_; }

    void setCV(const CV& cv);
    CV collect() const;
    void setDpi(UINT dpi);

private:
    friend struct FormImpl;
    HWND hwnd_ = nullptr;
    std::shared_ptr<struct FormImpl> impl_;
};

// ------------------------------------------------------------ preview pane
class PreviewPane {
public:
    bool create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

    void setFonts(const FontSet* fonts);
    void setDocument(Document doc);
    void setError(const std::wstring& message);

    int pageCount() const;
    int page() const;
    void setPage(int index);
    void setZoom(int percent);
    int zoom() const;

    // Called by the owner when the page or zoom changes.
    std::function<void()> onStateChanged;

private:
    HWND hwnd_ = nullptr;
    std::shared_ptr<struct PreviewImpl> impl_;
};

}  // namespace cvb
