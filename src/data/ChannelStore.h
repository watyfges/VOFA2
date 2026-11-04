#pragma once

#include "core/Limits.h"
#include "core/RingBuffer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace vofa::data {

// Fixed-size frame passed from the transport/parser thread to the UI thread.
// The frame ring is the only cross-thread structure; channel histories are
// owned and accessed exclusively by the UI thread.
struct SampleFrame {
    std::array<float, limits::kMaxChannels> values {};
    std::int64_t timestampNs = 0;
    std::uint64_t sequence = 0;
    std::uint16_t channels = 0;
};

// Consumer-side per-channel ring. Not thread-safe: the UI thread owns it.
class ChannelHistory {
public:
    explicit ChannelHistory(std::size_t capacity = limits::kDefaultRingCapacity);

    void reset(std::size_t capacity);
    void push(float value) noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint64_t absoluteStart() const noexcept { return absoluteStart_; }
    [[nodiscard]] std::uint64_t absoluteEnd() const noexcept { return absoluteEnd_; }

    // Copies [startAbsolute, startAbsolute + count) into out. Returns false if
    // the range is not fully retained.
    [[nodiscard]] bool copyRange(std::uint64_t startAbsolute,
                                 std::size_t count,
                                 float* out) const noexcept;

    [[nodiscard]] bool minMax(std::uint64_t startAbsolute,
                              std::size_t count,
                              float& minValue,
                              float& maxValue) const noexcept;

private:
    std::unique_ptr<float[]> data_;
    std::size_t capacity_ = 0;
    std::size_t head_ = 0; // ring index of the oldest sample
    std::size_t size_ = 0;
    std::uint64_t absoluteStart_ = 0;
    std::uint64_t absoluteEnd_ = 0;
};

class ChannelStore {
public:
    explicit ChannelStore(
        std::size_t frameRingCapacity = limits::kDefaultFrameRingCapacity,
        std::size_t channelCapacity = limits::kDefaultRingCapacity);

    // Producer thread only. Returns false if the frame ring is full or the
    // frame is invalid.
    [[nodiscard]] bool pushFrame(std::span<const float> samples,
                                 std::int64_t timestampNs) noexcept;

    // Consumer/UI thread only. Moves up to maxFrames frames from the frame
    // ring into the channel histories. Returns the number of frames popped.
    std::size_t drain(std::size_t maxFrames = 4096);

    [[nodiscard]] std::size_t channelCount() const noexcept { return channelCount_; }
    [[nodiscard]] std::uint64_t layoutEpoch() const noexcept { return layoutEpoch_; }
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept
    {
        return droppedFrames_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t framesStored() const noexcept { return framesStored_; }
    [[nodiscard]] std::int64_t latestTimestampNs() const noexcept { return latestTimestampNs_; }

    [[nodiscard]] const ChannelHistory* channel(std::size_t index) const noexcept;
    [[nodiscard]] ChannelHistory* channel(std::size_t index) noexcept;

    // Consumer/UI thread only. Clears sample history without changing the
    // channel layout (used when switching the active TCP client).
    void clearHistories();

private:
    void rebuild(std::size_t channels);

    core::SpscRingBuffer<SampleFrame> frameRing_;
    std::size_t channelCapacity_;
    std::vector<ChannelHistory> histories_;
    std::size_t channelCount_ = 0;
    std::uint64_t layoutEpoch_ = 0;
    std::uint64_t nextSequence_ = 0;
    std::size_t pendingChannelCount_ = 0;
    int pendingChannelCountFrames_ = 0;
    std::uint64_t framesStored_ = 0;
    std::int64_t latestTimestampNs_ = 0;
    std::atomic<std::uint64_t> droppedFrames_ {0};
};

} // namespace vofa::data
