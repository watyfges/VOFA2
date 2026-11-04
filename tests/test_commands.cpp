#include "test_framework.h"
#include "test_helpers.h"

#include "commands/TemplateFormatter.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace vofa;
using namespace vofa::commands;
using namespace vofa::test;

namespace {

std::string toText(const std::vector<std::byte>& bytes)
{
    std::string text;
    text.reserve(bytes.size());
    for (const auto byte : bytes)
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    return text;
}

std::vector<std::byte> toBytes(std::string_view text)
{
    std::vector<std::byte> bytes;
    for (const char c : text)
        bytes.push_back(std::byte {static_cast<unsigned char>(c)});
    return bytes;
}

} // namespace

VOFA_TEST(template_parses_and_renders_float)
{
    const auto payload = toBytes("kp1%.3f");
    const auto formatter = TemplateFormatter::parse(payload);
    VOFA_CHECK(formatter.has_value());
    VOFA_CHECK(!formatter->isRaw());
    VOFA_CHECK_EQ(formatter->slotCount(), 1u);

    std::vector<std::byte> out;
    const std::array<CommandArg, 1> args {CommandArg::fromFloat(0.75)};
    VOFA_CHECK(formatter->render(args, out));
    VOFA_CHECK_EQ(toText(out), std::string("kp10.750"));
}

VOFA_TEST(template_default_float_precision_is_six)
{
    const auto formatter = TemplateFormatter::parse(toBytes("speed%f"));
    VOFA_CHECK(formatter.has_value());

    std::vector<std::byte> out;
    const std::array<CommandArg, 1> args {CommandArg::fromFloat(12.5)};
    VOFA_CHECK(formatter->render(args, out));
    VOFA_CHECK_EQ(toText(out), std::string("speed12.500000"));
}

VOFA_TEST(template_renders_int_string_and_percent)
{
    const auto formatter = TemplateFormatter::parse(toBytes("%d,%s,%.2f,%%"));
    VOFA_CHECK(formatter.has_value());
    VOFA_CHECK_EQ(formatter->slotCount(), 3u);

    std::vector<std::byte> out;
    const std::array<CommandArg, 3> args {
        CommandArg::fromInt(-42),
        CommandArg::fromString("abc"),
        CommandArg::fromFloat(2.5),
    };
    VOFA_CHECK(formatter->render(args, out));
    VOFA_CHECK_EQ(toText(out), std::string("-42,abc,2.50,%"));
}

VOFA_TEST(template_precision_zero)
{
    const auto formatter = TemplateFormatter::parse(toBytes("%.0f"));
    VOFA_CHECK(formatter.has_value());

    std::vector<std::byte> out;
    const std::array<CommandArg, 1> args {CommandArg::fromFloat(2.7)};
    VOFA_CHECK(formatter->render(args, out));
    VOFA_CHECK_EQ(toText(out), std::string("3"));
}

VOFA_TEST(template_allows_trailing_newline)
{
    const auto formatter = TemplateFormatter::parse(toBytes("skp%.6f\n"));
    VOFA_CHECK(formatter.has_value());
    VOFA_CHECK(!formatter->isRaw());
    VOFA_CHECK_EQ(formatter->slotCount(), 1u);

    std::vector<std::byte> out;
    const std::array<CommandArg, 1> args {CommandArg::fromFloat(1.5)};
    VOFA_CHECK(formatter->render(args, out));
    VOFA_CHECK_EQ(toText(out), std::string("skp1.500000\n"));
}

VOFA_TEST(template_raw_command_ignores_args)
{
    const auto formatter = TemplateFormatter::parse(toBytes("start"));
    VOFA_CHECK(formatter.has_value());
    VOFA_CHECK(formatter->isRaw());
    VOFA_CHECK_EQ(formatter->slotCount(), 0u);

    std::vector<std::byte> out;
    VOFA_CHECK(formatter->render({}, out));
    VOFA_CHECK_EQ(toText(out), std::string("start"));
}

VOFA_TEST(template_rejects_invalid_formats)
{
    VOFA_CHECK(!TemplateFormatter::parse(toBytes("%n")).has_value());
    VOFA_CHECK(!TemplateFormatter::parse(toBytes("%*f")).has_value());
    VOFA_CHECK(!TemplateFormatter::parse(toBytes("%.31f")).has_value());
    VOFA_CHECK(!TemplateFormatter::parse(toBytes("%.f")).has_value());
    VOFA_CHECK(!TemplateFormatter::parse(toBytes("abc%")).has_value());
    VOFA_CHECK(!TemplateFormatter::parse(toBytes("%x")).has_value());
}

VOFA_TEST(template_render_fails_cleanly_on_missing_or_wrong_args)
{
    const auto formatter = TemplateFormatter::parse(toBytes("%d"));
    VOFA_CHECK(formatter.has_value());

    std::vector<std::byte> out = toBytes("prefix:");
    const auto original = out;
    VOFA_CHECK(!formatter->render({}, out));
    VOFA_CHECK(out == original);

    const std::array<CommandArg, 1> wrong {CommandArg::fromString("x")};
    VOFA_CHECK(!formatter->render(wrong, out));
    VOFA_CHECK(out == original);

    const std::array<CommandArg, 2> tooMany {
        CommandArg::fromInt(1),
        CommandArg::fromInt(2),
    };
    VOFA_CHECK(!formatter->render(tooMany, out));
    VOFA_CHECK(out == original);
}

VOFA_TEST(template_with_binary_bytes_is_raw)
{
    std::vector<std::byte> payload {std::byte {0x01}, std::byte {'%'}, std::byte {'d'}};
    const auto formatter = TemplateFormatter::parse(payload);
    VOFA_CHECK(formatter.has_value());
    VOFA_CHECK(formatter->isRaw());

    std::vector<std::byte> out;
    VOFA_CHECK(formatter->render({}, out));
    VOFA_CHECK(out == payload);
}
