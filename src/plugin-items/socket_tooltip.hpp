#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tcp::tooltips {

inline constexpr auto NoSocketLineInsertion = static_cast<std::size_t>(-1);

struct TooltipLocalization {
    std::unordered_map<std::string, std::vector<std::string>> statTemplates;
    std::unordered_map<std::string, std::vector<std::string>> compoundDamageTemplates;
    std::vector<std::string> metadataTemplates;
    std::vector<std::string> primaryStatTemplates;
    std::vector<std::string> defenseTemplates;
    std::string maxSocketsFormat{"Max Sockets: %d"};
    std::string baseDefenseFormat{"Base Defense: %d"};
    std::string rangeSeparator{"to"};
    bool nativeReady{};
};

using LocalizedStringResolver = std::function<std::string(std::string_view key)>;

[[nodiscard]] TooltipLocalization BuildTooltipLocalization(
    const std::unordered_map<std::string, std::vector<std::string>>& statStringKeys,
    const LocalizedStringResolver& resolver
);

[[nodiscard]] bool MatchesLocalizedTemplate(
    std::string_view line,
    std::string_view format,
    bool allowTrailingText = false
);

[[nodiscard]] std::string FormatLocalizedInteger(
    std::string_view format,
    std::int32_t value
);

std::string FormatMaxSocketsLine(unsigned maximumSockets, int currentSockets);

std::string FormatMaxSocketsLine(
    unsigned maximumSockets,
    int currentSockets,
    const TooltipLocalization& localization
);

std::size_t FindMaxSocketsInsertion(std::string_view tooltip);

std::size_t FindMaxSocketsInsertion(
    std::string_view tooltip,
    const TooltipLocalization& localization
);

} // namespace tcp::tooltips
