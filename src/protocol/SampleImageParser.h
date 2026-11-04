#pragma once

#include "IStreamParser.h"
#include "ParserFactory.h"
#include "core/FixedBuffer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace vofa::protocol {

// Wraps a sample parser (JustFloat/FireWater) and transparently extracts
// `image:<id>,<format>,<w>,<h>,<len>\n<payload>` frames from the stream.
//
// The wire format is un-framed: image frames can appear between sample frames.
// We scan for the "image:" prefix, feed everything before it to the inner
// parser, then consume the header/payload ourselves. Header and payload sizes
// are bounded, invalid headers are discarded and the stream is rescanned.
class SampleImageParser final : public IStreamParser {
public:
    explicit SampleImageParser(ProtocolKind innerKind);

    void reset() noexcept override;
    void feed(std::span<const std::byte> data,
              IFrameSink& sink,
              std::int64_t timestampNs) override;

    [[nodiscard]] const ParserStats& stats() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override { return "sample+image"; }

private:
    enum class State {
        Normal,
        Header,
        Payload,
    };

    [[nodiscard]] bool parseHeaderLine(ImageFrameView& out,
                                       std::uint32_t& payloadLength) const noexcept;
    void resetImage() noexcept;

    std::unique_ptr<IStreamParser> inner_;
    core::FixedBuffer header_;
    core::FixedBuffer pending_; // possible split "image:" prefix
    std::vector<std::byte> payload_;
    State state_ = State::Normal;
    ImageFrameView image_;
    std::uint32_t payloadLength_ = 0;
    std::uint64_t ownErrors_ = 0;
    std::uint64_t ownResyncs_ = 0;
    mutable ParserStats stats_;
};

// Factory used by the transport: JustFloat/FireWater also accept image frames.
[[nodiscard]] std::unique_ptr<IStreamParser> makeSampleParser(ProtocolKind kind);

} // namespace vofa::protocol
