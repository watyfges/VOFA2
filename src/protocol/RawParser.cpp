#include "RawParser.h"

namespace vofa::protocol {

void RawParser::reset() noexcept
{
    stats_ = {};
}

void RawParser::feed(std::span<const std::byte> data, IFrameSink& /*sink*/, std::int64_t /*timestampNs*/)
{
    stats_.bytesIn += data.size();
}

const ParserStats& RawParser::stats() const noexcept
{
    return stats_;
}

std::string_view RawParser::name() const noexcept
{
    return "rawdata";
}

} // namespace vofa::protocol
