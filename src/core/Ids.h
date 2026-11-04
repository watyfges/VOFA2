#pragma once

#include <cstdint>

namespace vofa {

// Client ids are monotonically increasing and never reused. They are never
// derived from a UI row index or a QList position.
using ClientId = std::uint64_t;
inline constexpr ClientId kNoClient = 0;

using CommandId = std::uint64_t;
using WidgetId = std::uint64_t;
using TabId = std::uint64_t;

} // namespace vofa
