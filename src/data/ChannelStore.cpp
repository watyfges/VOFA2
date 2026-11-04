#include "ChannelStore.h"
#include "core/Sanitize.h"

#include <algorithm>
#include <stdexcept>

namespace vofa::data {

ChannelHistory::ChannelHistory(std::size_t capacity)
{
    reset(capacity);
}

void ChannelHistory::reset(std::size_t capacity)
{
    capacity_ = std::max<std::size_t>(capacity, 1);
    data_ = std::make_unique<float[]>(capacity_);
    head_ = 0;
    size_ = 0;
    absoluteStart_ = 0;
    absoluteEnd_ = 0;
}

void ChannelHistory::push(float value) noexcept
{
    if (size_ < capacity_) {
        data_[(head_ + size_) % capacity_] = value;
        ++size_;
    } else {
        data_[head_] = value;
        head_ = (head_ + 1) % capacity_;
        ++absoluteStart_;
    }
    ++absoluteEnd_;
}

bool ChannelHistory::copyRange(std::uint64_t startAbsolute,
                               std::size_t count,
                               float* out) const noexcept
{
    if (out == nullptr && count != 0)
        return false;
    if (startAbsolute < absoluteStart_ || startAbsolute + count > absoluteEnd_)
        return false;
    if (count == 0)
        return true;

    const auto offset = static_cast<std::size_t>(startAbsolute - absoluteStart_);
    const auto firstIndex = (head_ + offset) % capacity_;
    const auto firstChunk = std::min(count, capacity_ - firstIndex);
    std::copy_n(data_.get() + firstIndex, firstChunk, out);
    if (firstChunk < count) {
        std::copy_n(data_.get(), count - firstChunk, out + firstChunk);
    }
    return true;
}

bool ChannelHistory::minMax(std::uint64_t startAbsolute,
                            std::size_t count,
                            float& minValue,
                            float& maxValue) const noexcept
{
    if (count == 0 || startAbsolute < absoluteStart_ || startAbsolute + count > absoluteEnd_)
        return false;

    const auto offset = static_cast<std::size_t>(startAbsolute - absoluteStart_);
    const auto firstIndex = (head_ + offset) % capacity_;
    const auto firstChunk = std::min(count, capacity_ - firstIndex);

    float minV = data_[firstIndex];
    float maxV = data_[firstIndex];
    for (std::size_t i = 1; i < firstChunk; ++i) {
        minV = std::min(minV, data_[firstIndex + i]);
        maxV = std::max(maxV, data_[firstIndex + i]);
    }
    for (std::size_t i = 0; i < count - firstChunk; ++i) {
        minV = std::min(minV, data_[i]);
        maxV = std::max(maxV, data_[i]);
    }

    minValue = minV;
    maxValue = maxV;
    return true;
}

ChannelStore::ChannelStore(std::size_t frameRingCapacity, std::size_t channelCapacity)
    : frameRing_(std::max<std::size_t>(frameRingCapacity, 1))
    , channelCapacity_(std::max<std::size_t>(channelCapacity, 1))
{
}

bool ChannelStore::pushFrame(std::span<const float> samples, std::int64_t timestampNs) noexcept
{
    if (samples.empty() || samples.size() > limits::kMaxChannels) {
        droppedFrames_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    SampleFrame frame;
    frame.channels = static_cast<std::uint16_t>(samples.size());
    frame.timestampNs = timestampNs;
    frame.sequence = nextSequence_++;
    for (std::size_t i = 0; i < samples.size(); ++i)
        frame.values[i] = core::sanitizeSample(samples[i]);

    if (!frameRing_.push(std::move(frame))) {
        droppedFrames_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

std::size_t ChannelStore::drain(std::size_t maxFrames)
{
    SampleFrame frame;
    std::size_t popped = 0;

    while (popped < maxFrames && frameRing_.pop(frame)) {
        ++popped;

        if (frame.channels == 0 || frame.channels > limits::kMaxChannels) {
            droppedFrames_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        if (channelCount_ == 0) {
            // First valid frame fixes the layout.
            rebuild(frame.channels);
        } else if (frame.channels != channelCount_) {
            if (pendingChannelCount_ == frame.channels) {
                ++pendingChannelCountFrames_;
            } else {
                pendingChannelCount_ = frame.channels;
                pendingChannelCountFrames_ = 1;
            }

            if (pendingChannelCountFrames_ >= limits::kChannelCountChangeConfirmFrames) {
                rebuild(frame.channels);
                pendingChannelCountFrames_ = 0;
            }
            // Do not store frames that belong to an unconfirmed layout.
            continue;
        }

        pendingChannelCountFrames_ = 0;

        for (std::size_t i = 0; i < frame.channels; ++i)
            histories_[i].push(frame.values[i]);

        ++framesStored_;
        latestTimestampNs_ = frame.timestampNs;
    }

    return popped;
}

void ChannelStore::rebuild(std::size_t channels)
{
    histories_.clear();
    histories_.reserve(channels);
    for (std::size_t i = 0; i < channels; ++i)
        histories_.emplace_back(channelCapacity_);
    channelCount_ = channels;
    ++layoutEpoch_;
}

const ChannelHistory* ChannelStore::channel(std::size_t index) const noexcept
{
    if (index >= histories_.size())
        return nullptr;
    return &histories_[index];
}

ChannelHistory* ChannelStore::channel(std::size_t index) noexcept
{
    if (index >= histories_.size())
        return nullptr;
    return &histories_[index];
}

void ChannelStore::clearHistories()
{
    for (auto& history : histories_)
        history.reset(channelCapacity_);
    // Also forget the layout: after a disconnect/client switch the UI must
    // show "waiting for data" instead of N empty channels. The next valid
    // frame re-establishes the layout; per-channel settings live in
    // AppController and are re-applied by ensureChannelConfigs().
    channelCount_ = 0;
    pendingChannelCount_ = 0;
    pendingChannelCountFrames_ = 0;
    ++layoutEpoch_;
    framesStored_ = 0;
    latestTimestampNs_ = 0;
}

} // namespace vofa::data
