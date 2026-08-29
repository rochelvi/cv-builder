// The main window: the toolbar, the two panes, the status line, and the wiring
// between the editor, the preview and the files on disk.
//
// The same shape as the Win32 window because it is the same application: a
// toolbar across the top, the editor on the left, the A4 preview on the right, a
// status line along the bottom.
#pragma once

#include <QMainWindow>
#include <QString>
#include <string>
#include <vector>

#include "file.h"
#include "font.h"
#include "model.h"

class QAction;
class QComboBox;
class QLabel;
class QTimer;
class QToolButton;

namespace cvb {
namespace qtui {

class EditorPane;
class PreviewPane;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

    // Opens the file named on the command line, if any, instead of the sample.
    void openInitial(const Path& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void buildToolBar();
    void buildPanes();
    void buildStatusBar();

    void actionNew();
    void actionOpen();
    bool actionSave();
    bool actionSaveAs();
    void actionExport();
    void actionPrint();
    void actionUndo();
    void actionRedo();
    void showRecentMenu();

    bool openPath(const Path& file);
    void loadCV(const CV& cv, const Path& from);
    bool confirmDiscard();
    void scheduleRefresh();
    void refreshPreview();
    void updateTitle();
    void updatePreviewControls();
    void setStatus(const QString& text);

    void takeSnapshot();
    void applySnapshot(const std::string& json);
    void resetHistory(const CV& cv);
    void updateHistoryButtons();
    void writeRecovery();
    bool offerRecovery();

    void applyTheme();

    EditorPane* editor_ = nullptr;
    PreviewPane* preview_ = nullptr;

    QToolButton* openButton_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* prevPage_ = nullptr;
    QAction* nextPage_ = nullptr;
    QLabel* pageLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QComboBox* themeBox_ = nullptr;

    QTimer* refreshTimer_ = nullptr;
    QTimer* snapshotTimer_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;

    FontSet fonts_;
    bool fontsReady_ = false;

    // Whole-document JSON snapshots rather than a list of edit operations: the
    // model already serialises itself losslessly, and a snapshot cannot go out of
    // step with the form the way a replayed operation can.
    std::vector<std::string> undoStack_;
    std::vector<std::string> redoStack_;
    std::string baseline_;

    Path path_;
    bool dirty_ = false;
};

}  // namespace qtui
}  // namespace cvb
