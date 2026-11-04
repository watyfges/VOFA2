#include "test_framework.h"
#include "test_helpers.h"

#include "core/ByteReader.h"
#include "core/FixedBuffer.h"
#include "core/RingBuffer.h"
#include "core/Sanitize.h"

#include <cmath>
#include <limits>
#include <span>

using namespace vofa;
using namespace vofa::test;

VOFA_TEST(byte_reader_reads_integers_and_floats)
{
    {
        auto data = bytes({0x34, 0x12});
        core::ByteReader reader(std::span<const std::byte>(data.data(), data.size()));
        const auto value = reader.readU16LE();
        VOFA_CHECK(value.has_value());
        VOFA_CHECK_EQ(*value, 0x1234);
        VOFA_CHECK(reader.empty());
    }
    {
        auto data = bytes({0x78, 0x56, 0x34, 0x12});
        core::ByteReader reader(std::span<const std::byte>(data.data(), data.size()));
        const auto value = reader.readU32LE();
        VOFA_CHECK(value.has_value());
        VOFA_CHECK_EQ(*value, 0x12345678u);
    }
    {
        auto data = bytes({0x00, 0x00, 0x80, 0x3f});
        core::ByteReader reader(std::span<const std::byte>(data.data(), data.size()));
        const auto value = reader.readF32LE();
        VOFA_CHECK(value.has_value());
        VOFA_CHECK_EQ(*value, 1.0f);
    }
}

VOFA_TEST(byte_reader_rejects_out_of_bounds)
{
    auto data = bytes({0x01, 0x02, 0x03});
    core::ByteReader reader(std::span<const std::byte>(data.data(), data.size()));

    const auto first = reader.readU8();
    VOFA_CHECK(first.has_value());
    VOFA_CHECK_EQ(*first, 1);

    const auto tooLarge = reader.readU32LE();
    VOFA_CHECK(!tooLarge.has_value());
    VOFA_CHECK_EQ(reader.position(), 1u);
    VOFA_CHECK_EQ(reader.remaining(), 2u);

    const auto available = reader.readBytes(2);
    VOFA_CHECK(available.has_value());
    VOFA_CHECK_EQ(available->size(), 2u);
    VOFA_CHECK(reader.empty());
    VOFA_CHECK(!reader.readU8().has_value());
}

VOFA_TEST(byte_reader_peek_and_skip)
{
    auto data = bytes({1, 2, 3, 4});
    core::ByteReader reader(std::span<const std::byte>(data.data(), data.size()));

    const auto peeked = reader.peek(2);
    VOFA_CHECK_EQ(peeked.size(), 2u);
    VOFA_CHECK_EQ(reader.position(), 0u);
    VOFA_CHECK(!reader.skip(5));
    VOFA_CHECK_EQ(reader.position(), 0u);
    VOFA_CHECK(reader.skip(2));
    VOFA_CHECK_EQ(reader.position(), 2u);
    VOFA_CHECK(reader.peek(5).empty());
}

VOFA_TEST(fixed_buffer_basic_and_overflow)
{
    core::FixedBuffer buffer(8);
    const auto first = bytes({1, 2, 3, 4, 5, 6});
    VOFA_CHECK(buffer.append(std::span<const std::byte>(first.data(), first.size())));
    VOFA_CHECK_EQ(buffer.size(), 6u);

    buffer.consume(4);
    VOFA_CHECK_EQ(buffer.size(), 2u);
    const auto remaining = buffer.data();
    VOFA_CHECK_EQ(std::to_integer<int>(remaining[0]), 5);
    VOFA_CHECK_EQ(std::to_integer<int>(remaining[1]), 6);

    const auto more = bytes({7, 8, 9});
    VOFA_CHECK(buffer.append(std::span<const std::byte>(more.data(), more.size())));
    VOFA_CHECK_EQ(buffer.size(), 5u);
    VOFA_CHECK(!buffer.append(std::span<const std::byte>(more.data(), 4)));

    const auto tooLarge = bytes({1, 2, 3, 4, 5, 6, 7, 8, 9});
    VOFA_CHECK(!buffer.append(std::span<const std::byte>(tooLarge.data(), tooLarge.size())));

    buffer.clear();
    VOFA_CHECK(buffer.empty());
}

VOFA_TEST(fixed_buffer_compacts_and_preserves_order)
{
    core::FixedBuffer buffer(16);
    const auto initial = bytes({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    VOFA_CHECK(buffer.append(std::span<const std::byte>(initial.data(), initial.size())));
    buffer.consume(4);

    const auto next = bytes({11, 12, 13, 14, 15, 16});
    VOFA_CHECK(buffer.append(std::span<const std::byte>(next.data(), next.size())));
    VOFA_CHECK_EQ(buffer.size(), 12u);

    const auto data = buffer.data();
    for (std::size_t i = 0; i < data.size(); ++i) {
        VOFA_CHECK_EQ(std::to_integer<int>(data[i]), static_cast<int>(i) + 5);
    }
}

VOFA_TEST(sp_sc_ring_buffer_fifo_and_drop)
{
    core::SpscRingBuffer<int> ring(3);
    VOFA_CHECK_EQ(ring.capacity(), 4u);

    VOFA_CHECK(ring.push(1));
    VOFA_CHECK(ring.push(2));
    VOFA_CHECK(ring.push(3));
    VOFA_CHECK(ring.push(4));
    VOFA_CHECK(!ring.push(5));
    VOFA_CHECK_EQ(ring.dropped(), 1u);
    VOFA_CHECK_EQ(ring.size(), 4u);

    for (int expected = 1; expected <= 4; ++expected) {
        int value = 0;
        VOFA_CHECK(ring.pop(value));
        VOFA_CHECK_EQ(value, expected);
    }

    int value = 0;
    VOFA_CHECK(!ring.pop(value));
    VOFA_CHECK_EQ(ring.size(), 0u);
}

VOFA_TEST(sp_sc_ring_buffer_capacity_one)
{
    core::SpscRingBuffer<int> ring(1);
    VOFA_CHECK_EQ(ring.capacity(), 1u);
    VOFA_CHECK(ring.push(42));
    VOFA_CHECK(!ring.push(43));
    int value = 0;
    VOFA_CHECK(ring.pop(value));
    VOFA_CHECK_EQ(value, 42);
    VOFA_CHECK(!ring.pop(value));
}

VOFA_TEST(sanitize_rejects_non_finite_and_absurd_values)
{
    VOFA_CHECK_EQ(core::sanitizeSample(1.5f), 1.5f);
    VOFA_CHECK_EQ(core::sanitizeSample(-2.25f), -2.25f);

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();
    VOFA_CHECK_EQ(core::sanitizeSample(nan), 0.0f);
    VOFA_CHECK_EQ(core::sanitizeSample(inf), 0.0f);
    VOFA_CHECK_EQ(core::sanitizeSample(-inf), 0.0f);
    VOFA_CHECK_EQ(core::sanitizeSample(1.0e31f), 1.0e30f);
    VOFA_CHECK_EQ(core::sanitizeSample(-1.0e31f), -1.0e30f);
}
