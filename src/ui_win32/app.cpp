// CV Builder: the main window, the menu, and the wiring between the editing
// form, the live preview and the files on disk.
// windows.h first: the other SDK headers assume its basic types exist.
#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <iterator>

#include "pdf.h"
#include "ui.h"
#include "version.h"

namespace cvb {
namespace {

const wchar_t* const kAppName = L"CV Builder";

enum : int {
    IDM_NEW = 100, IDM_OPEN, IDM_SAVE, IDM_SAVEAS, IDM_EXPORT, IDM_PRINT, IDM_EXIT,
    IDM_UNDO, IDM_REDO,
    // The recent-files popup numbers its entries from here; the range has to
    // stay clear of every other command id.
    IDM_RECENT_FIRST = 300, IDM_RECENT_LAST = 399, IDM_RECENT_CLEAR,
    IDC_PREV_PAGE = 200, IDC_NEXT_PAGE, IDC_ZOOM_OUT, IDC_ZOOM_IN,
    IDC_PAGE_LABEL, IDC_ZOOM_LABEL, IDC_THEME,
};

// Matches the ICON line in res/app.rc.
constexpr int IDI_APP = 1;

constexpr UINT kRefreshTimer = 1;
constexpr UINT kRefreshDelay = 200;  // ms of quiet before the preview redraws

// A separate, slower debounce for the undo history. Snapshotting on the same
// 200 ms as the repaint would make every pause between two words its own undo
// step and fill the whole history with keystrokes.
constexpr UINT kSnapshotTimer = 2;
constexpr UINT kSnapshotDelay = 500;

// The recovery snapshot is written on a fixed interval rather than on every
// edit: it costs a file write, and losing at most half a minute of work to a
// power cut is the trade being made.
constexpr UINT kAutosaveTimer = 3;
constexpr UINT kAutosaveInterval = 30000;

// Deep enough to undo a whole editing session, shallow enough that the
// snapshots stay a rounding error next to the preview bitmaps.
constexpr size_t kUndoDepth = 50;

constexpr int kToolbarHeight = 40;
constexpr int kToolButtonWidth = 118;
constexpr int kSmallButton = 30;
constexpr int kThemeWidth = 160;
constexpr int kStatusHeight = 24;

// "v1.2.0-release", built once. The macro is a run of narrow literals, so it
// cannot carry an L prefix; widening it here keeps the paint path free of the
// conversion.
const std::wstring& versionLabel() {
    static const std::wstring label = widen(VER_DISPLAY_STR);
    return label;
}

// In the order of ThemeMode, so the selection index is the mode.
const wchar_t* const kThemeNames[] = {L"Как в системе", L"Светлая", L"Тёмная"};

struct ToolButton {
    int id;
    const wchar_t* label;
    int width;
};

const ToolButton kFileButtons[] = {
    {IDM_NEW, L"Новый", kToolButtonWidth},
    {IDM_OPEN, L"Открыть…", kToolButtonWidth + 20},  // wider: it carries a drop-down arrow
    {IDM_SAVE, L"Сохранить", kToolButtonWidth},
    {IDM_SAVEAS, L"Сохранить как…", kToolButtonWidth + 34},
    {IDM_EXPORT, L"Экспорт PDF…", kToolButtonWidth + 16},
    {IDM_PRINT, L"Печать…", kToolButtonWidth - 20},
};

std::wstring directoryOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring fileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring exeDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return directoryOf(std::wstring(buffer, n));
}

// "Daniil Mishin" -> "Daniil_Mishin", the default name for Save / Export.
std::wstring suggestedName(const std::wstring& personName, const wchar_t* extension) {
    std::wstring base;
    for (wchar_t c : personName) {
        if (c == L' ')
            base += L'_';
        else if (wcschr(L"\\/:*?\"<>|", c) == nullptr)
            base += c;
    }
    if (base.empty()) base = L"cv";
    return base + extension;
}

struct App {
    HWND hwnd = nullptr;
    HWND fileButtons[std::size(kFileButtons)] = {};
    HWND theme = nullptr;
    HWND undoButton = nullptr, redoButton = nullptr;
    HWND prevPage = nullptr, nextPage = nullptr, pageLabel = nullptr;
    HWND zoomOut = nullptr, zoomIn = nullptr, zoomLabel = nullptr;
    HFONT uiFont = nullptr;
    HBRUSH background = nullptr;

    FormPane form;
    PreviewPane preview;
    FontSet fonts;
    bool fontsReady = false;

    // The undo history is whole-document JSON snapshots rather than a list of
    // edit operations: the model already serialises itself losslessly, and a
    // snapshot cannot go out of step with the form the way a replayed
    // operation can. A CV is a few kilobytes, so fifty of them are free.
    std::vector<std::string> undoStack;
    std::vector<std::string> redoStack;
    std::string baseline;  // the state the stacks were last reconciled with

    std::wstring path;   // the .json currently being edited, empty if never saved
    std::wstring status; // the line drawn along the bottom edge
    bool dirty = false;
    UINT dpi = 96;

    int scale(int value) const { return scaled(value, dpi); }

    void build(HINSTANCE instance);
    void layout();
    void refreshPreview();
    void scheduleRefresh();
    void updateTitle();
    void updatePreviewControls();
    void setStatus(const std::wstring& text);
    void applyTheme();
    RECT statusRect() const;
    void paintChrome(HDC dc);

    void actionNew();
    void actionOpen();
    bool openPath(const std::wstring& file);  // shared by the dialog and drag-and-drop
    void actionDrop(HDROP drop);
    bool actionSave();
    bool actionSaveAs();
    void actionExport();
    void actionPrint();
    void actionUndo();
    void actionRedo();
    void showRecentMenu();
    void openRecent(int index);
    void takeSnapshot();       // debounced: records a new undo step if anything moved
    void applySnapshot(const std::string& json);
    void resetHistory(const CV& cv);
    void updateHistoryButtons();
    void writeRecovery();
    bool confirmDiscard();
    void loadCV(const CV& cv, const std::wstring& from);
};

App* appOf(HWND window) {
    return reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

HWND makeToolButton(HWND parent, HINSTANCE instance, int id, const wchar_t* label, HFONT font,
                    DWORD kind = BS_PUSHBUTTON) {
    HWND button = CreateWindowExW(0, L"BUTTON", label,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | kind, 0, 0, 10, 10,
                                  parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  instance, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return button;
}

HWND makeToolLabel(HWND parent, HINSTANCE instance, int id, const wchar_t* text, HFONT font) {
    HWND label = CreateWindowExW(0, L"STATIC", text,
                                 WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0, 0, 10, 10,
                                 parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 instance, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return label;
}

HFONT makeUiFont(UINT dpi) {
    LOGFONTW lf{};
    lf.lfHeight = -scaled(12, dpi);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

// ---------------------------------------------------------------- App logic

// The bottom strip is painted rather than a STATUSCLASSNAME control: a common
// status bar always draws itself in the system colours, which leaves a bright
// band across the bottom of an otherwise dark window.
RECT App::statusRect() const {
    RECT client{};
    GetClientRect(hwnd, &client);
    client.top = std::max<LONG>(0, client.bottom - scale(kStatusHeight));
    return client;
}

void App::setStatus(const std::wstring& text) {
    status = text;
    RECT strip = statusRect();
    InvalidateRect(hwnd, &strip, TRUE);
}

void App::paintChrome(HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const RECT strip = statusRect();

    HPEN pen = CreatePen(PS_SOLID, 1, ui().cardEdge);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    const int bar = scale(kToolbarHeight);
    MoveToEx(dc, 0, bar - 1, nullptr);
    LineTo(dc, client.right, bar - 1);
    MoveToEx(dc, 0, strip.top, nullptr);
    LineTo(dc, client.right, strip.top);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    RECT text = strip;
    text.left += scale(10);
    text.right -= scale(10);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ui().subtext);
    HGDIOBJ oldFont = SelectObject(dc, uiFont);

    // The build names itself in the corner opposite the status message. It is
    // measured before the message is drawn, so the message gets the width that
    // is left and ellipsizes against the version instead of running underneath
    // it on a narrow window.
    const std::wstring& version = versionLabel();
    SIZE size{};
    GetTextExtentPoint32W(dc, version.c_str(), static_cast<int>(version.size()), &size);
    RECT versionBox = text;
    versionBox.left = versionBox.right - size.cx;
    DrawTextW(dc, version.c_str(), -1, &versionBox,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    text.right = versionBox.left - scale(12);

    DrawTextW(dc, status.c_str(), -1, &text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}

void App::applyTheme() {
    HBRUSH fresh = CreateSolidBrush(ui().window);
    DeleteObject(background);
    background = fresh;
    applyThemeToWindow(hwnd);
    form.applyTheme();
    preview.applyTheme();
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void App::updateTitle() {
    std::wstring title = kAppName;
    if (!path.empty()) title += L" — " + fileNameOf(path);
    if (dirty) title += L" *";
    SetWindowTextW(hwnd, title.c_str());
}

void App::updatePreviewControls() {
    wchar_t buffer[64];
    swprintf(buffer, 64, L"Стр. %d / %d", preview.page() + 1, preview.pageCount());
    SetWindowTextW(pageLabel, buffer);
    InvalidateRect(pageLabel, nullptr, TRUE);
    swprintf(buffer, 64, L"%d %%", preview.zoom());
    SetWindowTextW(zoomLabel, buffer);
    InvalidateRect(zoomLabel, nullptr, TRUE);
    EnableWindow(prevPage, preview.page() > 0);
    EnableWindow(nextPage, preview.page() + 1 < preview.pageCount());
}

void App::refreshPreview() {
    if (!fontsReady) return;
    preview.setDocument(::cvb::layout(form.collect(), fonts));
}

void App::scheduleRefresh() {
    dirty = true;
    updateTitle();
    SetTimer(hwnd, kRefreshTimer, kRefreshDelay, nullptr);
    SetTimer(hwnd, kSnapshotTimer, kSnapshotDelay, nullptr);
}

void App::loadCV(const CV& cv, const std::wstring& from) {
    form.setCV(cv);
    path = from;
    dirty = false;
    updateTitle();
    refreshPreview();
    // A new document starts a new history: undoing across an Open would put
    // the previous CV back under the new file's name.
    resetHistory(cv);
}

bool App::confirmDiscard() {
    if (!dirty) return true;
    int answer = MessageBoxW(hwnd, L"Сохранить изменения перед тем, как продолжить?", kAppName,
                             MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) return false;
    if (answer == IDNO) return true;
    return actionSave();
}

void App::actionNew() {
    if (!confirmDiscard()) return;
    loadCV(emptyCV(), std::wstring());
    setStatus(L"Новое резюме");
}

void App::actionOpen() {
    if (!confirmDiscard()) return;
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Резюме (*.json)\0*.json\0Все файлы\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"Открыть резюме";
    if (!GetOpenFileNameW(&ofn)) return;
    openPath(file);
}

// Loads a path that came either from the Open dialog or from a dropped file.
// Returns false and explains itself if the file is not a CV.
bool App::openPath(const std::wstring& file) {
    CV cv;
    std::string error;
    if (!load(file, cv, error)) {
        MessageBoxW(hwnd, (L"Не удалось открыть файл:\n" + widen(error)).c_str(), kAppName,
                    MB_ICONERROR);
        return false;
    }
    loadCV(cv, file);
    pushRecentFile(file);
    setStatus(L"Открыто: " + file);
    return true;
}

// A .json dropped onto the window opens it, exactly as the Open dialog would.
void App::actionDrop(HDROP drop) {
    // Take the name out and release the drop before anything modal: while an
    // HDROP is alive the window that started the drag is blocked, so putting
    // a "save your changes?" box in front of it would freeze Explorer too.
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::wstring file;
    if (count > 0) {
        // Asking for the length first, rather than assuming MAX_PATH, so a
        // deeply nested path is not silently truncated into a missing file.
        const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        file.resize(length);
        DragQueryFileW(drop, 0, file.data(), length + 1);
    }
    DragFinish(drop);
    if (file.empty()) return;

    if (!confirmDiscard()) return;
    if (!openPath(file)) return;
    // Only one CV is open at a time; say so rather than quietly ignoring
    // the rest of a multiple selection.
    if (count > 1)
        setStatus(status + L" (перетащено файлов: " + std::to_wstring(count) +
                  L", открыт первый)");
}

bool App::actionSave() {
    if (path.empty()) return actionSaveAs();
    std::string error;
    if (!save(path, form.collect(), error)) {
        MessageBoxW(hwnd, (L"Не удалось сохранить:\n" + widen(error)).c_str(), kAppName,
                    MB_ICONERROR);
        return false;
    }
    dirty = false;
    updateTitle();
    pushRecentFile(path);
    // The work is on disk now, so the recovery snapshot has nothing left to
    // rescue and must not be offered on the next start.
    clearAutosave();
    setStatus(L"Сохранено: " + path);
    return true;
}

bool App::actionSaveAs() {
    CV cv = form.collect();
    std::wstring suggestion = suggestedName(widen(cv.name), L".json");
    wchar_t file[MAX_PATH] = {};
    wcsncpy_s(file, suggestion.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Резюме (*.json)\0*.json\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"Сохранить резюме";
    std::wstring initial = directoryOf(path);
    if (!initial.empty()) ofn.lpstrInitialDir = initial.c_str();
    if (!GetSaveFileNameW(&ofn)) return false;

    path = file;
    return actionSave();
}

void App::actionExport() {
    if (!fontsReady) {
        MessageBoxW(hwnd, L"Шрифты не загружены, экспорт невозможен.", kAppName, MB_ICONERROR);
        return;
    }
    CV cv = form.collect();
    std::wstring suggestion = suggestedName(widen(cv.name), L".pdf");
    wchar_t file[MAX_PATH] = {};
    wcsncpy_s(file, suggestion.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PDF (*.pdf)\0*.pdf\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"Экспорт PDF";
    std::wstring initial = directoryOf(path);
    if (!initial.empty()) ofn.lpstrInitialDir = initial.c_str();
    if (!GetSaveFileNameW(&ofn)) return;

    std::string error;
    Document doc = ::cvb::layout(cv, fonts);
    if (!writePdf(doc, fonts, file, error)) {
        MessageBoxW(hwnd, (L"Не удалось записать PDF:\n" + widen(error)).c_str(), kAppName,
                    MB_ICONERROR);
        return;
    }
    setStatus(L"Экспортировано: " + std::wstring(file));
    ShellExecuteW(hwnd, L"open", file, nullptr, nullptr, SW_SHOWNORMAL);
}

// ------------------------------------------------------------ undo history

void App::updateHistoryButtons() {
    EnableWindow(undoButton, !undoStack.empty());
    EnableWindow(redoButton, !redoStack.empty());
}

void App::resetHistory(const CV& cv) {
    undoStack.clear();
    redoStack.clear();
    baseline = toJson(cv);
    updateHistoryButtons();
}

// Called on the snapshot debounce. Comparing the serialised form against the
// last recorded state is what makes this cheap to call speculatively: a burst
// of keystrokes collapses into one step, and a repaint that changed nothing
// records nothing.
void App::takeSnapshot() {
    const std::string current = toJson(form.collect());
    if (current == baseline) return;
    undoStack.push_back(baseline);
    if (undoStack.size() > kUndoDepth) undoStack.erase(undoStack.begin());
    baseline = current;
    // Editing after an undo abandons the future that was undone away, which is
    // what every editor does and what users expect.
    redoStack.clear();
    updateHistoryButtons();
}

void App::applySnapshot(const std::string& json) {
    CV cv;
    std::string error;
    if (!fromJson(json, cv, error)) return;
    form.setCV(cv);
    baseline = json;
    dirty = true;
    updateTitle();
    refreshPreview();
    updatePreviewControls();
    // Rebuilding the form announces itself as an edit. The pending snapshot
    // would compare equal to the baseline just set and do nothing, but killing
    // the timer means the history does not depend on that being true.
    KillTimer(hwnd, kSnapshotTimer);
    updateHistoryButtons();
}

void App::actionUndo() {
    // Whatever was typed in the last half second has not been recorded yet.
    // Without this the first Ctrl+Z would throw it away instead of undoing it.
    takeSnapshot();
    if (undoStack.empty()) {
        setStatus(L"Отменять нечего");
        return;
    }
    redoStack.push_back(baseline);
    const std::string state = undoStack.back();
    undoStack.pop_back();
    applySnapshot(state);
    setStatus(L"Отменено");
}

void App::actionRedo() {
    if (redoStack.empty()) {
        setStatus(L"Возвращать нечего");
        return;
    }
    undoStack.push_back(baseline);
    const std::string state = redoStack.back();
    redoStack.pop_back();
    applySnapshot(state);
    setStatus(L"Возвращено");
}

// ----------------------------------------------------------- recent files

void App::showRecentMenu() {
    const std::vector<std::wstring> files = recentFiles();
    HMENU menu = CreatePopupMenu();
    if (files.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Пока ничего не открывали");
    } else {
        for (size_t i = 0; i < files.size(); ++i) {
            // A single & in a path would be eaten as a mnemonic and underline
            // the character after it, so every one is doubled.
            std::wstring label = std::to_wstring(i + 1) + L". " + files[i];
            for (size_t at = label.find(L'&'); at != std::wstring::npos;
                 at = label.find(L'&', at + 2))
                label.insert(at, 1, L'&');
            AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(IDM_RECENT_FIRST + i),
                        label.c_str());
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_RECENT_CLEAR, L"Очистить список");
    }

    // Dropped from the bottom-left of the button it belongs to, the way a
    // split button's menu is expected to appear.
    RECT button{};
    GetWindowRect(fileButtons[1], &button);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, button.left, button.bottom, 0, hwnd,
                   nullptr);
    DestroyMenu(menu);
}

void App::openRecent(int index) {
    const std::vector<std::wstring> files = recentFiles();
    if (index < 0 || static_cast<size_t>(index) >= files.size()) return;
    if (!confirmDiscard()) return;
    openPath(files[static_cast<size_t>(index)]);
}

// --------------------------------------------------------------- recovery

void App::writeRecovery() {
    // Only unsaved work is worth a snapshot; a saved document is already on
    // disk in a better place than the recovery file.
    if (!dirty) return;
    writeAutosave(form.collect(), path);
}

// ------------------------------------------------------------------ print

void App::actionPrint() {
    if (!fontsReady) {
        MessageBoxW(hwnd, L"Шрифты не загружены, печать невозможна.", kAppName, MB_ICONERROR);
        return;
    }
    const std::wstring title = path.empty() ? std::wstring(kAppName) : fileNameOf(path);
    std::wstring error;
    switch (printDocument(hwnd, ::cvb::layout(form.collect(), fonts), fonts, title, error)) {
        case PrintResult::Printed:
            setStatus(L"Отправлено на печать: " + title);
            break;
        case PrintResult::Cancelled:
            break;
        case PrintResult::Failed:
            MessageBoxW(hwnd, (L"Не удалось напечатать:\n" + error).c_str(), kAppName,
                        MB_ICONERROR);
            break;
    }
}


void App::layout() {
    if (!form.hwnd() || !preview.hwnd()) return;  // a WM_SIZE that beat WM_CREATE
    RECT client{};
    GetClientRect(hwnd, &client);
    const int margin = scale(8);
    const int gap = scale(6);
    const int barHeight = scale(kToolbarHeight);
    const int buttonHeight = scale(28);
    const int small = scale(kSmallButton);

    const int statusHeight = scale(kStatusHeight);

    int x = margin;
    const int y = (barHeight - buttonHeight) / 2;
    for (size_t i = 0; i < std::size(kFileButtons); ++i) {
        int width = scale(kFileButtons[i].width);
        SetWindowPos(fileButtons[i], nullptr, x, y, width, buttonHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        x += width + gap;
    }

    // Undo and redo sit with the file actions: they act on the document.
    for (HWND control : {undoButton, redoButton}) {
        SetWindowPos(control, nullptr, x, y, small, buttonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        x += small + gap;
    }

    // The drop-down sits apart from the file actions: it changes the app, not
    // the document. Its height is the extent of the open list, not of the box.
    x += gap;
    SetWindowPos(theme, nullptr, x, y, scale(kThemeWidth), buttonHeight * 6,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Preview controls hug the right edge of the same bar.
    int right = client.right - margin;
    auto placeRight = [&](HWND control, int width) {
        right -= width;
        SetWindowPos(control, nullptr, right, y, width, buttonHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        right -= gap;
    };
    placeRight(zoomIn, small);
    placeRight(zoomLabel, scale(56));
    placeRight(zoomOut, small);
    right -= gap;
    placeRight(nextPage, small);
    placeRight(pageLabel, scale(90));
    placeRight(prevPage, small);

    const int top = barHeight;
    const int height = std::max(scale(80), static_cast<int>(client.bottom) - top - statusHeight);
    const int split = static_cast<int>(client.right) / 2;
    SetWindowPos(form.hwnd(), nullptr, 0, top, split, height, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(preview.hwnd(), nullptr, split, top, static_cast<int>(client.right) - split,
                 height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void App::build(HINSTANCE instance) {
    dpi = GetDpiForWindow(hwnd);
    if (!dpi) dpi = 96;
    uiFont = makeUiFont(dpi);
    background = CreateSolidBrush(ui().window);

    for (size_t i = 0; i < std::size(kFileButtons); ++i)
        fileButtons[i] = makeToolButton(hwnd, instance, kFileButtons[i].id, kFileButtons[i].label,
                                        uiFont);
    // Split button: the left half opens a dialog, the arrow drops the list of
    // recent files.
    SetWindowLongPtrW(fileButtons[1], GWL_STYLE,
                      GetWindowLongPtrW(fileButtons[1], GWL_STYLE) | BS_SPLITBUTTON);
    undoButton = makeToolButton(hwnd, instance, IDM_UNDO, L"↶", uiFont);
    redoButton = makeToolButton(hwnd, instance, IDM_REDO, L"↷", uiFont);
    prevPage = makeToolButton(hwnd, instance, IDC_PREV_PAGE, L"‹", uiFont);
    pageLabel = makeToolLabel(hwnd, instance, IDC_PAGE_LABEL, L"Стр. 1 / 1", uiFont);
    nextPage = makeToolButton(hwnd, instance, IDC_NEXT_PAGE, L"›", uiFont);
    zoomOut = makeToolButton(hwnd, instance, IDC_ZOOM_OUT, L"−", uiFont);
    zoomLabel = makeToolLabel(hwnd, instance, IDC_ZOOM_LABEL, L"100 %", uiFont);
    zoomIn = makeToolButton(hwnd, instance, IDC_ZOOM_IN, L"+", uiFont);

    theme = CreateWindowExW(0, L"COMBOBOX", L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 10, 200,
                            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_THEME)),
                            instance, nullptr);
    SendMessageW(theme, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
    for (const wchar_t* name : kThemeNames)
        SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    SendMessageW(theme, CB_SETCURSEL, static_cast<WPARAM>(themeMode()), 0);

    form.create(hwnd, instance, [this] { scheduleRefresh(); });
    preview.create(hwnd, instance);
    preview.onStateChanged = [this] { updatePreviewControls(); };
    applyTheme();

    std::string error;
    fontsReady = fonts.loadSystem(error);
    if (fontsReady) {
        preview.setFonts(&fonts);
    } else {
        preview.setError(L"Не удалось загрузить шрифт:\n" + widen(error));
    }

    // Unsaved work from a session that ended badly outranks anything else we
    // might open, so it is offered before the sample is even looked for.
    Recovery recovery;
    bool restored = false;
    if (findRecovery(recovery)) {
        const std::wstring what = recovery.origin.empty()
                                      ? std::wstring(L"несохранённое резюме")
                                      : fileNameOf(recovery.origin);
        const std::wstring question =
            L"Прошлый сеанс завершился, не сохранив изменения.\n\n"
            L"Восстановить " + what + L"?";
        if (MessageBoxW(hwnd, question.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION) == IDYES) {
            loadCV(recovery.cv, recovery.origin);
            // Restored work is unsaved by definition: the title has to say so,
            // and closing now has to ask.
            dirty = true;
            updateTitle();
            restored = true;
        } else {
            clearAutosave();
        }
    }

    if (restored) {
        setStatus(L"Восстановлено из автосохранения");
    } else {
        // Start on the sample if one is lying about, otherwise blank. The build
        // directory, the working directory and the project root are all plausible
        // places for it depending on how the app was started.
        CV startup = emptyCV();
        std::string ignored;
        bool loaded = false;
        for (const std::wstring& candidate : {exeDirectory() + L"\\sample_cv.json",
                                              std::wstring(L"sample_cv.json"),
                                              exeDirectory() + L"\\..\\sample_cv.json"}) {
            if (load(candidate, startup, ignored)) {
                loaded = true;
                break;
            }
        }
        if (!loaded) startup = emptyCV();
        loadCV(startup, std::wstring());
        setStatus(loaded ? L"Загружен пример sample_cv.json" : L"Готово");
    }

    updateHistoryButtons();
    updatePreviewControls();
    SetTimer(hwnd, kAutosaveTimer, kAutosaveInterval, nullptr);
}

// ------------------------------------------------------------- window proc

LRESULT CALLBACK mainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = appOf(window);
    switch (message) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            App* created = static_cast<App*>(cs->lpCreateParams);
            created->hwnd = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
            created->build(cs->hInstance);
            created->layout();
            // Explorer will not offer a drop target until a window asks for it.
            DragAcceptFiles(window, TRUE);
            return 0;
        }
        case WM_NOTIFY: {
            // A split button announces its arrow through WM_NOTIFY rather than
            // as a command, which is what keeps the two halves distinguishable.
            if (!app) break;
            const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header && header->code == BCN_DROPDOWN &&
                header->idFrom == static_cast<UINT_PTR>(IDM_OPEN)) {
                app->showRecentMenu();
                return 0;
            }
            break;
        }
        case WM_DROPFILES:
            if (app) app->actionDrop(reinterpret_cast<HDROP>(wParam));
            return 0;
        case WM_SIZE:
            if (app) app->layout();
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = app ? app->dpi : 96;
            // Wide enough that the toolbar never overlaps itself — but never
            // wider than the screen, or a small high-dpi display would be left
            // with a window it cannot fit. The two groups of controls need
            // 1356 units side by side once Print, Undo and Redo are counted;
            // the rest is slack.
            LONG width = scaled(1380, dpi);
            LONG height = scaled(600, dpi);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof monitor;
            if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
                width = std::min(width, monitor.rcWork.right - monitor.rcWork.left);
                height = std::min(height, monitor.rcWork.bottom - monitor.rcWork.top);
            }
            info->ptMinTrackSize.x = width;
            info->ptMinTrackSize.y = height;
            return 0;
        }
        case WM_ERASEBKGND: {
            if (!app) break;
            RECT client{};
            GetClientRect(window, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, app->background);
            return 1;
        }
        case WM_PRINTCLIENT: {
            // A themed control asks its parent for the pixels under its own
            // rounded corners through DrawThemeParentBackground, which sends
            // this. Left to DefWindowProc it falls back to COLOR_BTNFACE and
            // outlines every button in light grey.
            if (!app) break;
            RECT client{};
            GetClientRect(window, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, app->background);
            return 0;
        }
        case WM_PAINT: {
            if (!app) break;
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(window, &ps);
            app->paintChrome(dc);
            EndPaint(window, &ps);
            return 0;
        }
        case WM_SETTINGCHANGE: {
            // Windows announces a colour-scheme change this way; nothing else
            // tells an app that "Choose your mode" has been flipped.
            const wchar_t* area = reinterpret_cast<const wchar_t*>(lParam);
            if (!app || !area || wcscmp(area, L"ImmersiveColorSet") != 0) break;
            if (themeMode() != ThemeMode::System) break;
            refreshTheme();
            app->applyTheme();
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            // Opaque, in the toolbar's own colour. The page and zoom labels
            // change text as the user pages or zooms, and a transparent static
            // never wipes what it drew last time: the old digits stay under the
            // new ones and ClearType turns the overlap into coloured mush.
            HDC dc = reinterpret_cast<HDC>(wParam);
            if (!app) break;
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, ui().window);
            SetTextColor(dc, ui().text);
            return reinterpret_cast<LRESULT>(app->background);
        }
        case WM_TIMER:
            if (!app) return 0;
            if (wParam == kRefreshTimer) {
                KillTimer(window, kRefreshTimer);
                app->refreshPreview();
            } else if (wParam == kSnapshotTimer) {
                // One-shot: re-armed by the next edit, so an idle app does no
                // work at all.
                KillTimer(window, kSnapshotTimer);
                app->takeSnapshot();
            } else if (wParam == kAutosaveTimer) {
                app->writeRecovery();
            }
            return 0;
        case WM_COMMAND: {
            if (!app) break;
            if (LOWORD(wParam) == IDC_THEME) {
                if (HIWORD(wParam) != CBN_SELCHANGE) break;
                LRESULT choice = SendMessageW(app->theme, CB_GETCURSEL, 0, 0);
                if (choice == CB_ERR) return 0;
                setThemeMode(static_cast<ThemeMode>(choice));
                app->applyTheme();
                return 0;
            }
            switch (LOWORD(wParam)) {
                case IDM_NEW: app->actionNew(); return 0;
                case IDM_OPEN: app->actionOpen(); return 0;
                case IDM_SAVE: app->actionSave(); return 0;
                case IDM_SAVEAS: app->actionSaveAs(); return 0;
                case IDM_EXPORT: app->actionExport(); return 0;
                case IDM_PRINT: app->actionPrint(); return 0;
                case IDM_UNDO: app->actionUndo(); return 0;
                case IDM_REDO: app->actionRedo(); return 0;
                case IDM_RECENT_CLEAR: clearRecentFiles(); return 0;
                case IDM_EXIT: SendMessageW(window, WM_CLOSE, 0, 0); return 0;
                case IDC_PREV_PAGE: app->preview.setPage(app->preview.page() - 1); return 0;
                case IDC_NEXT_PAGE: app->preview.setPage(app->preview.page() + 1); return 0;
                case IDC_ZOOM_OUT: app->preview.setZoom(app->preview.zoom() - 10); return 0;
                case IDC_ZOOM_IN: app->preview.setZoom(app->preview.zoom() + 10); return 0;
                default:
                    // The recent-files popup hands back a command in its own
                    // reserved range.
                    if (LOWORD(wParam) >= IDM_RECENT_FIRST && LOWORD(wParam) <= IDM_RECENT_LAST) {
                        app->openRecent(LOWORD(wParam) - IDM_RECENT_FIRST);
                        return 0;
                    }
                    break;
            }
            switch (LOWORD(wParam)) {
                case IDOK:
                case IDCANCEL:
                    // IsDialogMessage turns Enter/Esc into these; swallow them
                    // so the default handler does not beep.
                    return 0;
                default: break;
            }
            break;
        }
        case WM_DPICHANGED: {
            if (!app) break;
            app->dpi = HIWORD(wParam);
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            HFONT old = app->uiFont;
            app->uiFont = makeUiFont(app->dpi);
            for (HWND control : {app->prevPage, app->nextPage, app->pageLabel, app->zoomOut,
                                 app->zoomIn, app->zoomLabel, app->theme, app->undoButton,
                                 app->redoButton})
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app->uiFont), TRUE);
            for (HWND control : app->fileButtons)
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app->uiFont), TRUE);
            DeleteObject(old);
            app->form.setDpi(app->dpi);
            app->layout();
            return 0;
        }
        case WM_CLOSE:
            if (app && !app->confirmDiscard()) return 0;
            // Closing on purpose - saved, or knowingly discarded - means there
            // is nothing left to recover on the next start.
            clearAutosave();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace
}  // namespace cvb

// WinMain, not wWinMain: the app never reads its command line, so there is no
// reason to pull in the wide entry point and -municode.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Before any window exists: uxtheme decides how to draw a control the
    // first time it is created.
    cvb::initTheme();

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof controls;
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof cls;
    cls.lpfnWndProc = cvb::mainProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = L"CVBuilderMain";
    // LoadIcon only ever returns the large size; the title bar and the
    // Alt+Tab list want the 16 px drawing, which LoadImage can ask for.
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(cvb::IDI_APP));
    cls.hIconSm = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(cvb::IDI_APP), IMAGE_ICON,
                   GetSystemMetrics(SM_CXSMICON),
                   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    RegisterClassExW(&cls);

    cvb::App app;
    HWND window = CreateWindowExW(0, L"CVBuilderMain", L"CV Builder",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 1500, 950, nullptr, nullptr, instance, &app);
    if (!window) return 1;
    ShowWindow(window, show);
    UpdateWindow(window);

    ACCEL accelerators[] = {
        {FVIRTKEY | FCONTROL, 'N', cvb::IDM_NEW},
        {FVIRTKEY | FCONTROL, 'O', cvb::IDM_OPEN},
        {FVIRTKEY | FCONTROL, 'S', cvb::IDM_SAVE},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', cvb::IDM_SAVEAS},
        {FVIRTKEY | FCONTROL, 'E', cvb::IDM_EXPORT},
        {FVIRTKEY | FCONTROL, 'P', cvb::IDM_PRINT},
        {FVIRTKEY | FCONTROL, 'Z', cvb::IDM_UNDO},
        {FVIRTKEY | FCONTROL, 'Y', cvb::IDM_REDO},
        // Ctrl+Shift+Z is the other half of the world's redo shortcut.
        {FVIRTKEY | FCONTROL | FSHIFT, 'Z', cvb::IDM_REDO},
        {FVIRTKEY | FCONTROL, 'Q', cvb::IDM_EXIT},
    };
    HACCEL table = CreateAcceleratorTableW(accelerators, static_cast<int>(std::size(accelerators)));

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (table && TranslateAcceleratorW(window, table, &message)) continue;
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
