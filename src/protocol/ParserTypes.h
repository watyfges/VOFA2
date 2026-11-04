#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace vofa::protocol {

// Matches the format enum used by the old VOFA+ image frames.
enum class ImageFormat : std::uint8_t {
    Unknown = 0,
    Bmp = 25,
    Gif = 26,
    Jpg = 27,
    Png = 28,
    Pbm = 29,
    Pgm = 30,
    Ppm = 31,
    Xbm = 32,
    Xpm = 33,
    Svg = 34,
};

[[nodiscard]] constexpr bool isKnownImageFormat(ImageFormat format) noexcept
{
    const auto value = static_cast<std::uint8_t>(format);
    return value >= 25 && value <= 34;
}

[[nodiscard]] constexpr const char* imageFormatName(ImageFormat format) noexcept
{
    switch (format) {
    case ImageFormat::Bmp: return "bmp";
    case ImageFormat::Gif: return "gif";
    case ImageFormat::Jpg: return "jpg";
    case ImageFormat::Png: return "png";
    case ImageFormat::Pbm: return "pbm";
    case ImageFormat::Pgm: return "pgm";
    case ImageFormat::Ppm: return "ppm";
    case ImageFormat::Xbm: return "xbm";
    case ImageFormat::Xpm: return "xpm";
    case ImageFormat::Svg: return "svg";
    case ImageFormat::Unknown: return "unknown";
    }
    return "unknown";
}

struct ParserStats {
    std::uint64_t bytesIn = 0;
    std::uint64_t frames = 0;
    std::uint64_t images = 0;
    std::uint64_t errors = 0;
    std::uint64_t resyncs = 0;
    std::uint64_t sanitized = 0;
};

struct ImageFrameView {
    std::uint32_t id = 0;
    ImageFormat format = ImageFormat::Unknown;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const std::byte> encoded;
};

// The sink must consume the spans before returning; the parser owns the
// storage and may reuse it on the next call.
class IFrameSink {
public:
    virtual ~IFrameSink() = default;

    virtual void onSamples(std::span<const float> samples, std::int64_t timestampNs) = 0;
    virtual void onImage(const ImageFrameView& /*image*/) {}
};

} // namespace vofa::protocol
