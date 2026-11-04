#pragma once

#include <cstddef>
#include <cstdint>

namespace vofa::limits {

// Hard limits are intentionally conservative. They are the last line of
// defence against malformed device data; callers may lower them, never raise
// them without a very good reason.
inline constexpr std::size_t kMaxChannels = 64;
inline constexpr std::size_t kDefaultParserCapacity = 64 * 1024;
inline constexpr std::size_t kMaxSampleFrameBytes = 1 * 1024 * 1024;
inline constexpr std::size_t kMaxImageFrameBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kMaxImageHeaderBytes = 256;
inline constexpr std::uint32_t kMaxImageDimension = 8192;
inline constexpr std::uint64_t kMaxImagePixels = 4096ull * 4096ull;
inline constexpr std::uint32_t kMaxImageIndex = 63;
inline constexpr std::size_t kMaxTextLineBytes = 64 * 1024;
inline constexpr std::size_t kMaxPendingSendBytes = 256 * 1024;
inline constexpr std::size_t kDefaultRingCapacity = 1u << 18;
// Frame ring between the transport thread and the UI consumer. Sized for a
// 20 MB/s burst of small JustFloat frames (~1M frames/s) without the UI
// having to drain more often than its 60 Hz tick.
inline constexpr std::size_t kDefaultFrameRingCapacity = 1u << 15;
inline constexpr std::size_t kMaxRingCapacity = 1u << 20;
inline constexpr int kChannelCountChangeConfirmFrames = 3;

} // namespace vofa::limits
