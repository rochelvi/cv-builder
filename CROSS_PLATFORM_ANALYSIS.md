# CV Builder — анализ перед переносом на Windows / Linux / macOS

Документ написан **до** любых изменений кода. Ссылки на строки соответствуют
состоянию дерева на коммите `a7dccb7` («Preparing for new release»).

Итог одной фразой: проект устроен гораздо удачнее, чем обычно бывает при
переносе Win32-приложений. Ядро (JSON, модель, раскладка, TrueType, PDF) уже
не знает про UI, а `layout.h` уже выдаёт нейтральное дерево отрисовки, которое
требует ТЗ. Переписывать ядро не нужно. Работа сводится к четырём вещам:
вынести из ядра `std::wstring`-пути и Win32-поиск шрифтов, устранить
зависимость ядра от локали, собрать всё современным CMake и написать один
кроссплатформенный фронтенд поверх неизменного ядра.

---

## 1. Текущая архитектура

### 1.1. Что есть на диске

| Файл | Строк | Слой | Платформа |
| --- | --- | --- | --- |
| `src/json.h/.cpp` | 359 | core | **portable** |
| `src/model.h/.cpp` | 456 | core | portable, кроме файлового I/O и UI-строк |
| `src/layout.h/.cpp` | 461 | core (layout + render tree) | **portable** |
| `src/font.h/.cpp` | 508 | core (TTF) + platform (поиск шрифта) | смешанный |
| `src/pdf.h/.cpp` | 366 | render backend (PDF) | portable, кроме записи файла |
| `src/ui.h` | 219 | UI API | Win32 (`windows.h`, `HWND`, `COLORREF`, `FILETIME`) |
| `src/app.cpp` | 924 | UI: окно, тулбар, файловые операции, undo | Win32 |
| `src/form.cpp` | 1248 | UI: панель редактора | Win32 / Common Controls 6 / GDI |
| `src/preview.cpp` | 348 | render backend (экран) | Direct2D + DirectWrite |
| `src/theme.cpp` | 163 | platform: тема, реестр | Win32 + uxtheme + DWM |
| `src/settings.cpp` | 164 | platform: настройки, автосейв | Win32 + реестр + shell |
| `src/print.cpp` | 165 | render backend (принтер) | Win32 comdlg + GDI |
| `src/cli.cpp` | 103 | приложение (консоль) | Win32 (argv, консольная кодовая страница) |
| `res/*.rc`, `*.manifest`, `*.ico` | — | ресурсы | Windows-only |
| `Makefile`, `build.ps1`, `run.ps1`, `release.ps1` | — | сборка | mingw-w64 из WSL |

Всего ~5.5 тыс. строк C++. Ноль внешних зависимостей.

### 1.2. Фактические зависимости между слоями

```
                 WinMain / app.cpp
                        │
        ┌───────────────┼────────────────┬──────────────┐
        v               v                v              v
    form.cpp        preview.cpp      print.cpp     theme.cpp
   (Win32 UI)      (D2D/DWrite)      (comdlg/GDI)   settings.cpp
        │               │                │              │
        └───────────────┴────────┬───────┴──────────────┘
                                 v
                              ui.h  (Win32 API проекта)
                                 │
        ┌────────────────────────┼────────────────────────┐
        v                        v                        v
    model.h                  layout.h                  font.h
   (модель CV)          (раскладка + Document)      (TrueType)
        │                        │                        │
        └────────► json.h        └────────► pdf.h ◄───────┘
```

**Главное наблюдение: направление зависимостей уже правильное.** Ни один файл
ядра не включает `ui.h` и ничего не знает об окнах. Единственное «протекание»
платформы в ядро — типы и функции, перечисленные в §2.2.

### 1.3. Document уже является Render Tree

`layout.h:46-57` описывает то, что ТЗ просит построить:

```cpp
struct Page   { RectItem background; std::vector<LineItem> lines; std::vector<TextItem> texts; };
struct Document { double width, height; std::vector<Page> pages; std::string title, author; };
```

Координаты — пункты PDF от левого верхнего угла листа, `TextItem::y` — базовая
линия, текст — UTF-8. Никаких `HDC`, никаких пикселей. `Document` уже
потребляют три независимых бэкенда:

* `pdf.cpp` — PDF 1.4, Identity-H, сабсет шрифта;
* `preview.cpp` — Direct2D `DrawGlyphRun`;
* `print.cpp` — GDI `TextOutW`.

Функции вида `void renderResume(ID2D1RenderTarget*)`, от которых предостерегает
ТЗ, в проекте **нет**. Требование «`RenderTree buildRenderTree(const
ResumeDocument&)`» фактически уже выполнено: это `cvb::layout(const CV&, const
FontSet&)` (`layout.h:63`). Ломать это ради формального переименования нельзя.

### 1.4. Как связаны preview и PDF

Ключевое свойство проекта: экран — не приближение экспорта, а тот же экспорт в
другом масштабе. `layout` меряет строки через собственный TrueType-парсер
(`Font::measure`), PDF вставляет сабсет **того же файла** и пишет те же glyph
id, preview отдаёт DirectWrite тот же файл (`preview.cpp:89`,
`CreateFontFileReference(font.path())`) и те же glyph id с теми же advance
(`preview.cpp:134-153`). Любой новый экранный бэкенд обязан сохранить именно
это: рисовать по **glyph id из нашего парсера**, а не «строку шрифтом с таким
именем». Это сразу отсекает часть UI-вариантов (§6).

Исключение — принтер: `print.cpp:104` берёт GDI-шрифт по **имени семейства**
(`FontSet::family()`) и печатает `TextOutW`, то есть шейпинг и кернинг делает
GDI. Это единственное место, где бэкенды расходятся (см. риск R5).

---

## 2. Найденные зависимости от Win32 / Direct2D / DirectWrite

### 2.1. Сводка по категориям

| Категория | Где | Что используется |
| --- | --- | --- |
| Окна, сообщения, тулбар | `app.cpp`, `form.cpp`, `preview.cpp` | `CreateWindowExW`, оконные процедуры, `WM_*`, `SetTimer`, акселераторы, `IsDialogMessage` |
| Контролы | `form.cpp`, `app.cpp` | `EDIT`, `BUTTON`, `COMBOBOX`, `STATIC`, `BS_OWNERDRAW`, `EM_SETCUEBANNER`, `BS_SPLITBUTTON`, `WM_CTLCOLOR*`, `WM_DRAWITEM` |
| Отрисовка UI | `form.cpp:911-956` | GDI: `RoundRect`, `CreateCompatibleBitmap`, `BitBlt`, `DrawTextW` |
| Отрисовка документа | `preview.cpp` | `ID2D1Factory`, `ID2D1HwndRenderTarget`, `IDWriteFactory`, `IDWriteFontFace`, `DWRITE_GLYPH_RUN`, `DrawGlyphRun` |
| Печать | `print.cpp` | `PrintDlgW`, `StartDoc/StartPage/EndPage/EndDoc`, `CreateFontIndirectW`, `TextOutW`, `GetDeviceCaps` |
| Диалоги | `app.cpp:337-467`, `form.cpp:981-994` | `GetOpenFileNameW`, `GetSaveFileNameW`, `ChooseColorW`, `MessageBoxW` |
| Реестр | `theme.cpp`, `settings.cpp` | `RegGetValueW`, `RegCreateKeyExW`, `RegSetValueExW`, `REG_MULTI_SZ`, `HKCU\Software\CV Builder` |
| Каталоги ОС | `settings.cpp:146-155`, `app.cpp:98` | `SHGetKnownFolderPath(FOLDERID_LocalAppData)`, `CreateDirectoryW`, `GetModuleFileNameW` |
| Файловая система | `settings.cpp:96-100,116,130,166` | `GetFileAttributesExW`, `FILETIME`, `CompareFileTime`, `GetFullPathNameW`, `DeleteFileW` |
| Тема ОС | `theme.cpp` | недокументированные ординалы uxtheme 133/135, `DwmSetWindowAttribute`, `RtlGetNtVersionNumbers`, `WM_SETTINGCHANGE`/`ImmersiveColorSet` |
| DPI | `app.cpp:966`, `form.cpp:40` | `SetProcessDpiAwarenessContext`, `GetDpiForWindow`, `WM_DPICHANGED`, `MulDiv` |
| Drag & drop, shell | `app.cpp:369-392,466` | `DragAcceptFiles`, `HDROP`, `DragQueryFileW`, `ShellExecuteW` |
| Кодировки | `form.cpp:22-38`, `cli.cpp:21` | `MultiByteToWideChar`, `WideCharToMultiByte`, `SetConsoleOutputCP` |
| Точка входа | `app.cpp:965`, `cli.cpp:69` | `WinMain`, `CommandLineToArgvW` |

Всего ~2 850 строк платформенного кода (`app` + `form` + `preview` + `theme` +
`settings` + `print` + `ui.h`) против ~2 150 строк ядра.

### 2.2. Протечки платформы в ядро (это и надо убрать)

Их немного и все точечные:

1. **`_wfopen` в ядре.** `model.cpp:161,174`, `font.cpp:82`, `pdf.cpp:381`.
   MSVC-расширение, на Linux/macOS не существует.
2. **`std::wstring` в публичном API ядра.** `model.h:124-125`
   (`load`/`save`), `pdf.h:20` (`writePdf`), `font.h:19,22`
   (`loadFromFile`, `path`). UTF-16 — свойство Windows, а не документа.
3. **`#include <windows.h>` в `font.cpp:3`** — только ради `FontSet::loadSystem`.
4. **Поиск системного шрифта внутри класса ядра.** `font.cpp:444-472`:
   `GetWindowsDirectoryW`, `\Fonts\`, жёсткий список `arial.ttf`, `segoeui.ttf`, …
5. **UI-строки в модели.** `model.h:28,81`, `model.cpp:15-40`:
   `kThemeLabels`, `kSectionEditorNames` — это `const wchar_t*` подписи
   редактора, лежащие в ядре. Плюс `Preset::name` (`model.h:37`) — тоже
   `const wchar_t*`.
6. **`FontSet::family()`** (`font.h:95`) существует исключительно ради
   GDI-принтера. Ядру это поле не нужно.

### 2.3. Скрытая, но критичная непортируемость: локаль

Это самая опасная находка аудита, и она не связана с Win32.

* `json.cpp:209` — `std::strtod` уважает `LC_NUMERIC`.
* `json.cpp:245-247` — `snprintf("%.10g")`.
* `pdf.cpp:16` — `snprintf("%.3f")` в функции `num()`, через которую проходит
  **каждое** число в PDF.
* `cli.cpp:48-58` — `fprintf("%.3f")` в `--dump-ops`.

Сейчас это не проявляется: приложение никогда не вызывает `setlocale`, поэтому
процесс живёт в локали `"C"`. Но любой кроссплатформенный тулкит (в частности
Qt при создании `QApplication`) вызывает `setlocale(LC_ALL, "")`. В локали с
запятой как десятичным разделителем (`ru_RU.UTF-8`, `de_DE.UTF-8`, — то есть у
целевой аудитории приложения) произойдёт следующее:

* PDF получит `MediaBox [0 0 595,276 841,89]` и станет **невалидным**;
* JSON-числа (`order`) начнут писаться с запятой и не будут читаться обратно;
* `strtod("1.5")` вернёт `1`.

Вывод: ядро обязано стать locale-independent (`std::to_chars`/`from_chars` или
ручное форматирование) **до** появления любого тулкита. Это идёт в первые шаги
плана и покрывается тестом, который прогоняет ядро под `de_DE.UTF-8`.

---

## 3. Что уже переносимо

* `src/json.*` — целиком (после правки локали).
* `src/layout.*` — целиком. Вся геометрия шаблона, переносы строк, разбиение на
  страницы, `parseColor`. Ни одного платформенного включения.
* `src/pdf.*` — вся генерация PDF, кроме последней функции записи файла.
* `src/font.*` — разбор TrueType (`head`/`maxp`/`hhea`/`hmtx`/`OS/2`/`cmap`
  форматов 4 и 12/`loca`/`glyf`), шейпинг UTF-8 → glyph id, измерение,
  сабсеттинг с пересчётом контрольных сумм. Непортируемы только чтение файла и
  `loadSystem`.
* `src/model.*` — модель, пресеты, чтение/запись JSON, починка блока
  `sections` для старых файлов. Непортируемы I/O и UI-подписи.

То есть **~2 000 из ~2 150 строк ядра переносятся без изменения логики.**
Формат JSON и формат PDF не меняются вообще: это гарантирует совместимость
файлов между ОС и с прошлыми версиями (п. 4 и 15 ТЗ) по построению.

---

## 4. Что требует абстракции

| Что | Кто пользуется сейчас | Нужный интерфейс |
| --- | --- | --- |
| Чтение/запись файла | ядро | `core/file.h`: `readFile/writeFile(const std::filesystem::path&)` |
| Пути (exe, документы, данные приложения, автосейв) | `app.cpp`, `settings.cpp` | `platform/paths.h` |
| Поиск и загрузка системного шрифта | `font.cpp:444` | `platform/font_source.h`: список кандидатов → ядро парсит |
| Постоянные настройки (тема, recent files, автосейв-origin) | `theme.cpp`, `settings.cpp` | `platform/settings.h` (ключ/значение + список) |
| Файловые метаданные (mtime для «свежести» автосейва) | `settings.cpp:96` | `std::filesystem::last_write_time` |
| Диалоги: открыть/сохранить/цвет/вопрос | `app.cpp`, `form.cpp` | часть UI-тулкита |
| Открыть готовый PDF во внешней программе | `app.cpp:466` | часть UI-тулкита |
| Тема ОС (светлая/тёмная) | `theme.cpp` | часть UI-тулкита + один хук для тёмной рамки окна на Windows |
| Печать | `print.cpp` | `render/painter` + принтер тулкита |
| Экранная отрисовка `Document` | `preview.cpp` | `render/painter` над абстракцией холста тулкита |

Обратите внимание, чего в списке **нет**: layout, PDF, JSON, TrueType,
модель. Их абстрагировать не нужно и не следует.

---

## 5. Целевая архитектура

```
                          Application (main window, undo, files)
                                        │
                                        v
                                  Resume Model            core, без ОС
                                        │
                                        v
                                 Layout Engine            core, без ОС
                                        │
                                        v
                            Document (Render Tree)         пункты, UTF-8
                            ╱          │          ╲
                           v           v           v
                  Screen Renderer  Print Renderer  PDF Renderer  ← общий обход
                           │           │              (core)
                           └─────┬─────┘
                                 v
                          Platform Backend
                    ╱            │            ╲
                   v             v             v
               Windows         Linux          macOS
```

Дерево каталогов:

```
src/
├── core/                 # resume_core: ни ОС, ни UI, ни локали
│   ├── json.h/.cpp
│   ├── model.h/.cpp      #   без wchar_t-подписей и без wstring-путей
│   ├── layout.h/.cpp     #   не меняется
│   ├── font.h/.cpp       #   только TrueType: parse / shape / measure / subset
│   ├── pdf.h/.cpp        #   не меняется, кроме типа пути
│   ├── file.h/.cpp       #   новое: I/O через std::filesystem
│   └── numeric.h         #   новое: locale-independent числа
├── platform/             # resume_platform: интерфейс + реализация по ОС
│   ├── paths.h           +  paths_windows.cpp / paths_linux.cpp / paths_macos.cpp
│   ├── font_source.h     +  font_source_windows.cpp / _linux.cpp / _macos.cpp
│   └── settings.h        +  реализация
├── render/               # resume_render: один обход Document для экрана и печати
│   └── document_painter.h/.cpp
├── ui/                   # resume_ui: один фронтенд на три ОС
├── ui_win32/             # эталон: нынешние app/form/preview/theme/print/ui.h
└── cli/                  # cvcli, теперь портируемый
tests/                    # resume_tests: ядро без GUI
```

Правило, которое проверяется автоматически (grep в CI): в `src/core` и
`src/render` не встречается ни `windows.h`, ни `d2d1.h`, ни `dwrite.h`, ни
`#ifdef _WIN32`. Платформенный выбор живёт в CMake, а не в препроцессоре.

---

## 6. Предлагаемый UI-подход: Qt 6 Widgets

### 6.1. Обязательное требование к кандидату

Фронтенд обязан уметь нарисовать `Document` как **последовательность glyph id с
явными позициями и advance, взятыми из конкретного .ttf-файла**, который уже
разобрало ядро. Иначе теряется главное свойство продукта (preview == PDF), и
приложение превращается в «примерно похожий» просмотрщик.

### 6.2. Сравнение

| Вариант | Отрисовка glyph id | Редактор (сотни полей ввода) | Старт / отзывчивость | Единый вид | Объём работы |
| --- | --- | --- | --- | --- | --- |
| **Qt 6 Widgets** | `QRawFont` + `QGlyphRun` + `QPainter::drawGlyphRun` — прямой аналог `DrawGlyphRun` | `QLineEdit`/`QPlainTextEdit`: выделение, IME, системный undo, доступность | ~30–60 мс на `QApplication`, отрисовка через растеризатор Qt | да, один стиль на всех ОС | средний |
| Qt 6 Quick (QML) | `QQuickPaintedItem`/`Text` — glyph run только через C++-элемент | всю форму надо переписать на QML + мост в C++ | + JS-движок и сцен-граф GPU | да | высокий |
| SDL3 + Dear ImGui | **нет**: нужен свой растеризатор контуров `glyf` и атлас | immediate-mode поля ввода: нет системного выделения/IME/доступности; форма на 300+ полей неудобна | очень быстрый старт | да | высокий + новый растеризатор |
| Нативный UI на каждой ОС | да (D2D / Cairo-Pango / CoreText) | 3 реализации формы | лучший | нет | очень высокий |
| WebView | только через свой рендер в canvas | HTML-форма | мост UI↔C++ на каждое нажатие | да | средний, но противоречит §10 ТЗ |
| Electron | — | — | — | — | исключён ТЗ |

### 6.3. Решение

**Qt 6 Widgets** (модули `Core`, `Gui`, `Widgets`, `PrintSupport`), C++17.

Почему именно Widgets, а не Quick: нынешний интерфейс — плотная форма из
десятков полей в скролле; это ровно то, для чего существуют виджеты.
QML добавил бы JS-движок, сцен-граф и обязательный GPU-путь, не дав ничего
взамен: анимаций и жестов в приложении нет.

Что Qt закрывает без единого `#ifdef`:

* `QFileDialog`, `QColorDialog`, `QMessageBox`, `QDesktopServices::openUrl`;
* `QSettings` — реестр на Windows (можно указать **тот же** ключ
  `HKCU\Software\CV Builder`, чтобы настройки существующих пользователей не
  потерялись), ini на Linux, plist на macOS;
* `QStandardPaths` — `%LOCALAPPDATA%`, XDG, `~/Library/Application Support`;
* HiDPI, включая дробное масштабирование на Linux и Retina;
* `QStyleHints::colorScheme` — системная светлая/тёмная тема с сигналом
  об изменении (заменяет 163 строки `theme.cpp`, включая недокументированные
  ординалы uxtheme);
* `QPrinter` + тот же `QPainter`-обход, что и на экране — печать перестанет
  расходиться с PDF (см. R5);
* drag & drop, буфер обмена, акселераторы, `QShortcut`.

Оценка риска зависимости: Qt 6 Widgets — стабильный C++-тулкит без рантайма
поверх (никакого JS/Node), доступен как пакет во всех целевых дистрибутивах и
как официальный установщик на Windows/macOS. Размер бинарника по условию ТЗ
больше не критерий.

### 6.4. Судьба Win32-реализации

Нынешний Win32/D2D-фронтенд **не удаляется**. Он переезжает в
`src/ui_win32/` и остаётся отдельной цель сборки, собираемой только на
Windows. Это даёт:

* эталон, с которым сравнивается новый фронтенд (пиксель в пиксель по
  `--dump-ops` и байт в байт по PDF);
* нулевой риск регрессии на Windows на время миграции;
* проверку того, что слой platform действительно абстрактный: если он собирается
  с двумя разными UI, значит ядро от UI не зависит.

---

## 7. Предлагаемый бэкенд отрисовки

Один обход `Document` на все не-PDF выходы:

```cpp
// render/document_painter.h — знает про Document, не знает про CV
class Canvas {                       // реализуется UI-слоем
public:
    virtual void fillRect(double x, double y, double w, double h, RGB) = 0;
    virtual void drawLine(double x1, double y1, double x2, double y2, double width, RGB) = 0;
    virtual void drawGlyphs(double x, double y, double size, bool bold,
                            const uint16_t* glyphs, const float* advances, size_t count, RGB) = 0;
    virtual ~Canvas() = default;
};

void paintPage(const Page&, const FontSet&, Canvas&);
```

Обход один, реализаций `Canvas` три и все тонкие (~50 строк): экран Qt
(`QPainter` + `QRawFont`), принтер Qt (тот же `QPainter`), и — на время
переходного периода — Direct2D. PDF остаётся отдельным бэкендом: он пишет не
пиксели, а операторы содержимого страницы, и его нынешний код правильный.

Виртуальные вызовы здесь бесплатны: на страницу приходится ~1 000–2 500
примитивов, то есть порядка микросекунд накладных расходов на кадр.

---

## 8. План миграции

Каждый пункт — отдельный логический коммит; после каждого дерево собирается.

| # | Шаг | Проверка |
| --- | --- | --- |
| 1 | Этот документ | — |
| 2 | Каркас CMake (target-based), нынешние исходники, Windows-сборка как раньше | `cvcli.exe` и `CVBuilder.exe` собираются, PDF байт в байт совпадает с эталоном |
| 3 | `core/numeric.h`: убрать зависимость от локали из `json`/`pdf` | тест под `de_DE.UTF-8` |
| 4 | `core/file.h` + `std::filesystem::path` в API ядра; убрать `_wfopen`; UI-подписи из `model` в UI | Windows-сборка, PDF не изменился |
| 5 | `platform/paths`, `platform/settings`; `theme.cpp`/`settings.cpp` переводятся на них | Windows-сборка, реестр и автосейв работают как раньше |
| 6 | `font.cpp` → `core/font` (TTF) + `platform/font_source` (поиск по ОС), поддержка `.ttc` | тесты шрифтов, PDF не изменился |
| 7 | `render/document_painter`; Direct2D-preview и GDI-печать переводятся на него | preview визуально идентичен |
| 8 | Линуксовый и macOS-бэкенды `platform/*`; `cli` становится портируемым | `cvcli` собирается и работает на Linux |
| 9 | Тесты ядра + regression по PDF (детерминированный вывод, сравнение хеша) | `ctest` зелёный на Windows и Linux |
| 10 | Qt-фронтенд: главное окно, редактор, preview, диалоги | паритет функций с Win32-версией |
| 11 | Печать через `QPrinter`; `FontSet::family()` уходит из ядра | печать совпадает с PDF |
| 12 | Проверка на Linux и macOS, DPI, документация, README | ручной прогон на трёх ОС |

Шаги 2–9 не трогают UI и не могут сломать Windows-версию: она продолжает
собираться из `src/ui_win32` до самого конца.

---

## 9. Риски

| # | Риск | Последствие | Что делаем |
| --- | --- | --- | --- |
| **R1** | Локаль (§2.3) | невалидный PDF, битый JSON у русскоязычных пользователей | шаг 3 + тест под `de_DE.UTF-8`; наивысший приоритет |
| **R2** | Разные шрифты на разных ОС | другие переносы строк → **разный PDF на Windows/Linux/macOS** | приоритетный список метрически совместимых с Helvetica лиц: Arial → Liberation Sans → Arimo → Helvetica; поиск шрифта в каталоге приложения раньше системного (возможность зафиксировать шрифт); тест, сверяющий метрики |
| **R3** | Парсер не читает `.ttc` и OpenType/CFF (`font.cpp:117-121`) | на «чистой» macOS Helvetica.ttc/.otf не годятся | поддержка `ttcf` (чтение офсета первого sfnt — десяток строк); список кандидатов macOS начинается с `/System/Library/Fonts/Supplemental/Arial.ttf` |
| **R4** | Qt-стиль ≠ uxtheme | интерфейс на Windows выглядит иначе, чем нынешний | одинаковая палитра и метрики задаются в коде (как сейчас в `theme.cpp`), а не наследуются от стиля |
| **R5** | Печать сейчас идёт через GDI по имени семейства | печатное расходится с PDF уже сегодня | шаг 11: печать теми же glyph id; расхождение устраняется, а не переносится |
| **R6** | Мелкие Win32-фичи: split-button с recent files, cue banner, тёмная рамка окна | мелкая потеря UX | recent files → `QToolButton` с меню; cue banner → `placeholderText`; тёмная рамка → один платформенный хук в `platform/` |
| **R7** | Совместимость настроек | пользователь теряет тему и список файлов | `QSettings` направляется в существующий ключ `HKCU\Software\CV Builder`, имена значений сохраняются |
| **R8** | `std::filesystem` + Unicode-пути на mingw | не открываются файлы с кириллицей в пути | проверяется отдельным тестом на первом же шаге 4 |
| **R9** | Два фронтенда одновременно | расхождение поведения | Win32-версия заморожена как эталон, новые функции туда не добавляются |

---

## 10. Влияние на производительность

### 10.1. Как сейчас

`app.cpp:299-309`: изменение поля → таймер 200 мс → `form.collect()` (обход
всех контролов с `GetWindowTextW`) → `layout()` целиком → `preview.setDocument`
→ полная перерисовка страницы. Плюс отдельный таймер 500 мс делает
`toJson(collect())` для истории undo, плюс раз в 30 с — автосейв на диск.

То есть «сериализовать весь документ» на каждое изменение здесь уже
происходит — но документ занимает ~8 КБ, а полная раскладка даёт ~1 000–2 500
примитивов, поэтому цикл стоит десятки-сотни микросекунд и в UI не заметен.

### 10.2. Как будет

Улучшения, а не регрессии:

* **Модель как источник истины.** Виджет пишет изменение прямо в `CV`
  (точечно), `collect()` со сбором строк из всех контролов исчезает.
* Дебаунс раскладки 120 мс, дебаунс истории 500 мс — как сейчас.
* `QRawFont` создаётся один раз на пару лиц и кэшируется, как сейчас
  кэшируется `IDWriteFontFace`.
* Перерисовывается только видимая страница и только грязный прямоугольник
  (`QWidget::update(rect)`), фон листа — один `fillRect`.
* `QPainter` на `QWidget` идёт через растеризатор Qt на CPU: для страницы из
  ~2 500 глифов это порядка 1–3 мс, что укладывается в кадр.

Чего избегаем сознательно: IPC, повторного разбора JSON на каждое нажатие,
QML-мостов, копий `Document` (передаётся перемещением, как сейчас в
`preview.cpp:349`), синхронных обращений к диску в UI-потоке (автосейв уходит
в фон).

Замер добавляется в тесты как бенчмарк: «layout + PDF для `sample_cv.json`»,
чтобы регрессия была видна числом, а не на глаз.

---

## 11. DPI и A4

`layout.cpp:8-9` уже задаёт A4 в пунктах один раз для всех:

```cpp
const double kPageWidth  = 595.2755905511812;   // 210 мм
const double kPageHeight = 841.8897637795276;   // 297 мм
```

Это свойство документа, оно не зависит от ОС и не будет зависеть. Три системы
координат разделяются явно:

```
координаты документа (pt, 72 на дюйм)
        ├── экран:   px = pt × (zoom/100) × (logicalDpi/96)      как сейчас
        └── PDF:     1 pt = 1 единица                            как сейчас
```

Замечание для честности: нынешняя формула означает, что «100 %» — это не
физически точный сантиметр, а «1 pt на 1 логический пиксель» (страница
получается 595 логических пикселей ≈ 15.7 см на 96 dpi). Поведение
сохраняется — менять привычный масштаб без просьбы нельзя, — но пропорция
страницы 1:√2 остаётся точной на любом DPI, а в новой версии добавляется режим
«по ширине»/«страница целиком», где масштаб считается от размера окна.

---

## 12. Готовность (критерии из ТЗ) и как проверяется

| Критерий | Как проверяется |
| --- | --- |
| Windows работает как раньше | `src/ui_win32` собирается до конца миграции; PDF из `sample_cv.json` сравнивается байт в байт |
| Linux/macOS собираются и запускаются | единый `cmake --preset` на каждой ОС |
| JSON совместим | формат не меняется; тест открывает `sample_cv.json` и старый файл без блока `sections` |
| A4 одинаков | константы в ядре; тест на размеры и число страниц |
| PDF соответствует preview | один `Document`, один обход, одни glyph id; тест сверяет `--dump-ops` |
| Ядро без Win32 | grep по `src/core`, `src/render` в CI |
| Нет разбросанных `#ifdef` | выбор файлов в CMake; grep-проверка |
| Ядро тестируется без GUI | `resume_tests` не линкуется с UI |

---

## 13. Что решено не делать

* Не менять формат JSON и формат PDF.
* Не заменять свой JSON-парсер, свой PDF-генератор, свой TrueType-парсер и
  движок раскладки сторонними библиотеками.
* Не переписывать `layout.cpp` — там сидит весь дизайн шаблона, и любая правка
  «для красоты» немедленно сдвинет вёрстку существующих резюме.
* Не удалять Win32-фронтенд.
* Не вводить абстракции ради абстракций: `Document` уже нейтрален, второго
  промежуточного представления между ним и бэкендами не будет.
