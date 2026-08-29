// The two palettes the interface is painted in, and which of them to use.
//
// Shared by every front end on purpose. The point of the port is not that each
// platform gets a native-looking window - it is that the application looks and
// behaves the same everywhere - so the colours cannot be a literal inside one
// front end's theme code, or the two drift the first time either is touched.
//
// These are interface colours. The eight colours of the *document* are a
// different thing entirely: they live in the resume file, are edited by the user
// and end up in the PDF.
#pragma once

#include "layout.h"  // RGB

namespace cvb {
namespace uicommon {

// Which palette to paint with. System follows the desktop's own light/dark
// setting and switches with it while the program is running.
enum class Mode : int { System = 0, Light = 1, Dark = 2 };

struct Palette {
    RGB window;       // behind everything
    RGB pane;         // the form surface
    RGB card;         // a job / skill group card
    RGB cardEdge;
    RGB field;        // inside an edit box
    RGB text;
    RGB subtext;
    RGB accent;
    RGB previewBack;  // the desk the page sits on
    bool dark;
};

const Palette& lightPalette();
const Palette& darkPalette();

inline const Palette& paletteFor(bool dark) { return dark ? darkPalette() : lightPalette(); }

// The name shown in the theme selector, UTF-8, indexed by Mode.
extern const char* const kModeNames[3];

// Where the choice is remembered. One name, so both front ends read and write the
// same setting and a user who runs either sees the theme they picked.
extern const char kModeSetting[];

}  // namespace uicommon
}  // namespace cvb
