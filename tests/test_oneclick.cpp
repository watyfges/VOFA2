#include "test_framework.h"
#include "test_helpers.h"

#include "commands/OneClickSend.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace vofa::commands;
using namespace vofa::test;

namespace {

std::vector<std::byte> toBytes(std::string_view text)
{
    std::vector<std::byte> bytes;
    for (const char c : text)
        bytes.push_back(std::byte {static_cast<unsigned char>(c)});
    return bytes;
}

std::string toText(const std::vector<std::byte>& bytes)
{
    std::string text;
    for (const auto byte : bytes)
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    return text;
}

TemplateFormatter formatter(std::string_view text)
{
    auto parsed = TemplateFormatter::parse(toBytes(text));
    VOFA_CHECK(parsed.has_value());
    return std::move(*parsed);
}

SendBinding slider(std::uint64_t id,
                   std::string label,
                   std::uint64_t commandId,
                   double value,
                   int order,
                   bool include = true)
{
    SendBinding binding;
    binding.bindingId = id;
    binding.label = std::move(label);
    binding.kind = ControlKind::Slider;
    binding.includeInOneClickSend = include;
    binding.commandId = commandId;
    binding.order = order;
    binding.args.push_back(CommandArg::fromFloat(value));
    return binding;
}

} // namespace

VOFA_TEST(one_click_sends_only_selected_sliders)
{
    const std::vector<CommandTemplateEntry> commands {
        {1, formatter("kp1%.3f")},
        {2, formatter("kp2%.3f")},
        {3, formatter("start")},
        {4, formatter("stop")},
    };

    std::vector<SendBinding> bindings {
        slider(100, "kp1", 1, 0.5, 1),
        slider(101, "kp2", 2, 1.5, 2),
        slider(102, "disabled", 2, 9.0, 0, false),
        {103, "start", ControlKind::Toggle, true, 3, 0, {}},
        {104, "stop", ControlKind::Button, true, 4, 3, {}},
    };

    const auto plan = OneClickSendPlanner::build(bindings, commands, 100);
    VOFA_CHECK_EQ(plan.steps.size(), 2u);
    VOFA_CHECK_EQ(plan.errors.size(), 0u);
    VOFA_CHECK_EQ(plan.intervalMs, 100);
    VOFA_CHECK_EQ(toText(plan.steps[0].payload), std::string("kp10.500"));
    VOFA_CHECK_EQ(toText(plan.steps[1].payload), std::string("kp21.500"));
    VOFA_CHECK_EQ(plan.steps[0].delayAfterMs, 100);
    VOFA_CHECK_EQ(plan.steps[1].delayAfterMs, 100);
}

VOFA_TEST(one_click_filters_by_command_group)
{
    const std::vector<CommandTemplateEntry> commands {
        {1, formatter("a%.1f")},
        {2, formatter("b%.1f")},
        {3, formatter("c%.1f")},
    };

    auto first = slider(400, "a", 1, 1.0, 1);
    first.group = "ProjectA";
    auto second = slider(401, "b", 2, 2.0, 2);
    second.group = "ProjectB";
    auto third = slider(402, "c", 3, 3.0, 3);
    third.group = "ProjectA";

    const std::vector<SendBinding> bindings {first, second, third};

    const auto planA =
        OneClickSendPlanner::build(bindings, commands, 100, "ProjectA");
    VOFA_CHECK_EQ(planA.steps.size(), 2u);
    VOFA_CHECK_EQ(toText(planA.steps[0].payload), std::string("a1.0"));
    VOFA_CHECK_EQ(toText(planA.steps[1].payload), std::string("c3.0"));

    const auto planB =
        OneClickSendPlanner::build(bindings, commands, 100, "ProjectB");
    VOFA_CHECK_EQ(planB.steps.size(), 1u);
    VOFA_CHECK_EQ(planB.steps[0].bindingId, 401u);

    // An empty group keeps the previous "all groups" behavior.
    const auto all = OneClickSendPlanner::build(bindings, commands, 100);
    VOFA_CHECK_EQ(all.steps.size(), 3u);
}

VOFA_TEST(one_click_includes_dials_but_not_buttons)
{
    const std::vector<CommandTemplateEntry> commands {
        {1, formatter("%.2f")},
    };
    std::vector<SendBinding> bindings {
        {200, "dial", ControlKind::Dial, true, 1, 1, {CommandArg::fromFloat(2.5)}},
        {201, "toggle", ControlKind::Toggle, true, 1, 2, {CommandArg::fromFloat(1.0)}},
        {202, "button", ControlKind::Button, true, 1, 3, {CommandArg::fromFloat(1.0)}},
    };

    const auto plan = OneClickSendPlanner::build(bindings, commands, 100);
    VOFA_CHECK_EQ(plan.steps.size(), 1u);
    VOFA_CHECK_EQ(plan.steps[0].bindingId, 200u);
}

VOFA_TEST(one_click_coalesces_duplicate_commands_to_last_value)
{
    const std::vector<CommandTemplateEntry> commands {
        {1, formatter("kp1%.3f")},
    };
    std::vector<SendBinding> bindings {
        slider(300, "first", 1, 1.0, 1),
        slider(301, "second", 1, 2.0, 2),
    };

    const auto plan = OneClickSendPlanner::build(bindings, commands, 100);
    VOFA_CHECK_EQ(plan.steps.size(), 1u);
    VOFA_CHECK_EQ(plan.steps[0].bindingId, 301u);
    VOFA_CHECK_EQ(plan.steps[0].label, std::string("second"));
    VOFA_CHECK_EQ(toText(plan.steps[0].payload), std::string("kp12.000"));
}

VOFA_TEST(one_click_clamps_interval)
{
    const std::vector<CommandTemplateEntry> commands {
        {1, formatter("%.1f")},
    };
    const std::vector<SendBinding> bindings {
        slider(400, "a", 1, 1.0, 1),
    };

    VOFA_CHECK_EQ(OneClickSendPlanner::build(bindings, commands, -5).intervalMs, 0);
    VOFA_CHECK_EQ(OneClickSendPlanner::build(bindings, commands, 999999).intervalMs, 60000);
}

VOFA_TEST(one_click_reports_missing_template_and_bad_args)
{
    const std::vector<CommandTemplateEntry> commands {
        {1, formatter("kp1%.3f")},
    };
    std::vector<SendBinding> bindings {
        slider(500, "ok", 1, 0.5, 1),
        slider(501, "missing", 999, 0.5, 2),
        {502, "bad args", ControlKind::Slider, true, 1, 3, {}},
    };

    const auto plan = OneClickSendPlanner::build(bindings, commands, 100);
    VOFA_CHECK_EQ(plan.steps.size(), 1u);
    VOFA_CHECK_EQ(plan.errors.size(), 2u);
    VOFA_CHECK_EQ(plan.errors[0].bindingId, 501u);
    VOFA_CHECK_EQ(plan.errors[1].bindingId, 502u);
}

VOFA_TEST(one_click_sorts_by_ui_order)
{
    const std::vector<CommandTemplateEntry> commands {
        {1, formatter("%.1f")},
        {2, formatter("%.1f")},
        {3, formatter("%.1f")},
    };
    std::vector<SendBinding> bindings {
        slider(600, "third", 3, 3.0, 30),
        slider(601, "first", 1, 1.0, 10),
        slider(602, "second", 2, 2.0, 20),
    };

    const auto plan = OneClickSendPlanner::build(bindings, commands, 100);
    VOFA_CHECK_EQ(plan.steps.size(), 3u);
    VOFA_CHECK_EQ(plan.steps[0].label, std::string("first"));
    VOFA_CHECK_EQ(plan.steps[1].label, std::string("second"));
    VOFA_CHECK_EQ(plan.steps[2].label, std::string("third"));
}
