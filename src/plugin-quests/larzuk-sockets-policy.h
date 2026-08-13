#pragma once

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RuffnecKk::ForceLarzukSockets {

enum class Difficulty : std::uint8_t {
    Normal = 0,
    Nightmare = 1,
    Hell = 2,
};

enum class ItemQuality : std::int32_t {
    Magic = 4,
    Set = 5,
    Rare = 6,
    Unique = 7,
    Crafted = 8,
};

struct SocketRule {
    std::uint8_t minSockets{};
    std::uint8_t maxSockets{};
};

constexpr std::size_t DifficultyCount = 3;
constexpr std::size_t QualityCount = 5;
using RuleMatrix = std::array<std::array<std::optional<SocketRule>, QualityCount>, DifficultyCount>;

struct Config {
    bool enabled{};
    RuleMatrix rules{};
};

constexpr std::optional<std::size_t> QualityIndex(std::int32_t quality) noexcept {
    switch (static_cast<ItemQuality>(quality)) {
    case ItemQuality::Magic: return 0;
    case ItemQuality::Rare: return 1;
    case ItemQuality::Set: return 2;
    case ItemQuality::Unique: return 3;
    case ItemQuality::Crafted: return 4;
    default: return std::nullopt;
    }
}

constexpr bool IsValidRule(SocketRule rule) noexcept {
    return rule.minSockets >= 1
        && rule.maxSockets <= 6
        && rule.minSockets <= rule.maxSockets;
}

constexpr std::uint8_t EffectiveLegalMaximum(
    std::uint8_t engineMaximum,
    std::uint8_t inventoryWidth,
    std::uint8_t inventoryHeight
) noexcept {
    const auto area = static_cast<std::uint16_t>(inventoryWidth)
        * static_cast<std::uint16_t>(inventoryHeight);
    if (area == 0) return 0;
    return std::min<std::uint8_t>(
        engineMaximum,
        static_cast<std::uint8_t>(std::min<std::uint16_t>(area, 6))
    );
}

constexpr std::uint32_t LimitedRandomIndex(
    std::uint32_t rawRoll,
    std::uint32_t range
) noexcept {
    if (range <= 1) return 0;
    return (range & (range - 1)) == 0
        ? (rawRoll & (range - 1))
        : (rawRoll % range);
}

constexpr std::uint8_t ResolveSockets(
    SocketRule rule,
    std::uint8_t legalMaximum,
    std::uint32_t rawRoll
) noexcept {
    if (legalMaximum == 0) return 0;
    const auto minimum = std::min(rule.minSockets, legalMaximum);
    const auto maximum = std::min(rule.maxSockets, legalMaximum);
    const auto range = static_cast<std::uint32_t>(maximum - minimum) + 1;
    return static_cast<std::uint8_t>(minimum + LimitedRandomIndex(rawRoll, range));
}

constexpr const std::optional<SocketRule>* FindRule(
    const RuleMatrix& rules,
    std::uint8_t difficulty,
    std::int32_t quality
) noexcept {
    const auto qualityIndex = QualityIndex(quality);
    if (difficulty >= DifficultyCount || !qualityIndex) return nullptr;
    return &rules[difficulty][*qualityIndex];
}

constexpr bool HasRules(const RuleMatrix& rules) noexcept {
    for (const auto& difficulty : rules) {
        for (const auto& rule : difficulty) {
            if (rule.has_value()) return true;
        }
    }
    return false;
}

inline void RequireAllowedKeys(
    const nlohmann::json& object,
    std::initializer_list<std::string_view> allowed,
    std::string_view context
) {
    for (const auto& [key, value] : object.items()) {
        (void)value;
        const auto found = std::find(allowed.begin(), allowed.end(), std::string_view(key));
        if (found == allowed.end()) {
            throw std::invalid_argument(
                std::string(context) + " contains unknown key '" + key + "'"
            );
        }
    }
}

inline SocketRule ParseRule(const nlohmann::json& value, std::string_view path) {
    if (!value.is_object()) {
        throw std::invalid_argument(std::string(path) + " must be an object or null");
    }
    RequireAllowedKeys(value, {"minSockets", "maxSockets"}, path);
    if (!value.contains("minSockets") || !value.contains("maxSockets")) {
        throw std::invalid_argument(
            std::string(path) + " must define both minSockets and maxSockets"
        );
    }
    if (!value.at("minSockets").is_number_integer()
        || !value.at("maxSockets").is_number_integer()) {
        throw std::invalid_argument(
            std::string(path) + " bounds must be integers from 1 through 6"
        );
    }
    const auto minimum = value.at("minSockets").get<std::int32_t>();
    const auto maximum = value.at("maxSockets").get<std::int32_t>();
    if (minimum < 1 || maximum > 6) {
        throw std::invalid_argument(
            std::string(path) + " requires 1 <= minSockets <= maxSockets <= 6"
        );
    }
    const SocketRule rule{
        static_cast<std::uint8_t>(minimum),
        static_cast<std::uint8_t>(maximum),
    };
    if (!IsValidRule(rule)) {
        throw std::invalid_argument(
            std::string(path) + " requires 1 <= minSockets <= maxSockets <= 6"
        );
    }
    return rule;
}

inline Config ParseConfig(const nlohmann::json& questsConfig) {
    if (!questsConfig.is_object()) {
        throw std::invalid_argument("quests must be an object");
    }

    Config parsed{};
    const auto entry = questsConfig.find("larzukSockets");
    if (entry == questsConfig.end()) return parsed;
    if (!entry->is_object()) {
        throw std::invalid_argument("quests.larzukSockets must be an object");
    }
    RequireAllowedKeys(
        *entry,
        {"enabled", "normal", "nightmare", "hell"},
        "quests.larzukSockets"
    );
    if (!entry->contains("enabled") || !entry->at("enabled").is_boolean()) {
        throw std::invalid_argument(
            "quests.larzukSockets.enabled must be a boolean"
        );
    }
    parsed.enabled = entry->at("enabled").get<bool>();

    constexpr std::array<std::string_view, DifficultyCount> difficultyNames{
        "normal", "nightmare", "hell"
    };
    constexpr std::array<std::string_view, QualityCount> qualityNames{
        "magic", "rare", "set", "unique", "crafted"
    };
    for (std::size_t difficulty = 0; difficulty < difficultyNames.size(); ++difficulty) {
        const auto difficultyName = difficultyNames[difficulty];
        const auto difficultyEntry = entry->find(difficultyName);
        if (difficultyEntry == entry->end()) continue;
        if (!difficultyEntry->is_object()) {
            throw std::invalid_argument(
                "quests.larzukSockets." + std::string(difficultyName)
                + " must be an object"
            );
        }
        RequireAllowedKeys(
            *difficultyEntry,
            {"magic", "rare", "set", "unique", "crafted"},
            "quests.larzukSockets." + std::string(difficultyName)
        );
        for (std::size_t quality = 0; quality < qualityNames.size(); ++quality) {
            const auto qualityName = qualityNames[quality];
            const auto qualityEntry = difficultyEntry->find(qualityName);
            if (qualityEntry == difficultyEntry->end() || qualityEntry->is_null()) {
                continue;
            }
            parsed.rules[difficulty][quality] = ParseRule(
                *qualityEntry,
                "quests.larzukSockets." + std::string(difficultyName)
                    + "." + std::string(qualityName)
            );
        }
    }
    return parsed;
}

} // namespace RuffnecKk::ForceLarzukSockets
