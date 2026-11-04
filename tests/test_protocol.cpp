#include "test_framework.h"
#include "test_helpers.h"

#include "protocol/FireWaterParser.h"
#include "protocol/ImageParser.h"
#include "protocol/JustFloatParser.h"
#include "protocol/ParserFactory.h"
#include "protocol/RawParser.h"
#include "protocol/SampleImageParser.h"

#include <cmath>
#include <limits>
#include <span>
#include <string>

using namespace vofa;
using namespace vofa::protocol;
using namespace vofa::test;

namespace {

template <typename Parser>
void feed(Parser& parser, const Bytes& data, CollectingSink& sink, std::int64_t timestampNs = 0)
{
    parser.feed(std::span<const std::byte>(data.data(), data.size()), sink, timestampNs);
}

void appendJustFloatTail(Bytes& out)
{
    out.push_back(std::byte {0x00});
    out.push_back(std::byte {0x00});
    out.push_back(std::byte {0x80});
    out.push_back(std::byte {0x7f});
}

} // namespace

VOFA_TEST(justfloat_single_frame)
{
    JustFloatParser parser;
    CollectingSink sink;

    const auto frame = justFloatFrame({1.0f, 2.0f, -3.5f});
    feed(parser, frame, sink, 123);

    VOFA_CHECK_EQ(sink.frames.size(), 1u);
    VOFA_CHECK_EQ(sink.frames[0].samples.size(), 3u);
    VOFA_CHECK_EQ(sink.frames[0].samples[0], 1.0f);
    VOFA_CHECK_EQ(sink.frames[0].samples[1], 2.0f);
    VOFA_CHECK_EQ(sink.frames[0].samples[2], -3.5f);
    VOFA_CHECK_EQ(sink.frames[0].timestampNs, 123);
    VOFA_CHECK_EQ(parser.stats().frames, 1u);
    VOFA_CHECK_EQ(parser.stats().errors, 0u);
}

VOFA_TEST(justfloat_handles_byte_by_byte_and_multiple_frames)
{
    JustFloatParser parser;
    CollectingSink sink;

    auto data = justFloatFrame({1.0f, 2.0f});
    const auto second = justFloatFrame({3.0f});
    data.insert(data.end(), second.begin(), second.end());

    for (const auto byte : data)
        parser.feed(std::span<const std::byte>(&byte, 1), sink, 0);

    VOFA_CHECK_EQ(sink.frames.size(), 2u);
    VOFA_CHECK_EQ(sink.frames[0].samples.size(), 2u);
    VOFA_CHECK_EQ(sink.frames[1].samples.size(), 1u);
    VOFA_CHECK_EQ(sink.frames[1].samples[0], 3.0f);
}

VOFA_TEST(justfloat_resynchronises_after_bad_payload)
{
    JustFloatParser parser;
    CollectingSink sink;

    // One garbage byte followed by a valid frame. The parser shifts by one byte
    // at a time; the important property is that it resynchronises and the next
    // clean frame is decoded correctly.
    Bytes bad {std::byte {0xde}};
    const auto valid = justFloatFrame({4.0f, 5.0f});
    bad.insert(bad.end(), valid.begin(), valid.end());
    feed(parser, bad, sink);
    VOFA_CHECK(parser.stats().errors >= 1u);
    VOFA_CHECK(parser.stats().resyncs >= 1u);

    feed(parser, valid, sink);
    VOFA_CHECK(!sink.frames.empty());
    VOFA_CHECK_EQ(sink.frames.back().samples.size(), 2u);
    VOFA_CHECK_EQ(sink.frames.back().samples[0], 4.0f);
    VOFA_CHECK_EQ(sink.frames.back().samples[1], 5.0f);
}

VOFA_TEST(justfloat_rejects_too_many_channels)
{
    JustFloatParser parser;
    CollectingSink sink;

    Bytes many;
    for (std::size_t i = 0; i < 65; ++i)
        appendFloatLE(many, 1.0f);
    appendJustFloatTail(many);

    feed(parser, many, sink);
    VOFA_CHECK_EQ(sink.frames.size(), 0u);
    VOFA_CHECK(parser.stats().errors >= 1u);

    const auto valid = justFloatFrame({1.0f});
    feed(parser, valid, sink);
    VOFA_CHECK_EQ(sink.frames.size(), 1u);
}

VOFA_TEST(justfloat_sanitizes_nan_and_infinity)
{
    JustFloatParser parser;
    CollectingSink sink;

    // Note: +Inf is intentionally not used here. Its little-endian bytes are
    // 00 00 80 7F, which is exactly the JustFloat tail marker; the protocol
    // cannot represent a +Inf sample. NaN and -Inf are safe.
    const auto frame = justFloatFrame({
        std::numeric_limits<float>::quiet_NaN(),
        -std::numeric_limits<float>::infinity(),
        1.0f,
    });
    feed(parser, frame, sink);

    VOFA_CHECK_EQ(sink.frames.size(), 1u);
    VOFA_CHECK_EQ(sink.frames[0].samples[0], 0.0f);
    VOFA_CHECK_EQ(sink.frames[0].samples[1], 0.0f);
    VOFA_CHECK_EQ(sink.frames[0].samples[2], 1.0f);
    VOFA_CHECK_EQ(parser.stats().sanitized, 2u);
}

VOFA_TEST(justfloat_bounded_without_tail)
{
    JustFloatParser parser(4096);
    CollectingSink sink;

    Bytes garbage(8192, std::byte {0x01});
    feed(parser, garbage, sink);

    VOFA_CHECK_EQ(sink.frames.size(), 0u);
    VOFA_CHECK(parser.stats().errors >= 1u);

    // After a long garbage run, resynchronisation is inherently ambiguous
    // because JustFloat has no start marker. The parser must keep accepting
    // input and stay bounded; it may need several tails to align again.
    const auto valid = justFloatFrame({1.0f});
    for (int i = 0; i < 100; ++i)
        feed(parser, valid, sink);
    VOFA_CHECK_EQ(parser.stats().bytesIn, 8192u + valid.size() * 100u);
    VOFA_CHECK(sink.frames.size() <= 100u);
}

VOFA_TEST(firewater_parses_plain_and_prefixed_lines)
{
    FireWaterParser parser;
    CollectingSink sink;

    const auto data = bytes("1.0,2.5,-3\nplot0:4,5.5\n");
    feed(parser, data, sink);

    VOFA_CHECK_EQ(sink.frames.size(), 2u);
    VOFA_CHECK_EQ(sink.frames[0].samples.size(), 3u);
    VOFA_CHECK_EQ(sink.frames[0].samples[2], -3.0f);
    VOFA_CHECK_EQ(sink.frames[1].samples.size(), 2u);
    VOFA_CHECK_EQ(sink.frames[1].samples[0], 4.0f);
    VOFA_CHECK_EQ(sink.frames[1].samples[1], 5.5f);
}

VOFA_TEST(firewater_handles_crlf_and_partial_lines)
{
    FireWaterParser parser;
    CollectingSink sink;

    feed(parser, bytes("1.0,"), sink);
    VOFA_CHECK_EQ(sink.frames.size(), 0u);

    feed(parser, bytes("2.0\r\n3.0,4.0\r\n"), sink);
    VOFA_CHECK_EQ(sink.frames.size(), 2u);
    VOFA_CHECK_EQ(sink.frames[0].samples.size(), 2u);
    VOFA_CHECK_EQ(sink.frames[0].samples[1], 2.0f);
    VOFA_CHECK_EQ(sink.frames[1].samples.size(), 2u);
}

VOFA_TEST(firewater_drops_invalid_lines_and_recovers)
{
    FireWaterParser parser;
    CollectingSink sink;

    feed(parser, bytes("1.0,abc,3.0\n"), sink);
    VOFA_CHECK_EQ(sink.frames.size(), 0u);
    VOFA_CHECK(parser.stats().errors >= 1u);

    feed(parser, bytes("1.0,2.0\n"), sink);
    VOFA_CHECK_EQ(sink.frames.size(), 1u);
}

VOFA_TEST(firewater_rejects_too_many_channels)
{
    FireWaterParser parser;
    CollectingSink sink;

    std::string line;
    for (std::size_t i = 0; i < 65; ++i) {
        if (i > 0)
            line.push_back(',');
        line.push_back('1');
    }
    line.push_back('\n');

    feed(parser, bytes(line), sink);
    VOFA_CHECK_EQ(sink.frames.size(), 0u);
    VOFA_CHECK(parser.stats().errors >= 1u);
}

VOFA_TEST(firewater_ignores_image_lines)
{
    FireWaterParser parser;
    CollectingSink sink;

    feed(parser, bytes("image:0,27,40,30,4\n"), sink);
    VOFA_CHECK_EQ(sink.frames.size(), 0u);
    VOFA_CHECK_EQ(parser.stats().errors, 0u);
}

VOFA_TEST(firewater_bounds_oversized_line)
{
    FireWaterParser parser(32);
    CollectingSink sink;

    std::string line(100, '1');
    line.push_back('\n');
    feed(parser, bytes(line), sink);

    VOFA_CHECK_EQ(sink.frames.size(), 0u);
    VOFA_CHECK(parser.stats().errors >= 1u);

    feed(parser, bytes("1,2\n"), sink);
    VOFA_CHECK_EQ(sink.frames.size(), 1u);
}

VOFA_TEST(image_parses_header_and_payload)
{
    ImageParser parser;
    CollectingSink sink;

    Bytes data = bytes("image:0,27,40,30,4\n");
    data.push_back(std::byte {1});
    data.push_back(std::byte {2});
    data.push_back(std::byte {3});
    data.push_back(std::byte {4});
    feed(parser, data, sink);

    VOFA_CHECK_EQ(sink.images.size(), 1u);
    VOFA_CHECK_EQ(sink.images[0].id, 0u);
    VOFA_CHECK_EQ(sink.images[0].format, ImageFormat::Jpg);
    VOFA_CHECK_EQ(sink.images[0].width, 40u);
    VOFA_CHECK_EQ(sink.images[0].height, 30u);
    VOFA_CHECK_EQ(sink.images[0].data.size(), 4u);
    VOFA_CHECK_EQ(std::to_integer<int>(sink.images[0].data[3]), 4);
    VOFA_CHECK_EQ(parser.stats().images, 1u);
    VOFA_CHECK_EQ(parser.stats().errors, 0u);
}

VOFA_TEST(image_handles_byte_by_byte_and_multiple_frames)
{
    ImageParser parser;
    CollectingSink sink;

    Bytes data = bytes("image:0,27,40,30,2\n");
    data.push_back(std::byte {0xaa});
    data.push_back(std::byte {0xbb});
    appendAscii(data, "image:1,28,10,20,1\n");
    data.push_back(std::byte {0xcc});

    for (const auto byte : data)
        parser.feed(std::span<const std::byte>(&byte, 1), sink, 0);

    VOFA_CHECK_EQ(sink.images.size(), 2u);
    VOFA_CHECK_EQ(sink.images[0].format, ImageFormat::Jpg);
    VOFA_CHECK_EQ(sink.images[1].id, 1u);
    VOFA_CHECK_EQ(sink.images[1].format, ImageFormat::Png);
    VOFA_CHECK_EQ(sink.images[1].data.size(), 1u);
}

VOFA_TEST(image_rejects_bad_headers_and_recovers)
{
    ImageParser parser;
    CollectingSink sink;

    feed(parser, bytes("image:0,99,40,30,1\na"), sink);
    VOFA_CHECK_EQ(sink.images.size(), 0u);
    VOFA_CHECK(parser.stats().errors >= 1u);

    feed(parser, bytes("image:0,27,9000,30,1\na"), sink);
    VOFA_CHECK_EQ(sink.images.size(), 0u);

    feed(parser, bytes("image:0,27,40,30,0\n"), sink);
    VOFA_CHECK_EQ(sink.images.size(), 0u);

    Bytes valid = bytes("image:0,27,40,30,1\n");
    valid.push_back(std::byte {0x42});
    feed(parser, valid, sink);
    VOFA_CHECK_EQ(sink.images.size(), 1u);
    VOFA_CHECK_EQ(std::to_integer<int>(sink.images[0].data[0]), 0x42);
}

VOFA_TEST(image_resynchronises_after_garbage)
{
    ImageParser parser;
    CollectingSink sink;

    Bytes data = bytes("garbage");
    appendAscii(data, "image:0,27,40,30,2\n");
    data.push_back(std::byte {1});
    data.push_back(std::byte {2});
    feed(parser, data, sink);

    VOFA_CHECK_EQ(sink.images.size(), 1u);
    VOFA_CHECK_EQ(sink.images[0].data.size(), 2u);
    VOFA_CHECK(parser.stats().resyncs >= 1u);
}

VOFA_TEST(image_rejects_out_of_range_id)
{
    ImageParser parser;
    CollectingSink sink;

    feed(parser, bytes("image:64,27,40,30,1\na"), sink);
    VOFA_CHECK_EQ(sink.images.size(), 0u);
    VOFA_CHECK(parser.stats().errors >= 1u);
}

VOFA_TEST(raw_parser_counts_bytes_and_emits_nothing)
{
    RawParser parser;
    CollectingSink sink;

    feed(parser, bytes({1, 2, 3, 4}), sink);
    VOFA_CHECK_EQ(sink.frames.size(), 0u);
    VOFA_CHECK_EQ(parser.stats().bytesIn, 4u);
    VOFA_CHECK_EQ(parser.stats().frames, 0u);
}

VOFA_TEST(sample_image_parser_mixes_justfloat_and_images)
{
    SampleImageParser parser(ProtocolKind::JustFloat);
    CollectingSink sink;

    Bytes data = justFloatFrame({1.0f, 2.0f});
    appendAscii(data, "image:2,28,10,8,4\n");
    data.push_back(std::byte {0xde});
    data.push_back(std::byte {0xad});
    data.push_back(std::byte {0xbe});
    data.push_back(std::byte {0xef});
    const auto tail = justFloatFrame({3.0f, 4.0f});
    data.insert(data.end(), tail.begin(), tail.end());

    feed(parser, data, sink);

    VOFA_CHECK_EQ(sink.frames.size(), 2u);
    VOFA_CHECK_EQ(sink.frames[0].samples[0], 1.0f);
    VOFA_CHECK_EQ(sink.frames[1].samples[1], 4.0f);
    VOFA_CHECK_EQ(sink.images.size(), 1u);
    VOFA_CHECK_EQ(sink.images[0].id, 2u);
    VOFA_CHECK_EQ(sink.images[0].width, 10u);
    VOFA_CHECK_EQ(sink.images[0].data.size(), 4u);
    VOFA_CHECK_EQ(std::to_integer<int>(sink.images[0].data[3]), 0xef);
    VOFA_CHECK_EQ(parser.stats().images, 1u);
}

VOFA_TEST(sample_image_parser_handles_byte_by_byte)
{
    SampleImageParser parser(ProtocolKind::FireWater);
    CollectingSink sink;

    Bytes data = bytes("1,2,3\n");
    appendAscii(data, "image:0,27,40,30,6\n");
    appendAscii(data, "a\nb\r\nc"); // payload is binary-ish and includes newlines
    appendAscii(data, "4,5,6\n");

    for (const auto byte : data)
        parser.feed(std::span<const std::byte>(&byte, 1), sink, 0);

    VOFA_CHECK_EQ(sink.images.size(), 1u);
    VOFA_CHECK_EQ(sink.images[0].id, 0u);
    VOFA_CHECK_EQ(sink.images[0].data.size(), 6u);
    VOFA_CHECK_EQ(sink.frames.size(), 2u);
    VOFA_CHECK_EQ(sink.frames[0].samples.size(), 3u);
    VOFA_CHECK_EQ(sink.frames[1].samples[2], 6.0f);
}

VOFA_TEST(sample_image_parser_payload_may_contain_prefix)
{
    SampleImageParser parser(ProtocolKind::JustFloat);
    CollectingSink sink;

    Bytes data;
    appendAscii(data, "image:7,25,4,4,12\n");
    const auto payload = bytes("xxximage:yy!");
    data.insert(data.end(), payload.begin(), payload.end());
    const auto tail = justFloatFrame({9.0f});
    data.insert(data.end(), tail.begin(), tail.end());

    feed(parser, data, sink);

    VOFA_CHECK_EQ(sink.images.size(), 1u);
    VOFA_CHECK_EQ(sink.images[0].id, 7u);
    VOFA_CHECK_EQ(sink.images[0].data.size(), 12u);
    VOFA_CHECK_EQ(sink.frames.size(), 1u);
    VOFA_CHECK_EQ(sink.frames[0].samples[0], 9.0f);
}

VOFA_TEST(sample_image_parser_rejects_bad_header_and_recovers)
{
    SampleImageParser parser(ProtocolKind::FireWater);
    CollectingSink sink;

    feed(parser, bytes("image:0,99,40,30,1\na\n"), sink);
    VOFA_CHECK_EQ(sink.images.size(), 0u);
    VOFA_CHECK(parser.stats().errors >= 1u);

    feed(parser, bytes("1,2\nimage:0,27,9000,30,1\na\n3,4\n"), sink);
    VOFA_CHECK_EQ(sink.images.size(), 0u);

    Bytes valid = bytes("image:0,27,40,30,2\n");
    valid.push_back(std::byte {0x11});
    valid.push_back(std::byte {0x22});
    appendAscii(valid, "\n5,6\n");
    feed(parser, valid, sink);
    VOFA_CHECK_EQ(sink.images.size(), 1u);
    VOFA_CHECK_EQ(sink.images[0].data.size(), 2u);
}

VOFA_TEST(parser_factory_returns_expected_names)
{
    VOFA_CHECK_EQ(makeParser(ProtocolKind::RawData)->name(), std::string_view("rawdata"));
    VOFA_CHECK_EQ(makeParser(ProtocolKind::JustFloat)->name(), std::string_view("justfloat"));
    VOFA_CHECK_EQ(makeParser(ProtocolKind::FireWater)->name(), std::string_view("firewater"));
    VOFA_CHECK_EQ(makeParser(ProtocolKind::Image)->name(), std::string_view("image"));
}
