#pragma once

#include <QColor>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QTimer>

#include <vector>

namespace vofa::app {

class AppController;

// Waveform widget. Rendering is done in C++ from fixed-size float snapshots:
// no QVariant/QML binding churn per sample, and only the visible pixel columns
// are touched. QML drives repaints through app.waveformChanged.
class WaveformItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(int axisFontPx READ axisFontPx WRITE setAxisFontPx NOTIFY axisFontPxChanged)

public:
    explicit WaveformItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QObject* controller() const;
    void setController(QObject* controller);
    [[nodiscard]] QColor gridColor() const { return gridColor_; }
    void setGridColor(const QColor& color);
    [[nodiscard]] int mode() const { return mode_; }
    void setMode(int mode);
    [[nodiscard]] int axisFontPx() const { return axisFontPx_; }
    void setAxisFontPx(int pixels);

    Q_INVOKABLE void resetY();
    Q_INVOKABLE void clearCursors();
    Q_INVOKABLE void zoomYToRange(qreal bottomNorm, qreal topNorm);

    void paint(QPainter* painter) override;

signals:
    void controllerChanged();
    void gridColorChanged();
    void modeChanged();
    void axisFontPxChanged();
    // Continuous zoom; factor < 1 zooms in, anchor in [0,1] along the axis.
    void zoomRequested(qreal factor, qreal anchorFraction);
    // Positive delta = move back in time (older samples).
    void panRequested(int deltaSamples);
    // Right-button long-press box selection.
    void areaZoomRequested(qreal startFraction, qreal endFraction);
    void menuRequested(qreal x, qreal y);
    void resetViewRequested();

protected:
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    AppController* controller_ = nullptr;
    std::vector<float> samples_;
    qreal hoverX_ = -1.0;

    bool dragging_ = false;
    bool pressMoved_ = false;
    bool shiftAtPress_ = false;
    qreal pressX_ = 0.0;
    qreal lastMouseX_ = 0.0;
    qreal cursorA_ = -1.0; // fraction of the plot width
    qreal cursorB_ = -1.0;

    qreal yScale_ = 1.0;
    qreal yOffset_ = 0.0;

    QTimer longPressTimer_;
    Qt::MouseButton pressedButton_ = Qt::NoButton;
    QPointF pressPosition_;
    QPointF lastPosition_;
    bool longPressTriggered_ = false;
    bool boxZoomActive_ = false;
    QPointF boxEnd_;

    [[nodiscard]] qreal plotUsableHeight() const;
    [[nodiscard]] qreal normalizedAtY(qreal y) const;
    QColor gridColor_ { QStringLiteral("#22303c") };
    int mode_ = 0; // 0 time, 1 XY, 2 FFT, 3 histogram
    int axisFontPx_ = 12;

    void paintSpectrum(QPainter* painter, qreal w, qreal h);
    void paintHistogram(QPainter* painter, qreal w, qreal h);
    void paintXY(QPainter* painter, qreal w, qreal h);
    void paintGrid(QPainter* painter, qreal w, qreal h);
};

} // namespace vofa::app
