#include "OneClickSend.h"

#include <algorithm>
#include <unordered_map>

namespace vofa::commands {
namespace {

constexpr int kMinIntervalMs = 0;
constexpr int kMaxIntervalMs = 60000;

const TemplateFormatter* findFormatter(std::span<const CommandTemplateEntry> commands,
                                       std::uint64_t commandId)
{
    for (const auto& entry : commands) {
        if (entry.commandId == commandId)
            return &entry.formatter;
    }
    return nullptr;
}

} // namespace

OneClickSendPlan OneClickSendPlanner::build(std::span<const SendBinding> bindings,
                                            std::span<const CommandTemplateEntry> commands,
                                            int intervalMs,
                                            std::string_view group)
{
    OneClickSendPlan plan;
    plan.intervalMs = std::clamp(intervalMs, kMinIntervalMs, kMaxIntervalMs);

    std::vector<const SendBinding*> selected;
    selected.reserve(bindings.size());
    for (const auto& binding : bindings) {
        if (!isValueControl(binding.kind))
            continue;
        if (!binding.includeInOneClickSend)
            continue;
        if (!group.empty() && binding.group != group)
            continue;
        selected.push_back(&binding);
    }

    std::sort(selected.begin(), selected.end(), [](const SendBinding* a, const SendBinding* b) {
        if (a->order != b->order)
            return a->order < b->order;
        return a->bindingId < b->bindingId;
    });

    // Coalesce duplicate command ids: the last binding in UI order wins.
    std::unordered_map<std::uint64_t, SendStep> byCommand;
    byCommand.reserve(selected.size());
    for (const auto* binding : selected) {
        const auto* formatter = findFormatter(commands, binding->commandId);
        if (formatter == nullptr) {
            plan.errors.push_back({binding->bindingId, "command template not found"});
            continue;
        }

        SendStep step;
        step.bindingId = binding->bindingId;
        step.commandId = binding->commandId;
        step.label = binding->label;
        step.delayAfterMs = plan.intervalMs;
        step.order = binding->order;

        if (!formatter->render(binding->args, step.payload)) {
            plan.errors.push_back({binding->bindingId, "argument count/type mismatch"});
            continue;
        }

        byCommand[binding->commandId] = std::move(step);
    }

    plan.steps.reserve(byCommand.size());
    for (auto& [commandId, step] : byCommand) {
        (void)commandId;
        plan.steps.push_back(std::move(step));
    }

    std::sort(plan.steps.begin(), plan.steps.end(), [](const SendStep& a, const SendStep& b) {
        if (a.order != b.order)
            return a.order < b.order;
        return a.bindingId < b.bindingId;
    });

    plan.eligibleBindings = plan.steps.size();
    return plan;
}

} // namespace vofa::commands
