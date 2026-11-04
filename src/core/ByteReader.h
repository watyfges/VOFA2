#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

namespace vofa::core {

// Bounds-checked reader for untrusted byte streams. Every accessor either
// succeeds or returns std::nullopt; the reader never reads past its span.
class ByteReader {
public:
    explicit constexpr ByteReader(std::span<const std::byte> bytes) noexcept
        : bytes_(bytes)
    {
    }

    [[nodiscard]] constexpr std::size_t remaining() const noexcept
    {
        return bytes_.size() - position_;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return position_ >= bytes_.size();
    }

    [[nodiscard]] constexpr std::size_t position() const noexcept
    {
        return position_;
    }

    [[nodiscard]] constexpr std::span<const std::byte> remainingBytes() const noexcept
    {
        return bytes_.subspan(position_);
    }

    [[nodiscard]] std::optional<std::uint8_t> readU8() noexcept
    {
        if (remaining() < 1)
            return std::nullopt;
        return static_cast<std::uint8_t>(bytes_[position_++]);
    }

    [[nodiscard]] std::optional<std::uint16_t> readU16LE() noexcept
    {
        if (remaining() < 2)
            return std::nullopt;
        const auto* p = reinterpret_cast<const std::uint8_t*>(bytes_.data() + position_);
        position_ += 2;
        return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
    }

    [[nodiscard]] std::optional<std::uint32_t> readU32LE() noexcept
    {
        if (remaining() < 4)
            return std::nullopt;
        const auto* p = reinterpret_cast<const std::uint8_t*>(bytes_.data() + position_);
        position_ += 4;
        return static_cast<std::uint32_t>(p[0])
            | (static_cast<std::uint32_t>(p[1]) << 8)
            | (static_cast<std::uint32_t>(p[2]) << 16)
            | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    [[nodiscard]] std::optional<std::int32_t> readI32LE() noexcept
    {
        const auto value = readU32LE();
        if (!value)
            return std::nullopt;
        return static_cast<std::int32_t>(*value);
    }

    [[nodiscard]] std::optional<float> readF32LE() noexcept
    {
        const auto value = readU32LE();
        if (!value)
            return std::nullopt;
        return std::bit_cast<float>(*value);
    }

    [[nodiscard]] std::optional<std::span<const std::byte>> readBytes(std::size_t count) noexcept
    {
        if (remaining() < count)
            return std::nullopt;
        const auto result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }

    [[nodiscard]] std::span<const std::byte> peek(std::size_t count) const noexcept
    {
        if (remaining() < count)
            return {};
        return bytes_.subspan(position_, count);
    }

    [[nodiscard]] bool skip(std::size_t count) noexcept
    {
        if (remaining() < count)
            return false;
        position_ += count;
        return true;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

} // namespace vofa::core
