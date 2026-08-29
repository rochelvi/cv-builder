// CV Builder, the portable front end.
//
// Everything below the window - the model, the layout engine, the TrueType reader,
// the PDF writer, the recent-files list, the recovery snapshot - is the same code
// the Win32 build uses. This file only starts a Qt application and hands it a
// window.
#include <QApplication>
#include <QLocale>
#include <QStringList>

#include "console.h"
#include "file.h"
#include "main_window.h"
#include "theme.h"
#include "version.h"

int main(int argc, char** argv) {
    // Names the program for QStandardPaths and for the window manager. No
    // organisation name on purpose: the settings and the data directory then sit
    // under "CV Builder" alone, which is where the Windows build has always kept
    // them.
    QApplication::setApplicationName(QStringLiteral("CV Builder"));
    QApplication::setApplicationDisplayName(QStringLiteral("CV Builder"));
    QApplication::setApplicationVersion(QString::fromUtf8(VER_VERSION_STR));
    QApplication::setDesktopFileName(QStringLiteral("cv-builder"));

    QApplication app(argc, argv);

    // QApplication's constructor calls setlocale(LC_ALL, ""), which is why the core
    // formats its own numbers instead of leaving them to the C library: in a locale
    // with a decimal comma, printf would write "595,276" into the PDF and no reader
    // would open the file. See src/core/numeric.h.

    cvb::qtui::initTheme();
    cvb::qtui::applyTheme();

    // A path on the command line opens that resume instead of the sample. Taken
    // through the platform layer so a non-ASCII path survives on Windows, where the
    // narrow argv cannot spell one.
    cvb::Path initial;
    const std::vector<std::string> args = cvb::platform::commandLine(argc, argv);
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i].rfind("--", 0) == 0) continue;
        initial = cvb::fromUtf8(args[i]);
        break;
    }

    cvb::qtui::MainWindow window;
    window.show();
    window.openInitial(initial);
    return app.exec();
}
