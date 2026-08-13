#pragma once

#include <json.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace RuffnecKk::GroundItemLabelLimit {

inline constexpr std::uint32_t VanillaLimit = 32;
inline constexpr std::uint32_t DefaultExpandedLimit = 64;
inline constexpr std::uint32_t MaximumExpandedLimit = 128;
inline constexpr std::uint32_t LabelEntrySize = 0x144;

struct Config {
	bool enabled{};
	std::uint32_t limit{DefaultExpandedLimit};
};

inline constexpr bool IsSupportedLimit(std::uint32_t value) noexcept {
	return value == DefaultExpandedLimit || value == MaximumExpandedLimit;
}

inline constexpr std::uint32_t LabelArrayByteOffset(std::uint32_t limit) noexcept {
	return limit * LabelEntrySize;
}

inline Config ParseConfig(const nlohmann::json& itemsConfig) {
	if (!itemsConfig.is_object()) {
		throw std::invalid_argument("items must be an object");
	}

	Config parsed{};
	const auto entry = itemsConfig.find("groundItemLabels");
	if (entry == itemsConfig.end()) return parsed;
	if (!entry->is_object()) {
		throw std::invalid_argument("items.groundItemLabels must be an object");
	}
	for (const auto& [key, value] : entry->items()) {
		(void)value;
		if (key != "enabled" && key != "limit") {
			throw std::invalid_argument(
				"items.groundItemLabels has unknown setting: " + key
			);
		}
	}
	if (!entry->contains("enabled") || !entry->at("enabled").is_boolean()) {
		throw std::invalid_argument(
			"items.groundItemLabels.enabled must be a boolean"
		);
	}
	if (!entry->contains("limit") || !entry->at("limit").is_number_integer()) {
		throw std::invalid_argument(
			"items.groundItemLabels.limit must be an integer"
		);
	}

	const auto configuredLimit = entry->at("limit").get<std::int64_t>();
	if (configuredLimit != DefaultExpandedLimit
		&& configuredLimit != MaximumExpandedLimit) {
		throw std::invalid_argument(
			"items.groundItemLabels.limit must be exactly 64 or 128"
		);
	}

	parsed.enabled = entry->at("enabled").get<bool>();
	parsed.limit = static_cast<std::uint32_t>(configuredLimit);
	return parsed;
}

inline constexpr std::uint32_t EffectiveLimit(const Config& config) noexcept {
	return config.enabled ? config.limit : VanillaLimit;
}

} // namespace RuffnecKk::GroundItemLabelLimit
