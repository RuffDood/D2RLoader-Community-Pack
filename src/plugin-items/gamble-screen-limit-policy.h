#pragma once

#include <json.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace RuffnecKk::GambleScreenLimit {

inline constexpr std::uint8_t VanillaLimit = 14;
inline constexpr std::uint8_t ExpandedLimit = 32;

struct Config {
	bool enabled{};
};

inline Config ParseConfig(const nlohmann::json& itemsConfig) {
	if (!itemsConfig.is_object()) {
		throw std::invalid_argument("items must be an object");
	}

	Config parsed{};
	const auto entry = itemsConfig.find("gambleScreenLimit");
	if (entry == itemsConfig.end()) return parsed;
	if (!entry->is_object()) {
		throw std::invalid_argument("items.gambleScreenLimit must be an object");
	}
	for (const auto& [key, value] : entry->items()) {
		(void)value;
		if (key != "enabled") {
			throw std::invalid_argument(
				"items.gambleScreenLimit has unknown setting: " + key
			);
		}
	}
	if (!entry->contains("enabled") || !entry->at("enabled").is_boolean()) {
		throw std::invalid_argument(
			"items.gambleScreenLimit.enabled must be a boolean"
		);
	}
	parsed.enabled = entry->at("enabled").get<bool>();
	return parsed;
}

inline constexpr std::uint8_t EffectiveLimit(const Config& config) noexcept {
	return config.enabled ? ExpandedLimit : VanillaLimit;
}

} // namespace RuffnecKk::GambleScreenLimit
