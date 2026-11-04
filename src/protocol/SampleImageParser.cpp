#include "SampleImageParser.h"

#include "core/Limits.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <system_error>

namespace vofa::protocol {
namespace {

constexpr std::array<std::byte, 6> kImagePrefix {
    std::byte {'i'}, std::byte {'m'}, std::byte {'a'},
    std::byte {'g'}, std::byte {'e'}, std::byte {':'},
};

bool isSpace(char c) noexcept
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

// Length of the longest suffix of `data` that is a prefix of `pattern`.
std::size_t partialPrefixSuffix(std::span<const std::byte> data) noexcept
{
    const std::size_t maxLength = std::min(data.size(), kImagePrefix.size() - 1);
    for (std::size_t length = maxLength; length > 0; --length) {
        if (std::equal(data.end() - static_cast<std::ptrdiff_t>(length),
                       data.end(),
                       kImagePrefix.begin())) {
            return length;
        }
    }
    return 0;
}

} // namespace

SampleImageParser::SampleImageParser(ProtocolKind innerKind)
    : inner_(makeParser(innerKind == ProtocolKind::Image ? ProtocolKind::RawData : innerKind))
    , header_(limits::kMaxImageHeaderBytes + 1)
    , pending_(kImagePrefix.size())
{
}

void SampleImageParser::reset() noexcept
{
    inner_->reset();
    state_ = State::Normal;
    pending_.clear();
    resetImage();
    ownErrors_ = 0;
    ownResyncs_ = 0;
    stats_ = {};
}

void SampleImageParser::resetImage() noexcept
{
    header_.clear();
    payload_.clear();
    payload_.shrink_to_fit();
    payloadLength_ = 0;
    image_ = {};
}

void SampleImageParser::feed(std::span<const std::byte> data,
                             IFrameSink& sink,
                             std::int64_t timestampNs)
{
    stats_.bytesIn += data.size();

    while (!data.empty()) {
        switch (state_) {
        case State::Normal: {
            // Complete a prefix that was split across feed() calls first.
            if (!pending_.empty()) {
                const auto want = kImagePrefix.size() - pending_.size();
                const auto take = std::min(want, data.size());
                if (!pending_.append(data.first(take))) {
                    pending_.clear();
                    ++ownErrors_;
                    ++ownResyncs_;
                }
                data = data.subspan(take);
                if (pending_.size() < kImagePrefix.size())
                    break;

                const auto held = pending_.data();
                pending_.clear();
                if (std::equal(held.begin(), held.end(), kImagePrefix.begin())) {
                    header_.clear();
                    state_ = State::Header;
                } else {
                    inner_->feed(held, sink, timestampNs);
                }
                break;
            }

            const auto found =
                std::search(data.begin(), data.end(), kImagePrefix.begin(), kImagePrefix.end());
            if (found == data.end()) {
                // Keep a partial prefix for the next chunk so an image frame
                // split at any byte boundary is still detected.
                const auto suffix = partialPrefixSuffix(data);
                const auto body = data.first(data.size() - suffix);
                if (!body.empty())
                    inner_->feed(body, sink, timestampNs);
                if (suffix > 0)
                    (void)pending_.append(data.last(suffix));
                data = {};
                break;
            }

            const auto prefixIndex = static_cast<std::size_t>(found - data.begin());
            if (prefixIndex > 0)
                inner_->feed(data.first(prefixIndex), sink, timestampNs);
            data = data.subspan(prefixIndex + kImagePrefix.size());
            header_.clear();
            state_ = State::Header;
            break;
        }

        case State::Header: {
            const auto newline = std::find(data.begin(), data.end(), std::byte {'\n'});
            if (newline == data.end()) {
                if (!header_.append(data)) {
                    // Header longer than the hard cap: drop it and resync.
                    ++ownErrors_;
                    ++ownResyncs_;
                    header_.clear();
                    state_ = State::Normal;
                }
                data = {};
                break;
            }

            const auto lineSize = static_cast<std::size_t>(newline - data.begin());
            if (!header_.append(data.first(lineSize))) {
                ++ownErrors_;
                ++ownResyncs_;
                header_.clear();
                state_ = State::Normal;
                data = data.subspan(lineSize + 1);
                break;
            }

            ImageFrameView image;
            std::uint32_t length = 0;
            if (!parseHeaderLine(image, length)) {
                ++ownErrors_;
                ++ownResyncs_;
                header_.clear();
                state_ = State::Normal;
                data = data.subspan(lineSize + 1);
                break;
            }

            image_ = image;
            payloadLength_ = length;
            image_.encoded = {};
            payload_.clear();
            // Bounded preallocation: hostile headers cannot force a 16 MB
            // allocation up front.
            payload_.reserve(std::min<std::size_t>(payloadLength_, 1u << 20));
            header_.clear();
            state_ = State::Payload;
            data = data.subspan(lineSize + 1);
            break;
        }

        case State::Payload: {
            const auto remaining =
                static_cast<std::size_t>(payloadLength_) - payload_.size();
            const auto take = std::min(remaining, data.size());
            payload_.insert(payload_.end(), data.begin(), data.begin() + take);
            data = data.subspan(take);
            if (payload_.size() < payloadLength_)
                break;

            ImageFrameView view;
            view.id = image_.id;
            view.format = image_.format;
            view.width = image_.width;
            view.height = image_.height;
            view.encoded = std::span<const std::byte>(payload_.data(), payload_.size());
            sink.onImage(view);
            ++stats_.images;

            payload_.clear();
            if (payload_.capacity() > (1u << 20))
                payload_.shrink_to_fit();
            image_ = {};
            payloadLength_ = 0;
            state_ = State::Normal;
            break;
        }
        }
    }
}

bool SampleImageParser::parseHeaderLine(ImageFrameView& out,
                                        std::uint32_t& payloadLength) const noexcept
{
    const auto bytes = header_.data();
    if (bytes.empty())
        return false;

    std::string_view line(reinterpret_cast<const char*>(bytes.data()), bytes.size());
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
    out.encoded = {};
    payloadLength = length;
    return true;
}

const ParserStats& SampleImageParser::stats() const noexcept
{
    const auto& inner = inner_->stats();
    stats_.frames = inner.frames;
    stats_.sanitized = inner.sanitized;
    stats_.errors = inner.errors + ownErrors_;
    stats_.resyncs = inner.resyncs + ownResyncs_;
    return stats_;
}

std::unique_ptr<IStreamParser> makeSampleParser(ProtocolKind kind)
{
    switch (kind) {
    case ProtocolKind::JustFloat:
    case ProtocolKind::FireWater:
        return std::make_unique<SampleImageParser>(kind);
    case ProtocolKind::RawData:
    case ProtocolKind::Image:
        return makeParser(kind);
    }
    return makeParser(ProtocolKind::RawData);
}

} // namespace vofa::protocol
