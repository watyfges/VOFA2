#pragma once

#include "protocol/ParserTypes.h"

#include <bit>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace vofa::test {

using Bytes = std::vector<std::byte>;

inline Bytes bytes(std::initializer_list<std::uint8_t> values)
{
    Bytes result;
    result.reserve(values.size());
    for (const auto value : values)
        result.push_back(std::byte {value});
    return result;
}

inline Bytes bytes(std::string_view text)
{
    Bytes result;
    result.reserve(text.size());
    for (const char c : text)
        result.push_back(std::byte {static_cast<unsigned char>(c)});
    return result;
}

inline void appendAscii(Bytes& out, std::string_view text)
{
    for (const char c : text)
        out.push_back(std::byte {static_cast<unsigned char>(c)});
}

inline void appendU32LE(Bytes& out, std::uint32_t value)
{
    out.push_back(std::byte {static_cast<std::uint8_t>(value & 0xff)});
    out.push_back(std::byte {static_cast<std::uint8_t>((value >> 8) & 0xff)});
    out.push_back(std::byte {static_cast<std::uint8_t>((value >> 16) & 0xff)});
    out.push_back(std::byte {static_cast<std::uint8_t>((value >> 24) & 0xff)});
}

inline void appendFloatLE(Bytes& out, float value)
{
    appendU32LE(out, std::bit_cast<std::uint32_t>(value));
}

inline Bytes justFloatFrame(std::initializer_list<float> values)
{
    Bytes result;
    for (const float value : values)
        appendFloatLE(result, value);
    result.push_back(std::byte {0x00});
    result.push_back(std::byte {0x00});
    result.push_back(std::byte {0x80});
    result.push_back(std::byte {0x7f});
    return result;
}

struct CollectedFrame {
    std::vector<float> samples;
    std::int64_t timestampNs = 0;
};

struct CollectedImage {
    std::uint32_t id = 0;
    protocol::ImageFormat format = protocol::ImageFormat::Unknown;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Bytes data;
};

struct CollectingSink final : protocol::IFrameSink {
    std::vector<CollectedFrame> frames;
    std::vector<CollectedImage> images;

    void onSamples(std::span<const float> samples, std::int64_t timestampNs) override
    {
        frames.push_back({std::vector<float>(samples.begin(), samples.end()), timestampNs});
    }

    void onImage(const protocol::ImageFrameView& image) override
    {
        images.push_back({image.id,
                          image.format,
                          image.width,
                          image.height,
                          Bytes(image.encoded.begin(), image.encoded.end())});
    }
};

} // namespace vofa::test
