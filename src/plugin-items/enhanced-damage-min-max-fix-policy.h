#pragma once

#include <json.hpp>

#include <cstdint>
#include <stdexcept>

namespace RuffnecKk::EnhancedDamageMinMaxFix {

constexpr std::int32_t ItemMaxDamagePercentStat = 17;
constexpr std::int32_t ItemMinDamagePercentStat = 18;
constexpr std::int32_t ItemUnitType = 4;
constexpr std::int32_t WeaponItemTypeId = 45;
constexpr std::uint8_t AddItemStatPercentOperation = 13;

struct Policy {
	bool enabled{};
};

inline Policy ParseConfig(const nlohmann::json& itemsConfig) {
	if (!itemsConfig.is_object()) {
		throw std::invalid_argument("items must be an object");
	}
	const auto entry = itemsConfig.find("enhancedDamageMinMaxFix");
	if (entry == itemsConfig.end()) return {};
	if (!entry->is_object()) {
		throw std::invalid_argument("items.enhancedDamageMinMaxFix must be an object");
	}
	for (const auto& [key, value] : entry->items()) {
		(void)value;
		if (key != "enabled") {
			throw std::invalid_argument(
				"items.enhancedDamageMinMaxFix has unknown setting: " + key);
		}
	}
	if (!entry->contains("enabled") || !entry->at("enabled").is_boolean()) {
		throw std::invalid_argument(
			"items.enhancedDamageMinMaxFix.enabled must be a boolean");
	}
	return {.enabled = entry->at("enabled").get<bool>()};
}

constexpr std::int32_t PackStat(
	std::int32_t stat,
	std::uint16_t layer = 0
) noexcept {
	return static_cast<std::int32_t>(
		(static_cast<std::uint32_t>(stat) << 16U) | layer);
}

constexpr bool IsEnhancedDamageStat(
	std::int32_t stat,
	std::uint16_t layer
) noexcept {
	return layer == 0
		&& (stat == ItemMaxDamagePercentStat || stat == ItemMinDamagePercentStat);
}

constexpr bool IsEnhancedDamagePackedStat(std::int32_t packedStat) noexcept {
	const auto value = static_cast<std::uint32_t>(packedStat);
	return IsEnhancedDamageStat(
		static_cast<std::int32_t>(value >> 16U),
		static_cast<std::uint16_t>(value & 0xFFFFU));
}

constexpr bool ShouldRestoreSuppressedUpdate(
	std::int32_t ownerType,
	std::uint8_t operation,
	std::int32_t packedStat,
	bool effectiveItemIsWeapon,
	std::int32_t evaluatedValue,
	std::int32_t retainedValue
) noexcept {
	return ownerType == ItemUnitType
		&& operation == AddItemStatPercentOperation
		&& IsEnhancedDamagePackedStat(packedStat)
		&& !effectiveItemIsWeapon
		&& evaluatedValue != retainedValue;
}

} // namespace RuffnecKk::EnhancedDamageMinMaxFix
