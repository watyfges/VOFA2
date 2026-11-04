#pragma once

#include <cmath>
#include <limits>

namespace vofa::core {

// Device-provided floats are untrusted. NaN/Inf and absurd magnitudes must not
// reach the chart, auto-fit, cursor math or the renderer.
inline float sanitizeSample(float value) noexcept
{
    if (!std::isfinite(value))
        return 0.0f;

    constexpr float kLimit = 1.0e30f;
    if (value > kLimit)
        return kLimit;
    if (value < -kLimit)
        return -kLimit;
    return value;
}

} // namespace vofa::core
