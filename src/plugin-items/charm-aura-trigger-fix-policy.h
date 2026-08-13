#pragma once

#include <json.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace RuffnecKk::CharmAuraTriggerFix {

inline constexpr std::int32_t CharmItemTypeId = 0x0D;
inline constexpr std::uint8_t InventoryNodePosition = 3;
inline constexpr std::uint32_t IdentifiedItemFlag = 0x10;
inline constexpr std::size_t MaximumRefreshedCharms = 32;
inline constexpr std::uint16_t ItemAuraStatId = 151;

struct PackedStatRecord {
	std::uint32_t packed{};
	std::int32_t value{};
};

struct Config {
	bool enabled{};
};

inline Config ParseConfig(const nlohmann::json& itemsConfig) {
	if (!itemsConfig.is_object()) {
		throw std::invalid_argument("items must be an object");
	}

	const auto entry = itemsConfig.find("charmAuraTriggerFix");
	if (entry == itemsConfig.end()) return {};
	if (!entry->is_object()) {
		throw std::invalid_argument(
			"items.charmAuraTriggerFix must be an object");
	}
	for (const auto& [key, value] : entry->items()) {
		(void)value;
		if (key != "enabled") {
			throw std::invalid_argument(
				"items.charmAuraTriggerFix has unknown setting: " + key);
		}
	}
	for (const auto* key : {"enabled"}) {
		if (!entry->contains(key) || !entry->at(key).is_boolean()) {
			throw std::invalid_argument(
				std::string("items.charmAuraTriggerFix.") + key
				+ " must be a boolean");
		}
	}
	return {
		.enabled = entry->at("enabled").get<bool>(),
	};
}

inline constexpr std::uint16_t StatId(std::uint32_t packed) noexcept {
	return static_cast<std::uint16_t>(packed >> 16U);
}

inline constexpr bool HasNonzeroStat(
	const PackedStatRecord* records,
	std::size_t count,
	std::uint16_t wantedStat
) noexcept {
	if (!records) return false;
	for (std::size_t index = 0; index < count; ++index) {
		const auto stat = StatId(records[index].packed);
		if (stat > wantedStat) return false;
		if (stat == wantedStat && records[index].value != 0) return true;
	}
	return false;
}

inline constexpr bool IsEligible(
	bool matchesCharmType,
	std::uint8_t nodePosition,
	std::uint32_t itemFlags
) noexcept {
	return matchesCharmType
		&& nodePosition == InventoryNodePosition
		&& (itemFlags & IdentifiedItemFlag) != 0;
}

} // namespace RuffnecKk::CharmAuraTriggerFix
