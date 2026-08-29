#include "theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

#include "settings.h"

namespace cvb {
namespace qtui {
namespace {

uicommon::Mode gMode = uicommon::Mode::System;

}  // namespace

bool systemPrefersDark() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    // Before styleHints could be asked, infer it from the palette the platform
    // theme handed us: a window darker than its text means a dark desktop.
    const QPalette system = QGuiApplication::palette();
    return system.color(QPalette::Window).lightness() <
           system.color(QPalette::WindowText).lightness();
#endif
}

void initTheme() {
    const int saved = platform::settings::readInt(uicommon::kModeSetting,
                                                  static_cast<int>(uicommon::Mode::System));
    gMode = (saved >= 0 && saved <= static_cast<int>(uicommon::Mode::Dark))
                ? static_cast<uicommon::Mode>(saved)
                : uicommon::Mode::System;
}

uicommon::Mode themeMode() { return gMode; }

const uicommon::Palette& currentPalette() {
    const bool dark = gMode == uicommon::Mode::Dark ||
                      (gMode == uicommon::Mode::System && systemPrefersDark());
    return uicommon::paletteFor(dark);
}

void applyTheme() {
    const uicommon::Palette& colours = currentPalette();

    // Fusion, explicitly. The native styles on Windows and macOS paint controls
    // in colours of their own choosing and would ignore half of the palette
    // below; Fusion honours it, and honours it identically on all three systems,
    // which is what makes the application look like one program.
    if (QStyle* style = QStyleFactory::create("Fusion")) QApplication::setStyle(style);

    const QColor window = toQt(colours.window);
    const QColor pane = toQt(colours.pane);
    const QColor field = toQt(colours.field);
    const QColor text = toQt(colours.text);
    const QColor subtext = toQt(colours.subtext);
    const QColor accent = toQt(colours.accent);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, field);
    p.setColor(QPalette::AlternateBase, pane);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::PlaceholderText, subtext);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase, pane);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, colours.dark ? QColor(Qt::black) : QColor(Qt::white));
    p.setColor(QPalette::Link, accent);

    // Disabled controls have to read as disabled in both palettes; the default
    // grey is nearly invisible on the dark one.
    const QColor faded = colours.dark ? subtext.darker(140) : subtext.lighter(130);
    p.setColor(QPalette::Disabled, QPalette::Text, faded);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, faded);
    p.setColor(QPalette::Disabled, QPalette::WindowText, faded);

    QApplication::setPalette(p);
}

void setThemeMode(uicommon::Mode mode) {
    gMode = mode;
    platform::settings::writeInt(uicommon::kModeSetting, static_cast<int>(mode));
    applyTheme();
}

}  // namespace qtui
}  // namespace cvb
