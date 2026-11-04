#include "test_framework.h"
#include "test_helpers.h"

#include "commands/CommandStore.h"

#include <string>
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

CommandStore makeStore()
{
    CommandStore store;
    store.addCommand(1, "kp1", formatter("kp1%.3f"));

    SendBinding binding;
    binding.bindingId = 10;
    binding.label = "kp1";
    binding.kind = ControlKind::Slider;
    binding.includeInOneClickSend = true;
    binding.commandId = 1;
    binding.order = 1;
    binding.args = {CommandArg::fromFloat(0.5)};
    binding.minValue = 0.0;
    binding.maxValue = 5.0;
    binding.stepSize = 0.01;
    store.addBinding(binding);
    return store;
}

} // namespace

VOFA_TEST(command_store_builds_one_click_plan)
{
    auto store = makeStore();
    const auto plan = store.buildOneClickPlan(100);

    VOFA_CHECK_EQ(plan.steps.size(), 1u);
    VOFA_CHECK_EQ(plan.errors.size(), 0u);
    VOFA_CHECK_EQ(toText(plan.steps[0].payload), std::string("kp10.500"));
    VOFA_CHECK_EQ(plan.steps[0].delayAfterMs, 100);
}

VOFA_TEST(command_store_updates_values_and_clamps)
{
    auto store = makeStore();
    VOFA_CHECK(store.setBindingValue(10, 2.5));
    auto plan = store.buildOneClickPlan(100);
    VOFA_CHECK_EQ(toText(plan.steps[0].payload), std::string("kp12.500"));

    VOFA_CHECK(store.setBindingValue(10, 99.0));
    plan = store.buildOneClickPlan(100);
    VOFA_CHECK_EQ(toText(plan.steps[0].payload), std::string("kp15.000"));

    VOFA_CHECK(store.setBindingMetadata(10, 0.0, 10.0, 0.1));
    VOFA_CHECK(store.setBindingValue(10, -1.0));
    plan = store.buildOneClickPlan(100);
    VOFA_CHECK_EQ(toText(plan.steps[0].payload), std::string("kp10.000"));

    VOFA_CHECK(!store.setBindingValue(999, 1.0));
}

VOFA_TEST(command_store_respects_include_flag)
{
    auto store = makeStore();
    VOFA_CHECK(store.setBindingIncluded(10, false));
    VOFA_CHECK_EQ(store.buildOneClickPlan(100).steps.size(), 0u);

    VOFA_CHECK(store.setBindingIncluded(10, true));
    VOFA_CHECK_EQ(store.buildOneClickPlan(100).steps.size(), 1u);

    VOFA_CHECK(!store.setBindingIncluded(999, false));
}

VOFA_TEST(command_store_reports_missing_template)
{
    CommandStore store;
    SendBinding binding;
    binding.bindingId = 20;
    binding.kind = ControlKind::Slider;
    binding.commandId = 404;
    binding.order = 1;
    binding.args = {CommandArg::fromFloat(1.0)};
    store.addBinding(binding);

    const auto plan = store.buildOneClickPlan(100);
    VOFA_CHECK_EQ(plan.steps.size(), 0u);
    VOFA_CHECK_EQ(plan.errors.size(), 1u);
    VOFA_CHECK_EQ(plan.errors[0].bindingId, 20u);
}
