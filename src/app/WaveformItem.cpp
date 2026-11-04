#include "WaveformItem.h"

#include "AppController.h"

#include <QCursor>
#include <QFont>
#include <QFontMetricsF>
#include <QHoverEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QRectF>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vofa::app {

WaveformItem::WaveformItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setCursor(QCursor(Qt::CrossCursor));

    longPressTimer_.setSingleShot(true);
    longPressTimer_.setInterval(450);
    connect(&longPressTimer_, &QTimer::timeout, this, [this]() {
        if (pressedButton_ == Qt::NoButton || pressMoved_)
            return;
        longPressTriggered_ = true;
        if (pressedButton_ == Qt::LeftButton) {
            emit menuRequested(pressPosition_.x(), pressPosition_.y());
        } else if (pressedButton_ == Qt::MiddleButton) {
            resetY();
            clearCursors();
            emit resetViewRequested();
        } else if (pressedButton_ == Qt::RightButton) {
            boxZoomActive_ = true;
            boxEnd_ = pressPosition_;
            update();
        }
    });
}

qreal WaveformItem::plotUsableHeight() const
{
    return std::max<qreal>(height() - 8.0 - 14.0, 1.0);
}

qreal WaveformItem::normalizedAtY(qreal y) const
{
    const qreal usable = plotUsableHeight();
    return 1.0 - std::clamp((y - 4.0) / usable, 0.0, 1.0);
}

void WaveformItem::resetY()
{
    yScale_ = 1.0;
    yOffset_ = 0.0;
    update();
}

void WaveformItem::clearCursors()
{
    cursorA_ = -1.0;
    cursorB_ = -1.0;
    update();
}

void WaveformItem::zoomYToRange(qreal bottomNorm, qreal topNorm)
{
    if (topNorm < bottomNorm)
        std::swap(bottomNorm, topNorm);
    if (topNorm - bottomNorm < 0.01)
        return;

    const qreal baseBottom = (bottomNorm - 0.5 - yOffset_) / yScale_ + 0.5;
    const qreal baseTop = (topNorm - 0.5 - yOffset_) / yScale_ + 0.5;
    if (baseTop - baseBottom < 1.0e-6)
        return;

    const qreal scale = std::clamp(1.0 / (baseTop - baseBottom), 0.1, 20.0);
    yOffset_ = std::clamp(0.5 - (baseBottom - 0.5) * scale, -8.0, 8.0);
    yScale_ = scale;
    update();
}

void WaveformItem::setMode(int mode)
{
    mode = std::clamp(mode, 0, 3);
    if (mode_ == mode)
        return;
    mode_ = mode;
    emit modeChanged();
    update();
}

void WaveformItem::setAxisFontPx(int pixels)
{
    pixels = std::clamp(pixels, 9, 32);
    if (axisFontPx_ == pixels)
        return;
    axisFontPx_ = pixels;
    emit axisFontPxChanged();
    update();
}

void WaveformItem::setGridColor(const QColor& color)
{
    if (gridColor_ == color)
        return;
    gridColor_ = color;
    emit gridColorChanged();
    update();
}

void WaveformItem::mousePressEvent(QMouseEvent* event)
{
    if (mode_ != 0) {
        event->ignore();
        return;
    }
    const auto button = event->button();
    if (button != Qt::LeftButton && button != Qt::MiddleButton
        && button != Qt::RightButton) {
        event->ignore();
        return;
    }

    pressedButton_ = button;
    pressPosition_ = event->position();
    lastPosition_ = pressPosition_;
    pressMoved_ = false;
    longPressTriggered_ = false;
    boxZoomActive_ = false;
    dragging_ = true;
    pressX_ = pressPosition_.x();
    lastMouseX_ = pressX_;
    shiftAtPress_ = event->modifiers().testFlag(Qt::ShiftModifier);
    longPressTimer_.start();
    event->accept();
}

void WaveformItem::mouseMoveEvent(QMouseEvent* event)
{
    if (mode_ != 0 || pressedButton_ == Qt::NoButton) {
        event->ignore();
        return;
    }

    const QPointF position = event->position();
    const qreal dx = position.x() - lastPosition_.x();
    const qreal dy = position.y() - lastPosition_.y();

    if (!pressMoved_) {
        const qreal moved = std::hypot(position.x() - pressPosition_.x(),
                                       position.y() - pressPosition_.y());
        if (moved > 3.0) {
            pressMoved_ = true;
            longPressTimer_.stop();
        }
    }

    const int visible = controller_ != nullptr
        ? controller_->waveformVisibleSampleCount()
        : 0;

    if (pressedButton_ == Qt::LeftButton) {
        if (pressMoved_ && !longPressTriggered_ && visible > 0 && width() > 1.0) {
            const int delta = static_cast<int>(
                dx * static_cast<qreal>(visible) / width());
            if (delta != 0)
                emit panRequested(delta);
        }
    } else if (pressedButton_ == Qt::MiddleButton) {
        if (pressMoved_ && !longPressTriggered_) {
            if (visible > 0 && width() > 1.0) {
                const int delta = static_cast<int>(
                    dx * static_cast<qreal>(visible) / width());
                if (delta != 0)
                    emit panRequested(delta);
            }
            yOffset_ = std::clamp(yOffset_ - dy / plotUsableHeight(), -8.0, 8.0);
            update();
        }
    } else if (pressedButton_ == Qt::RightButton && boxZoomActive_) {
        boxEnd_ = position;
        update();
    }

    lastPosition_ = position;
    event->accept();
}

void WaveformItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (mode_ != 0 || pressedButton_ == Qt::NoButton) {
        event->ignore();
        return;
    }

    const auto button = event->button();
    if (button == Qt::LeftButton) {
        if (!longPressTriggered_ && !pressMoved_ && width() > 1.0) {
            const qreal fraction =
                std::clamp(event->position().x() / width(), 0.0, 1.0);
            if (shiftAtPress_) {
                cursorB_ = fraction;
            } else if (cursorA_ < 0.0 || (cursorA_ >= 0.0 && cursorB_ >= 0.0)) {
                cursorA_ = fraction;
                cursorB_ = -1.0;
            } else {
                cursorB_ = fraction;
            }
            update();
        }
    } else if (button == Qt::MiddleButton) {
        if (!longPressTriggered_ && !pressMoved_) {
            resetY();
            update();
        }
    } else if (button == Qt::RightButton) {
        if (boxZoomActive_) {
            const qreal x0 = std::clamp(
                std::min(pressPosition_.x(), boxEnd_.x()) / width(), 0.0, 1.0);
            const qreal x1 = std::clamp(
                std::max(pressPosition_.x(), boxEnd_.x()) / width(), 0.0, 1.0);
            const qreal yBottom = normalizedAtY(
                std::max(pressPosition_.y(), boxEnd_.y()));
            const qreal yTop = normalizedAtY(
                std::min(pressPosition_.y(), boxEnd_.y()));
            if (x1 - x0 > 0.005 && yTop - yBottom > 0.005) {
                zoomYToRange(yBottom, yTop);
                emit areaZoomRequested(x0, x1);
            } else {
                resetY();
                clearCursors();
                emit resetViewRequested();
            }
            boxZoomActive_ = false;
            update();
        } else if (!longPressTriggered_ && !pressMoved_) {
            emit menuRequested(event->position().x(), event->position().y());
        }
    }

    longPressTimer_.stop();
    pressedButton_ = Qt::NoButton;
    dragging_ = false;
    pressMoved_ = false;
    longPressTriggered_ = false;
    event->accept();
}

void WaveformItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (mode_ != 0) {
        event->ignore();
        return;
    }
    clearCursors();
    event->accept();
}

void WaveformItem::hoverMoveEvent(QHoverEvent* event)
{
    if (mode_ != 0) {
        event->ignore();
        return;
    }
    hoverX_ = event->position().x();
    update();
    event->accept();
}

void WaveformItem::hoverLeaveEvent(QHoverEvent* event)
{
    hoverX_ = -1.0;
    update();
    event->accept();
}

void WaveformItem::wheelEvent(QWheelEvent* event)
{
    const auto delta = event->angleDelta().y();
    if (delta == 0) {
        event->accept();
        return;
    }

    const qreal factor = std::pow(1.0015, -static_cast<qreal>(delta));
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        const qreal anchor = normalizedAtY(event->position().y());
        const qreal scale = std::clamp(yScale_ * factor, 0.1, 20.0);
        yOffset_ = std::clamp(
            yOffset_ + (anchor - 0.5) * (yScale_ - scale), -8.0, 8.0);
        yScale_ = scale;
        update();
    } else {
        emit zoomRequested(factor,
                           std::clamp(event->position().x() / std::max(width(), 1.0),
                                      0.0, 1.0));
    }
    event->accept();
}

QObject* WaveformItem::controller() const
{
    return controller_;
}

void WaveformItem::setController(QObject* controller)
{
    auto* typed = qobject_cast<AppController*>(controller);
    if (typed == controller_)
        return;
    controller_ = typed;
    emit controllerChanged();
    update();
}

void WaveformItem::paint(QPainter* painter)
{
    const qreal w = width();
    const qreal h = height();
    if (w <= 1.0 || h <= 1.0)
        return;

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    paintGrid(painter, w, h);

    if (controller_ == nullptr) {
        painter->setPen(QColor(QStringLiteral("#7f8c8d")));
        painter->drawText(QPointF(12.0, 26.0), QStringLiteral("等待数据…"));
        return;
    }

    const int channels = controller_->waveformChannelCount();
    if (channels <= 0) {
        painter->setPen(QColor(QStringLiteral("#7f8c8d")));
        painter->drawText(QPointF(12.0, 26.0), QStringLiteral("等待数据…"));
        return;
    }

    if (mode_ == 2) {
        paintSpectrum(painter, w, h);
        return;
    }
    if (mode_ == 3) {
        paintHistogram(painter, w, h);
        return;
    }
    if (mode_ == 1) {
        paintXY(painter, w, h);
        return;
    }

    const int columns = std::clamp(static_cast<int>(std::ceil(w)), 1, 4096);
    constexpr qreal kPadding = 4.0;
    constexpr qreal kAxisHeight = 14.0;

    QPolygonF centre;
    QVector<QLineF> envelope;
    centre.reserve(columns);
    envelope.reserve(columns);
    std::vector<float> hoverValues(static_cast<std::size_t>(channels),
                                   std::numeric_limits<float>::quiet_NaN());
    std::vector<float> cursorAValues(static_cast<std::size_t>(channels),
                                     std::numeric_limits<float>::quiet_NaN());
    std::vector<float> cursorBValues(static_cast<std::size_t>(channels),
                                     std::numeric_limits<float>::quiet_NaN());
    float axisMin = 0.0f;
    float axisMax = 1.0f;
    bool haveAxis = false;

    for (int channel = 0; channel < channels; ++channel) {
        if (!controller_->channelVisible(channel))
            continue;

        controller_->buildWaveform(channel, columns, samples_);
        if (samples_.size() < 2)
            continue;

        float minValue = samples_[0];
        float maxValue = samples_[1];

        float fixedMin = 0.0f;
        float fixedMax = 0.0f;
        const bool fixedRange =
            controller_->channelFixedRange(channel, fixedMin, fixedMax);
        if (fixedRange) {
            minValue = fixedMin;
            maxValue = fixedMax;
        } else {
            for (std::size_t i = 2; i + 1 < samples_.size(); i += 2) {
                minValue = std::min(minValue, samples_[i]);
                maxValue = std::max(maxValue, samples_[i + 1]);
            }
            if (!std::isfinite(minValue) || !std::isfinite(maxValue))
                continue;
            if (maxValue - minValue < 1.0e-9f) {
                minValue -= 0.5f;
                maxValue += 0.5f;
            }
            const float padding = (maxValue - minValue) * 0.08f;
            minValue -= padding;
            maxValue += padding;
        }
        if (!std::isfinite(minValue) || !std::isfinite(maxValue)
            || maxValue - minValue < 1.0e-9f) {
            continue;
        }
        const qreal usable =
            std::max<qreal>(h - kPadding * 2.0 - kAxisHeight, 1.0);
        const qreal baseSpan =
            std::max<qreal>(static_cast<qreal>(maxValue) - minValue, 1.0e-9);
        const auto screenNorm = [this](qreal normalized) {
            return (normalized - 0.5) * yScale_ + 0.5 + yOffset_;
        };

        const int pairs = static_cast<int>(samples_.size() / 2);
        centre.clear();
        envelope.clear();
        const auto valueAtFraction = [this, pairs](qreal fraction) {
            const int index = std::clamp(
                static_cast<int>(fraction * static_cast<qreal>(pairs)), 0, pairs - 1);
            return (samples_[static_cast<std::size_t>(index) * 2]
                    + samples_[static_cast<std::size_t>(index) * 2 + 1]) * 0.5f;
        };

        if (hoverX_ >= 0.0 && pairs > 0) {
            hoverValues[static_cast<std::size_t>(channel)] =
                valueAtFraction(hoverX_ / w);
        }
        if (pairs > 0 && cursorA_ >= 0.0) {
            cursorAValues[static_cast<std::size_t>(channel)] =
                valueAtFraction(cursorA_);
        }
        if (pairs > 0 && cursorB_ >= 0.0) {
            cursorBValues[static_cast<std::size_t>(channel)] =
                valueAtFraction(cursorB_);
        }
        for (int i = 0; i < pairs; ++i) {
            const qreal x = (static_cast<qreal>(i) + 0.5) * w / static_cast<qreal>(pairs);
            const qreal sampleLow = samples_[static_cast<std::size_t>(i) * 2];
            const qreal sampleHigh = samples_[static_cast<std::size_t>(i) * 2 + 1];
            const qreal normLow =
                (sampleLow - static_cast<qreal>(minValue)) / baseSpan;
            const qreal normHigh =
                (sampleHigh - static_cast<qreal>(minValue)) / baseSpan;
            const qreal screenLow = screenNorm(normLow);
            const qreal screenHigh = screenNorm(normHigh);
            const qreal yMin = kPadding + (1.0 - screenHigh) * usable;
            const qreal yMax = kPadding + (1.0 - screenLow) * usable;
            centre.append(QPointF(x, (yMin + yMax) * 0.5));
            envelope.append(QLineF(x, yMin, x, yMax));
        }

        QColor colour = controller_->channelColor(channel);
        if (!colour.isValid())
            colour = QColor(QStringLiteral("#1abc9c"));

        QColor envelopeColour = colour;
        envelopeColour.setAlpha(110);
        painter->setPen(QPen(envelopeColour, 1.0));
        painter->drawLines(envelope);
        painter->setPen(QPen(colour, 1.4));
        painter->drawPolyline(centre);

        if (channel == 0) {
            axisMin = minValue;
            axisMax = maxValue;
            haveAxis = true;
        }
    }

    if (haveAxis && controller_ != nullptr) {
        QFont axisFont = painter->font();
        axisFont.setPixelSize(std::max(9, axisFontPx_));
        painter->setFont(axisFont);
        const QFontMetricsF axisMetrics(axisFont);
        painter->setPen(QColor(140, 155, 170, 205));

        const double baseSpan =
            static_cast<double>(axisMax) - static_cast<double>(axisMin);
        const double normBottom = (0.0 - 0.5 - yOffset_) / yScale_ + 0.5;
        const double normTop = (1.0 - 0.5 - yOffset_) / yScale_ + 0.5;
        const double lowVisible =
            static_cast<double>(axisMin) + normBottom * baseSpan;
        const double highVisible =
            static_cast<double>(axisMin) + normTop * baseSpan;
        const double values[3] = {
            highVisible,
            (lowVisible + highVisible) * 0.5,
            lowVisible,
        };
        const qreal ys[3] = {
            kPadding + axisMetrics.ascent(),
            kPadding + (h - kPadding * 2.0 - kAxisHeight) * 0.5
                + axisMetrics.ascent() * 0.5,
            kPadding + (h - kPadding * 2.0 - kAxisHeight)
                - axisMetrics.descent() - 2.0,
        };
        for (int i = 0; i < 3; ++i) {
            painter->drawText(QPointF(4.0, ys[i]),
                              QString::number(values[i], 'f', 2));
        }

        const int visible = controller_->waveformVisibleSampleCount();
        const double rate = controller_->samplesPerSecond();
        if (visible > 0) {
            const bool timeAxis = rate > 1.0;
            const double duration = timeAxis
                ? static_cast<double>(visible) / rate
                : 0.0;
            for (int i = 0; i <= 5; ++i) {
                QString label;
                if (timeAxis) {
                    const double seconds = -duration + duration * i / 5.0;
                    if (i == 5)
                        label = QStringLiteral("0s");
                    else
                        label = QString::number(seconds, 'f',
                                                duration < 10.0 ? 2 : 1)
                            + QStringLiteral("s");
                } else {
                    const int offset = -visible + visible * i / 5;
                    const int magnitude = std::abs(offset);
                    if (magnitude >= 1000) {
                        label = QString::number(offset / 1000.0, 'f',
                                                magnitude >= 10000 ? 0 : 1)
                            + QStringLiteral("k");
                    } else {
                        label = QString::number(offset);
                    }
                }
                const qreal x = w * static_cast<qreal>(i) / 5.0;
                const qreal textWidth = axisMetrics.horizontalAdvance(label);
                const qreal textX =
                    std::clamp(x - textWidth / 2.0, 2.0, w - textWidth - 2.0);
                painter->drawText(QPointF(textX, h - 3.0), label);
            }
        }
    }

    if (hoverX_ >= 0.0) {
        painter->setPen(QPen(QColor(200, 210, 220, 170), 1.0, Qt::DashLine));
        painter->drawLine(QPointF(hoverX_, 0.0), QPointF(hoverX_, h));

        const QFontMetricsF metrics(painter->font());
        const qreal lineHeight = metrics.height() + 2.0;
        qreal maxWidth = 0.0;
        int visible = 0;
        for (int channel = 0; channel < channels; ++channel) {
            if (!std::isfinite(hoverValues[static_cast<std::size_t>(channel)]))
                continue;
            ++visible;
            maxWidth = std::max(
                maxWidth,
                metrics.horizontalAdvance(
                    QStringLiteral("CH%1  %2")
                        .arg(channel)
                        .arg(hoverValues[static_cast<std::size_t>(channel)], 0, 'f', 3)));
        }

        if (visible > 0) {
            const qreal boxWidth = maxWidth + 16.0;
            const qreal boxHeight = lineHeight * visible + 8.0;
            const QRectF box(w - boxWidth - 6.0, 6.0, boxWidth, boxHeight);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(10, 16, 22, 190));
            painter->drawRoundedRect(box, 4.0, 4.0);

            qreal y = box.top() + metrics.ascent() + 6.0;
            for (int channel = 0; channel < channels; ++channel) {
                const float value = hoverValues[static_cast<std::size_t>(channel)];
                if (!std::isfinite(value))
                    continue;
                QColor colour = controller_->channelColor(channel);
                if (!colour.isValid())
                    colour = QColor(QStringLiteral("#1abc9c"));
                painter->setPen(colour);
                painter->drawText(
                    QPointF(box.right() - 8.0
                                - metrics.horizontalAdvance(
                                    QStringLiteral("CH%1  %2").arg(channel).arg(value, 0, 'f', 3)),
                            y),
                    QStringLiteral("CH%1  %2").arg(channel).arg(value, 0, 'f', 3));
                y += lineHeight;
            }
            painter->setBrush(Qt::NoBrush);
        }
    }

    if (cursorA_ >= 0.0 || cursorB_ >= 0.0) {
        const qreal xA = cursorA_ >= 0.0 ? cursorA_ * w : -1.0;
        const qreal xB = cursorB_ >= 0.0 ? cursorB_ * w : -1.0;
        const bool both = xA >= 0.0 && xB >= 0.0;

        if (both) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(241, 196, 15, 22));
            painter->drawRect(QRectF(std::min(xA, xB), 0.0, std::abs(xB - xA), h));
            painter->setBrush(Qt::NoBrush);
        }
        if (xA >= 0.0) {
            painter->setPen(QPen(QColor(QStringLiteral("#f1c40f")), 1.2, Qt::DashLine));
            painter->drawLine(QPointF(xA, 0.0), QPointF(xA, h));
            painter->drawText(QPointF(xA + 4.0, 14.0), QStringLiteral("A"));
        }
        if (xB >= 0.0) {
            painter->setPen(QPen(QColor(QStringLiteral("#4fc3f7")), 1.2, Qt::DashLine));
            painter->drawLine(QPointF(xB, 0.0), QPointF(xB, h));
            painter->drawText(QPointF(xB + 4.0, 14.0), QStringLiteral("B"));
        }

        if (both && controller_ != nullptr) {
            const QFontMetricsF metrics(painter->font());
            const qreal lineHeight = metrics.height() + 2.0;
            QStringList lines;
            const int visibleSamples = controller_->waveformVisibleSampleCount();
            const int deltaSamples = static_cast<int>(
                std::abs(cursorB_ - cursorA_) * static_cast<qreal>(visibleSamples));
            lines << QStringLiteral("Δsamples  %1").arg(deltaSamples);
            for (int channel = 0; channel < channels; ++channel) {
                const float a = cursorAValues[static_cast<std::size_t>(channel)];
                const float b = cursorBValues[static_cast<std::size_t>(channel)];
                if (!std::isfinite(a) || !std::isfinite(b))
                    continue;
                lines << QStringLiteral("CH%1  Δ %2")
                             .arg(channel)
                             .arg(static_cast<double>(b - a), 0, 'f', 3);
            }

            qreal boxWidth = 0.0;
            for (const auto& line : lines)
                boxWidth = std::max(boxWidth, metrics.horizontalAdvance(line));
            const qreal boxHeight = lineHeight * lines.size() + 8.0;
            const QRectF box(8.0, h - boxHeight - 24.0, boxWidth + 16.0, boxHeight);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(10, 16, 22, 205));
            painter->drawRoundedRect(box, 4.0, 4.0);

            qreal y = box.top() + metrics.ascent() + 6.0;
            painter->setPen(QColor(QStringLiteral("#f1c40f")));
            painter->drawText(QPointF(box.left() + 8.0, y), lines.first());
            y += lineHeight;
            painter->setPen(QColor(QStringLiteral("#cfd8dc")));
            for (int i = 1; i < lines.size(); ++i) {
                painter->drawText(QPointF(box.left() + 8.0, y), lines.at(i));
                y += lineHeight;
            }
            painter->setBrush(Qt::NoBrush);
        }
    }

    if (boxZoomActive_) {
        const QPointF topLeft(std::min(pressPosition_.x(), boxEnd_.x()),
                              std::min(pressPosition_.y(), boxEnd_.y()));
        const QPointF bottomRight(std::max(pressPosition_.x(), boxEnd_.x()),
                                  std::max(pressPosition_.y(), boxEnd_.y()));
        painter->setPen(QPen(QColor(QStringLiteral("#4fc3f7")), 1.0, Qt::DashLine));
        painter->setBrush(QColor(79, 195, 247, 40));
        painter->drawRect(QRectF(topLeft, bottomRight));
        painter->setBrush(Qt::NoBrush);
    }
}


void WaveformItem::paintGrid(QPainter* painter, qreal w, qreal h)
{
    QColor minor = gridColor_;
    minor.setAlpha(115);
    QColor major = gridColor_;
    major.setAlpha(215);

    for (int i = 0; i <= 10; ++i) {
        painter->setPen(QPen(i % 5 == 0 ? major : minor, 1.0));
        const qreal x = w * static_cast<qreal>(i) / 10.0;
        painter->drawLine(QPointF(x, 0.0), QPointF(x, h));
    }
    for (int i = 0; i <= 8; ++i) {
        painter->setPen(QPen(i % 4 == 0 ? major : minor, 1.0));
        const qreal y = h * static_cast<qreal>(i) / 8.0;
        painter->drawLine(QPointF(0.0, y), QPointF(w, y));
    }
}

void WaveformItem::paintSpectrum(QPainter* painter, qreal w, qreal h)
{
    constexpr int kBins = 192;
    struct Spectrum {
        QColor color;
        std::vector<float> mags;
    };
    std::vector<Spectrum> spectra;
    float peak = 0.0f;

    const int channels = controller_->waveformChannelCount();
    for (int channel = 0; channel < channels; ++channel) {
        if (!controller_->channelVisible(channel))
            continue;
        std::vector<float> mags;
        controller_->buildSpectrum(channel, kBins, mags);
        if (mags.empty())
            continue;
        float localPeak = 0.0f;
        for (const float value : mags)
            localPeak = std::max(localPeak, value);
        if (localPeak <= 0.0f)
            continue;
        QColor colour = controller_->channelColor(channel);
        if (!colour.isValid())
            colour = QColor(QStringLiteral("#1abc9c"));
        spectra.push_back({colour, std::move(mags)});
        peak = std::max(peak, localPeak);
    }

    if (spectra.empty() || peak <= 0.0f) {
        painter->setPen(QColor(QStringLiteral("#7f8c8d")));
        painter->drawText(QPointF(12.0, 26.0), QStringLiteral("等待足够的数据…"));
        return;
    }

    const qreal bottom = 18.0;
    const qreal top = 8.0;
    const qreal plotHeight = std::max<qreal>(h - bottom - top, 1.0);

    for (const auto& spectrum : spectra) {
        QPolygonF line;
        line.reserve(static_cast<int>(spectrum.mags.size()));
        for (int i = 0; i < static_cast<int>(spectrum.mags.size()); ++i) {
            const qreal x = spectrum.mags.size() > 1
                ? w * static_cast<qreal>(i) / static_cast<qreal>(spectrum.mags.size() - 1)
                : 0.0;
            const qreal y =
                top + (1.0 - spectrum.mags[static_cast<std::size_t>(i)] / peak) * plotHeight;
            line.append(QPointF(x, y));
        }
        painter->setPen(QPen(spectrum.color, 1.3));
        painter->drawPolyline(line);
    }

    QFont font = painter->font();
    font.setPixelSize(std::max(9, axisFontPx_));
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    painter->setPen(QColor(140, 155, 170, 205));

    const double rate = controller_->samplesPerSecond();
    const auto frequencyText = [](double hz) {
        return hz >= 1000.0
            ? QString::number(hz / 1000.0, 'f', 1) + QStringLiteral(" kHz")
            : QString::number(hz, 'f', 0) + QStringLiteral(" Hz");
    };
    if (rate > 1.0) {
        painter->drawText(QPointF(4.0, h - 3.0), QStringLiteral("0"));
        const auto middle = frequencyText(rate / 4.0);
        painter->drawText(QPointF(w / 2.0 - metrics.horizontalAdvance(middle) / 2.0,
                                  h - 3.0),
                          middle);
        const auto right = frequencyText(rate / 2.0);
        painter->drawText(QPointF(w - metrics.horizontalAdvance(right) - 4.0, h - 3.0),
                          right);
    } else {
        painter->drawText(QPointF(4.0, h - 3.0), QStringLiteral("频率 →"));
    }

    const auto peakText = QStringLiteral("峰值 %1").arg(peak, 0, 'f', 3);
    painter->drawText(
        QPointF(w - metrics.horizontalAdvance(peakText) - 6.0, top + metrics.ascent()),
        peakText);
}

void WaveformItem::paintHistogram(QPainter* painter, qreal w, qreal h)
{
    constexpr int kBins = 96;
    struct Histogram {
        QColor color;
        float minValue = 0.0f;
        float maxValue = 0.0f;
        float peak = 0.0f;
        std::vector<float> counts;
    };

    std::vector<Histogram> histograms;
    float peakCount = 0.0f;
    const int channels = controller_->waveformChannelCount();
    for (int channel = 0; channel < channels; ++channel) {
        if (!controller_->channelVisible(channel))
            continue;
        Histogram histogram;
        QColor colour = controller_->channelColor(channel);
        if (!colour.isValid())
            colour = QColor(QStringLiteral("#1abc9c"));
        histogram.color = colour;
        controller_->buildHistogram(channel, kBins, histogram.minValue,
                                    histogram.maxValue, histogram.counts);
        if (histogram.counts.empty())
            continue;
        for (const float count : histogram.counts)
            histogram.peak = std::max(histogram.peak, count);
        peakCount = std::max(peakCount, histogram.peak);
        histograms.push_back(std::move(histogram));
    }

    if (histograms.empty() || peakCount <= 0.0f) {
        painter->setPen(QColor(QStringLiteral("#7f8c8d")));
        painter->drawText(QPointF(12.0, 26.0), QStringLiteral("等待足够的数据…"));
        return;
    }

    const qreal bottom = 18.0;
    const qreal top = 8.0;
    const qreal plotHeight = std::max<qreal>(h - bottom - top, 1.0);
    const qreal barWidth = w / static_cast<qreal>(kBins);

    for (const auto& histogram : histograms) {
        QColor fill = histogram.color;
        fill.setAlpha(105);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        for (int i = 0; i < static_cast<int>(histogram.counts.size()); ++i) {
            const qreal barHeight =
                histogram.counts[static_cast<std::size_t>(i)] / peakCount * plotHeight;
            if (barHeight < 0.5)
                continue;
            painter->drawRect(QRectF(static_cast<qreal>(i) * barWidth,
                                     h - bottom - barHeight,
                                     std::max<qreal>(1.0, barWidth - 1.0),
                                     barHeight));
        }
    }
    painter->setBrush(Qt::NoBrush);

    QFont font = painter->font();
    font.setPixelSize(std::max(9, axisFontPx_));
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    painter->setPen(QColor(140, 155, 170, 205));

    const auto& first = histograms.front();
    const auto lowText = QString::number(first.minValue, 'f', 2);
    const auto midText =
        QString::number((first.minValue + first.maxValue) * 0.5f, 'f', 2);
    const auto highText = QString::number(first.maxValue, 'f', 2);
    painter->drawText(QPointF(4.0, h - 3.0), lowText);
    painter->drawText(QPointF(w / 2.0 - metrics.horizontalAdvance(midText) / 2.0,
                              h - 3.0),
                      midText);
    painter->drawText(QPointF(w - metrics.horizontalAdvance(highText) - 4.0, h - 3.0),
                      highText);
    const auto peakText = QStringLiteral("峰值计数 %1").arg(peakCount, 0, 'f', 0);
    painter->drawText(
        QPointF(w - metrics.horizontalAdvance(peakText) - 6.0, top + metrics.ascent()),
        peakText);
}

void WaveformItem::paintXY(QPainter* painter, qreal w, qreal h)
{
    const int channels = controller_->waveformChannelCount();
    if (channels < 2) {
        painter->setPen(QColor(QStringLiteral("#7f8c8d")));
        painter->drawText(QPointF(12.0, 26.0),
                          QStringLiteral("需要至少两个通道才能显示 XY"));
        return;
    }

    struct Curve {
        int channel = 0;
        QColor color;
        float yMin = 0.0f;
        float yMax = 1.0f;
        std::vector<float> points;
    };
    std::vector<Curve> curves;
    float xMin = std::numeric_limits<float>::max();
    float xMax = std::numeric_limits<float>::lowest();

    for (int channel = 1; channel < channels; ++channel) {
        if (!controller_->channelVisible(channel))
            continue;
        std::vector<float> points;
        controller_->buildXY(0, channel, 2000, points);
        if (points.size() < 8)
            continue;
        Curve curve;
        curve.channel = channel;
        curve.color = controller_->channelColor(channel);
        if (!curve.color.isValid())
            curve.color = QColor(QStringLiteral("#1abc9c"));
        curve.yMin = std::numeric_limits<float>::max();
        curve.yMax = std::numeric_limits<float>::lowest();
        for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
            xMin = std::min(xMin, points[i]);
            xMax = std::max(xMax, points[i]);
            curve.yMin = std::min(curve.yMin, points[i + 1]);
            curve.yMax = std::max(curve.yMax, points[i + 1]);
        }
        curve.points = std::move(points);
        curves.push_back(std::move(curve));
    }

    if (curves.empty() || xMax <= xMin) {
        painter->setPen(QColor(QStringLiteral("#7f8c8d")));
        painter->drawText(QPointF(12.0, 26.0),
                          QStringLiteral("需要 CH0 与其它通道的数据"));
        return;
    }

    if (xMax - xMin < 1.0e-9f) {
        xMin -= 0.5f;
        xMax += 0.5f;
    }

    const qreal left = 6.0;
    const qreal top = 8.0;
    const qreal bottom = 18.0;
    const qreal plotWidth = std::max<qreal>(w - left * 2.0, 1.0);
    const qreal plotHeight = std::max<qreal>(h - top - bottom, 1.0);

    for (auto& curve : curves) {
        if (curve.yMax - curve.yMin < 1.0e-9f) {
            curve.yMin -= 0.5f;
            curve.yMax += 0.5f;
        }
        QPolygonF line;
        line.reserve(static_cast<int>(curve.points.size() / 2));
        for (std::size_t i = 0; i + 1 < curve.points.size(); i += 2) {
            const qreal x = left
                + (curve.points[i] - xMin) / (xMax - xMin) * plotWidth;
            const qreal y = top
                + (1.0 - (curve.points[i + 1] - curve.yMin)
                            / (curve.yMax - curve.yMin))
                    * plotHeight;
            line.append(QPointF(x, y));
        }
        painter->setPen(QPen(curve.color, 1.2));
        painter->drawPolyline(line);
    }

    QFont font = painter->font();
    font.setPixelSize(std::max(9, axisFontPx_));
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    painter->setPen(QColor(140, 155, 170, 205));

    const auto xLow = QString::number(xMin, 'f', 2);
    const auto xMid = QString::number((xMin + xMax) * 0.5f, 'f', 2);
    const auto xHigh = QString::number(xMax, 'f', 2);
    painter->drawText(QPointF(4.0, h - 3.0), xLow);
    painter->drawText(QPointF(w / 2.0 - metrics.horizontalAdvance(xMid) / 2.0,
                              h - 3.0),
                      xMid);
    painter->drawText(QPointF(w - metrics.horizontalAdvance(xHigh) - 4.0, h - 3.0),
                      xHigh);

    qreal labelY = top + metrics.ascent();
    painter->drawText(QPointF(6.0, labelY), QStringLiteral("X: CH0"));
    labelY += metrics.height() + 1.0;
    for (const auto& curve : curves) {
        painter->setPen(curve.color);
        painter->drawText(
            QPointF(6.0, labelY),
            QStringLiteral("Y CH%1  [%2 .. %3]")
                .arg(curve.channel)
                .arg(curve.yMin, 0, 'f', 2)
                .arg(curve.yMax, 0, 'f', 2));
        labelY += metrics.height() + 1.0;
        if (labelY > h - bottom - metrics.height())
            break;
    }
}

} // namespace vofa::app
