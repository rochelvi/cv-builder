"""Fluent 2 look: native Windows 11 styling, typography and the Mica backdrop.

Qt 6.7+ ships a `windows11` style that already draws Fluent controls - rounded
text boxes with the accent underline, native combo boxes, thin scrollbars - and
follows the system light/dark setting on its own. So this module styles as
little as possible: it sets the Fluent type ramp, paints the surfaces Qt has no
concept of (panes, cards, secondary labels) and matches the title bar to the
system theme. Overriding more than that would replace Fluent with an imitation.

No Mica backdrop, deliberately. Mica needs a translucent client area, and with
Qt's widgets a translucent window composites each repaint over the previous one
instead of clearing it, so frequently updated labels smear. An opaque window
with the Fluent layer colours costs nothing visually and always draws correctly.

On a non-Windows host the title bar call fails quietly and the rest still holds.
"""

from __future__ import annotations

import ctypes
import sys
from string import Template

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont, QGuiApplication

# Fluent "Body" is 14px, which is 10.5pt at 96 dpi. Point sizes, not pixels, so
# that the ramp still scales with the display.
BODY_POINT_SIZE = 10.5
BODY_FAMILIES = ["Segoe UI Variable Text", "Segoe UI"]

# Surface tokens lifted from the WinUI resource set; the alphas are what make a
# card read as a layer above the backdrop rather than a flat colour.
SURFACES = {
    "dark": {
        "layer": "rgba(58, 58, 58, 77)",  # LayerFillColorDefault
        "card": "rgba(255, 255, 255, 15)",  # CardBackgroundFillColorDefault
        "card_stroke": "rgba(255, 255, 255, 19)",
        "text_secondary": "rgba(255, 255, 255, 200)",
    },
    "light": {
        "layer": "rgba(255, 255, 255, 128)",
        "card": "rgba(255, 255, 255, 179)",
        "card_stroke": "rgba(0, 0, 0, 15)",
        "text_secondary": "rgba(0, 0, 0, 155)",
    },
}

_STYLE = Template("""
QWidget[pane="true"] { background: $layer; }
QFrame[card="true"] {
    background: $card;
    border: 1px solid $card_stroke;
    border-radius: 8px;
}
QLabel[heading="true"] { font-weight: 600; }
QLabel[sub="true"] { color: $text_secondary; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
""")

# DWM window attribute (dwmapi.h)
_USE_IMMERSIVE_DARK_MODE = 20


def is_dark() -> bool:
    """Whether Windows is currently set to the dark app theme."""
    return QGuiApplication.styleHints().colorScheme() == Qt.ColorScheme.Dark


def font() -> QFont:
    """The Fluent body font, falling back through the Segoe UI family."""
    body = QFont(BODY_FAMILIES[0], -1)
    body.setFamilies(BODY_FAMILIES)
    body.setPointSizeF(BODY_POINT_SIZE)
    return body


def stylesheet(dark: bool) -> str:
    """The thin layer on top of the native style."""
    return _STYLE.substitute(SURFACES["dark" if dark else "light"])


def apply_titlebar(window, dark: bool) -> bool:
    """Match the window's title bar to the system theme. Returns whether it took."""
    if not sys.platform.startswith("win"):
        return False
    try:
        setting = ctypes.c_int(int(dark))
        return ctypes.windll.dwmapi.DwmSetWindowAttribute(
            int(window.winId()), _USE_IMMERSIVE_DARK_MODE,
            ctypes.byref(setting), ctypes.sizeof(setting),
        ) == 0
    except (AttributeError, OSError):  # no dwmapi, or a build without the flag
        return False
