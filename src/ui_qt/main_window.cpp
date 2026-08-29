#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QGlyphRun>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

#include <algorithm>

#include "canvas.h"
#include "document_painter.h"
#include "editor_pane.h"
#include "editor_widgets.h"
#include "fonts.h"
#include "layout.h"
#include "paths.h"
#include "pdf.h"
#include "preview_pane.h"
#include "recent_files.h"
#include "recovery.h"
#include "theme.h"
#include "version.h"

namespace cvb {
namespace qtui {
namespace {

const char* const kAppName = "CV Builder";

// ms of quiet before the preview redraws. A little shorter than the Win32
// version's 200: there is no round trip through the controls to pay for here.
constexpr int kRefreshDelay = 120;

// A separate, slower debounce for the undo history. Snapshotting on the same
// interval as the repaint would make every pause between two words its own undo
// step and fill the whole history with keystrokes.
constexpr int kSnapshotDelay = 500;

// The recovery snapshot is written on a fixed interval rather than on every edit:
// it costs a file write, and losing at most half a minute of work to a power cut
// is the trade being made.
constexpr int kAutosaveInterval = 30000;

// Deep enough to undo a whole editing session.
constexpr size_t kUndoDepth = 50;

// "Daniil Mishin" -> "Daniil_Mishin", the default name for Save and Export.
QString suggestedName(const QString& personName, const QString& extension) {
    QString base;
    for (QChar c : personName) {
        if (c == QLatin1Char(' '))
            base += QLatin1Char('_');
        else if (!QStringLiteral("\\/:*?\"<>|").contains(c))
            base += c;
    }
    if (base.isEmpty()) base = QStringLiteral("cv");
    return base + extension;
}

// A QPainter as a Canvas, for the printer. Points to device units, shifted by the
// printer's unprintable margin: the page rectangle starts at the corner of the
// printable area, but the layout measures from the corner of the sheet.
class PrinterCanvas : public Canvas {
public:
    PrinterCanvas(QPainter& painter, double scale, QPointF offset, const QRawFont faces[2])
        : painter_(painter), scale_(scale), offset_(offset), faces_{faces[0], faces[1]} {}

    void fillRect(double x, double y, double width, double height, RGB color) override {
        painter_.fillRect(QRectF(map(x, y), QSizeF(width * scale_, height * scale_)), toQt(color));
    }

    void drawLine(double x1, double y1, double x2, double y2, double width, RGB color) override {
        QPen pen(toQt(color));
        pen.setWidthF(std::max(width * scale_, 1.0));
        painter_.setPen(pen);
        painter_.drawLine(QLineF(map(x1, y1), map(x2, y2)));
    }

    void drawGlyphs(double x, double y, double size, bool bold, const GlyphRun& run,
                    RGB color) override {
        if (run.count == 0) return;
        // One face per weight, scaled by the painter rather than rebuilt per size:
        // a printer's resolution makes the raster size enormous, and the outlines
        // are what get sent anyway.
        QRawFont face = faces_[bold ? 1 : 0];
        if (!face.isValid()) return;
        face.setPixelSize(size * scale_);

        QList<quint32> indexes;
        QList<QPointF> positions;
        double pen = 0.0;
        for (size_t i = 0; i < run.count; ++i) {
            indexes.append(run.glyphs[i]);
            positions.append(QPointF(pen, 0.0));
            pen += static_cast<double>(run.advances[i]) * scale_;
        }

        QGlyphRun glyphs;
        glyphs.setRawFont(face);
        glyphs.setGlyphIndexes(indexes);
        glyphs.setPositions(positions);
        painter_.setPen(toQt(color));
        painter_.drawGlyphRun(map(x, y), glyphs);
    }

private:
    QPointF map(double x, double y) const {
        return QPointF(offset_.x() + x * scale_, offset_.y() + y * scale_);
    }

    QPainter& painter_;
    double scale_;
    QPointF offset_;
    QRawFont faces_[2];
};

QRawFont rawFontFor(const Font& font) {
    const std::vector<uint8_t>& data = font.fileData();
    return QRawFont(QByteArray(reinterpret_cast<const char*>(data.data()),
                               static_cast<qsizetype>(data.size())),
                    12.0, QFont::PreferVerticalHinting);
}

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle(QString::fromUtf8(kAppName));
    setAcceptDrops(true);
    // Wide enough that the toolbar never overlaps itself, and tall enough to show
    // a useful slice of the page.
    resize(1500, 950);
    setMinimumSize(1100, 600);

    buildToolBar();
    buildPanes();
    buildStatusBar();

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(kRefreshDelay);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshPreview);

    snapshotTimer_ = new QTimer(this);
    snapshotTimer_->setSingleShot(true);
    snapshotTimer_->setInterval(kSnapshotDelay);
    connect(snapshotTimer_, &QTimer::timeout, this, &MainWindow::takeSnapshot);

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(kAutosaveInterval);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::writeRecovery);
    autosaveTimer_->start();

    std::string error;
    fontsReady_ = app::loadFonts(fonts_, error);
    if (fontsReady_) {
        preview_->setFonts(&fonts_);
    } else {
        preview_->setError(tr("Не удалось загрузить шрифт:\n%1").arg(fromUtf8(error)));
    }

    applyTheme();
}

void MainWindow::buildToolBar() {
    auto* bar = addToolBar(tr("Действия"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto add = [&](const QString& text, const QKeySequence& shortcut, void (MainWindow::*slot)()) {
        auto* action = bar->addAction(text);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    add(tr("Новый"), QKeySequence::New, &MainWindow::actionNew);

    // Open is a split button: the arrow drops the list of recent files, the same
    // as the Win32 toolbar.
    auto* open = new QAction(tr("Открыть…"), this);
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, &MainWindow::actionOpen);
    openButton_ = new QToolButton;
    openButton_->setDefaultAction(open);
    openButton_->setPopupMode(QToolButton::MenuButtonPopup);
    openButton_->setMenu(new QMenu(openButton_));
    connect(openButton_->menu(), &QMenu::aboutToShow, this, &MainWindow::showRecentMenu);
    bar->addWidget(openButton_);

    auto* save = bar->addAction(tr("Сохранить"));
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, [this] { actionSave(); });

    auto* saveAs = bar->addAction(tr("Сохранить как…"));
    saveAs->setShortcut(QKeySequence::SaveAs);
    connect(saveAs, &QAction::triggered, this, [this] { actionSaveAs(); });

    add(tr("Экспорт PDF…"), QKeySequence(QStringLiteral("Ctrl+E")), &MainWindow::actionExport);
    add(tr("Печать…"), QKeySequence::Print, &MainWindow::actionPrint);

    bar->addSeparator();
    undoAction_ = add(QStringLiteral("↶"), QKeySequence::Undo, &MainWindow::actionUndo);
    undoAction_->setToolTip(tr("Отменить"));
    redoAction_ = add(QStringLiteral("↷"), QKeySequence::Redo, &MainWindow::actionRedo);
    redoAction_->setToolTip(tr("Вернуть"));
    // The other half of the world's redo shortcut.
    auto* redoAlso = new QAction(this);
    redoAlso->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    connect(redoAlso, &QAction::triggered, this, &MainWindow::actionRedo);
    addAction(redoAlso);

    auto* quit = new QAction(this);
    quit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(quit, &QAction::triggered, this, &QWidget::close);
    addAction(quit);

    bar->addSeparator();
    themeBox_ = new QComboBox;
    for (const char* name : uicommon::kModeNames) themeBox_->addItem(QString::fromUtf8(name));
    themeBox_->setCurrentIndex(static_cast<int>(themeMode()));
    connect(themeBox_, &QComboBox::activated, this, [this](int index) {
        setThemeMode(static_cast<uicommon::Mode>(index));
        applyTheme();
    });
    bar->addWidget(themeBox_);

    // Preview controls hug the right edge of the same bar.
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    prevPage_ = bar->addAction(QStringLiteral("‹"));
    prevPage_->setToolTip(tr("Предыдущая страница"));
    connect(prevPage_, &QAction::triggered, this,
            [this] { preview_->setPage(preview_->page() - 1); });

    pageLabel_ = new QLabel(tr("Стр. 1 / 1"));
    pageLabel_->setAlignment(Qt::AlignCenter);
    pageLabel_->setMinimumWidth(90);
    bar->addWidget(pageLabel_);

    nextPage_ = bar->addAction(QStringLiteral("›"));
    nextPage_->setToolTip(tr("Следующая страница"));
    connect(nextPage_, &QAction::triggered, this,
            [this] { preview_->setPage(preview_->page() + 1); });

    bar->addSeparator();
    auto* zoomOut = bar->addAction(QStringLiteral("−"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, this,
            [this] { preview_->setZoom(preview_->zoom() - 10); });

    zoomLabel_ = new QLabel(QStringLiteral("100 %"));
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomLabel_->setMinimumWidth(56);
    bar->addWidget(zoomLabel_);

    auto* zoomIn = bar->addAction(QStringLiteral("+"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, this,
            [this] { preview_->setZoom(preview_->zoom() + 10); });

    auto* fit = bar->addAction(tr("Вписать"));
    connect(fit, &QAction::triggered, this, [this] { preview_->zoomToFit(); });
}

void MainWindow::buildPanes() {
    editor_ = new EditorPane;
    preview_ = new PreviewPane;

    connect(editor_, &EditorPane::changed, this, &MainWindow::scheduleRefresh);
    connect(preview_, &PreviewPane::stateChanged, this, &MainWindow::updatePreviewControls);

    // A splitter rather than a fixed half: the editor and the page want different
    // amounts of room depending on the screen, and the user knows which.
    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(editor_);
    split->addWidget(preview_);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setChildrenCollapsible(false);
    setCentralWidget(split);
}

void MainWindow::buildStatusBar() {
    statusLabel_ = new QLabel;
    statusBar()->addWidget(statusLabel_, 1);
    // The build names itself in the corner opposite the message, as in the Win32
    // window.
    statusBar()->addPermanentWidget(new QLabel(QString::fromUtf8(VER_DISPLAY_STR)));
    statusBar()->setSizeGripEnabled(true);
}

void MainWindow::setStatus(const QString& text) { statusLabel_->setText(text); }

void MainWindow::applyTheme() {
    qtui::applyTheme();
    editor_->applyTheme();
    preview_->applyTheme();
    update();
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    // The desktop's colour scheme changed under us. Only interesting while we are
    // following it.
    if (event->type() == QEvent::PaletteChange && themeMode() == uicommon::Mode::System)
        applyTheme();
}

// ------------------------------------------------------------------- documents

void MainWindow::updateTitle() {
    QString title = QString::fromUtf8(kAppName);
    if (!path_.empty()) title += QStringLiteral(" — ") + QFileInfo(fromUtf8(cvb::toUtf8(path_))).fileName();
    if (dirty_) title += QStringLiteral(" *");
    setWindowTitle(title);
}

void MainWindow::loadCV(const CV& cv, const Path& from) {
    editor_->setCV(cv);
    path_ = from;
    dirty_ = false;
    updateTitle();
    refreshPreview();
    // A new document starts a new history: undoing across an Open would put the
    // previous CV back under the new file's name.
    resetHistory(cv);
}

void MainWindow::scheduleRefresh() {
    dirty_ = true;
    updateTitle();
    refreshTimer_->start();
    snapshotTimer_->start();
}

void MainWindow::refreshPreview() {
    if (!fontsReady_) return;
    preview_->setDocument(::cvb::layout(editor_->collect(), fonts_));
}

void MainWindow::updatePreviewControls() {
    pageLabel_->setText(tr("Стр. %1 / %2").arg(preview_->page() + 1).arg(preview_->pageCount()));
    zoomLabel_->setText(QStringLiteral("%1 %").arg(preview_->zoom()));
    prevPage_->setEnabled(preview_->page() > 0);
    nextPage_->setEnabled(preview_->page() + 1 < preview_->pageCount());
}

bool MainWindow::confirmDiscard() {
    if (!dirty_) return true;
    const auto answer = QMessageBox::question(
        this, QString::fromUtf8(kAppName), tr("Сохранить изменения перед тем, как продолжить?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::No) return true;
    return actionSave();
}

void MainWindow::actionNew() {
    if (!confirmDiscard()) return;
    loadCV(emptyCV(), Path());
    setStatus(tr("Новое резюме"));
}

void MainWindow::actionOpen() {
    if (!confirmDiscard()) return;
    const QString start = path_.empty() ? fromUtf8(cvb::toUtf8(platform::documentsDirectory()))
                                        : fromUtf8(cvb::toUtf8(path_.parent_path()));
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Открыть резюме"), start, tr("Резюме (*.json);;Все файлы (*)"));
    if (file.isEmpty()) return;
    openPath(cvb::fromUtf8(toUtf8(file)));
}

bool MainWindow::openPath(const Path& file) {
    CV cv;
    std::string error;
    if (!load(file, cv, error)) {
        QMessageBox::critical(this, QString::fromUtf8(kAppName),
                              tr("Не удалось открыть файл:\n%1").arg(fromUtf8(error)));
        return false;
    }
    loadCV(cv, file);
    app::pushRecentFile(file);
    setStatus(tr("Открыто: %1").arg(fromUtf8(cvb::toUtf8(file))));
    return true;
}

bool MainWindow::actionSave() {
    if (path_.empty()) return actionSaveAs();
    std::string error;
    if (!save(path_, editor_->collect(), error)) {
        QMessageBox::critical(this, QString::fromUtf8(kAppName),
                              tr("Не удалось сохранить:\n%1").arg(fromUtf8(error)));
        return false;
    }
    dirty_ = false;
    updateTitle();
    app::pushRecentFile(path_);
    // The work is on disk now, so the recovery snapshot has nothing left to rescue
    // and must not be offered on the next start.
    app::clearAutosave();
    setStatus(tr("Сохранено: %1").arg(fromUtf8(cvb::toUtf8(path_))));
    return true;
}

bool MainWindow::actionSaveAs() {
    const CV cv = editor_->collect();
    const QString directory = path_.empty()
                                  ? fromUtf8(cvb::toUtf8(platform::documentsDirectory()))
                                  : fromUtf8(cvb::toUtf8(path_.parent_path()));
    const QString suggestion =
        QDir(directory).filePath(suggestedName(fromUtf8(cv.name), QStringLiteral(".json")));

    const QString file = QFileDialog::getSaveFileName(this, tr("Сохранить резюме"), suggestion,
                                                      tr("Резюме (*.json)"));
    if (file.isEmpty()) return false;
    path_ = cvb::fromUtf8(toUtf8(file));
    return actionSave();
}

void MainWindow::actionExport() {
    if (!fontsReady_) {
        QMessageBox::critical(this, QString::fromUtf8(kAppName),
                              tr("Шрифты не загружены, экспорт невозможен."));
        return;
    }
    const CV cv = editor_->collect();
    const QString directory = path_.empty()
                                  ? fromUtf8(cvb::toUtf8(platform::documentsDirectory()))
                                  : fromUtf8(cvb::toUtf8(path_.parent_path()));
    const QString suggestion =
        QDir(directory).filePath(suggestedName(fromUtf8(cv.name), QStringLiteral(".pdf")));

    const QString file =
        QFileDialog::getSaveFileName(this, tr("Экспорт PDF"), suggestion, tr("PDF (*.pdf)"));
    if (file.isEmpty()) return;

    const Path target = cvb::fromUtf8(toUtf8(file));
    std::string error;
    const Document doc = ::cvb::layout(cv, fonts_);
    if (!writePdf(doc, fonts_, target, error)) {
        QMessageBox::critical(this, QString::fromUtf8(kAppName),
                              tr("Не удалось записать PDF:\n%1").arg(fromUtf8(error)));
        return;
    }
    setStatus(tr("Экспортировано: %1").arg(file));
    QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}

void MainWindow::actionPrint() {
    if (!fontsReady_) {
        QMessageBox::critical(this, QString::fromUtf8(kAppName),
                              tr("Шрифты не загружены, печать невозможна."));
        return;
    }
    const Document doc = ::cvb::layout(editor_->collect(), fonts_);
    if (doc.pages.empty()) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    // The template's background is part of the design and is printed like
    // everything else, so the sheet has no margins of its own to respect.
    printer.setFullPage(true);
    printer.setDocName(path_.empty() ? QString::fromUtf8(kAppName)
                                     : QFileInfo(fromUtf8(cvb::toUtf8(path_))).fileName());

    QPrintDialog dialog(&printer, this);
    dialog.setOption(QAbstractPrintDialog::PrintPageRange);
    dialog.setFromTo(1, static_cast<int>(doc.pages.size()));
    if (dialog.exec() != QDialog::Accepted) return;

    size_t first = 0, last = doc.pages.size() - 1;
    if (printer.printRange() == QPrinter::PageRange) {
        first = static_cast<size_t>(std::max(1, printer.fromPage())) - 1;
        last = std::min<size_t>(static_cast<size_t>(std::max(1, printer.toPage())) - 1, last);
    }

    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::critical(this, QString::fromUtf8(kAppName), tr("Не удалось начать печать."));
        return;
    }
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    // Points to device units. The same glyph ids the PDF embeds are sent to the
    // printer, so a printed page cannot disagree with the export.
    const double scale = printer.resolution() / 72.0;
    const QRawFont faces[2] = {rawFontFor(fonts_.regular()), rawFontFor(fonts_.bold())};

    DocumentPainter pages(fonts_);
    for (size_t i = first; i <= last; ++i) {
        if (i != first) printer.newPage();
        PrinterCanvas canvas(painter, scale, QPointF(0, 0), faces);
        pages.paint(doc.pages[i], canvas);
    }
    painter.end();
    setStatus(tr("Отправлено на печать"));
}

// ------------------------------------------------------------- undo history

void MainWindow::updateHistoryButtons() {
    undoAction_->setEnabled(!undoStack_.empty());
    redoAction_->setEnabled(!redoStack_.empty());
}

void MainWindow::resetHistory(const CV& cv) {
    undoStack_.clear();
    redoStack_.clear();
    baseline_ = toJson(cv);
    updateHistoryButtons();
}

void MainWindow::takeSnapshot() {
    // Comparing the serialised form against the last recorded state is what makes
    // this cheap to call speculatively: a burst of keystrokes collapses into one
    // step, and a repaint that changed nothing records nothing.
    const std::string current = toJson(editor_->collect());
    if (current == baseline_) return;
    undoStack_.push_back(baseline_);
    if (undoStack_.size() > kUndoDepth) undoStack_.erase(undoStack_.begin());
    baseline_ = current;
    // Editing after an undo abandons the future that was undone away, which is
    // what every editor does and what users expect.
    redoStack_.clear();
    updateHistoryButtons();
}

void MainWindow::applySnapshot(const std::string& json) {
    CV cv;
    std::string error;
    if (!fromJson(json, cv, error)) return;
    editor_->setCV(cv);
    baseline_ = json;
    dirty_ = true;
    updateTitle();
    refreshPreview();
    updatePreviewControls();
    snapshotTimer_->stop();
    updateHistoryButtons();
}

void MainWindow::actionUndo() {
    // Whatever was typed in the last half second has not been recorded yet;
    // without this the first Ctrl+Z would throw it away instead of undoing it.
    takeSnapshot();
    if (undoStack_.empty()) {
        setStatus(tr("Отменять нечего"));
        return;
    }
    redoStack_.push_back(baseline_);
    const std::string state = undoStack_.back();
    undoStack_.pop_back();
    applySnapshot(state);
    setStatus(tr("Отменено"));
}

void MainWindow::actionRedo() {
    if (redoStack_.empty()) {
        setStatus(tr("Возвращать нечего"));
        return;
    }
    undoStack_.push_back(baseline_);
    const std::string state = redoStack_.back();
    redoStack_.pop_back();
    applySnapshot(state);
    setStatus(tr("Возвращено"));
}

// ------------------------------------------------------------ recent files

void MainWindow::showRecentMenu() {
    QMenu* menu = openButton_->menu();
    menu->clear();

    const std::vector<Path> files = app::recentFiles();
    if (files.empty()) {
        menu->addAction(tr("Пока ничего не открывали"))->setEnabled(false);
        return;
    }
    for (size_t i = 0; i < files.size(); ++i) {
        const Path file = files[i];
        QAction* action =
            menu->addAction(QStringLiteral("%1. %2").arg(i + 1).arg(fromUtf8(cvb::toUtf8(file))));
        connect(action, &QAction::triggered, this, [this, file] {
            if (confirmDiscard()) openPath(file);
        });
    }
    menu->addSeparator();
    connect(menu->addAction(tr("Очистить список")), &QAction::triggered, this,
            [] { app::clearRecentFiles(); });
}

// --------------------------------------------------------------- recovery

void MainWindow::writeRecovery() {
    // Only unsaved work is worth a snapshot; a saved document is already on disk
    // in a better place than the recovery file.
    if (!dirty_) return;
    app::writeAutosave(editor_->collect(), path_);
}

bool MainWindow::offerRecovery() {
    app::Recovery recovery;
    if (!app::findRecovery(recovery)) return false;

    const QString what = recovery.origin.empty()
                             ? tr("несохранённое резюме")
                             : QFileInfo(fromUtf8(cvb::toUtf8(recovery.origin))).fileName();
    const QString question = tr("Прошлый сеанс завершился, не сохранив изменения.\n\n"
                                "Восстановить %1?")
                                 .arg(what);
    if (QMessageBox::question(this, QString::fromUtf8(kAppName), question,
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        app::clearAutosave();
        return false;
    }

    loadCV(recovery.cv, recovery.origin);
    // Restored work is unsaved by definition: the title has to say so, and closing
    // now has to ask.
    dirty_ = true;
    updateTitle();
    setStatus(tr("Восстановлено из автосохранения"));
    return true;
}

void MainWindow::openInitial(const Path& file) {
    if (!file.empty() && openPath(file)) return;

    // Unsaved work from a session that ended badly outranks anything else we might
    // open, so it is offered before the sample is even looked for.
    if (offerRecovery()) return;

    // Start on the sample if one is lying about, otherwise blank. The directory of
    // the executable, the working directory and the directory above are all
    // plausible places for it depending on how the program was started.
    CV startup = emptyCV();
    std::string ignored;
    bool loaded = false;
    const Path exe = platform::executableDirectory();
    for (const Path& candidate :
         {exe / "sample_cv.json", Path("sample_cv.json"), exe.parent_path() / "sample_cv.json"}) {
        if (load(candidate, startup, ignored)) {
            loaded = true;
            break;
        }
    }
    if (!loaded) startup = emptyCV();
    loadCV(startup, Path());
    setStatus(loaded ? tr("Загружен пример sample_cv.json") : tr("Готово"));
}

// ------------------------------------------------------------------ window

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    // Closing on purpose - saved, or knowingly discarded - means there is nothing
    // left to recover on the next start.
    app::clearAutosave();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    const QString file = urls.first().toLocalFile();
    if (file.isEmpty()) return;

    event->acceptProposedAction();
    if (!confirmDiscard()) return;
    if (!openPath(cvb::fromUtf8(toUtf8(file)))) return;
    // Only one CV is open at a time; say so rather than quietly ignoring the rest
    // of a multiple selection.
    if (urls.size() > 1)
        setStatus(statusLabel_->text() +
                  tr(" (перетащено файлов: %1, открыт первый)").arg(urls.size()));
}

}  // namespace qtui
}  // namespace cvb
