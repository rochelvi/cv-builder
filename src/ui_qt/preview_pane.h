// The live preview.
//
// Draws the laid-out document through the shared painter, with the same glyph ids
// and the same advances the PDF writer embeds - the faces handed to Qt are the
// very files the layout engine measured with, so this is not an approximation of
// the export, it is the export rasterised.
#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QHash>
#include <QRawFont>
#include <QString>

#include "document_painter.h"
#include "font.h"
#include "layout.h"

namespace cvb {
namespace qtui {

class PreviewPane : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit PreviewPane(QWidget* parent = nullptr);

    void setFonts(const FontSet* fonts);
    void setDocument(Document doc);
    void setError(const QString& message);

    int pageCount() const;
    int page() const { return page_; }
    void setPage(int index);

    int zoom() const { return zoom_; }
    void setZoom(int percent);
    // Chooses the zoom that makes the whole sheet fit the viewport, which is what
    // a window narrower than the page wants.
    void zoomToFit();

    void applyTheme();

signals:
    void stateChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // One QRawFont per face and pixel size actually drawn. A document uses a
    // handful of sizes, and a zoom step changes all of them at once, so this is
    // rebuilt rarely and reused on every repaint.
    const QRawFont& faceFor(bool bold, double pixelSize);
    void refreshFaceCache();
    void updateScrollBars();
    double scale() const { return zoom_ / 100.0; }
    double pageWidthPx() const;
    double pageHeightPx() const;
    QPointF pageOrigin() const;

    const FontSet* fonts_ = nullptr;
    DocumentPainter painter_;
    Document doc_;
    QString error_;
    int page_ = 0;
    int zoom_ = 100;

    QByteArray faceData_[2];
    QHash<int, QRawFont> faces_[2];  // keyed on pixel size in tenths
};

}  // namespace qtui
}  // namespace cvb
