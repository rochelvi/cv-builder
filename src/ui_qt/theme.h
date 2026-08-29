// The palette, applied to Qt.
//
// The colours come from uicommon, shared with the Win32 front end, so the two are
// the same program in two window systems rather than two programs. What lives
// here is only the Qt plumbing: turning that palette into a QPalette and a small
// style sheet, and following the desktop's light/dark setting when asked to.
#pragma once

#include <QColor>
#include <QObject>

#include "palette.h"

namespace cvb {
namespace qtui {

// Reads the remembered choice through the platform settings layer - the same
// setting the Win32 build uses, so a user who runs both sees one theme. Call
// initTheme once, before the first window exists.
void initTheme();
uicommon::Mode themeMode();
void setThemeMode(uicommon::Mode mode);  // remembered for the next run

// The palette in force, after resolving Mode::System against the desktop. Named
// so as not to be shadowed by QWidget::palette inside a widget.
const uicommon::Palette& currentPalette();

inline QColor toQt(const RGB& c) { return QColor(c.r, c.g, c.b); }

// Applies the palette to the whole application. Call once at startup and again
// whenever the choice or the system setting changes.
void applyTheme();

// True when the desktop itself is set to a dark colour scheme. Qt answers this
// on all three platforms from 6.5 onwards; older builds are assumed light.
bool systemPrefersDark();

}  // namespace qtui
}  // namespace cvb
