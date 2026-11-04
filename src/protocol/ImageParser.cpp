#include "ImageParser.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>
#include <system_error>

namespace vofa::protocol {
namespace {

constexpr bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r';
}

std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

template <typename T>
bool parseUnsigned(std::string_view text, T& out) noexcept
{
    text = trim(text);
    if (text.empty())
        return false;
    T value {};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc {} || result.ptr != text.data() + text.size())
        return false;
    out = value;
    return true;
}

} // namespace

ImageParser::ImageParser(std::size_t capacity)
    : buffer_(std::max<std::size_t>(
          capacity, limits::kMaxImageHeaderBytes + 2))
{
}

void ImageParser::reset() noexcept
{
    buffer_.clear();
    stats_ = {};
}

void ImageParser::feed(std::span<const std::byte> data,
                       IFrameSink& sink,
                       std::int64_t /*timestampNs*/)
{
    stats_.bytesIn += data.size();

    while (!data.empty()) {
        const std::size_t space = buffer_.capacity() - buffer_.size();
        if (space == 0) {
            processAvailable(sink);
            if (buffer_.capacity() - buffer_.size() == 0) {
                // A single frame cannot be completed within the hard cap.
                buffer_.clear();
                ++stats_.errors;
                ++stats_.resyncs;
            }
            continue;
        }

        const std::size_t take = std::min(space, data.size());
        if (!buffer_.append(data.first(take))) {
            buffer_.clear();
            ++stats_.errors;
            ++stats_.resyncs;
        }
        data = data.subspan(take);
        processAvailable(sink);
    }
}

void ImageParser::processAvailable(IFrameSink& sink)
{
    for (;;) {
        const auto bytes = buffer_.data();
        if (bytes.empty())
            return;

        const auto prefixIt = std::search(bytes.begin(), bytes.end(), kPrefix.begin(), kPrefix.end());
        if (prefixIt == bytes.end()) {
            // Keep a possible partial prefix for the next chunk.
            if (bytes.size() > kPrefix.size() - 1) {
                buffer_.consume(bytes.size() - (kPrefix.size() - 1));
                ++stats_.resyncs;
            }
            return;
        }

        const auto prefixIndex = static_cast<std::size_t>(prefixIt - bytes.begin());
        if (prefixIndex != 0) {
            buffer_.consume(prefixIndex);
            ++stats_.resyncs;
            continue;
        }

        const auto newline = std::find(bytes.begin(), bytes.end(), std::byte {'\n'});
        if (newline == bytes.end()) {
            if (bytes.size() > limits::kMaxImageHeaderBytes) {
                buffer_.consume(1);
                ++stats_.errors;
                ++stats_.resyncs;
                continue;
            }
            return;
        }

        const auto headerSize = static_cast<std::size_t>(newline - bytes.begin());
        if (headerSize > limits::kMaxImageHeaderBytes) {
            buffer_.consume(1);
            ++stats_.errors;
            ++stats_.resyncs;
            continue;
        }

        const std::string_view header(
            reinterpret_cast<const char*>(bytes.data()), headerSize);
        Header parsed;
        if (!parseHeader(header, parsed)) {
            buffer_.consume(1);
            ++stats_.errors;
            ++stats_.resyncs;
            continue;
        }

        const std::size_t payloadStart = headerSize + 1;
        const std::size_t total = payloadStart + parsed.length;
        if (total > buffer_.capacity()) {
            buffer_.consume(1);
            ++stats_.errors;
            ++stats_.resyncs;
            continue;
        }

        if (bytes.size() < total) {
            // Wait for the rest of the payload. If the buffer is already full
            // the frame can never complete; drop it.
            if (bytes.size() >= buffer_.capacity()) {
                buffer_.clear();
                ++stats_.errors;
                ++stats_.resyncs;
            }
            return;
        }

        ImageFrameView image;
        image.id = parsed.id;
        image.format = parsed.format;
        image.width = parsed.width;
        image.height = parsed.height;
        image.encoded = bytes.subspan(payloadStart, parsed.length);
        sink.onImage(image);
        ++stats_.images;
        buffer_.consume(total);
    }
}

bool ImageParser::parseHeader(std::string_view line, Header& out) noexcept
{
    if (!line.starts_with("image:"))
        return false;
    line.remove_prefix(6);

    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);

    std::array<std::string_view, 5> fields {};
    std::size_t fieldCount = 0;
    std::size_t start = 0;
    for (;;) {
        const auto comma = line.find(',', start);
        if (fieldCount >= fields.size())
            return false;
        fields[fieldCount++] = line.substr(
            start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    if (fieldCount != fields.size())
        return false;

    std::uint32_t id = 0;
    std::uint32_t format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t length = 0;

    if (!parseUnsigned(fields[0], id)
        || !parseUnsigned(fields[1], format)
        || !parseUnsigned(fields[2], width)
        || !parseUnsigned(fields[3], height)
        || !parseUnsigned(fields[4], length)) {
        return false;
    }

    if (id > limits::kMaxImageIndex)
        return false;
    if (format < 25 || format > 34)
        return false;
    if (width == 0 || height == 0
        || width > limits::kMaxImageDimension
        || height > limits::kMaxImageDimension) {
        return false;
    }
    if (static_cast<std::uint64_t>(width) * height > limits::kMaxImagePixels)
        return false;
    if (length == 0 || length > limits::kMaxImageFrameBytes)
        return false;

    out.id = id;
    out.format = static_cast<ImageFormat>(format);
    out.width = width;
    out.height = height;
    out.length = length;
    return true;
}

const ParserStats& ImageParser::stats() const noexcept
{
    return stats_;
}

std::string_view ImageParser::name() const noexcept
{
    return "image";
}

} // namespace vofa::protocol
