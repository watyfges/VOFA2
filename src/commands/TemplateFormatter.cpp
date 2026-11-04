#include "TemplateFormatter.h"

#include <array>
#include <charconv>
#include <system_error>

namespace vofa::commands {
namespace {

constexpr int kMaxPrecision = 30;

bool isTemplateChar(std::byte value) noexcept
{
    const auto c = static_cast<unsigned char>(value);
    // Printable ASCII plus the whitespace controls commonly used in legacy
    // command templates (e.g. "skp%.6f\n").
    return (c >= 0x20 && c <= 0x7e) || c == '\n' || c == '\r' || c == '\t';
}

void appendBytes(std::vector<std::byte>& out, std::string_view text)
{
    for (const char c : text)
        out.push_back(std::byte {static_cast<unsigned char>(c)});
}

bool appendFloat(std::vector<std::byte>& out, double value, int precision)
{
    std::array<char, 512> buffer {};
    const auto result = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value,
        std::chars_format::fixed, precision);
    if (result.ec != std::errc {})
        return false;
    for (const char* p = buffer.data(); p != result.ptr; ++p)
        out.push_back(std::byte {static_cast<unsigned char>(*p)});
    return true;
}

bool appendInt(std::vector<std::byte>& out, std::int64_t value)
{
    std::array<char, 32> buffer {};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc {})
        return false;
    for (const char* p = buffer.data(); p != result.ptr; ++p)
        out.push_back(std::byte {static_cast<unsigned char>(*p)});
    return true;
}

} // namespace

std::optional<TemplateFormatter> TemplateFormatter::parse(std::span<const std::byte> payload)
{
    TemplateFormatter formatter;
    formatter.rawBytes_.assign(payload.begin(), payload.end());
    formatter.raw_ = true;
    formatter.valid_ = false;

    bool hasFormat = false;
    for (const auto byte : payload) {
        if (byte == std::byte {'%'}) {
            hasFormat = true;
            break;
        }
    }

    if (!hasFormat) {
        // A raw command is valid as-is. Reject NULs only if they would make the
        // command impossible to display/edit; raw binary commands are allowed.
        formatter.raw_ = true;
        formatter.valid_ = true;
        return formatter;
    }

    for (const auto byte : payload) {
        if (!isTemplateChar(byte)) {
            // Formats are text. Non-printable bytes in a format template are
            // treated as a raw binary command instead.
            formatter.raw_ = true;
            formatter.valid_ = true;
            return formatter;
        }
    }

    formatter.raw_ = false;
    formatter.rawBytes_.clear();
    formatter.slotCount_ = 0;

    std::string literal;
    for (std::size_t i = 0; i < payload.size(); ++i) {
        const char c = static_cast<char>(std::to_integer<unsigned char>(payload[i]));
        if (c != '%') {
            literal.push_back(c);
            continue;
        }

        if (i + 1 >= payload.size())
            return std::nullopt;

        const char next = static_cast<char>(std::to_integer<unsigned char>(payload[i + 1]));
        if (next == '%') {
            literal.push_back('%');
            ++i;
            continue;
        }

        int precision = 6;
        char kind = next;
        if (next == '.') {
            // %.Nf
            std::size_t j = i + 2;
            int digits = 0;
            bool hasDigit = false;
            while (j < payload.size()) {
                const char d = static_cast<char>(std::to_integer<unsigned char>(payload[j]));
                if (d < '0' || d > '9')
                    break;
                hasDigit = true;
                digits = digits * 10 + (d - '0');
                if (digits > kMaxPrecision)
                    return std::nullopt;
                ++j;
            }
            if (!hasDigit || j >= payload.size())
                return std::nullopt;
            kind = static_cast<char>(std::to_integer<unsigned char>(payload[j]));
            if (kind != 'f')
                return std::nullopt;
            precision = digits;
            i = j;
        } else {
            if (kind != 'f' && kind != 'd' && kind != 's')
                return std::nullopt;
            ++i;
        }

        if (!literal.empty()) {
            formatter.segments_.push_back({false, 'f', 0, std::move(literal)});
            literal.clear();
        }
        formatter.segments_.push_back({true, kind, precision, {}});
        ++formatter.slotCount_;
    }

    if (!literal.empty())
        formatter.segments_.push_back({false, 'f', 0, std::move(literal)});

    formatter.valid_ = true;
    return formatter;
}

bool TemplateFormatter::render(std::span<const CommandArg> args,
                               std::vector<std::byte>& out) const
{
    if (!valid_)
        return false;

    if (raw_) {
        out.insert(out.end(), rawBytes_.begin(), rawBytes_.end());
        return true;
    }

    const auto originalSize = out.size();
    std::size_t argIndex = 0;
    for (const auto& segment : segments_) {
        if (!segment.isSlot) {
            appendBytes(out, segment.literal);
            continue;
        }

        if (argIndex >= args.size()) {
            out.resize(originalSize);
            return false;
        }

        const auto& arg = args[argIndex++];
        switch (segment.kind) {
        case 'f': {
            double value = 0.0;
            if (arg.type == CommandArg::Type::Float)
                value = arg.floatValue;
            else if (arg.type == CommandArg::Type::Int)
                value = static_cast<double>(arg.intValue);
            else {
                out.resize(originalSize);
                return false;
            }
            if (!appendFloat(out, value, segment.precision)) {
                out.resize(originalSize);
                return false;
            }
            break;
        }
        case 'd': {
            std::int64_t value = 0;
            if (arg.type == CommandArg::Type::Int)
                value = arg.intValue;
            else if (arg.type == CommandArg::Type::Float)
                value = static_cast<std::int64_t>(arg.floatValue);
            else {
                out.resize(originalSize);
                return false;
            }
            if (!appendInt(out, value)) {
                out.resize(originalSize);
                return false;
            }
            break;
        }
        case 's':
            if (arg.type != CommandArg::Type::String) {
                out.resize(originalSize);
                return false;
            }
            appendBytes(out, arg.stringValue);
            break;
        default:
            out.resize(originalSize);
            return false;
        }
    }

    if (argIndex != args.size()) {
        out.resize(originalSize);
        return false;
    }
    return true;
}

} // namespace vofa::commands
