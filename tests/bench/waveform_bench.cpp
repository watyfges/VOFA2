// CPU-side waveform decimation benchmark: 8 channels x 250k samples scanned
// into a fixed number of pixel columns, matching AppController::
// buildWaveformRange's min/max pass (the work done for every plot refresh).
#include "data/ChannelStore.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>

int main()
{
    constexpr int kChannels = 8;
    constexpr std::size_t kSamples = 250000;
    constexpr int kWidth = 2000;
    constexpr int kFrames = 120;

    vofa::data::ChannelStore store(32768, kSamples);
    std::array<float, kChannels> values {};

    for (std::size_t i = 0; i < kSamples; ++i) {
        const double t = static_cast<double>(i) / 1000.0;
        for (int c = 0; c < kChannels; ++c)
            values[static_cast<std::size_t>(c)] = static_cast<float>(
                std::sin(t * (1.0 + c * 0.1)) + (i % 7) * 0.01);
        while (!store.pushFrame(values, static_cast<std::int64_t>(i)))
            (void)store.drain(32768);
        if ((i & 1023u) == 0u)
            (void)store.drain(32768);
    }
    (void)store.drain(1u << 20);

    const auto* history = store.channel(0);
    if (history == nullptr || history->size() < kSamples / 2) {
        std::printf("waveform bench: store is empty\n");
        return 1;
    }

    const auto start = history->absoluteStart();
    const auto count = history->absoluteEnd() - start;
    volatile float sink = 0.0f;

    const auto begin = std::chrono::steady_clock::now();
    for (int frame = 0; frame < kFrames; ++frame) {
        for (int c = 0; c < kChannels; ++c) {
            const auto* channel = store.channel(static_cast<std::size_t>(c));
            if (channel == nullptr)
                continue;
            for (int column = 0; column < kWidth; ++column) {
                const auto first = start
                    + static_cast<std::uint64_t>(column) * count / kWidth;
                const auto last = start
                    + static_cast<std::uint64_t>(column + 1) * count / kWidth;
                float minValue = 0.0f;
                float maxValue = 0.0f;
                if (channel->minMax(first, static_cast<std::size_t>(last - first),
                                    minValue, maxValue)) {
                    sink += minValue + maxValue;
                }
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const double totalMs =
        std::chrono::duration<double, std::milli>(end - begin).count();

    std::printf(
        "channels=%d samples=%zu width=%d frames=%d total=%.2fms per_frame=%.2fms sink=%.3f\n",
        kChannels, kSamples, kWidth, kFrames, totalMs, totalMs / kFrames,
        static_cast<double>(sink));
    return 0;
}
