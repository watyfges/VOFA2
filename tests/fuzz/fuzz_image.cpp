#include "protocol/ImageParser.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace {

struct NullSink final : vofa::protocol::IFrameSink {
    void onSamples(std::span<const float>, std::int64_t) override {}
    void onImage(const vofa::protocol::ImageFrameView&) override {}
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    thread_local vofa::protocol::ImageParser parser;
    parser.reset();
    NullSink sink;
    parser.feed(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size), sink, 0);
    return 0;
}
