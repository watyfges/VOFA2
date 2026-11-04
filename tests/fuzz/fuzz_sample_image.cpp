#include "protocol/SampleImageParser.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace {

struct NullSink final : vofa::protocol::IFrameSink {
    void onSamples(std::span<const float>, std::int64_t) override {}
    void onImage(const vofa::protocol::ImageFrameView&) override {}
};

void run(std::span<const std::byte> data, std::size_t split)
{
    thread_local vofa::protocol::SampleImageParser parser(
        vofa::protocol::ProtocolKind::JustFloat);
    parser.reset();
    NullSink sink;
    parser.feed(data.first(split), sink, 0);
    parser.feed(data.subspan(split), sink, 0);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size == 0)
        return 0;

    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    const std::span<const std::byte> input(bytes, size);

    // Exercise both single-shot parsing and a split at an arbitrary offset,
    // which covers the header/payload state machine across chunk boundaries.
    run(input, 0);
    run(input, static_cast<std::size_t>(data[0]) % (size + 1));

    if (size <= 128) {
        run(input, size / 2);
        for (std::size_t i = 0; i < size; ++i)
            run(input.subspan(i), 0);
    }
    return 0;
}
