#pragma once

#include <QColor>
#include <QPointF>
#include <QQuickItem>
#include <QStringList>
#include <QVariantList>

#include <vector>

class QSGGeometryNode;
class QSGFlatColorMaterial;
class QSGNode;

namespace vofa::app {

class AppController;

// Scene-graph based oscilloscope plot for the time-domain view.
// Geometry is computed on the GUI thread at the data/view rate and uploaded
// as plain line vertices on the render thread. No full-window texture uploads
// are involved, unlike the previous QQuickPaintedItem implementation.
class TimePlotItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)
    Q_PROPERTY(double spanSeconds READ spanSeconds WRITE setSpanSeconds NOTIFY spanChanged)
    Q_PROPERTY(double visibleSpanSeconds READ visibleSpanSeconds NOTIFY spanChanged)
    Q_PROPERTY(bool autoY READ autoY WRITE setAutoY NOTIFY autoYChanged)
    Q_PROPERTY(bool follow READ follow NOTIFY viewModeChanged)
    Q_PROPERTY(QString viewMode READ viewMode NOTIFY viewModeChanged)
    Q_PROPERTY(double yMaxVisible READ yMaxVisible NOTIFY yRangeChanged)
    Q_PROPERTY(double yMidVisible READ yMidVisible NOTIFY yRangeChanged)
    Q_PROPERTY(double yMinVisible READ yMinVisible NOTIFY yRangeChanged)
    Q_PROPERTY(bool hoverVisible READ hoverVisible NOTIFY hoverChanged)
    Q_PROPERTY(QVariantList hoverValues READ hoverValues NOTIFY hoverChanged)
    Q_PROPERTY(QVariantList hoverColors READ hoverColors NOTIFY hoverChanged)
    Q_PROPERTY(double hoverX READ hoverX NOTIFY hoverChanged)
    Q_PROPERTY(double hoverY READ hoverY NOTIFY hoverChanged)
    Q_PROPERTY(double hoverYValue READ hoverYValue NOTIFY hoverChanged)
    Q_PROPERTY(double hoverTimeSeconds READ hoverTimeSeconds NOTIFY hoverChanged)
    Q_PROPERTY(bool cursorsEnabled READ cursorsEnabled NOTIFY cursorChanged)
    Q_PROPERTY(double cursorAX READ cursorAX NOTIFY cursorChanged)
    Q_PROPERTY(double cursorBX READ cursorBX NOTIFY cursorChanged)
    Q_PROPERTY(bool cursorAVisible READ cursorAVisible NOTIFY cursorChanged)
    Q_PROPERTY(bool cursorBVisible READ cursorBVisible NOTIFY cursorChanged)
    Q_PROPERTY(double cursorTimeA READ cursorTimeA NOTIFY cursorChanged)
    Q_PROPERTY(double cursorTimeB READ cursorTimeB NOTIFY cursorChanged)
    Q_PROPERTY(double cursorDeltaSeconds READ cursorDeltaSeconds NOTIFY cursorChanged)
    Q_PROPERTY(QVariantList cursorValuesA READ cursorValuesA NOTIFY cursorChanged)
    Q_PROPERTY(QVariantList cursorValuesB READ cursorValuesB NOTIFY cursorChanged)
    Q_PROPERTY(QVariantList cursorDeltas READ cursorDeltas NOTIFY cursorChanged)
    Q_PROPERTY(QVariantList cursorColors READ cursorColors NOTIFY cursorChanged)

public:
    explicit TimePlotItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QObject* controller() const;
    void setController(QObject* controller);
    [[nodiscard]] QColor gridColor() const { return gridColor_; }
    void setGridColor(const QColor& color);
    [[nodiscard]] double spanSeconds() const { return spanSeconds_; }
    void setSpanSeconds(double seconds);
    [[nodiscard]] double visibleSpanSeconds() const { return visibleSpanSeconds_; }
    [[nodiscard]] bool autoY() const { return autoY_; }
    void setAutoY(bool enabled);
    [[nodiscard]] bool follow() const { return follow_; }
    [[nodiscard]] QString viewMode() const;
    [[nodiscard]] double yMaxVisible() const { return yMax_; }
    [[nodiscard]] double yMidVisible() const { return (yMin_ + yMax_) * 0.5; }
    [[nodiscard]] double yMinVisible() const { return yMin_; }
    [[nodiscard]] bool hoverVisible() const { return hoverVisible_; }
    [[nodiscard]] QVariantList hoverValues() const { return hoverValues_; }
    [[nodiscard]] QVariantList hoverColors() const { return hoverColors_; }
    [[nodiscard]] double hoverX() const { return hoverX_; }
    [[nodiscard]] double hoverY() const { return hoverY_; }
    [[nodiscard]] double hoverYValue() const { return hoverYValue_; }
    [[nodiscard]] double hoverTimeSeconds() const;
    [[nodiscard]] bool cursorsEnabled() const { return cursorsEnabled_; }
    [[nodiscard]] double cursorAX() const;
    [[nodiscard]] double cursorBX() const;
    [[nodiscard]] bool cursorAVisible() const;
    [[nodiscard]] bool cursorBVisible() const;
    [[nodiscard]] double cursorTimeA() const;
    [[nodiscard]] double cursorTimeB() const;
    [[nodiscard]] double cursorDeltaSeconds() const;
    [[nodiscard]] QVariantList cursorValuesA() const { return cursorValuesA_; }
    [[nodiscard]] QVariantList cursorValuesB() const { return cursorValuesB_; }
    [[nodiscard]] QVariantList cursorDeltas() const { return cursorDeltas_; }
    [[nodiscard]] QVariantList cursorColors() const { return cursorColors_; }

    Q_INVOKABLE void showAll();
    Q_INVOKABLE void showFollow();
    Q_INVOKABLE void resetY();
    Q_INVOKABLE void setYRange(double start, double span);
    Q_INVOKABLE void setCursorsEnabled(bool enabled);
    Q_INVOKABLE void resetCursors();

signals:
    void controllerChanged();
    void gridColorChanged();
    void spanChanged();
    void autoYChanged();
    void viewModeChanged();
    void yRangeChanged();
    void hoverChanged();
    void cursorChanged();
    void menuRequested(qreal x, qreal y);

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void refreshView();
    void refreshGrid();
    void updateHover(qreal x, qreal y);
    void applyBoxZoom(const QPointF& start, const QPointF& end);
    void applyDragZoom(const QPointF& start, const QPointF& end);
    void placeCursorsAtFractions(double aFraction, double bFraction);
    void refreshCursorReadouts();
    [[nodiscard]] double sampleForX(qreal x) const;
    [[nodiscard]] bool cursorSampleVisible(double sample) const;

    struct Curve {
        QSGGeometryNode* node = nullptr;
        QSGFlatColorMaterial* material = nullptr;
        QColor color;
        std::vector<QPointF> vertices;
    };

    QSGNode* root_ = nullptr;
    QSGGeometryNode* gridMinorNode_ = nullptr;
    QSGGeometryNode* gridMajorNode_ = nullptr;
    QSGFlatColorMaterial* gridMinorMaterial_ = nullptr;
    QSGFlatColorMaterial* gridMajorMaterial_ = nullptr;
    QSGGeometryNode* hoverNode_ = nullptr;
    QSGFlatColorMaterial* hoverMaterial_ = nullptr;
    QSGGeometryNode* hoverHNode_ = nullptr;
    QSGFlatColorMaterial* hoverHMaterial_ = nullptr;
    QSGGeometryNode* triggerNode_ = nullptr;
    QSGFlatColorMaterial* triggerMaterial_ = nullptr;
    QSGGeometryNode* boxLineNode_ = nullptr;
    QSGFlatColorMaterial* boxLineMaterial_ = nullptr;
    QSGGeometryNode* boxFillNode_ = nullptr;
    QSGFlatColorMaterial* boxFillMaterial_ = nullptr;

    AppController* controller_ = nullptr;
    QColor gridColor_ { QStringLiteral("#22303c") };
    double spanSeconds_ = 10.0;
    double visibleSpanSeconds_ = 10.0;
    bool autoY_ = true;
    bool follow_ = true;
    bool viewAll_ = false;
    double yMin_ = -1.0;
    double yMax_ = 1.0;
    double frozenEnd_ = 0.0;

    double viewStartSample_ = 0.0;
    double viewEndSample_ = 0.0;
    double viewSpanSamples_ = 0.0;
    double dataStart_ = 0.0;
    double dataEnd_ = 0.0;
    int activeChannels_ = 0;
    qint64 lastYLabelMs_ = 0;

    std::vector<Curve> curves_;
    std::vector<QPointF> gridMinorVertices_;
    std::vector<QPointF> gridMajorVertices_;
    std::vector<QPointF> hoverVertices_;
    std::vector<QPointF> hoverHVertices_;
    std::vector<QPointF> triggerVertices_;
    int handledTriggerRevision_ = -1;
    std::vector<QPointF> boxLineVertices_;
    std::vector<QPointF> boxFillVertices_;
    bool gridDirty_ = true;

    bool hoverVisible_ = false;
    qreal hoverX_ = -1.0;
    qreal hoverY_ = -1.0;
    double hoverYValue_ = 0.0;
    QVariantList hoverValues_;
    QVariantList hoverColors_;

    bool cursorsEnabled_ = false;
    double cursorASample_ = 0.0;
    double cursorBSample_ = 0.0;
    enum class DragCursor { None, A, B };
    DragCursor dragCursor_ = DragCursor::None;
    QVariantList cursorValuesA_;
    QVariantList cursorValuesB_;
    QVariantList cursorDeltas_;
    QVariantList cursorColors_;

    Qt::MouseButton pressedButton_ = Qt::NoButton;
    QPointF pressPosition_;
    QPointF lastPosition_;
    bool pressMoved_ = false;
    bool boxActive_ = false;
    double rightBaseSpanSamples_ = 0.0;
    double rightBaseViewStart_ = 0.0;
    double rightBaseYMin_ = 0.0;
    double rightBaseYMax_ = 0.0;
};

} // namespace vofa::app
