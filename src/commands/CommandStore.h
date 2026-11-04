#pragma once

#include "OneClickSend.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vofa::commands {

// In-memory command/binding registry. The UI and persistence layers own the
// display metadata; the core planner owns only the send semantics.
class CommandStore {
public:
    void clear();
    void addCommand(std::uint64_t commandId, std::string name, TemplateFormatter formatter);
    void addCommand(std::uint64_t commandId,
                    std::string name,
                    std::string templateText,
                    TemplateFormatter formatter);
    void addBinding(SendBinding binding);

    [[nodiscard]] const std::vector<CommandTemplateEntry>& commands() const noexcept
    {
        return commands_;
    }
    [[nodiscard]] const std::vector<SendBinding>& bindings() const noexcept
    {
        return bindings_;
    }
    // App-owned metadata (group assignment) may be edited in place.
    [[nodiscard]] std::vector<SendBinding>& bindings() noexcept { return bindings_; }

    [[nodiscard]] bool setBindingValue(std::uint64_t bindingId, double value);
    [[nodiscard]] bool setBindingIncluded(std::uint64_t bindingId, bool included);
    [[nodiscard]] bool setBindingMetadata(std::uint64_t bindingId,
                                          double minValue,
                                          double maxValue,
                                          double stepSize);
    [[nodiscard]] bool removeBinding(std::uint64_t bindingId);
    [[nodiscard]] bool updateCommand(std::uint64_t commandId,
                                     std::string name,
                                     std::string templateText,
                                     TemplateFormatter formatter);

    [[nodiscard]] const SendBinding* findBinding(std::uint64_t bindingId) const noexcept;
    [[nodiscard]] SendBinding* findBinding(std::uint64_t bindingId) noexcept;
    [[nodiscard]] const CommandTemplateEntry* findCommand(std::uint64_t commandId) const noexcept;
    [[nodiscard]] CommandTemplateEntry* findCommand(std::uint64_t commandId) noexcept;

    [[nodiscard]] OneClickSendPlan buildOneClickPlan(
        int intervalMs, std::string_view group = {}) const;

private:
    std::vector<CommandTemplateEntry> commands_;
    std::vector<SendBinding> bindings_;
};

} // namespace vofa::commands
