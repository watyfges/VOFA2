#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vofa::commands {

// A single command argument. The legacy VOFA+ command editor supports float,
// integer and string arguments; hex bytes can be represented as a String of
// raw bytes or as an Int.
struct CommandArg {
    enum class Type {
        Float,
        Int,
        String,
    };

    Type type = Type::Float;
    double floatValue = 0.0;
    std::int64_t intValue = 0;
    std::string_view stringValue;

    static CommandArg fromFloat(double value) { return {Type::Float, value, 0, {}}; }
    static CommandArg fromInt(std::int64_t value) { return {Type::Int, 0.0, value, {}}; }
    static CommandArg fromString(std::string_view value) { return {Type::String, 0.0, 0, value}; }
};

// Parses the legacy command template format:
//   kp1%.3f, speed%f, %d, %s, %%
// The formatter never evaluates a runtime printf format string. It parses the
// template once into literal/argument segments and renders it with checked
// argument access.
class TemplateFormatter {
public:
    static std::optional<TemplateFormatter> parse(std::span<const std::byte> payload);

    // Renders the command into out (appending). Returns false if an argument is
    // missing or has the wrong type. The output is not modified on failure.
    [[nodiscard]] bool render(std::span<const CommandArg> args,
                              std::vector<std::byte>& out) const;

    [[nodiscard]] bool isRaw() const noexcept { return raw_; }
    [[nodiscard]] std::size_t slotCount() const noexcept { return slotCount_; }
    [[nodiscard]] std::span<const std::byte> rawBytes() const noexcept
    {
        return std::span<const std::byte>(rawBytes_.data(), rawBytes_.size());
    }
    [[nodiscard]] bool valid() const noexcept { return valid_; }

private:
    struct Segment {
        bool isSlot = false;
        char kind = 'f'; // f, d, s
        int precision = 6;
        std::string literal;
    };

    std::vector<Segment> segments_;
    std::vector<std::byte> rawBytes_;
    std::size_t slotCount_ = 0;
    bool raw_ = true;
    bool valid_ = false;
};

} // namespace vofa::commands
