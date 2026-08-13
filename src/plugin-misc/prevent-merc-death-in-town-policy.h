#pragma once

#include <json.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace RuffnecKk::PreventMercDeathInTown {

struct Config {
    bool enabled{};
};

constexpr bool IsHirelingClass(std::uint32_t classId) noexcept {
    return classId == 271 || classId == 338 || classId == 359
        || classId == 560 || classId == 561;
}

constexpr bool IsProjectedLethal(
    std::int32_t hitpoints,
    std::int32_t regeneration
) noexcept {
    return regeneration < 0
        && static_cast<std::int64_t>(hitpoints) + regeneration <= 0;
}

inline Config ParseConfig(const nlohmann::json& miscConfig) {
    if (!miscConfig.is_object()) {
        throw std::invalid_argument("misc must be an object");
    }
    Config parsed{};
    const auto entry = miscConfig.find("preventMercDeathInTown");
    if (entry == miscConfig.end()) return parsed;
    if (!entry->is_object()) {
        throw std::invalid_argument(
            "misc.preventMercDeathInTown must be an object");
    }
    for (const auto& [key, value] : entry->items()) {
        (void)value;
        if (key != "enabled") {
            throw std::invalid_argument(
                "misc.preventMercDeathInTown contains unknown key '" + key + "'");
        }
    }
    if (entry->contains("enabled") && !entry->at("enabled").is_boolean()) {
        throw std::invalid_argument(
            "misc.preventMercDeathInTown.enabled must be a boolean");
    }
    parsed.enabled = entry->value("enabled", false);
    return parsed;
}

} // namespace RuffnecKk::PreventMercDeathInTown
