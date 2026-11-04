#pragma once

#include "IStreamParser.h"
#include "core/FixedBuffer.h"
#include "core/Limits.h"

#include <array>
#include <cstddef>

namespace vofa::protocol {

// JustFloat: little-endian float array terminated by 00 00 80 7F.
class JustFloatParser final : public IStreamParser {
public:
    explicit JustFloatParser(
        std::size_t capacity = limits::kMaxSampleFrameBytes + limits::kDefaultParserCapacity);

    void reset() noexcept override;
    void feed(std::span<const std::byte> data,
              IFrameSink& sink,
              std::int64_t timestampNs) override;
    [[nodiscard]] const ParserStats& stats() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;

private:
    void processAvailable(IFrameSink& sink, std::int64_t timestampNs);

    core::FixedBuffer buffer_;
    ParserStats stats_;
    std::array<float, limits::kMaxChannels> samples_{};

    static constexpr std::array<std::byte, 4> kTail {
        std::byte {0x00}, std::byte {0x00}, std::byte {0x80}, std::byte {0x7f}
    };
};

} // namespace vofa::protocol
