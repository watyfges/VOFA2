#include "TimePlotItem.h"

#include "AppController.h"

#include <QCursor>
#include <QDateTime>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vofa::app {
namespace {

constexpr double kAutoYFill = 0.618;
constexpr double kTopMargin = 6.0;
constexpr double kBottomMargin = 20.0;
// Logical-pixel half widths. Thick lines are built as triangles instead of
// relying on glLineWidth, which many Windows/ANGLE/RHI backends clamp to 1.
constexpr qreal kCurveHalfWidth = 1.0;
constexpr qreal kGridMinorHalfWidth = 0.6;
constexpr qreal kGridMajorHalfWidth = 1.0;

QPointF point(qreal x, qreal y)
{
    return QPointF(x, y);
}

// Appends one thick line segment (two triangles) to a DrawTriangles node.
void addThickSegment(std::vector<QPointF>& out,
                     const QPointF& a,
                     const QPointF& b,
                     qreal halfWidth)
{
    const qreal dx = b.x() - a.x();
    const qreal dy = b.y() - a.y();
    const qreal length = std::hypot(dx, dy);
    if (length < 1.0e-6)
        return;

    const qreal nx = -dy / length * halfWidth;
    const qreal ny = dx / length * halfWidth;
    const QPointF p1(a.x() + nx, a.y() + ny);
    const QPointF p2(a.x() - nx, a.y() - ny);
    const QPointF p3(b.x() - nx, b.y() - ny);
    const QPointF p4(b.x() + nx, b.y() + ny);
    out.push_back(p1);
    out.push_back(p2);
    out.push_back(p3);
    out.push_back(p1);
    out.push_back(p3);
    out.push_back(p4);
}

} // namespace

TimePlotItem::TimePlotItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents, true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setCursor(QCursor(Qt::CrossCursor));
    refreshGrid();
}

QObject* TimePlotItem::controller() const
{
    return controller_;
}

void TimePlotItem::setController(QObject* controller)
{
    auto* typed = qobject_cast<AppController*>(controller);
    if (typed == controller_)
        return;
    if (controller_ != nullptr)
        disconnect(controller_, nullptr, this, nullptr);
    controller_ = typed;
    if (controller_ != nullptr) {
        connect(controller_, &AppController::waveformChanged,
                this, &TimePlotItem::refreshView);
        connect(controller_, &AppController::channelSettingsChanged,
                this, &TimePlotItem::refreshView);
        connect(controller_, &AppController::statsChanged,
                this, &TimePlotItem::refreshView);
        connect(controller_, &AppController::triggerChanged,
                this, &TimePlotItem::refreshView);
    }
    emit controllerChanged();
    refreshView();
}

void TimePlotItem::setGridColor(const QColor& color)
{
    if (gridColor_ == color)
        return;
    gridColor_ = color;
    emit gridColorChanged();
    update();
}

void TimePlotItem::setSpanSeconds(double seconds)
{
    seconds = std::clamp(seconds, 0.05, 3600.0);
    if (std::abs(spanSeconds_ - seconds) < 1.0e-9)
        return;
    spanSeconds_ = seconds;
    viewAll_ = false;
    emit spanChanged();
    refreshView();
}

void TimePlotItem::setAutoY(bool enabled)
{
    if (autoY_ == enabled)
        return;
    autoY_ = enabled;
    emit autoYChanged();
    refreshView();
}

QString TimePlotItem::viewMode() const
{
    if (follow_)
        return QStringLiteral("自动跟随");
    if (viewAll_)
        return QStringLiteral("显示全部");
    return QStringLiteral("实时刷新停止");
}

void TimePlotItem::showAll()
{
    viewAll_ = true;
    follow_ = false;
    autoY_ = true;
    if (controller_ != nullptr) {
        const double rate = std::max(1.0, controller_->samplesPerSecond());
        const double start = static_cast<double>(controller_->dataAbsoluteStart());
        const double end = static_cast<double>(controller_->dataAbsoluteEnd());
        if (end > start)
            spanSeconds_ = std::clamp((end - start) / rate, 0.05, 3600.0);
    }
    emit spanChanged();
    emit autoYChanged();
    emit viewModeChanged();
    refreshView();
}

void TimePlotItem::showFollow()
{
    viewAll_ = false;
    follow_ = true;
    emit viewModeChanged();
    refreshView();
}

void TimePlotItem::resetY()
{
    autoY_ = true;
    emit autoYChanged();
    refreshView();
}

void TimePlotItem::setYRange(double start, double span)
{
    if (!std::isfinite(start) || !std::isfinite(span) || span <= 0.0)
        return;
    autoY_ = false;
    yMin_ = start;
    yMax_ = start + std::max(span, 1.0e-9);
    emit autoYChanged();
    emit yRangeChanged();
    refreshView();
}

double TimePlotItem::hoverTimeSeconds() const
{
    if (!hoverVisible_ || width() <= 1.0 || viewSpanSamples_ <= 0.0)
        return 0.0;
    const double sample = viewStartSample_
        + static_cast<double>(hoverX_) / static_cast<double>(width())
            * viewSpanSamples_;
    const double rate = controller_ != nullptr
        ? std::max(1.0, controller_->samplesPerSecond())
        : 1.0;
    // Match the static time axis, where the right edge is 0s and values to
    // the left are negative.
    return (sample - viewEndSample_) / rate;
}

bool TimePlotItem::cursorSampleVisible(double sample) const
{
    return cursorsEnabled_ && width() > 1.0 && viewSpanSamples_ > 0.0
        && sample >= viewStartSample_ && sample <= viewEndSample_;
}

double TimePlotItem::cursorAX() const
{
    if (!cursorSampleVisible(cursorASample_))
        return -1.0;
    return (cursorASample_ - viewStartSample_) / viewSpanSamples_ * width();
}

double TimePlotItem::cursorBX() const
{
    if (!cursorSampleVisible(cursorBSample_))
        return -1.0;
    return (cursorBSample_ - viewStartSample_) / viewSpanSamples_ * width();
}

bool TimePlotItem::cursorAVisible() const
{
    return cursorSampleVisible(cursorASample_);
}

bool TimePlotItem::cursorBVisible() const
{
    return cursorSampleVisible(cursorBSample_);
}

double TimePlotItem::cursorTimeA() const
{
    const double rate = controller_ != nullptr
        ? std::max(1.0, controller_->samplesPerSecond())
        : 1.0;
    return cursorASample_ / rate;
}

double TimePlotItem::cursorTimeB() const
{
    const double rate = controller_ != nullptr
        ? std::max(1.0, controller_->samplesPerSecond())
        : 1.0;
    return cursorBSample_ / rate;
}

double TimePlotItem::cursorDeltaSeconds() const
{
    return std::abs(cursorTimeB() - cursorTimeA());
}

double TimePlotItem::sampleForX(qreal x) const
{
    if (width() <= 1.0 || viewSpanSamples_ <= 0.0)
        return cursorASample_;
    const double fraction = std::clamp(
        static_cast<double>(x) / static_cast<double>(width()), 0.0, 1.0);
    const double sample = viewStartSample_ + fraction * viewSpanSamples_;
    if (dataEnd_ > dataStart_)
        return std::clamp(sample, dataStart_, dataEnd_);
    return sample;
}

void TimePlotItem::placeCursorsAtFractions(double aFraction, double bFraction)
{
    cursorASample_ = sampleForX(width() * std::clamp(aFraction, 0.0, 1.0));
    cursorBSample_ = sampleForX(width() * std::clamp(bFraction, 0.0, 1.0));
}

void TimePlotItem::refreshCursorReadouts()
{
    cursorValuesA_.clear();
    cursorValuesB_.clear();
    cursorDeltas_.clear();
    cursorColors_.clear();
    if (!cursorsEnabled_ || controller_ == nullptr)
        return;

    const auto a = static_cast<std::uint64_t>(std::llround(cursorASample_));
    const auto b = static_cast<std::uint64_t>(std::llround(cursorBSample_));
    const int channels = std::clamp(controller_->waveformChannelCount(), 0, 64);
    for (int channel = 0; channel < channels; ++channel) {
        const float valueA = controller_->sampleAt(channel, a);
        const float valueB = controller_->sampleAt(channel, b);
        const bool haveA = std::isfinite(valueA);
        const bool haveB = std::isfinite(valueB);
        cursorValuesA_.push_back(haveA ? QVariant(static_cast<double>(valueA))
                                       : QVariant());
        cursorValuesB_.push_back(haveB ? QVariant(static_cast<double>(valueB))
                                       : QVariant());
        cursorDeltas_.push_back(
            (haveA && haveB)
                ? QVariant(static_cast<double>(valueB) - static_cast<double>(valueA))
                : QVariant());
        QColor color = controller_->channelColor(channel);
        if (!color.isValid())
            color = QColor(QStringLiteral("#1abc9e"));
        cursorColors_.push_back(color);
    }
}

void TimePlotItem::setCursorsEnabled(bool enabled)
{
    if (cursorsEnabled_ == enabled)
        return;
    cursorsEnabled_ = enabled;
    dragCursor_ = DragCursor::None;

    if (enabled) {
        // Measurement mode stops live scrolling so the A/B lines stay useful;
        // 跟随 / double-click resumes live view.
        if (follow_ && dataEnd_ > dataStart_) {
            follow_ = false;
            viewAll_ = false;
            frozenEnd_ = viewEndSample_;
            emit viewModeChanged();
        }
        refreshView();
        placeCursorsAtFractions(0.25, 0.75);
    }
    refreshCursorReadouts();
    emit cursorChanged();
    update();
}

void TimePlotItem::resetCursors()
{
    if (!cursorsEnabled_)
        return;
    refreshView();
    placeCursorsAtFractions(0.25, 0.75);
    refreshCursorReadouts();
    emit cursorChanged();
    update();
}

void TimePlotItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    refreshGrid();
    refreshView();
}

void TimePlotItem::refreshGrid()
{
    gridMinorVertices_.clear();
    gridMajorVertices_.clear();
    const qreal w = width();
    const qreal h = height();
    if (w <= 1.0 || h <= 1.0) {
        gridDirty_ = true;
        return;
    }

    for (int i = 0; i <= 20; ++i) {
        const qreal x = w * static_cast<qreal>(i) / 20.0;
        const bool major = i % 5 == 0;
        auto& target = major ? gridMajorVertices_ : gridMinorVertices_;
        addThickSegment(target, point(x, 0.0), point(x, h),
                        major ? kGridMajorHalfWidth : kGridMinorHalfWidth);
    }
    for (int i = 0; i <= 10; ++i) {
        const qreal y = h * static_cast<qreal>(i) / 10.0;
        const bool major = i % 5 == 0;
        auto& target = major ? gridMajorVertices_ : gridMinorVertices_;
        addThickSegment(target, point(0.0, y), point(w, y),
                        major ? kGridMajorHalfWidth : kGridMinorHalfWidth);
    }
    gridDirty_ = true;
}

void TimePlotItem::refreshView()
{
    gridDirty_ = true;
    if (width() <= 1.0 || height() <= 1.0) {
        update();
        return;
    }

    if (controller_ == nullptr) {
        activeChannels_ = 0;
        curves_.clear();
        hoverVertices_.clear();
        update();
        return;
    }

    const double dataStart = static_cast<double>(controller_->dataAbsoluteStart());
    const double dataEnd = static_cast<double>(controller_->dataAbsoluteEnd());
    dataStart_ = dataStart;
    dataEnd_ = dataEnd;

    // A captured trigger freezes the scope at the current end of data. The
    // controller captures a short post-trigger window before setting ready.
    if (controller_->triggerReady()
        && controller_->triggerRevision() != handledTriggerRevision_) {
        handledTriggerRevision_ = controller_->triggerRevision();
        follow_ = false;
        viewAll_ = false;
        frozenEnd_ = dataEnd;
        emit viewModeChanged();
    }

    const double total = std::max(0.0, dataEnd - dataStart);
    const double rate = std::max(1.0, controller_->samplesPerSecond());

    double span = 0.0;
    if (total > 0.0) {
        span = viewAll_ ? total
                        : std::clamp(spanSeconds_ * rate,
                                     std::min(16.0, total), total);
    }

    double viewEnd = dataEnd;
    if (follow_) {
        // Keep the newest sample slightly inside the right edge.
        viewEnd = dataEnd + span * 0.006;
    } else if (total > 0.0) {
        viewEnd = std::clamp(frozenEnd_, dataStart + span, dataEnd);
    }
    const double viewStart = viewEnd - span;
    viewStartSample_ = viewStart;
    viewEndSample_ = viewEnd;
    viewSpanSamples_ = span;

    const int channels = std::clamp(controller_->waveformChannelCount(), 0, 64);
    activeChannels_ = channels;
    if (static_cast<int>(curves_.size()) < channels)
        curves_.resize(static_cast<std::size_t>(channels));
    for (int channel = channels; channel < static_cast<int>(curves_.size()); ++channel)
        curves_[static_cast<std::size_t>(channel)].vertices.clear();

    // One column per pixel is only needed when downsampling. When there are
    // fewer samples than pixels (e.g. 100 Hz over a 5 s window), repeating the
    // nearest sample per column produced a staircase; use one column per
    // sample instead so the curve is a smooth polyline.
    const int pixelColumns = std::clamp(
        static_cast<int>(std::ceil(width())), 1, 8192);
    const int sampleColumns = std::clamp(
        static_cast<int>(std::llround(std::max(1.0, span))), 1, 8192);
    const int columns = std::min(pixelColumns, sampleColumns);
    const qreal w = width();
    const qreal h = height();
    const qreal plotTop = kTopMargin;
    const qreal plotHeight = std::max<qreal>(h - kTopMargin - kBottomMargin, 1.0);

    // Vertical marker at the captured trigger sample (amber, like a scope's
    // trigger position indicator).
    triggerVertices_.clear();
    if (controller_->triggerSample() > 0 && span > 0.0) {
        const double trigger = static_cast<double>(controller_->triggerSample());
        if (trigger >= viewStart && trigger <= viewEnd) {
            const qreal x = static_cast<qreal>((trigger - viewStart) / span) * w;
            if (x >= 0.0 && x <= w) {
                triggerVertices_.push_back(point(x, plotTop));
                triggerVertices_.push_back(point(x, plotTop + plotHeight));
            }
        }
    }

    std::vector<std::vector<float>> channelSamples(static_cast<std::size_t>(channels));
    double dataMin = std::numeric_limits<double>::infinity();
    double dataMax = -std::numeric_limits<double>::infinity();
    bool haveData = false;

    for (int channel = 0; channel < channels; ++channel) {
        auto& samples = channelSamples[static_cast<std::size_t>(channel)];
        auto& curve = curves_[static_cast<std::size_t>(channel)];
        curve.vertices.clear();
        if (!controller_->channelVisible(channel))
            continue;

        QColor color = controller_->channelColor(channel);
        if (!color.isValid())
            color = QColor(QStringLiteral("#1abc9e"));
        curve.color = color;

        controller_->buildWaveformRange(
            channel, static_cast<std::uint64_t>(std::llround(viewStart)),
            static_cast<std::size_t>(std::llround(span)), columns, samples);
        if (samples.empty())
            continue;
        haveData = true;
        for (std::size_t i = 0; i + 1 < samples.size(); i += 2) {
            dataMin = std::min(dataMin, static_cast<double>(samples[i]));
            dataMax = std::max(dataMax, static_cast<double>(samples[i + 1]));
        }
    }

    if (autoY_ && haveData && std::isfinite(dataMin) && std::isfinite(dataMax)) {
        double dataSpan = dataMax - dataMin;
        if (dataSpan < 1.0e-9) {
            const double center = (dataMin + dataMax) * 0.5;
            dataSpan = std::max(1.0e-6, std::abs(center) * 0.01);
        }
        const double axisSpan = dataSpan / kAutoYFill;
        const double center = (dataMin + dataMax) * 0.5;
        yMin_ = center - axisSpan * 0.5;
        yMax_ = center + axisSpan * 0.5;
    }
    if (!std::isfinite(yMin_) || !std::isfinite(yMax_) || yMax_ - yMin_ < 1.0e-9) {
        yMin_ = -1.0;
        yMax_ = 1.0;
    }

    const double ySpan = yMax_ - yMin_;
    for (int channel = 0; channel < channels; ++channel) {
        const auto& samples = channelSamples[static_cast<std::size_t>(channel)];
        auto& vertices = curves_[static_cast<std::size_t>(channel)].vertices;
        if (samples.size() < 2)
            continue;
        const int pairs = static_cast<int>(samples.size() / 2);
        vertices.reserve(static_cast<std::size_t>(pairs) * 12);
        qreal previousX = 0.0;
        qreal previousMid = 0.0;
        for (int i = 0; i < pairs; ++i) {
            const qreal x = (static_cast<qreal>(i) + 0.5) * w / pairs;
            const qreal yLow = plotTop
                + (yMax_ - samples[static_cast<std::size_t>(i) * 2]) / ySpan * plotHeight;
            const qreal yHigh = plotTop
                + (yMax_ - samples[static_cast<std::size_t>(i) * 2 + 1]) / ySpan
                    * plotHeight;
            // Vertical min/max bar for this pixel column.
            addThickSegment(vertices, point(x, yLow), point(x, yHigh),
                            kCurveHalfWidth);
            const qreal mid = (yLow + yHigh) * 0.5;
            if (i > 0) {
                addThickSegment(vertices, point(previousX, previousMid),
                                point(x, mid), kCurveHalfWidth);
            }
            previousX = x;
            previousMid = mid;
        }
    }

    if (std::abs(visibleSpanSeconds_ - (span / rate)) > 1.0e-4) {
        visibleSpanSeconds_ = span / rate;
        emit spanChanged();
    }

    if (hoverVisible_)
        updateHover(hoverX_, hoverY_);
    else {
        hoverVertices_.clear();
        hoverHVertices_.clear();
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastYLabelMs_ >= 100) {
        lastYLabelMs_ = now;
        emit yRangeChanged();
    }
    refreshCursorReadouts();
    if (cursorsEnabled_)
        emit cursorChanged();
    emit hoverChanged();
    update();
}

void TimePlotItem::updateHover(qreal x, qreal y)
{
    if (controller_ == nullptr || width() <= 1.0 || dataEnd_ <= dataStart_
        || viewSpanSamples_ <= 0.0) {
        hoverVisible_ = false;
        hoverVertices_.clear();
        hoverHVertices_.clear();
        emit hoverChanged();
        update();
        return;
    }

    if (x < 0.0 || x > width() || y < 0.0 || y > height()) {
        hoverVisible_ = false;
        hoverVertices_.clear();
        hoverHVertices_.clear();
        emit hoverChanged();
        update();
        return;
    }

    const double fraction = std::clamp(x / width(), 0.0, 1.0);
    const double sample =
        viewStartSample_ + fraction * viewSpanSamples_;
    const auto index = static_cast<std::uint64_t>(std::llround(sample));
    const auto start = static_cast<std::uint64_t>(std::llround(dataStart_));
    const auto end = static_cast<std::uint64_t>(std::llround(dataEnd_));
    if (index < start || index >= end) {
        hoverVisible_ = false;
        hoverVertices_.clear();
        hoverHVertices_.clear();
        emit hoverChanged();
        update();
        return;
    }

    hoverValues_.clear();
    hoverColors_.clear();
    for (int channel = 0; channel < activeChannels_; ++channel) {
        const float value = controller_->sampleAt(channel, index);
        if (!std::isfinite(value))
            continue;
        hoverValues_.push_back(static_cast<double>(value));
        QColor color = controller_->channelColor(channel);
        if (!color.isValid())
            color = QColor(QStringLiteral("#1abc9e"));
        hoverColors_.push_back(color);
    }

    hoverVisible_ = true;
    hoverX_ = x;
    hoverY_ = y;
    const double plotH = std::max<qreal>(height() - kTopMargin - kBottomMargin, 1.0);
    const double fy = std::clamp(1.0 - (y - kTopMargin) / plotH, 0.0, 1.0);
    hoverYValue_ = yMin_ + fy * (yMax_ - yMin_);
    hoverVertices_ = {point(x, 0.0), point(x, height())};
    hoverHVertices_ = {point(0.0, y), point(width(), y)};
    emit hoverChanged();
    update();
}

void TimePlotItem::hoverMoveEvent(QHoverEvent* event)
{
    updateHover(event->position().x(), event->position().y());
    event->accept();
}

void TimePlotItem::hoverLeaveEvent(QHoverEvent* event)
{
    hoverVisible_ = false;
    hoverVertices_.clear();
    hoverHVertices_.clear();
    emit hoverChanged();
    update();
    event->accept();
}

void TimePlotItem::applyBoxZoom(const QPointF& start, const QPointF& end)
{
    if (width() <= 1.0 || height() <= 1.0 || viewSpanSamples_ <= 0.0)
        return;
    const qreal boxWidth = std::abs(end.x() - start.x());
    const qreal boxHeight = std::abs(end.y() - start.y());
    // Ignore accidental tiny boxes; they would produce an extreme zoom and
    // the waveform can vanish from the view.
    if (boxWidth < 12.0 || boxHeight < 12.0)
        return;

    const double x0 = std::clamp(std::min(start.x(), end.x()) / width(), 0.0, 1.0);
    const double x1 = std::clamp(std::max(start.x(), end.x()) / width(), 0.0, 1.0);
    if (x1 - x0 < 0.005)
        return;

    const double plotTop = kTopMargin;
    const double plotHeight = std::max<qreal>(height() - kTopMargin - kBottomMargin, 1.0);
    const double yTop = std::clamp(
        1.0 - (std::min(start.y(), end.y()) - plotTop) / plotHeight, 0.0, 1.0);
    const double yBottom = std::clamp(
        1.0 - (std::max(start.y(), end.y()) - plotTop) / plotHeight, 0.0, 1.0);

    const double oldSpan = viewSpanSamples_;
    const double newSpan = std::clamp((x1 - x0) * oldSpan,
                                      std::max(16.0, oldSpan * 0.02),
                                      oldSpan);
    const double newStart = viewStartSample_ + x0 * oldSpan;
    spanSeconds_ = std::clamp(newSpan / std::max(1.0, controller_->samplesPerSecond()),
                              0.05, 3600.0);
    frozenEnd_ = newStart + newSpan;
    follow_ = false;
    viewAll_ = false;

    // Only zoom Y when the selection has a reasonable vertical extent.
    // A very flat box would otherwise collapse the Y range and make every
    // curve leave the screen.
    if (boxHeight >= plotHeight * 0.08 && yTop - yBottom >= 0.01) {
        const double yRange = yMax_ - yMin_;
        // fy is measured from the bottom of the plot (0 = bottom, 1 = top).
        const double low = yMin_ + yBottom * yRange;
        const double high = yMin_ + yTop * yRange;
        const double selected = std::abs(high - low);
        const double clamped = std::clamp(
            selected, std::max(1.0e-9, yRange * 0.02), yRange);
        yMin_ = std::min(low, high);
        yMax_ = yMin_ + clamped;
        autoY_ = false;
    }

    emit spanChanged();
    emit autoYChanged();
    emit viewModeChanged();
    refreshView();
}

void TimePlotItem::applyDragZoom(const QPointF& start, const QPointF& end)
{
    if (width() <= 1.0 || height() <= 1.0 || viewSpanSamples_ <= 0.0)
        return;
    const qreal dx = end.x() - start.x();
    if (std::abs(dx) < 3.0)
        return;

    const double factor = std::clamp(std::pow(1.5, -dx / 100.0), 0.2, 5.0);
    const double fx = std::clamp(start.x() / width(), 0.0, 1.0);
    const double fy = std::clamp(
        1.0 - (start.y() - kTopMargin)
            / std::max<qreal>(height() - kTopMargin - kBottomMargin, 1.0),
        0.0, 1.0);

    const double anchorSample = viewStartSample_ + fx * viewSpanSamples_;
    const double newSpan = std::clamp(viewSpanSamples_ * factor, 16.0, 262144.0);
    frozenEnd_ = anchorSample + (1.0 - fx) * newSpan;
    spanSeconds_ = std::clamp(newSpan / std::max(1.0, controller_->samplesPerSecond()),
                              0.05, 3600.0);

    const double yRange = yMax_ - yMin_;
    const double anchorValue = yMin_ + fy * yRange;
    const double newYRange = std::max(1.0e-9, yRange * factor);
    yMin_ = anchorValue - fy * newYRange;
    yMax_ = yMin_ + newYRange;

    follow_ = false;
    viewAll_ = false;
    autoY_ = false;
    emit spanChanged();
    emit autoYChanged();
    emit viewModeChanged();
    refreshView();
}

void TimePlotItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton
        && event->button() != Qt::RightButton) {
        event->ignore();
        return;
    }
    pressedButton_ = event->button();
    pressPosition_ = event->position();
    lastPosition_ = pressPosition_;
    pressMoved_ = false;
    boxActive_ = false;
    boxLineVertices_.clear();
    boxFillVertices_.clear();

    // Cursor mode: Shift+left places/moves A, Ctrl+left places/moves B, and
    // pressing near an existing A/B line drags it. Everything else keeps the
    // normal pan/zoom gestures.
    if (cursorsEnabled_ && event->button() == Qt::LeftButton) {
        const auto modifiers = event->modifiers();
        DragCursor target = DragCursor::None;
        if (modifiers & Qt::ShiftModifier) {
            target = DragCursor::A;
        } else if (modifiers & Qt::ControlModifier) {
            target = DragCursor::B;
        } else {
            const qreal x = event->position().x();
            const double ax = cursorAX();
            const double bx = cursorBX();
            if (ax >= 0.0 && std::abs(x - ax) <= 8.0)
                target = DragCursor::A;
            else if (bx >= 0.0 && std::abs(x - bx) <= 8.0)
                target = DragCursor::B;
        }

        if (target != DragCursor::None) {
            if (follow_ && dataEnd_ > dataStart_) {
                follow_ = false;
                viewAll_ = false;
                frozenEnd_ = viewEndSample_;
                emit viewModeChanged();
                refreshView();
            }
            const double sample = sampleForX(event->position().x());
            if (target == DragCursor::A)
                cursorASample_ = sample;
            else
                cursorBSample_ = sample;
            dragCursor_ = target;
            refreshCursorReadouts();
            emit cursorChanged();
            update();
            event->accept();
            return;
        }
    }

    // Show the Y reference value immediately on press, not only on hover.
    updateHover(event->position().x(), event->position().y());

    // Any mouse gesture freezes the view first (like the reference). This is
    // what makes a middle-drag selection match the region under the cursor.
    if (follow_ && dataEnd_ > dataStart_) {
        follow_ = false;
        viewAll_ = false;
        frozenEnd_ = viewEndSample_;
        emit viewModeChanged();
        refreshView();
    }

    if (pressedButton_ == Qt::RightButton) {
        rightBaseSpanSamples_ = viewSpanSamples_;
        rightBaseViewStart_ = viewStartSample_;
        rightBaseYMin_ = yMin_;
        rightBaseYMax_ = yMax_;
    }
    event->accept();
}

void TimePlotItem::mouseMoveEvent(QMouseEvent* event)
{
    if (pressedButton_ == Qt::NoButton) {
        event->ignore();
        return;
    }

    if (dragCursor_ != DragCursor::None && pressedButton_ == Qt::LeftButton) {
        const double sample = sampleForX(event->position().x());
        if (dragCursor_ == DragCursor::A)
            cursorASample_ = sample;
        else
            cursorBSample_ = sample;
        lastPosition_ = event->position();
        refreshCursorReadouts();
        emit cursorChanged();
        update();
        event->accept();
        return;
    }

    const QPointF position = event->position();
    if (!pressMoved_
        && std::hypot(position.x() - pressPosition_.x(),
                      position.y() - pressPosition_.y()) > 3.0) {
        pressMoved_ = true;
    }

    if (pressedButton_ == Qt::LeftButton && pressMoved_) {
        const qreal dx = position.x() - lastPosition_.x();
        const qreal dy = position.y() - lastPosition_.y();
        if (controller_ != nullptr && width() > 1.0) {
            const double spanSamples = std::max(16.0, viewSpanSamples_);
            frozenEnd_ = viewEndSample_ - dx * spanSamples / width();
            const double yRange = yMax_ - yMin_;
            yMin_ += dy * yRange / std::max<qreal>(height(), 1.0);
            yMax_ += dy * yRange / std::max<qreal>(height(), 1.0);
            follow_ = false;
            viewAll_ = false;
            autoY_ = false;
            emit viewModeChanged();
            emit autoYChanged();
            refreshView();
        }
    } else if (pressedButton_ == Qt::RightButton && pressMoved_
               && rightBaseSpanSamples_ > 0.0) {
        // Live zoom while dragging the right button, anchored at the press
        // point. Horizontal drag zooms X; vertical drag zooms Y. Recompute
        // from the state captured on press so the gesture is not compounded.
        const qreal dx = position.x() - pressPosition_.x();
        const qreal dy = position.y() - pressPosition_.y();
        const double factorX = std::clamp(std::pow(1.5, dx / 150.0), 0.2, 5.0);
        const double factorY = std::clamp(std::pow(1.5, dy / 150.0), 0.2, 5.0);
        const double fx = std::clamp(
            pressPosition_.x() / std::max(width(), 1.0), 0.0, 1.0);
        const double fy = std::clamp(
            1.0 - (pressPosition_.y() - kTopMargin)
                / std::max<qreal>(height() - kTopMargin - kBottomMargin, 1.0),
            0.0, 1.0);

        const double anchorSample =
            rightBaseViewStart_ + fx * rightBaseSpanSamples_;
        const double newSpan = std::clamp(
            rightBaseSpanSamples_ * factorX, 16.0, 262144.0);
        frozenEnd_ = anchorSample + (1.0 - fx) * newSpan;
        spanSeconds_ = std::clamp(
            newSpan / std::max(1.0, controller_->samplesPerSecond()),
            0.05, 3600.0);

        const double baseYRange = rightBaseYMax_ - rightBaseYMin_;
        if (std::abs(dy) >= 1.0) {
            const double anchorValue = rightBaseYMin_ + fy * baseYRange;
            const double newYRange = std::clamp(
                baseYRange * factorY,
                std::max(1.0e-9, baseYRange * 0.02),
                baseYRange * 50.0);
            yMin_ = anchorValue - fy * newYRange;
            yMax_ = yMin_ + newYRange;
            autoY_ = false;
        }

        follow_ = false;
        viewAll_ = false;
        emit spanChanged();
        emit autoYChanged();
        emit viewModeChanged();
        refreshView();
    } else if (pressedButton_ == Qt::MiddleButton && pressMoved_) {
        boxActive_ = true;
        const QRectF box(QPointF(std::min(pressPosition_.x(), position.x()),
                                 std::min(pressPosition_.y(), position.y())),
                         QPointF(std::max(pressPosition_.x(), position.x()),
                                 std::max(pressPosition_.y(), position.y())));
        boxLineVertices_ = {
            point(box.left(), box.top()), point(box.right(), box.top()),
            point(box.right(), box.top()), point(box.right(), box.bottom()),
            point(box.right(), box.bottom()), point(box.left(), box.bottom()),
            point(box.left(), box.bottom()), point(box.left(), box.top()),
        };
        boxFillVertices_ = {
            point(box.left(), box.top()), point(box.right(), box.top()),
            point(box.left(), box.bottom()), point(box.left(), box.bottom()),
            point(box.right(), box.top()), point(box.right(), box.bottom()),
        };
        update();
    }

    // Hover events are not delivered while a button is held. Keep the
    // crosshair and the Y-axis reference readout following the drag so the
    // value under the pointer is visible while panning/zooming.
    updateHover(position.x(), position.y());

    lastPosition_ = position;
    event->accept();
}

void TimePlotItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (pressedButton_ == Qt::NoButton) {
        event->ignore();
        return;
    }

    if (dragCursor_ != DragCursor::None) {
        dragCursor_ = DragCursor::None;
        pressedButton_ = Qt::NoButton;
        pressMoved_ = false;
        update();
        event->accept();
        return;
    }

    const auto button = event->button();
    if (button == Qt::MiddleButton) {
        const qreal boxWidth = std::abs(event->position().x() - pressPosition_.x());
        const qreal boxHeight = std::abs(event->position().y() - pressPosition_.y());
        if (boxActive_ && boxWidth >= 12.0 && boxHeight >= 12.0)
            applyBoxZoom(pressPosition_, event->position());
        else
            resetY(); // middle click: restore automatic Y range
    } else if (button == Qt::RightButton) {
        if (!pressMoved_)
            emit menuRequested(event->position().x(), event->position().y());
    }

    boxActive_ = false;
    boxLineVertices_.clear();
    boxFillVertices_.clear();
    pressedButton_ = Qt::NoButton;
    pressMoved_ = false;
    update();
    event->accept();
}

void TimePlotItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    showFollow();
    autoY_ = true;
    emit autoYChanged();
    refreshView();
    event->accept();
}

void TimePlotItem::wheelEvent(QWheelEvent* event)
{
    int delta = event->angleDelta().y();
    if (delta == 0)
        delta = event->pixelDelta().y();
    if (delta == 0 || controller_ == nullptr || viewSpanSamples_ <= 0.0) {
        event->accept();
        return;
    }

    const double factor = std::pow(1.0015, -static_cast<double>(delta));
    const double fx = std::clamp(event->position().x() / std::max(width(), 1.0), 0.0, 1.0);
    const double fy = std::clamp(
        1.0 - (event->position().y() - kTopMargin)
            / std::max<qreal>(height() - kTopMargin - kBottomMargin, 1.0),
        0.0, 1.0);

    // Zoom both axes around the pointer, matching the reference oscilloscope.
    const double anchorSample = viewStartSample_ + fx * viewSpanSamples_;
    const double newSpan = std::clamp(viewSpanSamples_ * factor, 16.0, 262144.0);
    frozenEnd_ = anchorSample + (1.0 - fx) * newSpan;
    spanSeconds_ = std::clamp(newSpan / std::max(1.0, controller_->samplesPerSecond()),
                              0.05, 3600.0);

    const double yRange = yMax_ - yMin_;
    const double anchorValue = yMin_ + fy * yRange;
    const double newYRange = std::max(1.0e-9, yRange * factor);
    yMin_ = anchorValue - fy * newYRange;
    yMax_ = yMin_ + newYRange;

    follow_ = false;
    viewAll_ = false;
    autoY_ = false;
    emit spanChanged();
    emit autoYChanged();
    emit viewModeChanged();
    refreshView();
    event->accept();
}

QSGNode* TimePlotItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* root = oldNode != nullptr ? static_cast<QSGNode*>(oldNode) : new QSGNode;
    if (root_ == nullptr)
        root_ = root;

    const auto makeLineNode = [](QSGGeometryNode*& node,
                                 QSGFlatColorMaterial*& material,
                                 const QColor& color) {
        node = new QSGGeometryNode;
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawLines);
        geometry->setLineWidth(1.0);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        material = new QSGFlatColorMaterial;
        material->setColor(color);
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
    };

    const auto makeTriangleNode = [](QSGGeometryNode*& node,
                                     QSGFlatColorMaterial*& material,
                                     const QColor& color) {
        node = new QSGGeometryNode;
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        material = new QSGFlatColorMaterial;
        material->setColor(color);
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
    };

    if (gridMinorNode_ == nullptr) {
        QColor minor = gridColor_;
        minor.setAlpha(155);
        QColor major = gridColor_;
        major.setAlpha(255);
        makeTriangleNode(gridMinorNode_, gridMinorMaterial_, minor);
        makeTriangleNode(gridMajorNode_, gridMajorMaterial_, major);
        root->appendChildNode(gridMinorNode_);
        root->appendChildNode(gridMajorNode_);

        makeLineNode(hoverNode_, hoverMaterial_, QColor(231, 76, 60, 210));
        root->appendChildNode(hoverNode_);

        makeLineNode(hoverHNode_, hoverHMaterial_, QColor(231, 76, 60, 130));
        root->appendChildNode(hoverHNode_);

        makeLineNode(triggerNode_, triggerMaterial_, QColor(255, 179, 0, 235));
        root->appendChildNode(triggerNode_);

        boxLineNode_ = new QSGGeometryNode;
        auto* boxLineGeometry =
            new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        boxLineGeometry->setDrawingMode(QSGGeometry::DrawLines);
        boxLineNode_->setGeometry(boxLineGeometry);
        boxLineNode_->setFlag(QSGNode::OwnsGeometry);
        boxLineMaterial_ = new QSGFlatColorMaterial;
        boxLineMaterial_->setColor(QColor(79, 195, 247, 220));
        boxLineNode_->setMaterial(boxLineMaterial_);
        boxLineNode_->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(boxLineNode_);

        boxFillNode_ = new QSGGeometryNode;
        auto* boxFillGeometry =
            new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        boxFillGeometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        boxFillNode_->setGeometry(boxFillGeometry);
        boxFillNode_->setFlag(QSGNode::OwnsGeometry);
        boxFillMaterial_ = new QSGFlatColorMaterial;
        boxFillMaterial_->setColor(QColor(79, 195, 247, 45));
        boxFillNode_->setMaterial(boxFillMaterial_);
        boxFillNode_->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(boxFillNode_);
    }

    const auto upload = [](QSGGeometryNode* node,
                           const std::vector<QPointF>& vertices,
                           const QColor& color) {
        if (node == nullptr)
            return;
        auto* geometry = node->geometry();
        const int count = static_cast<int>(vertices.size());
        if (geometry->vertexCount() != count)
            geometry->allocate(count);
        auto* data = geometry->vertexDataAsPoint2D();
        for (int i = 0; i < count; ++i)
            data[i].set(vertices[static_cast<std::size_t>(i)].x(),
                        vertices[static_cast<std::size_t>(i)].y());
        node->markDirty(QSGNode::DirtyGeometry);

        if (auto* material = static_cast<QSGFlatColorMaterial*>(node->material());
            material != nullptr && material->color() != color) {
            material->setColor(color);
            node->markDirty(QSGNode::DirtyMaterial);
        }
    };

    QColor minor = gridColor_;
    minor.setAlpha(155);
    QColor major = gridColor_;
    major.setAlpha(255);
    upload(gridMinorNode_, gridMinorVertices_, minor);
    upload(gridMajorNode_, gridMajorVertices_, major);
    upload(hoverNode_, hoverVertices_, QColor(231, 76, 60, 210));
    upload(hoverHNode_, hoverHVertices_, QColor(231, 76, 60, 130));
    upload(triggerNode_, triggerVertices_, QColor(255, 179, 0, 235));
    upload(boxLineNode_, boxLineVertices_, QColor(79, 195, 247, 220));
    upload(boxFillNode_, boxFillVertices_, QColor(79, 195, 247, 45));

    for (std::size_t i = 0; i < curves_.size(); ++i) {
        auto& curve = curves_[i];
        if (curve.node == nullptr) {
            makeTriangleNode(curve.node, curve.material, curve.color);
            root->appendChildNode(curve.node);
        }
        const bool visible = static_cast<int>(i) < activeChannels_
            && !curve.vertices.empty();
        upload(curve.node, visible ? curve.vertices : std::vector<QPointF> {},
               curve.color);
    }

    gridDirty_ = false;
    return root;
}

} // namespace vofa::app
