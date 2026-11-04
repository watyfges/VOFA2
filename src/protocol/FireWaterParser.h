#pragma once

#include "IStreamParser.h"
#include "core/FixedBuffer.h"
#include "core/Limits.h"

#include <array>
#include <cstddef>

namespace vofa::protocol {

// FireWater: newline-delimited ASCII CSV, optionally prefixed by "<name>:".
// Example: "plot0:1.0,2.5,-3.0\n".
class FireWaterParser final : public IStreamParser {
public:
    explicit FireWaterParser(std::size_t capacity = limits::kMaxTextLineBytes);

    void reset() noexcept override;
    void feed(std::span<const std::byte> data,
              IFrameSink& sink,
              std::int64_t timestampNs) override;
    [[nodiscard]] const ParserStats& stats() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;

private:
    void processLine(IFrameSink& sink, std::int64_t timestampNs);

    core::FixedBuffer line_;
    ParserStats stats_;
    std::array<float, limits::kMaxChannels> values_{};
    bool discardingLine_ = false;
};

} // namespace vofa::protocol
