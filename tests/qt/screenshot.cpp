// Renders the whole interface to a PNG without a display.
//
// Useful for three things a compile cannot answer: what the interface actually
// looks like on a machine with no window server, whether both palettes are
// coherent, and - attached to a bug report - what the person saw.
//
//   QT_QPA_PLATFORM=offscreen resume_qt_shot out.png [light|dark|system] [resume.json]
#include <QApplication>
#include <QEventLoop>
#include <QPixmap>
#include <QString>
#include <QTimer>

#include <cstdio>

#include "file.h"
#include "main_window.h"
#include "palette.h"
#include "theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    const QStringList args = QApplication::arguments();
    const QString output = args.size() > 1 ? args[1] : QStringLiteral("interface.png");
    const QString mode = args.size() > 2 ? args[2] : QStringLiteral("dark");
    const cvb::Path resume = args.size() > 3 ? cvb::fromUtf8(args[3].toStdString()) : cvb::Path();

    cvb::qtui::initTheme();
    if (mode == QStringLiteral("light"))
        cvb::qtui::setThemeMode(cvb::uicommon::Mode::Light);
    else if (mode == QStringLiteral("dark"))
        cvb::qtui::setThemeMode(cvb::uicommon::Mode::Dark);
    cvb::qtui::applyTheme();

    cvb::qtui::MainWindow window;
    window.resize(1500, 950);
    window.show();
    window.openInitial(resume);

    // The preview redraws on a timer, so the window is not finished the moment it
    // is shown. Let the event loop run until the debounce has fired.
    QEventLoop loop;
    QTimer::singleShot(400, &loop, &QEventLoop::quit);
    loop.exec();

    const QPixmap shot = window.grab();
    if (!shot.save(output)) {
        std::fprintf(stderr, "could not write %s\n", qPrintable(output));
        return 1;
    }
    std::printf("%s: %dx%d\n", qPrintable(output), shot.width(), shot.height());
    return 0;
}
