#include "protocol/JustFloatParser.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace {

struct CountingSink final : vofa::protocol::IFrameSink {
    std::uint64_t samples = 0;
    void onSamples(std::span<const float> values, std::int64_t) override
    {
        samples += values.size();
    }
};

void appendFloatLE(std::vector<std::byte>& out, float value)
{
    const auto raw = std::bit_cast<std::uint32_t>(value);
    out.push_back(std::byte {static_cast<std::uint8_t>(raw & 0xff)});
    out.push_back(std::byte {static_cast<std::uint8_t>((raw >> 8) & 0xff)});
    out.push_back(std::byte {static_cast<std::uint8_t>((raw >> 16) & 0xff)});
    out.push_back(std::byte {static_cast<std::uint8_t>((raw >> 24) & 0xff)});
}

} // namespace

int main()
{
    constexpr std::size_t kFrames = 1'000'000;
    constexpr std::size_t kChannels = 4;
    constexpr std::size_t kChunk = 64 * 1024;

    std::vector<std::byte> stream;
    stream.reserve(kFrames * (kChannels * sizeof(float) + 4));
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        appendFloatLE(stream, static_cast<float>(frame));
        appendFloatLE(stream, 1.0f);
        appendFloatLE(stream, 2.0f);
        appendFloatLE(stream, 3.0f);
        stream.push_back(std::byte {0x00});
        stream.push_back(std::byte {0x00});
        stream.push_back(std::byte {0x80});
        stream.push_back(std::byte {0x7f});
    }

    vofa::protocol::JustFloatParser parser;
    CountingSink sink;

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t offset = 0; offset < stream.size(); offset += kChunk) {
        const auto count = std::min(kChunk, stream.size() - offset);
        parser.feed(std::span<const std::byte>(stream.data() + offset, count), sink, 0);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto seconds = std::chrono::duration<double>(elapsed).count();

    const auto bytes = static_cast<double>(stream.size());
    const auto samples = static_cast<double>(sink.samples);
    std::printf("frames=%zu samples=%llu bytes=%.0f time=%.3fs throughput=%.1f MB/s samples/s=%.0f\n",
                kFrames,
                static_cast<unsigned long long>(sink.samples),
                bytes,
                seconds,
                bytes / seconds / (1024.0 * 1024.0),
                samples / seconds);
    return sink.samples == kFrames * kChannels ? 0 : 1;
}
