#pragma once

#include "TemplateFormatter.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vofa::commands {

enum class ControlKind {
    Slider,
    Dial,
    Toggle,
    Button,
    Other,
};

// Only value controls are eligible for one-click sending. Action controls
// (buttons/toggles) are deliberately excluded: they often start/stop the MCU
// program and must not be replayed blindly.
[[nodiscard]] constexpr bool isValueControl(ControlKind kind) noexcept
{
    return kind == ControlKind::Slider || kind == ControlKind::Dial;
}

struct SendBinding {
    std::uint64_t bindingId = 0;
    std::string label;
    ControlKind kind = ControlKind::Slider;
    bool includeInOneClickSend = true;
    std::uint64_t commandId = 0;
    int order = 0;
    std::vector<CommandArg> args;

    // UI metadata; unused by the planner.
    double minValue = 0.0;
    double maxValue = 1.0;
    double stepSize = 0.01;

    // Command group / project page. Kept last so existing aggregate
    // initializers stay source-compatible.
    std::string group;
};

struct CommandTemplateEntry {
    std::uint64_t commandId = 0;
    TemplateFormatter formatter;
    std::string name;
    std::string templateText;

    CommandTemplateEntry() = default;
    CommandTemplateEntry(std::uint64_t id, TemplateFormatter fmt)
        : commandId(id)
        , formatter(std::move(fmt))
    {
    }
    CommandTemplateEntry(std::uint64_t id,
                         TemplateFormatter fmt,
                         std::string commandName,
                         std::string text)
        : commandId(id)
        , formatter(std::move(fmt))
        , name(std::move(commandName))
        , templateText(std::move(text))
    {
    }
};

struct SendStep {
    std::uint64_t bindingId = 0;
    std::uint64_t commandId = 0;
    std::string label;
    std::vector<std::byte> payload;
    int delayAfterMs = 100;
    int order = 0;
};

struct SendPlanError {
    std::uint64_t bindingId = 0;
    std::string message;
};

struct OneClickSendPlan {
    std::vector<SendStep> steps;
    std::vector<SendPlanError> errors;
    int intervalMs = 100;
    std::size_t eligibleBindings = 0;
};

// Builds the ordered one-click send plan. Sliders/dials with
// includeInOneClickSend == true are rendered in UI order, with a configurable
// delay between steps. Duplicate command ids are coalesced to the last value.
class OneClickSendPlanner {
public:
    static OneClickSendPlan build(std::span<const SendBinding> bindings,
                                  std::span<const CommandTemplateEntry> commands,
                                  int intervalMs = 100,
                                  std::string_view group = {});
};

} // namespace vofa::commands
