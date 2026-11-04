#pragma once

#include "IStreamParser.h"

namespace vofa::protocol {

// RawData performs no sample parsing. The transport still forwards the bytes
// to the terminal/text path.
class RawParser final : public IStreamParser {
public:
    void reset() noexcept override;
    void feed(std::span<const std::byte> data,
              IFrameSink& sink,
              std::int64_t timestampNs) override;
    [[nodiscard]] const ParserStats& stats() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;

private:
    ParserStats stats_;
};

} // namespace vofa::protocol
