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

namespace cvb {
namespace {

const wchar_t* const kAppName = L"CV Builder";

enum : int {
    IDM_NEW = 100, IDM_OPEN, IDM_SAVE, IDM_SAVEAS, IDM_EXPORT, IDM_EXIT,
    IDC_PREV_PAGE = 200, IDC_NEXT_PAGE, IDC_ZOOM_OUT, IDC_ZOOM_IN,
    IDC_PAGE_LABEL, IDC_ZOOM_LABEL, IDC_THEME,
};

constexpr UINT kRefreshTimer = 1;
constexpr UINT kRefreshDelay = 200;  // ms of quiet before the preview redraws

constexpr int kToolbarHeight = 40;
constexpr int kToolButtonWidth = 118;
constexpr int kSmallButton = 30;
constexpr int kThemeWidth = 160;
constexpr int kStatusHeight = 24;

// In the order of ThemeMode, so the selection index is the mode.
const wchar_t* const kThemeNames[] = {L"Как в системе", L"Светлая", L"Тёмная"};

struct ToolButton {
    int id;
    const wchar_t* label;
    int width;
};

const ToolButton kFileButtons[] = {
    {IDM_NEW, L"Новый", kToolButtonWidth},
    {IDM_OPEN, L"Открыть…", kToolButtonWidth},
    {IDM_SAVE, L"Сохранить", kToolButtonWidth},
    {IDM_SAVEAS, L"Сохранить как…", kToolButtonWidth + 34},
    {IDM_EXPORT, L"Экспорт PDF…", kToolButtonWidth + 16},
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
    HWND fileButtons[5] = {};
    HWND theme = nullptr;
    HWND prevPage = nullptr, nextPage = nullptr, pageLabel = nullptr;
    HWND zoomOut = nullptr, zoomIn = nullptr, zoomLabel = nullptr;
    HFONT uiFont = nullptr;
    HBRUSH background = nullptr;

    FormPane form;
    PreviewPane preview;
    FontSet fonts;
    bool fontsReady = false;

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
    bool actionSave();
    bool actionSaveAs();
    void actionExport();
    bool confirmDiscard();
    void loadCV(const CV& cv, const std::wstring& from);
};

App* appOf(HWND window) {
    return reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

HWND makeToolButton(HWND parent, HINSTANCE instance, int id, const wchar_t* label, HFONT font) {
    HWND button = CreateWindowExW(0, L"BUTTON", label,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
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
}

void App::loadCV(const CV& cv, const std::wstring& from) {
    form.setCV(cv);
    path = from;
    dirty = false;
    updateTitle();
    refreshPreview();
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

    CV cv;
    std::string error;
    if (!load(file, cv, error)) {
        MessageBoxW(hwnd, (L"Не удалось открыть файл:\n" + widen(error)).c_str(), kAppName,
                    MB_ICONERROR);
        return;
    }
    loadCV(cv, file);
    setStatus(L"Открыто: " + std::wstring(file));
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
    for (size_t i = 0; i < 5; ++i) {
        int width = scale(kFileButtons[i].width);
        SetWindowPos(fileButtons[i], nullptr, x, y, width, buttonHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        x += width + gap;
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

    for (size_t i = 0; i < 5; ++i)
        fileButtons[i] = makeToolButton(hwnd, instance, kFileButtons[i].id, kFileButtons[i].label,
                                        uiFont);
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
    updatePreviewControls();
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
            return 0;
        }
        case WM_SIZE:
            if (app) app->layout();
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = app ? app->dpi : 96;
            // Wide enough that the toolbar never overlaps itself — but never
            // wider than the screen, or a small high-dpi display would be left
            // with a window it cannot fit.
            LONG width = scaled(1180, dpi);
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
            if (app && wParam == kRefreshTimer) {
                KillTimer(window, kRefreshTimer);
                app->refreshPreview();
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
                case IDM_EXIT: SendMessageW(window, WM_CLOSE, 0, 0); return 0;
                case IDC_PREV_PAGE: app->preview.setPage(app->preview.page() - 1); return 0;
                case IDC_NEXT_PAGE: app->preview.setPage(app->preview.page() + 1); return 0;
                case IDC_ZOOM_OUT: app->preview.setZoom(app->preview.zoom() - 10); return 0;
                case IDC_ZOOM_IN: app->preview.setZoom(app->preview.zoom() + 10); return 0;
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
                                 app->zoomIn, app->zoomLabel, app->theme})
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
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
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
