#include "JustFloatParser.h"
#include "core/Sanitize.h"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace vofa::protocol {

JustFloatParser::JustFloatParser(std::size_t capacity)
    : buffer_(std::max<std::size_t>(capacity, kTail.size() + 4))
{
}

void JustFloatParser::reset() noexcept
{
    buffer_.clear();
    stats_ = {};
}

void JustFloatParser::feed(std::span<const std::byte> data,
                           IFrameSink& sink,
                           std::int64_t timestampNs)
{
    stats_.bytesIn += data.size();

    while (!data.empty()) {
        const std::size_t space = buffer_.capacity() - buffer_.size();
        if (space == 0) {
            processAvailable(sink, timestampNs);
            if (buffer_.capacity() - buffer_.size() == 0) {
                // No complete frame and no room: drop the oldest half so the
                // parser can resynchronise on the next tail.
                buffer_.consume(buffer_.size() / 2 + 1);
                ++stats_.errors;
                ++stats_.resyncs;
            }
            continue;
        }

        const std::size_t take = std::min(space, data.size());
        const auto chunk = data.first(take);
        if (!buffer_.append(chunk)) {
            // Defensive: append should fit because take <= space.
            buffer_.clear();
            ++stats_.errors;
            ++stats_.resyncs;
        }
        data = data.subspan(take);
        processAvailable(sink, timestampNs);
    }
}

void JustFloatParser::processAvailable(IFrameSink& sink, std::int64_t timestampNs)
{
    for (;;) {
        const auto bytes = buffer_.data();
        if (bytes.size() < kTail.size())
            return;

        const auto tailIt = std::search(bytes.begin(), bytes.end(), kTail.begin(), kTail.end());
        if (tailIt == bytes.end()) {
            if (bytes.size() > limits::kMaxSampleFrameBytes) {
                // Keep the last 3 bytes: a tail may be split across chunks.
                buffer_.consume(bytes.size() - (kTail.size() - 1));
                ++stats_.errors;
                ++stats_.resyncs;
            }
            return;
        }

        const auto tailIndex = static_cast<std::size_t>(tailIt - bytes.begin());
        const std::size_t payloadSize = tailIndex;
        const std::size_t channels = payloadSize / sizeof(float);

        if (payloadSize == 0) {
            // Empty frame: skip the tail and continue.
            buffer_.consume(kTail.size());
            ++stats_.errors;
            ++stats_.resyncs;
            continue;
        }

        if (payloadSize % sizeof(float) != 0) {
            // The candidate payload is not float-aligned. Shift by one byte and
            // look for the next tail; dropping the whole candidate could throw
            // away an otherwise valid frame after a short garbage prefix.
            buffer_.consume(1);
            ++stats_.errors;
            ++stats_.resyncs;
            continue;
        }

        if (channels > limits::kMaxChannels
            || payloadSize > limits::kMaxSampleFrameBytes) {
            buffer_.consume(tailIndex + kTail.size());
            ++stats_.errors;
            ++stats_.resyncs;
            continue;
        }

        for (std::size_t i = 0; i < channels; ++i) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(bytes.data() + i * sizeof(float));
            const std::uint32_t raw = static_cast<std::uint32_t>(p[0])
                | (static_cast<std::uint32_t>(p[1]) << 8)
                | (static_cast<std::uint32_t>(p[2]) << 16)
                | (static_cast<std::uint32_t>(p[3]) << 24);
            const float value = std::bit_cast<float>(raw);
            const float clean = core::sanitizeSample(value);
            if (clean != value)
                ++stats_.sanitized;
            samples_[i] = clean;
        }

        sink.onSamples(std::span<const float>(samples_.data(), channels), timestampNs);
        ++stats_.frames;
        buffer_.consume(tailIndex + kTail.size());
    }
}

const ParserStats& JustFloatParser::stats() const noexcept
{
    return stats_;
}

std::string_view JustFloatParser::name() const noexcept
{
    return "justfloat";
}

} // namespace vofa::protocol
