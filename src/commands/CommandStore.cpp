#include "CommandStore.h"

#include <algorithm>

namespace vofa::commands {

void CommandStore::clear()
{
    commands_.clear();
    bindings_.clear();
}

void CommandStore::addCommand(std::uint64_t commandId,
                              std::string name,
                              TemplateFormatter formatter)
{
    commands_.push_back({commandId, std::move(formatter), std::move(name), {}});
}

void CommandStore::addCommand(std::uint64_t commandId,
                              std::string name,
                              std::string templateText,
                              TemplateFormatter formatter)
{
    commands_.push_back({commandId,
                         std::move(formatter),
                         std::move(name),
                         std::move(templateText)});
}

void CommandStore::addBinding(SendBinding binding)
{
    bindings_.push_back(std::move(binding));
}

SendBinding* CommandStore::findBinding(std::uint64_t bindingId) noexcept
{
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [bindingId](const SendBinding& binding) {
                                     return binding.bindingId == bindingId;
                                 });
    return it == bindings_.end() ? nullptr : &*it;
}

const SendBinding* CommandStore::findBinding(std::uint64_t bindingId) const noexcept
{
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [bindingId](const SendBinding& binding) {
                                     return binding.bindingId == bindingId;
                                 });
    return it == bindings_.end() ? nullptr : &*it;
}

CommandTemplateEntry* CommandStore::findCommand(std::uint64_t commandId) noexcept
{
    const auto it = std::find_if(commands_.begin(), commands_.end(),
                                 [commandId](const CommandTemplateEntry& command) {
                                     return command.commandId == commandId;
                                 });
    return it == commands_.end() ? nullptr : &*it;
}

const CommandTemplateEntry* CommandStore::findCommand(std::uint64_t commandId) const noexcept
{
    const auto it = std::find_if(commands_.begin(), commands_.end(),
                                 [commandId](const CommandTemplateEntry& command) {
                                     return command.commandId == commandId;
                                 });
    return it == commands_.end() ? nullptr : &*it;
}

bool CommandStore::setBindingValue(std::uint64_t bindingId, double value)
{
    auto* binding = findBinding(bindingId);
    if (binding == nullptr)
        return false;

    value = std::clamp(value, binding->minValue, binding->maxValue);
    if (binding->args.empty())
        binding->args.push_back(CommandArg::fromFloat(value));
    else {
        binding->args[0].type = CommandArg::Type::Float;
        binding->args[0].floatValue = value;
    }
    return true;
}

bool CommandStore::setBindingIncluded(std::uint64_t bindingId, bool included)
{
    auto* binding = findBinding(bindingId);
    if (binding == nullptr)
        return false;
    binding->includeInOneClickSend = included;
    return true;
}

bool CommandStore::setBindingMetadata(std::uint64_t bindingId,
                                      double minValue,
                                      double maxValue,
                                      double stepSize)
{
    auto* binding = findBinding(bindingId);
    if (binding == nullptr)
        return false;
    binding->minValue = minValue;
    binding->maxValue = std::max(minValue, maxValue);
    binding->stepSize = std::max(0.0, stepSize);
    if (!binding->args.empty()) {
        binding->args[0].floatValue =
            std::clamp(binding->args[0].floatValue, binding->minValue, binding->maxValue);
    }
    return true;
}

bool CommandStore::removeBinding(std::uint64_t bindingId)
{
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [bindingId](const SendBinding& binding) {
                                     return binding.bindingId == bindingId;
                                 });
    if (it == bindings_.end())
        return false;

    const auto commandId = it->commandId;
    bindings_.erase(it);

    const bool stillUsed = std::any_of(bindings_.begin(), bindings_.end(),
                                       [commandId](const SendBinding& binding) {
                                           return binding.commandId == commandId;
                                       });
    if (!stillUsed) {
        commands_.erase(std::remove_if(commands_.begin(), commands_.end(),
                                       [commandId](const CommandTemplateEntry& command) {
                                           return command.commandId == commandId;
                                       }),
                        commands_.end());
    }
    return true;
}

bool CommandStore::updateCommand(std::uint64_t commandId,
                                 std::string name,
                                 std::string templateText,
                                 TemplateFormatter formatter)
{
    auto* command = findCommand(commandId);
    if (command == nullptr)
        return false;
    command->name = std::move(name);
    command->templateText = std::move(templateText);
    command->formatter = std::move(formatter);
    return true;
}

OneClickSendPlan CommandStore::buildOneClickPlan(int intervalMs,
                                                 std::string_view group) const
{
    return OneClickSendPlanner::build(bindings_, commands_, intervalMs, group);
}

} // namespace vofa::commands
