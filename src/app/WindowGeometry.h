#pragma once

#include <QRect>

#include <algorithm>

namespace vofa::app {

// Shrinks and moves `window` so it lies completely inside `available`.
// The size is clamped first and the position afterwards, so a window that is
// larger than the screen ends up anchored to the top-left corner instead of
// hanging off the right/bottom edge.
[[nodiscard]] inline QRect clampWindowToAvailable(const QRect& window,
                                                  const QRect& available)
{
    if (!window.isValid() || !available.isValid())
        return window;

    QRect result = window;
    result.setWidth(std::min(result.width(), available.width()));
    result.setHeight(std::min(result.height(), available.height()));

    if (result.left() < available.left())
        result.moveLeft(available.left());
    if (result.top() < available.top())
        result.moveTop(available.top());
    if (result.right() > available.right())
        result.moveRight(available.right());
    if (result.bottom() > available.bottom())
        result.moveBottom(available.bottom());

    return result;
}

} // namespace vofa::app
