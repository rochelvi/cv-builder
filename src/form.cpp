// The editing pane: every control the user types into, plus the scrolling and
// painting that holds them together.
//
// Cards are not windows. A card is a rectangle the pane paints, with ordinary
// child controls positioned inside it, which keeps the handle count reasonable
// and means reordering a card is a swap in a vector plus a relayout.
// windows.h first: the other SDK headers assume its basic types exist.
#include <windows.h>

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <unordered_map>

#include "ui.h"

namespace cvb {

// ------------------------------------------------------------- conversions

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), n,
                        nullptr, nullptr);
    return out;
}

const UiTheme& ui() {
    static const UiTheme theme;
    return theme;
}

int scaled(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

namespace {

// Sizes at 96 dpi; everything goes through FormHost::scale before use.
constexpr int kMargin = 14;
constexpr int kGap = 6;
constexpr int kRowHeight = 26;
constexpr int kButtonSize = 26;
constexpr int kCardPad = 10;
constexpr int kHeadingHeight = 22;
constexpr int kSectionGap = 14;
constexpr int kSummaryHeight = 96;
constexpr int kAddButtonWidth = 170;
constexpr int kSwatchWidth = 84;
constexpr int kWheelStep = 60;

std::wstring textOf(HWND control) {
    int length = GetWindowTextLengthW(control);
    if (length <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(length), L'\0');
    GetWindowTextW(control, out.data(), length + 1);
    return out;
}

// Edit controls keep hard line breaks as CRLF; the model uses plain LF.
std::string readText(HWND control) {
    std::wstring text = textOf(control);
    std::wstring flat;
    flat.reserve(text.size());
    for (wchar_t c : text)
        if (c != L'\r') flat += c;
    return narrow(flat);
}

void writeText(HWND control, const std::string& value) {
    std::wstring text = widen(value);
    std::wstring crlf;
    crlf.reserve(text.size());
    for (wchar_t c : text) {
        if (c == L'\n') crlf += L'\r';
        crlf += c;
    }
    SetWindowTextW(control, crlf.c_str());
}

std::string trimmed(const std::string& s) {
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t a = 0, b = s.size();
    while (a < b && space(s[a])) ++a;
    while (b > a && space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

void place(HWND control, int x, int y, int width, int height) {
    if (control) SetWindowPos(control, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

std::string hexOf(COLORREF color) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", GetRValue(color), GetGValue(color),
                  GetBValue(color));
    return buf;
}

COLORREF colorRef(const std::string& hex) {
    RGB rgb = parseColor(hex, RGB{0, 0, 0});
    return RGB(rgb.r, rgb.g, rgb.b);
}

}  // namespace

// ---------------------------------------------------------------- FormImpl

struct FormImpl : FormHost {
    HWND pane = nullptr;
    HWND body = nullptr;  // the scrolling child that owns every control
    HFONT regular = nullptr;
    HFONT strong = nullptr;
    HBRUSH paneBrush = nullptr;
    HBRUSH cardBrush = nullptr;
    HBRUSH fieldBrush = nullptr;
    UINT dpi = 96;
    int scrollY = 0;
    int contentHeight = 0;
    bool loading = false;
    std::function<void()> notify;
    std::unordered_map<HWND, std::function<void()>> clicks;

    enum Heading {
        H_Colors, H_Header, H_Summary, H_Experience, H_Volunteer,
        H_Skills, H_Soft, H_Education, H_Lab, H_Count
    };
    HWND headings[H_Count] = {};
    HWND skillsHint = nullptr;
    HWND presetLabel = nullptr;

    HWND name = nullptr, role = nullptr, email = nullptr, location = nullptr, website = nullptr;
    HWND summary = nullptr, volunteerTitle = nullptr, labTitle = nullptr;

    HWND preset = nullptr;
    HWND swatch[TR_Count] = {};
    HWND swatchLabel[TR_Count] = {};
    std::string colors[TR_Count];

    ListEditor softSkills, labBullets;
    CardList jobs, volunteering, skillGroups, education;
    std::vector<RECT> frames;

    // ---- FormHost ----
    HWND content() const override { return body; }
    HFONT font() const override { return regular; }
    HFONT boldFont() const override { return strong; }
    int scale(int value) const override { return scaled(value, dpi); }
    void onClick(HWND button, std::function<void()> action) override {
        clicks[button] = std::move(action);
    }
    void forget(HWND control) override { clicks.erase(control); }
    void changed() override {
        if (!loading && notify) notify();
    }
    void restructured() override {
        relayout();
        InvalidateRect(body, nullptr, TRUE);
        changed();
    }

    void relayout();
    void paint();
    // Labels are transparent-looking but painted opaque: a static that never
    // erases leaves its old glyphs behind whenever it moves or its text
    // changes. Tells which surface a control sits on so it erases to match.
    bool onCard(HWND control) const;
    void refresh();  // repaint the body and every control in it
    void updateScrollBar();
    void scrollTo(int value);
    void applyPreset(int index);
    void pickColor(int role);
    void syncPresetSelection();
};

// -------------------------------------------------------------- control shop

namespace {

HWND makeEdit(FormHost& host, const wchar_t* cue, bool multiline = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER;
    style |= multiline ? (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN)
                       : ES_AUTOHSCROLL;
    HWND edit = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10, host.content(), nullptr,
                                nullptr, nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(host.font()), TRUE);
    if (cue && *cue) SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
    return edit;
}

HWND makeButton(FormHost& host, const wchar_t* label, DWORD extra = 0) {
    HWND button = CreateWindowExW(0, L"BUTTON", label,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extra, 0, 0,
                                  10, 10, host.content(), nullptr, nullptr, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(host.font()), TRUE);
    return button;
}

HWND makeCheck(FormHost& host, const wchar_t* label) {
    HWND box = CreateWindowExW(0, L"BUTTON", label,
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 10, 10,
                               host.content(), nullptr, nullptr, nullptr);
    SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(host.font()), TRUE);
    return box;
}

HWND makeLabel(FormHost& host, const wchar_t* text, bool strong) {
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
                                 host.content(), nullptr, nullptr, nullptr);
    SendMessageW(label, WM_SETFONT,
                 reinterpret_cast<WPARAM>(strong ? host.boldFont() : host.font()), TRUE);
    return label;
}

void makeRowButtons(FormHost& host, HWND& up, HWND& down, HWND& del) {
    up = makeButton(host, L"↑");
    down = makeButton(host, L"↓");
    del = makeButton(host, L"✕");
}

void destroyControl(FormHost& host, HWND& control) {
    if (!control) return;
    host.forget(control);
    DestroyWindow(control);
    control = nullptr;
}

}  // namespace

// -------------------------------------------------------------- ListEditor

void ListEditor::create(FormHost& host, const wchar_t* placeholder, bool multiline) {
    host_ = &host;
    placeholder_ = placeholder;
    multiline_ = multiline;
    add_ = makeButton(host, L"+ Добавить");
    host.onClick(add_, [this] {
        addRow(std::string());
        host_->restructured();
    });
}

void ListEditor::addRow(const std::string& text) {
    Row row;
    row.edit = makeEdit(*host_, placeholder_, multiline_);
    writeText(row.edit, text);
    makeRowButtons(*host_, row.up, row.down, row.del);
    rows_.push_back(row);

    // Rows get reordered, so handlers look their row up by handle instead of
    // capturing an index that goes stale after the first move.
    HWND key = row.edit;
    auto find = [this, key]() -> size_t {
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].edit == key) return i;
        return rows_.size();
    };
    host_->onClick(row.up, [this, find] { moveRow(find(), -1); });
    host_->onClick(row.down, [this, find] { moveRow(find(), 1); });
    host_->onClick(row.del, [this, find] { removeRow(find()); });
}

void ListEditor::removeRow(size_t index) {
    if (index >= rows_.size()) return;
    Row row = rows_[index];
    rows_.erase(rows_.begin() + static_cast<long>(index));
    destroyControl(*host_, row.edit);
    destroyControl(*host_, row.up);
    destroyControl(*host_, row.down);
    destroyControl(*host_, row.del);
    host_->restructured();
}

void ListEditor::moveRow(size_t index, int delta) {
    size_t target = static_cast<size_t>(static_cast<long>(index) + delta);
    if (index >= rows_.size() || target >= rows_.size()) return;
    std::swap(rows_[index], rows_[target]);
    host_->restructured();
}

void ListEditor::setValues(const std::vector<std::string>& values) {
    while (!rows_.empty()) {
        Row row = rows_.back();
        rows_.pop_back();
        destroyControl(*host_, row.edit);
        destroyControl(*host_, row.up);
        destroyControl(*host_, row.down);
        destroyControl(*host_, row.del);
    }
    for (const std::string& value : values) addRow(value);
}

std::vector<std::string> ListEditor::values() const {
    std::vector<std::string> out;
    for (const Row& row : rows_) {
        std::string text = trimmed(readText(row.edit));
        if (!text.empty()) out.push_back(text);
    }
    return out;
}

int ListEditor::layout(int x, int y, int width) {
    const int gap = host_->scale(4);
    const int button = host_->scale(kButtonSize);
    const int rowHeight = host_->scale(kRowHeight);
    const int top = y;
    for (const Row& row : rows_) {
        int editWidth = width - 3 * (button + gap);
        place(row.edit, x, y, editWidth, rowHeight);
        int bx = x + editWidth + gap;
        place(row.up, bx, y, button, button);
        place(row.down, bx + button + gap, y, button, button);
        place(row.del, bx + 2 * (button + gap), y, button, button);
        y += rowHeight + gap;
    }
    place(add_, x, y, host_->scale(120), host_->scale(kRowHeight));
    y += host_->scale(kRowHeight);
    return y - top;
}

void ListEditor::destroy() { setValues({}); }

// ---------------------------------------------------------------- CardList

void CardList::create(FormHost& host, const wchar_t* addLabel,
                      std::function<std::unique_ptr<Card>()> factory) {
    host_ = &host;
    factory_ = std::move(factory);
    add_ = makeButton(host, addLabel);
    host.onClick(add_, [this] {
        append();
        host_->restructured();
    });
}

Card& CardList::append() { return appendCard(factory_()); }

Card& CardList::appendCard(std::unique_ptr<Card> card) {
    card->build(*host_);
    makeRowButtons(*host_, card->up, card->down, card->del);
    Card* raw = card.get();
    auto find = [this, raw]() -> size_t {
        for (size_t i = 0; i < cards_.size(); ++i)
            if (cards_[i].get() == raw) return i;
        return cards_.size();
    };
    host_->onClick(card->up, [this, find] { move(find(), -1); });
    host_->onClick(card->down, [this, find] { move(find(), 1); });
    host_->onClick(card->del, [this, find] { remove(find()); });
    cards_.push_back(std::move(card));
    return *raw;
}

void CardList::remove(size_t index) {
    if (index >= cards_.size()) return;
    std::unique_ptr<Card> card = std::move(cards_[index]);
    cards_.erase(cards_.begin() + static_cast<long>(index));
    card->destroyBody();
    destroyControl(*host_, card->up);
    destroyControl(*host_, card->down);
    destroyControl(*host_, card->del);
    host_->restructured();
}

void CardList::move(size_t index, int delta) {
    size_t target = static_cast<size_t>(static_cast<long>(index) + delta);
    if (index >= cards_.size() || target >= cards_.size()) return;
    std::swap(cards_[index], cards_[target]);
    host_->restructured();
}

void CardList::clear() {
    while (!cards_.empty()) {
        std::unique_ptr<Card> card = std::move(cards_.back());
        cards_.pop_back();
        card->destroyBody();
        destroyControl(*host_, card->up);
        destroyControl(*host_, card->down);
        destroyControl(*host_, card->del);
    }
}

void CardList::destroy() { clear(); }

int CardList::layout(int x, int y, int width) {
    const int pad = host_->scale(kCardPad);
    const int gap = host_->scale(4);
    const int button = host_->scale(kButtonSize);
    const int top = y;
    for (std::unique_ptr<Card>& card : cards_) {
        const int cardTop = y;
        const int inner = x + pad;
        const int innerWidth = width - 2 * pad;
        const int toolsX = x + width - pad - 3 * button - 2 * gap;
        place(card->up, toolsX, y + pad, button, button);
        place(card->down, toolsX + button + gap, y + pad, button, button);
        place(card->del, toolsX + 2 * (button + gap), y + pad, button, button);
        const int bodyTop = y + pad + button + gap;
        y = bodyTop + card->layoutBody(inner, bodyTop, innerWidth) + pad;
        card->frame = RECT{x, cardTop, x + width, y};
        y += host_->scale(8);
    }
    place(add_, x, y, host_->scale(kAddButtonWidth), host_->scale(kRowHeight));
    y += host_->scale(kRowHeight);
    return y - top;
}

void CardList::collectFrames(std::vector<RECT>& out) const {
    for (const std::unique_ptr<Card>& card : cards_) out.push_back(card->frame);
}

// ------------------------------------------------------------------- cards

namespace {

class JobCard : public Card {
public:
    explicit JobCard(Job job) : job_(std::move(job)) {}

    void build(FormHost& host) override {
        host_ = &host;
        title_ = makeEdit(host, L"Должность");
        period_ = makeEdit(host, L"2022 – 2025 · 3 года");
        company_ = makeEdit(host, L"Компания");
        location_ = makeEdit(host, L"Город");
        writeText(title_, job_.title);
        writeText(period_, job_.period);
        writeText(company_, job_.company);
        writeText(location_, job_.location);
        bulletsLabel_ = makeLabel(host, L"Пункты", false);
        bullets_.create(host, L"Достижение или обязанность");
        bullets_.setValues(job_.bullets);
    }

    int layoutBody(int x, int y, int width) override {
        const int gap = host_->scale(kGap);
        const int row = host_->scale(kRowHeight);
        const int top = y;
        const int wide = (width * 2) / 3 - gap;
        place(title_, x, y, wide, row);
        place(period_, x + wide + gap, y, width - wide - gap, row);
        y += row + gap;
        place(company_, x, y, wide, row);
        place(location_, x + wide + gap, y, width - wide - gap, row);
        y += row + gap;
        place(bulletsLabel_, x, y, width, host_->scale(18));
        y += host_->scale(20);
        y += bullets_.layout(x, y, width);
        return y - top;
    }

    void destroyBody() override {
        bullets_.destroy();
        destroyControl(*host_, title_);
        destroyControl(*host_, period_);
        destroyControl(*host_, company_);
        destroyControl(*host_, location_);
        destroyControl(*host_, bulletsLabel_);
    }

    Job read() const {
        Job job;
        job.title = readText(title_);
        job.period = readText(period_);
        job.company = readText(company_);
        job.location = readText(location_);
        job.bullets = bullets_.values();
        return job;
    }

private:
    FormHost* host_ = nullptr;
    Job job_;
    HWND title_ = nullptr, period_ = nullptr, company_ = nullptr, location_ = nullptr;
    HWND bulletsLabel_ = nullptr;
    ListEditor bullets_;
};

// A skill group: a title plus rows of "name + highlight".
class SkillGroupCard : public Card {
public:
    explicit SkillGroupCard(SkillGroup group) : group_(std::move(group)) {}

    void build(FormHost& host) override {
        host_ = &host;
        title_ = makeEdit(host, L"Название группы, например «Сети»");
        writeText(title_, group_.title);
        add_ = makeButton(host, L"+ Навык");
        host.onClick(add_, [this] {
            addRow(Skill());
            host_->restructured();
        });
        for (const Skill& skill : group_.skills) addRow(skill);
    }

    int layoutBody(int x, int y, int width) override {
        const int gap = host_->scale(4);
        const int row = host_->scale(kRowHeight);
        const int button = host_->scale(kButtonSize);
        const int check = host_->scale(90);
        const int top = y;
        place(title_, x, y, width, row);
        y += row + host_->scale(kGap);
        for (const Row& item : rows_) {
            int editWidth = width - check - button - 2 * gap;
            place(item.name, x, y, editWidth, row);
            place(item.highlight, x + editWidth + gap, y, check, row);
            place(item.del, x + editWidth + gap + check + gap, y, button, button);
            y += row + gap;
        }
        place(add_, x, y, host_->scale(110), row);
        y += row;
        return y - top;
    }

    void destroyBody() override {
        while (!rows_.empty()) {
            Row row = rows_.back();
            rows_.pop_back();
            destroyControl(*host_, row.name);
            destroyControl(*host_, row.highlight);
            destroyControl(*host_, row.del);
        }
        destroyControl(*host_, title_);
        destroyControl(*host_, add_);
    }

    SkillGroup read() const {
        SkillGroup group;
        group.title = readText(title_);
        for (const Row& row : rows_) {
            std::string name = trimmed(readText(row.name));
            if (name.empty()) continue;
            Skill skill;
            skill.name = name;
            skill.highlight = Button_GetCheck(row.highlight) == BST_CHECKED;
            group.skills.push_back(std::move(skill));
        }
        return group;
    }

private:
    struct Row {
        HWND name = nullptr;
        HWND highlight = nullptr;
        HWND del = nullptr;
    };

    void addRow(const Skill& skill) {
        Row row;
        row.name = makeEdit(*host_, L"Навык");
        writeText(row.name, skill.name);
        row.highlight = makeCheck(*host_, L"выделить");
        Button_SetCheck(row.highlight, skill.highlight ? BST_CHECKED : BST_UNCHECKED);
        row.del = makeButton(*host_, L"✕");
        rows_.push_back(row);
        HWND key = row.name;
        host_->onClick(row.del, [this, key] {
            for (size_t i = 0; i < rows_.size(); ++i) {
                if (rows_[i].name != key) continue;
                Row dead = rows_[i];
                rows_.erase(rows_.begin() + static_cast<long>(i));
                destroyControl(*host_, dead.name);
                destroyControl(*host_, dead.highlight);
                destroyControl(*host_, dead.del);
                host_->restructured();
                return;
            }
        });
    }

    FormHost* host_ = nullptr;
    SkillGroup group_;
    HWND title_ = nullptr;
    HWND add_ = nullptr;
    std::vector<Row> rows_;
};

class EducationCard : public Card {
public:
    explicit EducationCard(Education item) : item_(std::move(item)) {}

    void build(FormHost& host) override {
        host_ = &host;
        title_ = makeEdit(host, L"Программа или сертификация");
        subtitle_ = makeEdit(host, L"Учреждение, год / статус");
        writeText(title_, item_.title);
        writeText(subtitle_, item_.subtitle);
        highlight_ = makeCheck(host, L"выделить (синим)");
        Button_SetCheck(highlight_, item_.highlight ? BST_CHECKED : BST_UNCHECKED);
    }

    int layoutBody(int x, int y, int width) override {
        const int row = host_->scale(kRowHeight);
        const int gap = host_->scale(kGap);
        const int top = y;
        place(title_, x, y, width, row);
        y += row + gap;
        place(subtitle_, x, y, width, row);
        y += row + gap;
        place(highlight_, x, y, width, row);
        y += row;
        return y - top;
    }

    void destroyBody() override {
        destroyControl(*host_, title_);
        destroyControl(*host_, subtitle_);
        destroyControl(*host_, highlight_);
    }

    Education read() const {
        Education item;
        item.title = readText(title_);
        item.subtitle = readText(subtitle_);
        item.highlight = Button_GetCheck(highlight_) == BST_CHECKED;
        return item;
    }

private:
    FormHost* host_ = nullptr;
    Education item_;
    HWND title_ = nullptr, subtitle_ = nullptr, highlight_ = nullptr;
};

}  // namespace

// -------------------------------------------------------------- the layout

bool FormImpl::onCard(HWND control) const {
    if (!control || frames.empty()) return false;
    RECT box{};
    GetWindowRect(control, &box);
    MapWindowPoints(HWND_DESKTOP, body, reinterpret_cast<POINT*>(&box), 2);
    POINT centre{(box.left + box.right) / 2, (box.top + box.bottom) / 2};
    for (const RECT& frame : frames)
        if (PtInRect(&frame, centre)) return true;
    return false;
}

void FormImpl::refresh() {
    // RDW_ALLCHILDREN: the controls moved with the body, so their pixels are
    // stale too; without this they keep whatever the last blit left them.
    RedrawWindow(body, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void FormImpl::relayout() {
    RECT client{};
    GetClientRect(pane, &client);
    const int margin = scale(kMargin);
    const int scrollbar = GetSystemMetrics(SM_CXVSCROLL);
    const int width = std::max(scale(240), static_cast<int>(client.right) - 2 * margin - scrollbar);
    const int row = scale(kRowHeight);
    const int gap = scale(kGap);
    const int heading = scale(kHeadingHeight);
    const int section = scale(kSectionGap);
    const int x = margin;
    int y = margin;

    // Controls are about to be moved one by one; without this each SetWindowPos
    // repaints on its own and the pane visibly churns.
    SetWindowRedraw(body, FALSE);

    auto putHeading = [&](Heading which) {
        place(headings[which], x, y, width, heading);
        y += heading + scale(4);
    };

    // Colours.
    putHeading(H_Colors);
    const int labelWidth = scale(60);
    place(presetLabel, x, y + scale(4), labelWidth, row);
    place(preset, x + labelWidth, y, width - labelWidth, row * 8);  // height = dropdown extent
    y += row + gap;
    const int swatchWidth = scale(kSwatchWidth);
    const int columnWidth = (width - gap) / 2;
    for (int i = 0; i < TR_Count; ++i) {
        int column = i % 2;
        int line = i / 2;
        int cx = x + column * (columnWidth + gap);
        int cy = y + line * (row + scale(4));
        place(swatch[i], cx, cy, swatchWidth, row);
        place(swatchLabel[i], cx + swatchWidth + gap, cy + scale(4),
              columnWidth - swatchWidth - gap, row);
    }
    y += (TR_Count / 2) * (row + scale(4)) + section;

    // Header.
    putHeading(H_Header);
    place(name, x, y, width, row);
    y += row + gap;
    place(role, x, y, width, row);
    y += row + gap;
    const int third = (width - 2 * gap) / 3;
    place(email, x, y, third, row);
    place(location, x + third + gap, y, third, row);
    place(website, x + 2 * (third + gap), y, width - 2 * (third + gap), row);
    y += row + section;

    // Summary.
    putHeading(H_Summary);
    place(summary, x, y, width, scale(kSummaryHeight));
    y += scale(kSummaryHeight) + section;

    // Experience.
    putHeading(H_Experience);
    y += jobs.layout(x, y, width) + section;

    // Volunteering.
    putHeading(H_Volunteer);
    place(volunteerTitle, x, y, width, row);
    y += row + gap;
    y += volunteering.layout(x, y, width) + section;

    // Technical skills.
    putHeading(H_Skills);
    place(skillsHint, x, y, width, scale(18));
    y += scale(20);
    y += skillGroups.layout(x, y, width) + section;

    // Soft skills.
    putHeading(H_Soft);
    y += softSkills.layout(x, y, width) + section;

    // Education.
    putHeading(H_Education);
    y += education.layout(x, y, width) + section;

    // Personal lab.
    putHeading(H_Lab);
    place(labTitle, x, y, width, row);
    y += row + gap;
    y += labBullets.layout(x, y, width) + margin;

    contentHeight = y;
    frames.clear();
    jobs.collectFrames(frames);
    volunteering.collectFrames(frames);
    skillGroups.collectFrames(frames);
    education.collectFrames(frames);

    scrollY = std::min(scrollY, std::max(0, contentHeight - static_cast<int>(client.bottom)));
    // Never leave a strip of the pane uncovered: the pane paints nothing of
    // its own, so anything the body does not reach keeps stale pixels.
    const int bodyHeight = std::max(contentHeight, static_cast<int>(client.bottom) + scrollY);
    SetWindowPos(body, nullptr, 0, -scrollY, client.right, bodyHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    SetWindowRedraw(body, TRUE);
    updateScrollBar();
    refresh();
}

void FormImpl::updateScrollBar() {
    RECT client{};
    GetClientRect(pane, &client);
    SCROLLINFO info{};
    info.cbSize = sizeof info;
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, contentHeight - 1);
    info.nPage = static_cast<UINT>(client.bottom);
    info.nPos = scrollY;
    SetScrollInfo(pane, SB_VERT, &info, TRUE);
}

void FormImpl::scrollTo(int value) {
    RECT client{};
    GetClientRect(pane, &client);
    int limit = std::max(0, contentHeight - static_cast<int>(client.bottom));
    value = std::max(0, std::min(limit, value));
    if (value == scrollY) return;
    scrollY = value;
    // Let Windows copy the pixels: every control here erases its own
    // background, so the blitted image is already correct, and only the strip
    // that scrolls into view needs painting. Repainting the whole pane instead
    // makes the text flicker on every notch of the wheel.
    SetWindowPos(body, nullptr, 0, -scrollY, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    updateScrollBar();
    UpdateWindow(body);  // paint the newly exposed strip before the next notch
}

void FormImpl::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(body, &ps);
    const int width = ps.rcPaint.right - ps.rcPaint.left;
    const int height = ps.rcPaint.bottom - ps.rcPaint.top;
    if (width <= 0 || height <= 0) {
        EndPaint(body, &ps);
        return;
    }

    // Draw into a bitmap and blit once: the pane repaints in full on every
    // scroll, and painting straight to the screen flickers badly.
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    SetViewportOrgEx(memory, -ps.rcPaint.left, -ps.rcPaint.top, nullptr);

    HBRUSH background = CreateSolidBrush(ui().pane);
    FillRect(memory, &ps.rcPaint, background);
    DeleteObject(background);

    HBRUSH card = CreateSolidBrush(ui().card);
    HPEN edge = CreatePen(PS_SOLID, 1, ui().cardEdge);
    HGDIOBJ oldBrush = SelectObject(memory, card);
    HGDIOBJ oldPen = SelectObject(memory, edge);
    const int radius = scale(8);
    for (const RECT& frame : frames) {
        RECT test = frame;
        RECT hit;
        if (!IntersectRect(&hit, &test, &ps.rcPaint)) continue;
        RoundRect(memory, frame.left, frame.top, frame.right, frame.bottom, radius, radius);
    }
    SelectObject(memory, oldBrush);
    SelectObject(memory, oldPen);
    DeleteObject(card);
    DeleteObject(edge);

    SetViewportOrgEx(memory, 0, 0, nullptr);
    BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(body, &ps);
}

void FormImpl::syncPresetSelection() {
    Theme current;
    for (int i = 0; i < TR_Count; ++i) current.c[i] = colors[i];
    const std::vector<Preset>& list = presets();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].theme == current) {
            ComboBox_SetCurSel(preset, static_cast<int>(i));
            return;
        }
    }
    ComboBox_SetCurSel(preset, -1);
}

void FormImpl::applyPreset(int index) {
    const std::vector<Preset>& list = presets();
    if (index < 0 || static_cast<size_t>(index) >= list.size()) return;
    for (int i = 0; i < TR_Count; ++i) {
        colors[i] = list[static_cast<size_t>(index)].theme.c[i];
        InvalidateRect(swatch[i], nullptr, TRUE);
    }
    changed();
}

void FormImpl::pickColor(int role) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof cc;
    cc.hwndOwner = body;
    cc.rgbResult = colorRef(colors[role]);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (!ChooseColorW(&cc)) return;
    colors[role] = hexOf(cc.rgbResult);
    InvalidateRect(swatch[role], nullptr, TRUE);
    syncPresetSelection();
    changed();
}

// -------------------------------------------------------------- window procs

namespace {

FormImpl* implOf(HWND window) {
    return reinterpret_cast<FormImpl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

// Draws one colour swatch: the colour itself, with its hex value written in
// whichever of black or white stays readable on it.
void drawSwatch(FormImpl& impl, int role, DRAWITEMSTRUCT* item) {
    COLORREF color = colorRef(impl.colors[role]);
    HBRUSH fill = CreateSolidBrush(color);
    FillRect(item->hDC, &item->rcItem, fill);
    DeleteObject(fill);

    HPEN pen = CreatePen(PS_SOLID, 1, (item->itemState & ODS_FOCUS) ? ui().accent : ui().cardEdge);
    HGDIOBJ oldPen = SelectObject(item->hDC, pen);
    HGDIOBJ oldBrush = SelectObject(item->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right,
              item->rcItem.bottom);
    SelectObject(item->hDC, oldPen);
    SelectObject(item->hDC, oldBrush);
    DeleteObject(pen);

    int luma = (GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114) / 1000;
    SetTextColor(item->hDC, luma > 140 ? RGB(0, 0, 0) : RGB(255, 255, 255));
    SetBkMode(item->hDC, TRANSPARENT);
    SelectObject(item->hDC, impl.regular);
    std::wstring text = widen(impl.colors[role]);
    DrawTextW(item->hDC, text.c_str(), -1, &item->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK bodyProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    FormImpl* impl = implOf(window);
    switch (message) {
        case WM_ERASEBKGND:
            return 1;  // WM_PAINT covers the whole client area
        case WM_PAINT:
            if (impl) impl->paint();
            return 0;
        case WM_CTLCOLORSTATIC: {
            // Paint opaque in whichever colour the label is standing on. A
            // transparent static never clears itself, so old glyphs pile up
            // under the new ones every time it moves or its text changes.
            HDC dc = reinterpret_cast<HDC>(wParam);
            if (!impl) break;
            const bool card = impl->onCard(reinterpret_cast<HWND>(lParam));
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, card ? ui().card : ui().pane);
            SetTextColor(dc, ui().text);
            return reinterpret_cast<LRESULT>(card ? impl->cardBrush : impl->paneBrush);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, ui().text);
            SetBkColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(impl ? impl->fieldBrush : nullptr);
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (impl && item) {
                for (int i = 0; i < TR_Count; ++i) {
                    if (impl->swatch[i] != item->hwndItem) continue;
                    drawSwatch(*impl, i, item);
                    return TRUE;
                }
            }
            return FALSE;
        }
        case WM_COMMAND: {
            if (!impl) break;
            HWND control = reinterpret_cast<HWND>(lParam);
            WORD code = HIWORD(wParam);
            if (code == EN_CHANGE) {
                impl->changed();
                return 0;
            }
            if (code == CBN_SELCHANGE && control == impl->preset) {
                impl->applyPreset(ComboBox_GetCurSel(impl->preset));
                return 0;
            }
            if (code == BN_CLICKED) {
                for (int i = 0; i < TR_Count; ++i) {
                    if (impl->swatch[i] != control) continue;
                    impl->pickColor(i);
                    return 0;
                }
                auto it = impl->clicks.find(control);
                if (it != impl->clicks.end()) {
                    // Copy first: the handler may destroy the button, and with
                    // it the entry we are standing on.
                    std::function<void()> action = it->second;
                    action();
                    return 0;
                }
                impl->changed();  // a checkbox toggled
                return 0;
            }
            break;
        }
        case WM_MOUSEWHEEL:
            if (impl) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                impl->scrollTo(impl->scrollY - delta * impl->scale(kWheelStep) / WHEEL_DELTA);
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK paneProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    FormImpl* impl = implOf(window);
    switch (message) {
        case WM_SIZE:
            if (impl) impl->relayout();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_VSCROLL: {
            if (!impl) break;
            SCROLLINFO info{};
            info.cbSize = sizeof info;
            info.fMask = SIF_ALL;
            GetScrollInfo(window, SB_VERT, &info);
            int position = info.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: position -= impl->scale(30); break;
                case SB_LINEDOWN: position += impl->scale(30); break;
                case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
                case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: position = info.nTrackPos; break;
                default: break;
            }
            impl->scrollTo(position);
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (impl) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                impl->scrollTo(impl->scrollY - delta * impl->scale(kWheelStep) / WHEEL_DELTA);
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void registerClasses(HINSTANCE instance) {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW pane{};
    pane.cbSize = sizeof pane;
    pane.lpfnWndProc = paneProc;
    pane.hInstance = instance;
    pane.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pane.lpszClassName = L"CVBFormPane";
    RegisterClassExW(&pane);

    WNDCLASSEXW body{};
    body.cbSize = sizeof body;
    body.lpfnWndProc = bodyProc;
    body.hInstance = instance;
    body.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    body.lpszClassName = L"CVBFormBody";
    RegisterClassExW(&body);
}

HFONT makeFont(UINT dpi, bool bold) {
    LOGFONTW lf{};
    lf.lfHeight = -scaled(12, dpi);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

}  // namespace

// ---------------------------------------------------------------- FormPane

bool FormPane::create(HWND parent, HINSTANCE instance, std::function<void()> onChange) {
    registerClasses(instance);
    impl_ = std::make_shared<FormImpl>();
    FormImpl& impl = *impl_;
    impl.notify = std::move(onChange);
    impl.dpi = GetDpiForWindow(parent);
    if (!impl.dpi) impl.dpi = 96;
    impl.regular = makeFont(impl.dpi, false);
    impl.strong = makeFont(impl.dpi, true);
    impl.paneBrush = CreateSolidBrush(ui().pane);
    impl.cardBrush = CreateSolidBrush(ui().card);
    impl.fieldBrush = CreateSolidBrush(RGB(255, 255, 255));

    hwnd_ = CreateWindowExW(0, L"CVBFormPane", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 10, 10,
                            parent, nullptr, instance, nullptr);
    if (!hwnd_) return false;
    impl.pane = hwnd_;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&impl));

    impl.body = CreateWindowExW(0, L"CVBFormBody", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0,
                                0, 10, 10, hwnd_, nullptr, instance, nullptr);
    if (!impl.body) return false;
    SetWindowLongPtrW(impl.body, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&impl));

    const wchar_t* titles[FormImpl::H_Count] = {
        L"Цвета", L"Шапка", L"О себе", L"Опыт работы", L"Волонтёрство",
        L"Технические навыки", L"Soft skills", L"Образование и сертификаты",
        L"Личная лаборатория",
    };
    for (int i = 0; i < FormImpl::H_Count; ++i) impl.headings[i] = makeLabel(impl, titles[i], true);

    impl.presetLabel = makeLabel(impl, L"Пресет", false);
    impl.preset = CreateWindowExW(0, L"COMBOBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                                      WS_VSCROLL,
                                  0, 0, 10, 200, impl.body, nullptr, instance, nullptr);
    SendMessageW(impl.preset, WM_SETFONT, reinterpret_cast<WPARAM>(impl.regular), TRUE);
    for (const Preset& item : presets())
        ComboBox_AddString(impl.preset, item.name);

    Theme defaults;
    for (int i = 0; i < TR_Count; ++i) {
        impl.colors[i] = defaults.c[i];
        impl.swatch[i] = makeButton(impl, L"", BS_OWNERDRAW);
        impl.swatchLabel[i] = makeLabel(impl, kThemeLabels[i], false);
    }

    impl.name = makeEdit(impl, L"Полное имя");
    impl.role = makeEdit(impl, L"Должность, например SYSTEM ADMINISTRATOR");
    impl.email = makeEdit(impl, L"Email");
    impl.location = makeEdit(impl, L"Город, страна");
    impl.website = makeEdit(impl, L"Сайт / GitHub");
    impl.summary = makeEdit(impl, L"", true);
    impl.volunteerTitle = makeEdit(impl, L"Заголовок секции");
    impl.labTitle = makeEdit(impl, L"Заголовок секции");
    impl.skillsHint = makeLabel(impl, L"Группы заполняют две колонки слева направо.", false);

    impl.jobs.create(impl, L"+ Добавить работу", [] {
        return std::unique_ptr<Card>(new JobCard(Job{"Должность", "2024 – 2025", "Компания", "", {}}));
    });
    impl.volunteering.create(impl, L"+ Добавить волонтёрство", [] {
        return std::unique_ptr<Card>(new JobCard(Job{"Роль", "2024", "Организация", "", {}}));
    });
    impl.skillGroups.create(impl, L"+ Добавить группу навыков", [] {
        SkillGroup group;
        group.title = "Новая группа";
        group.skills.push_back(Skill{"Навык", true});
        return std::unique_ptr<Card>(new SkillGroupCard(std::move(group)));
    });
    impl.education.create(impl, L"+ Добавить запись", [] {
        return std::unique_ptr<Card>(new EducationCard(Education{"Сертификация", "Запланировано", false}));
    });
    impl.softSkills.create(impl, L"Soft skill");
    impl.labBullets.create(impl, L"Проект или пункт лаборатории");

    impl.relayout();
    return true;
}

void FormPane::setCV(const CV& cv) {
    FormImpl& impl = *impl_;
    impl.loading = true;

    writeText(impl.name, cv.name);
    writeText(impl.role, cv.role);
    writeText(impl.email, cv.email);
    writeText(impl.location, cv.location);
    writeText(impl.website, cv.website);
    writeText(impl.summary, cv.summary);
    writeText(impl.volunteerTitle, cv.volunteerTitle);
    writeText(impl.labTitle, cv.labTitle);

    for (int i = 0; i < TR_Count; ++i) {
        impl.colors[i] = cv.theme.c[i];
        InvalidateRect(impl.swatch[i], nullptr, TRUE);
    }
    impl.syncPresetSelection();

    impl.jobs.clear();
    for (const Job& job : cv.jobs) impl.jobs.appendCard(std::make_unique<JobCard>(job));
    impl.volunteering.clear();
    for (const Job& job : cv.volunteering)
        impl.volunteering.appendCard(std::make_unique<JobCard>(job));
    impl.skillGroups.clear();
    for (const SkillGroup& group : cv.skillGroups)
        impl.skillGroups.appendCard(std::make_unique<SkillGroupCard>(group));
    impl.education.clear();
    for (const Education& item : cv.education)
        impl.education.appendCard(std::make_unique<EducationCard>(item));

    impl.softSkills.setValues(cv.softSkills);
    impl.labBullets.setValues(cv.labBullets);

    impl.loading = false;
    impl.relayout();
    InvalidateRect(impl.body, nullptr, TRUE);
}

CV FormPane::collect() const {
    const FormImpl& impl = *impl_;
    CV cv;
    cv.name = readText(impl.name);
    cv.role = readText(impl.role);
    cv.email = readText(impl.email);
    cv.location = readText(impl.location);
    cv.website = readText(impl.website);
    cv.summary = readText(impl.summary);
    cv.volunteerTitle = readText(impl.volunteerTitle);
    cv.labTitle = readText(impl.labTitle);

    for (const std::unique_ptr<Card>& card : impl.jobs.cards())
        cv.jobs.push_back(static_cast<const JobCard*>(card.get())->read());
    for (const std::unique_ptr<Card>& card : impl.volunteering.cards())
        cv.volunteering.push_back(static_cast<const JobCard*>(card.get())->read());
    for (const std::unique_ptr<Card>& card : impl.skillGroups.cards())
        cv.skillGroups.push_back(static_cast<const SkillGroupCard*>(card.get())->read());
    for (const std::unique_ptr<Card>& card : impl.education.cards())
        cv.education.push_back(static_cast<const EducationCard*>(card.get())->read());

    cv.softSkills = impl.softSkills.values();
    cv.labBullets = impl.labBullets.values();
    for (int i = 0; i < TR_Count; ++i) cv.theme.c[i] = impl.colors[i];
    return cv;
}

void FormPane::setDpi(UINT dpi) {
    FormImpl& impl = *impl_;
    if (dpi == impl.dpi || !dpi) return;
    impl.dpi = dpi;
    HFONT regular = makeFont(dpi, false);
    HFONT strong = makeFont(dpi, true);
    // Re-point every control at the new fonts before dropping the old ones.
    struct Walk {
        static BOOL CALLBACK proc(HWND child, LPARAM param) {
            FormImpl* self = reinterpret_cast<FormImpl*>(param);
            bool heading = false;
            for (HWND h : self->headings) heading = heading || h == child;
            SendMessageW(child, WM_SETFONT,
                         reinterpret_cast<WPARAM>(heading ? self->strong : self->regular), TRUE);
            return TRUE;
        }
    };
    HFONT oldRegular = impl.regular;
    HFONT oldStrong = impl.strong;
    impl.regular = regular;
    impl.strong = strong;
    EnumChildWindows(impl.body, Walk::proc, reinterpret_cast<LPARAM>(&impl));
    DeleteObject(oldRegular);
    DeleteObject(oldStrong);
    impl.relayout();
}

}  // namespace cvb
