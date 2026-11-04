#include "test_framework.h"

#include "data/ChannelStore.h"

#include <array>
#include <cmath>
#include <limits>
#include <span>

using namespace vofa;
using namespace vofa::data;

namespace {

template <std::size_t N>
bool push(ChannelStore& store, const std::array<float, N>& values, std::int64_t timestampNs = 0)
{
    return store.pushFrame(std::span<const float>(values.data(), values.size()), timestampNs);
}

} // namespace

VOFA_TEST(channel_history_wraps_and_copies)
{
    ChannelHistory history(4);
    for (int i = 1; i <= 6; ++i)
        history.push(static_cast<float>(i));

    VOFA_CHECK_EQ(history.size(), 4u);
    VOFA_CHECK_EQ(history.absoluteStart(), 2u);
    VOFA_CHECK_EQ(history.absoluteEnd(), 6u);

    std::array<float, 4> values {};
    VOFA_CHECK(history.copyRange(2, 4, values.data()));
    VOFA_CHECK_EQ(values[0], 3.0f);
    VOFA_CHECK_EQ(values[1], 4.0f);
    VOFA_CHECK_EQ(values[2], 5.0f);
    VOFA_CHECK_EQ(values[3], 6.0f);

    VOFA_CHECK(!history.copyRange(1, 4, values.data()));
    VOFA_CHECK(!history.copyRange(2, 5, values.data()));

    float minValue = 0.0f;
    float maxValue = 0.0f;
    VOFA_CHECK(history.minMax(2, 4, minValue, maxValue));
    VOFA_CHECK_EQ(minValue, 3.0f);
    VOFA_CHECK_EQ(maxValue, 6.0f);
}

VOFA_TEST(channel_store_first_frame_fixes_layout)
{
    ChannelStore store(8, 16);
    const std::array<float, 2> values {1.0f, 2.0f};
    VOFA_CHECK(push(store, values, 100));

    VOFA_CHECK_EQ(store.drain(), 1u);
    VOFA_CHECK_EQ(store.channelCount(), 2u);
    VOFA_CHECK_EQ(store.layoutEpoch(), 1u);
    VOFA_CHECK_EQ(store.framesStored(), 1u);
    VOFA_CHECK_EQ(store.latestTimestampNs(), 100);

    const auto* channel0 = store.channel(0);
    const auto* channel1 = store.channel(1);
    VOFA_CHECK(channel0 != nullptr);
    VOFA_CHECK(channel1 != nullptr);
    VOFA_CHECK_EQ(channel0->size(), 1u);
    VOFA_CHECK_EQ(channel1->size(), 1u);

    float value = 0.0f;
    VOFA_CHECK(channel0->copyRange(0, 1, &value));
    VOFA_CHECK_EQ(value, 1.0f);
    VOFA_CHECK(channel1->copyRange(0, 1, &value));
    VOFA_CHECK_EQ(value, 2.0f);
}

VOFA_TEST(channel_store_history_wraps)
{
    ChannelStore store(32, 4);
    for (int i = 0; i < 10; ++i) {
        const std::array<float, 2> values {static_cast<float>(i), static_cast<float>(i * 10)};
        VOFA_CHECK(push(store, values));
    }
    VOFA_CHECK_EQ(store.drain(), 10u);

    const auto* channel0 = store.channel(0);
    VOFA_CHECK(channel0 != nullptr);
    VOFA_CHECK_EQ(channel0->size(), 4u);
    VOFA_CHECK_EQ(channel0->absoluteStart(), 6u);

    std::array<float, 4> values {};
    VOFA_CHECK(channel0->copyRange(6, 4, values.data()));
    VOFA_CHECK_EQ(values[0], 6.0f);
    VOFA_CHECK_EQ(values[3], 9.0f);

    float minValue = 0.0f;
    float maxValue = 0.0f;
    VOFA_CHECK(channel0->minMax(6, 4, minValue, maxValue));
    VOFA_CHECK_EQ(minValue, 6.0f);
    VOFA_CHECK_EQ(maxValue, 9.0f);
}

VOFA_TEST(channel_store_clear_forgets_layout_and_history)
{
    ChannelStore store(32, 16);
    const std::array<float, 3> values {1.0f, 2.0f, 3.0f};
    VOFA_CHECK(push(store, values));
    VOFA_CHECK_EQ(store.drain(), 1u);
    VOFA_CHECK_EQ(store.channelCount(), 3u);
    VOFA_CHECK(store.channel(0) != nullptr && store.channel(0)->size() == 1u);

    store.clearHistories();
    VOFA_CHECK_EQ(store.channelCount(), 0u);
    VOFA_CHECK_EQ(store.framesStored(), 0u);
    VOFA_CHECK(store.channel(0) != nullptr && store.channel(0)->size() == 0u);

    // A later frame re-establishes the layout without stale values.
    VOFA_CHECK(push(store, values));
    VOFA_CHECK_EQ(store.drain(), 1u);
    VOFA_CHECK_EQ(store.channelCount(), 3u);
    VOFA_CHECK(store.channel(0) != nullptr && store.channel(0)->size() == 1u);
}

VOFA_TEST(channel_store_requires_confirmation_before_layout_change)
{
    ChannelStore store(32, 8);
    const std::array<float, 2> two {1.0f, 2.0f};
    VOFA_CHECK(push(store, two));
    VOFA_CHECK_EQ(store.drain(), 1u);
    VOFA_CHECK_EQ(store.channelCount(), 2u);
    VOFA_CHECK_EQ(store.layoutEpoch(), 1u);

    const std::array<float, 3> three {1.0f, 2.0f, 3.0f};
    VOFA_CHECK(push(store, three));
    VOFA_CHECK(push(store, three));
    VOFA_CHECK_EQ(store.drain(), 2u);
    VOFA_CHECK_EQ(store.channelCount(), 2u);
    VOFA_CHECK_EQ(store.layoutEpoch(), 1u);

    VOFA_CHECK(push(store, three));
    VOFA_CHECK_EQ(store.drain(), 1u);
    VOFA_CHECK_EQ(store.channelCount(), 3u);
    VOFA_CHECK_EQ(store.layoutEpoch(), 2u);

    // The first frame after the confirmed layout is stored normally.
    VOFA_CHECK(push(store, three));
    VOFA_CHECK_EQ(store.drain(), 1u);
    const auto* channel2 = store.channel(2);
    VOFA_CHECK(channel2 != nullptr);
    VOFA_CHECK_EQ(channel2->size(), 1u);
    float value = 0.0f;
    VOFA_CHECK(channel2->copyRange(channel2->absoluteStart(), 1, &value));
    VOFA_CHECK_EQ(value, 3.0f);
}

VOFA_TEST(channel_store_rejects_invalid_frames)
{
    ChannelStore store(8, 8);
    VOFA_CHECK(!store.pushFrame({}, 0));
    VOFA_CHECK_EQ(store.droppedFrames(), 1u);

    std::array<float, limits::kMaxChannels + 1> tooMany {};
    VOFA_CHECK(!store.pushFrame(
        std::span<const float>(tooMany.data(), tooMany.size()), 0));
    VOFA_CHECK_EQ(store.droppedFrames(), 2u);
}

VOFA_TEST(channel_store_default_ring_handles_20mb_burst)
{
    // Default ring = 32768 four-channel JustFloat frames (~20 MB/s for one
    // 16 ms UI tick). The UI drains it in a single call.
    ChannelStore store;
    const std::array<float, 4> values {1.0f, 2.0f, 3.0f, 4.0f};
    for (std::size_t i = 0; i < limits::kDefaultFrameRingCapacity; ++i)
        VOFA_CHECK(push(store, values, static_cast<std::int64_t>(i)));

    // Ring is full, so the next frame is counted as dropped, not silently lost.
    VOFA_CHECK_EQ(store.droppedFrames(), 0u);
    VOFA_CHECK(!push(store, values, 0));
    VOFA_CHECK_EQ(store.droppedFrames(), 1u);

    const auto drained = store.drain(limits::kDefaultFrameRingCapacity);
    VOFA_CHECK_EQ(drained, limits::kDefaultFrameRingCapacity);
    VOFA_CHECK_EQ(store.channelCount(), 4u);
    VOFA_CHECK_EQ(store.framesStored(), limits::kDefaultFrameRingCapacity);
    VOFA_CHECK(push(store, values, 0));
}

VOFA_TEST(channel_store_drops_when_frame_ring_is_full)
{
    ChannelStore store(2, 8);
    const std::array<float, 1> value {1.0f};
    for (int i = 0; i < 10; ++i)
        VOFA_CHECK(!push(store, value) == (i >= 2));

    VOFA_CHECK_EQ(store.droppedFrames(), 8u);
    VOFA_CHECK_EQ(store.drain(), 2u);
    VOFA_CHECK_EQ(store.framesStored(), 2u);
}

VOFA_TEST(channel_store_sanitizes_values)
{
    ChannelStore store(8, 8);
    const std::array<float, 1> value {std::numeric_limits<float>::quiet_NaN()};
    VOFA_CHECK(push(store, value));
    VOFA_CHECK_EQ(store.drain(), 1u);

    const auto* channel0 = store.channel(0);
    VOFA_CHECK(channel0 != nullptr);
    float stored = 1.0f;
    VOFA_CHECK(channel0->copyRange(0, 1, &stored));
    VOFA_CHECK_EQ(stored, 0.0f);
}

VOFA_TEST(channel_store_drain_limit_is_respected)
{
    ChannelStore store(32, 8);
    const std::array<float, 1> value {1.0f};
    for (int i = 0; i < 5; ++i)
        VOFA_CHECK(push(store, value));

    VOFA_CHECK_EQ(store.drain(2), 2u);
    VOFA_CHECK_EQ(store.framesStored(), 2u);
    VOFA_CHECK_EQ(store.drain(10), 3u);
    VOFA_CHECK_EQ(store.framesStored(), 5u);
}
