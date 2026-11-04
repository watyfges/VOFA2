#pragma once

#include "IStreamParser.h"
#include "core/FixedBuffer.h"
#include "core/Limits.h"

#include <array>
#include <cstddef>

namespace vofa::protocol {

// VOFA+ image frame:
//   image:<id>,<format>,<width>,<height>,<length>\n<payload bytes>
// The payload is an encoded image (JPEG/PNG/BMP/GIF/...); it is passed to the
// sink without decoding. Dimensions and length are validated before the frame
// is exposed.
class ImageParser final : public IStreamParser {
public:
    explicit ImageParser(
        std::size_t capacity = limits::kMaxImageFrameBytes + limits::kMaxImageHeaderBytes + 2);

    void reset() noexcept override;
    void feed(std::span<const std::byte> data,
              IFrameSink& sink,
              std::int64_t timestampNs) override;
    [[nodiscard]] const ParserStats& stats() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;

private:
    struct Header {
        std::uint32_t id = 0;
        ImageFormat format = ImageFormat::Unknown;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t length = 0;
    };

    void processAvailable(IFrameSink& sink);
    [[nodiscard]] static bool parseHeader(std::string_view line, Header& out) noexcept;

    core::FixedBuffer buffer_;
    ParserStats stats_;

    static constexpr std::array<std::byte, 6> kPrefix {
        std::byte {'i'}, std::byte {'m'}, std::byte {'a'},
        std::byte {'g'}, std::byte {'e'}, std::byte {':'}
    };
};

} // namespace vofa::protocol
