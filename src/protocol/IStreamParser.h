#pragma once

#include "ParserTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace vofa::protocol {

// Every parser must be a total function over arbitrary byte streams: it may
// resynchronise and report errors, but it must never throw, read out of bounds
// or allocate without bound. The transport may recreate a parser after an
// error without restarting the connection.
class IStreamParser {
public:
    virtual ~IStreamParser() = default;

    virtual void reset() noexcept = 0;

    virtual void feed(std::span<const std::byte> data,
                      IFrameSink& sink,
                      std::int64_t timestampNs) = 0;

    [[nodiscard]] virtual const ParserStats& stats() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace vofa::protocol
