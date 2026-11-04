#include "FireWaterParser.h"
#include "core/Sanitize.h"

#include <algorithm>
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

} // namespace

FireWaterParser::FireWaterParser(std::size_t capacity)
    : line_(std::max<std::size_t>(capacity, 16))
{
}

void FireWaterParser::reset() noexcept
{
    line_.clear();
    stats_ = {};
    discardingLine_ = false;
}

void FireWaterParser::feed(std::span<const std::byte> data,
                           IFrameSink& sink,
                           std::int64_t timestampNs)
{
    stats_.bytesIn += data.size();

    while (!data.empty()) {
        const auto newline = std::find(data.begin(), data.end(), std::byte {'\n'});
        const bool hasNewline = newline != data.end();
        const std::size_t segmentSize = hasNewline
            ? static_cast<std::size_t>(newline - data.begin())
            : data.size();

        if (discardingLine_) {
            if (!hasNewline) {
                data = {};
                continue;
            }
            discardingLine_ = false;
            data = data.subspan(segmentSize + 1);
            continue;
        }

        if (segmentSize > 0 && !line_.append(data.first(segmentSize))) {
            // The line exceeds the hard limit. Drop it and skip to the next
            // newline without allocating.
            line_.clear();
            discardingLine_ = true;
            ++stats_.errors;
            ++stats_.resyncs;
            if (!hasNewline) {
                data = {};
                continue;
            }
            discardingLine_ = false;
            data = data.subspan(segmentSize + 1);
            continue;
        }

        if (hasNewline) {
            processLine(sink, timestampNs);
            line_.clear();
            data = data.subspan(segmentSize + 1);
        } else {
            data = {};
        }
    }
}

void FireWaterParser::processLine(IFrameSink& sink, std::int64_t timestampNs)
{
    const auto bytes = line_.data();
    if (bytes.empty())
        return;

    std::string_view line(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.empty())
        return;

    // Image frames are handled by ImageParser; ignore them here.
    if (line.starts_with("image:"))
        return;

    const auto firstComma = line.find(',');
    if (firstComma == std::string_view::npos) {
        ++stats_.errors;
        return;
    }

    const auto colon = line.find(':');
    if (colon != std::string_view::npos && colon < firstComma)
        line.remove_prefix(colon + 1);

    std::size_t count = 0;
    std::size_t start = 0;
    for (;;) {
        const auto comma = line.find(',', start);
        const auto token = trim(line.substr(start,
            comma == std::string_view::npos ? std::string_view::npos : comma - start));

        if (token.empty() || count >= limits::kMaxChannels) {
            ++stats_.errors;
            return;
        }

        float value = 0.0f;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec != std::errc {} || result.ptr != token.data() + token.size()) {
            ++stats_.errors;
            return;
        }

        const float clean = core::sanitizeSample(value);
        if (clean != value)
            ++stats_.sanitized;
        values_[count++] = clean;

        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }

    if (count == 0) {
        ++stats_.errors;
        return;
    }

    sink.onSamples(std::span<const float>(values_.data(), count), timestampNs);
    ++stats_.frames;
}

const ParserStats& FireWaterParser::stats() const noexcept
{
    return stats_;
}

std::string_view FireWaterParser::name() const noexcept
{
    return "firewater";
}

} // namespace vofa::protocol
