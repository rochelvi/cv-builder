#include "preview_pane.h"

#include <QGlyphRun>
#include <QList>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

#include "canvas.h"
#include "theme.h"

namespace cvb {
namespace qtui {
namespace {

constexpr int kPageGap = 16;  // margin around the sheet, in logical pixels
constexpr int kWheelStep = 60;

QByteArray faceBytes(const Font& font) {
    if (font.faceIndex() == 0) {
        const std::vector<uint8_t>& data = font.fileData();
        return QByteArray(reinterpret_cast<const char*>(data.data()),
                          static_cast<qsizetype>(data.size()));
    }
    // A face out of a .ttc: QRawFont takes a font file, not a face inside a
    // collection, so the face is repacked as a font of its own. The subsetter
    // already knows how to write one - asked for every glyph it copies the whole
    // face - and the glyph ids it produces are unchanged, which is what matters:
    // the ids the layout engine measured must stay the ids Qt draws.
    std::vector<uint16_t> all(static_cast<size_t>(std::max(font.numGlyphs(), 0)));
    for (size_t i = 0; i < all.size(); ++i) all[i] = static_cast<uint16_t>(i);
    const std::vector<uint8_t> packed = font.subset(all);
    return QByteArray(reinterpret_cast<const char*>(packed.data()),
                      static_cast<qsizetype>(packed.size()));
}

// Qt as a Canvas. Document points are mapped to logical pixels here rather than
// through the painter's transform, so that a glyph run can be asked for at the
// pixel size it will actually be drawn at - a scaled-up rasterisation of a small
// size is exactly the blur this avoids.
class QtCanvas : public Canvas {
public:
    using FaceLookup = std::function<const QRawFont&(bool bold, double pixelSize)>;

    QtCanvas(QPainter& painter, QPointF origin, double scale, FaceLookup faces)
        : painter_(painter), origin_(origin), scale_(scale), faces_(std::move(faces)) {}

    void fillRect(double x, double y, double width, double height, RGB color) override {
        painter_.fillRect(QRectF(map(x, y), QSizeF(width * scale_, height * scale_)), toQt(color));
    }

    void drawLine(double x1, double y1, double x2, double y2, double width, RGB color) override {
        QPen pen(toQt(color));
        // Cosmetic below one pixel: a 0.5 pt rule must stay visible at 50 % zoom
        // rather than fading out, the same way it stays visible on paper.
        pen.setWidthF(std::max(width * scale_, 0.6));
        painter_.setPen(pen);
        painter_.drawLine(QLineF(map(x1, y1), map(x2, y2)));
    }

    void drawGlyphs(double x, double y, double size, bool bold, const GlyphRun& run,
                    RGB color) override {
        if (run.count == 0) return;
        const QRawFont& face = faces_(bold, size * scale_);
        if (!face.isValid()) return;

        QList<quint32> indexes;
        QList<QPointF> positions;
        indexes.reserve(static_cast<qsizetype>(run.count));
        positions.reserve(static_cast<qsizetype>(run.count));

        // Positions are relative to the point handed to drawGlyphRun, and they are
        // the advances the layout engine measured with - not whatever Qt would
        // choose - so a line ends exactly where the PDF puts it.
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
        return QPointF(origin_.x() + x * scale_, origin_.y() + y * scale_);
    }

    QPainter& painter_;
    QPointF origin_;
    double scale_;
    FaceLookup faces_;
};

}  // namespace

PreviewPane::PreviewPane(QWidget* parent) : QAbstractScrollArea(parent) {
    setFrameShape(QFrame::NoFrame);
    viewport()->setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
}

void PreviewPane::setFonts(const FontSet* fonts) {
    fonts_ = fonts;
    painter_.setFonts(fonts);
    refreshFaceCache();
    viewport()->update();
}

void PreviewPane::refreshFaceCache() {
    faces_[0].clear();
    faces_[1].clear();
    faceData_[0].clear();
    faceData_[1].clear();
    if (!fonts_ || !fonts_->valid()) return;
    faceData_[0] = faceBytes(fonts_->regular());
    faceData_[1] = faceBytes(fonts_->bold());
}

const QRawFont& PreviewPane::faceFor(bool bold, double pixelSize) {
    const int slot = bold ? 1 : 0;
    // Keyed to a tenth of a pixel: finer than any zoom step can distinguish, and
    // coarse enough that the cache does not grow one entry per repaint.
    const int key = static_cast<int>(std::lround(pixelSize * 10.0));
    auto found = faces_[slot].constFind(key);
    if (found != faces_[slot].constEnd()) return *found;

    // PreferFullHinting would snap stems to the pixel grid and move glyphs off the
    // positions the layout engine chose; the preview has to show where text will
    // actually be, so hinting that changes metrics is refused.
    QRawFont face(faceData_[slot], key / 10.0, QFont::PreferVerticalHinting);
    return *faces_[slot].insert(key, face);
}

void PreviewPane::setDocument(Document doc) {
    doc_ = std::move(doc);
    error_.clear();
    if (page_ >= static_cast<int>(doc_.pages.size()))
        page_ = std::max(0, static_cast<int>(doc_.pages.size()) - 1);
    updateScrollBars();
    viewport()->update();
    emit stateChanged();
}

void PreviewPane::setError(const QString& message) {
    error_ = message;
    viewport()->update();
}

int PreviewPane::pageCount() const { return std::max(1, static_cast<int>(doc_.pages.size())); }

void PreviewPane::setPage(int index) {
    const int limit = static_cast<int>(doc_.pages.size()) - 1;
    page_ = std::max(0, std::min(limit < 0 ? 0 : limit, index));
    verticalScrollBar()->setValue(0);
    updateScrollBars();
    viewport()->update();
    emit stateChanged();
}

void PreviewPane::setZoom(int percent) {
    zoom_ = std::max(50, std::min(300, percent));
    updateScrollBars();
    viewport()->update();
    emit stateChanged();
}

void PreviewPane::zoomToFit() {
    const double available = viewport()->height() - 2.0 * kPageGap;
    if (available <= 0) return;
    setZoom(static_cast<int>(available / kPageHeight * 100.0));
}

void PreviewPane::applyTheme() { viewport()->update(); }

double PreviewPane::pageWidthPx() const { return kPageWidth * scale(); }
double PreviewPane::pageHeightPx() const { return kPageHeight * scale(); }

void PreviewPane::updateScrollBars() {
    const int width = static_cast<int>(std::ceil(pageWidthPx())) + 2 * kPageGap;
    const int height = static_cast<int>(std::ceil(pageHeightPx())) + 2 * kPageGap;
    horizontalScrollBar()->setRange(0, std::max(0, width - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setRange(0, std::max(0, height - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setSingleStep(30);
    horizontalScrollBar()->setSingleStep(30);
}

QPointF PreviewPane::pageOrigin() const {
    // Centred while the sheet fits, pinned to the scroll position once it does
    // not - the same behaviour the Win32 preview has.
    const double width = pageWidthPx();
    double x = (viewport()->width() - width) / 2.0;
    if (width + 2 * kPageGap > viewport()->width()) x = kPageGap - horizontalScrollBar()->value();
    const double y = kPageGap - verticalScrollBar()->value();
    return QPointF(x, y);
}

void PreviewPane::paintEvent(QPaintEvent*) {
    QPainter painter(viewport());
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter.fillRect(viewport()->rect(), toQt(currentPalette().previewBack));

    if (!error_.isEmpty() || doc_.pages.empty() || !fonts_ || !fonts_->valid()) {
        painter.setPen(QColor(Qt::white));
        const QString text = error_.isEmpty() ? tr("Нет страниц") : error_;
        painter.drawText(viewport()->rect().adjusted(20, 20, -20, -20),
                         Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, text);
        return;
    }

    const int index = std::max(0, std::min(page_, static_cast<int>(doc_.pages.size()) - 1));
    const QPointF origin = pageOrigin();

    // A soft edge, so the sheet reads as paper on a desk rather than a rectangle
    // that happens to be a different colour.
    painter.fillRect(QRectF(origin + QPointF(2, 3), QSizeF(pageWidthPx() + 1, pageHeightPx() + 1)),
                     QColor(0, 0, 0, 64));

    QtCanvas canvas(painter, origin, scale(),
                    [this](bool bold, double pixelSize) -> const QRawFont& {
                        return faceFor(bold, pixelSize);
                    });
    painter_.paint(doc_.pages[static_cast<size_t>(index)], canvas);
}

void PreviewPane::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void PreviewPane::wheelEvent(QWheelEvent* event) {
    // Ctrl+wheel zooms, which is what every document viewer does; a plain wheel
    // scrolls.
    if (event->modifiers() & Qt::ControlModifier) {
        setZoom(zoom_ + (event->angleDelta().y() > 0 ? 10 : -10));
        event->accept();
        return;
    }
    const int delta = event->angleDelta().y();
    verticalScrollBar()->setValue(verticalScrollBar()->value() - delta * kWheelStep / 120);
    event->accept();
}

}  // namespace qtui
}  // namespace cvb
