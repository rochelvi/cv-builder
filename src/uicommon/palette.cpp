#include "palette.h"

namespace cvb {
namespace uicommon {
namespace {

constexpr RGB rgb(uint8_t r, uint8_t g, uint8_t b) { return RGB{r, g, b}; }

}  // namespace

const Palette& lightPalette() {
    static const Palette palette = {
        rgb(0xF3, 0xF4, 0xF6),  // window
        rgb(0xFF, 0xFF, 0xFF),  // pane
        rgb(0xF8, 0xF9, 0xFB),  // card
        rgb(0xDF, 0xE3, 0xE8),  // cardEdge
        rgb(0xFF, 0xFF, 0xFF),  // field
        rgb(0x1B, 0x1F, 0x25),  // text
        rgb(0x60, 0x6A, 0x78),  // subtext
        rgb(0x1E, 0x6F, 0xD9),  // accent
        rgb(0x5A, 0x5F, 0x66),  // previewBack
        false,
    };
    return palette;
}

const Palette& darkPalette() {
    static const Palette palette = {
        rgb(0x20, 0x20, 0x20),  // window
        rgb(0x1B, 0x1B, 0x1B),  // pane
        rgb(0x2A, 0x2B, 0x2E),  // card
        rgb(0x3D, 0x3F, 0x44),  // cardEdge
        rgb(0x2D, 0x2E, 0x31),  // field
        rgb(0xE6, 0xE8, 0xEA),  // text
        rgb(0x9A, 0xA1, 0xAC),  // subtext
        rgb(0x4C, 0x9B, 0xF5),  // accent
        rgb(0x2A, 0x2C, 0x2F),  // previewBack
        true,
    };
    return palette;
}

const char* const kModeNames[3] = {"Как в системе", "Светлая", "Тёмная"};

const char kModeSetting[] = "ThemeMode";

}  // namespace uicommon
}  // namespace cvb
