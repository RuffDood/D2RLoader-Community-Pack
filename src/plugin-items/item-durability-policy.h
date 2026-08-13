#pragma once

#include <json.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace RuffnecKk::ItemDurability {

constexpr std::uint32_t PackItemTypeCode(
	char first,
	char second,
	char third,
	char fourth = ' '
) noexcept {
	return static_cast<std::uint32_t>(static_cast<std::uint8_t>(first))
		| (static_cast<std::uint32_t>(static_cast<std::uint8_t>(second)) << 8U)
		| (static_cast<std::uint32_t>(static_cast<std::uint8_t>(third)) << 16U)
		| (static_cast<std::uint32_t>(static_cast<std::uint8_t>(fourth)) << 24U);
}

constexpr bool IsBowOrCrossbowItemTypeCode(std::uint32_t code) noexcept {
	return code == PackItemTypeCode('b', 'o', 'w')
		|| code == PackItemTypeCode('x', 'b', 'o', 'w');
}

struct Policy {
	bool enabled{};
	std::uint32_t normalResistancePercent{};
	std::uint32_t etherealResistancePercent{};
	std::uint32_t etherealMaximumPercent{50};
	bool forceMaximumDurability{};
	bool bowsAndCrossbowsHaveDurability{};
};

inline Policy ParseConfig(const nlohmann::json& itemsConfig) {
	if (!itemsConfig.is_object()) {
		throw std::invalid_argument("items must be an object");
	}
	const auto entry = itemsConfig.find("itemDurability");
	if (entry == itemsConfig.end()) return {};
	if (!entry->is_object()) {
		throw std::invalid_argument("items.itemDurability must be an object");
	}
	for (const auto& [key, value] : entry->items()) {
		(void)value;
		if (key != "enabled"
			&& key != "normalResistancePercent"
			&& key != "etherealResistancePercent"
			&& key != "etherealMaximumPercent"
			&& key != "forceMaximumDurability"
			&& key != "bowsAndCrossbowsHaveDurability") {
			throw std::invalid_argument(
				"items.itemDurability has unknown setting: " + key);
		}
	}

	for (const auto* key : {
		"enabled",
		"normalResistancePercent",
		"etherealResistancePercent",
		"etherealMaximumPercent",
		"forceMaximumDurability",
		"bowsAndCrossbowsHaveDurability"
	}) {
		if (!entry->contains(key)) {
			throw std::invalid_argument(
				std::string("items.itemDurability.") + key + " is required");
		}
	}
	if (!entry->at("enabled").is_boolean()
		|| !entry->at("forceMaximumDurability").is_boolean()
		|| !entry->at("bowsAndCrossbowsHaveDurability").is_boolean()) {
		throw std::invalid_argument(
			"items.itemDurability boolean settings must be booleans");
	}
	for (const auto* key : {
		"normalResistancePercent",
		"etherealResistancePercent",
		"etherealMaximumPercent"
	}) {
		if (!entry->at(key).is_number_integer()) {
			throw std::invalid_argument(
				std::string("items.itemDurability.") + key + " must be an integer");
		}
	}

	const auto normal = entry->at("normalResistancePercent").get<std::int64_t>();
	const auto ethereal = entry->at("etherealResistancePercent").get<std::int64_t>();
	const auto maximum = entry->at("etherealMaximumPercent").get<std::int64_t>();
	if (normal < 0 || normal > 100) {
		throw std::out_of_range(
			"items.itemDurability.normalResistancePercent must be between 0 and 100");
	}
	if (ethereal < 0 || ethereal > 100) {
		throw std::out_of_range(
			"items.itemDurability.etherealResistancePercent must be between 0 and 100");
	}
	if (maximum < 1 || maximum > 200) {
		throw std::out_of_range(
			"items.itemDurability.etherealMaximumPercent must be between 1 and 200");
	}

	return {
		.enabled = entry->at("enabled").get<bool>(),
		.normalResistancePercent = static_cast<std::uint32_t>(normal),
		.etherealResistancePercent = static_cast<std::uint32_t>(ethereal),
		.etherealMaximumPercent = static_cast<std::uint32_t>(maximum),
		.forceMaximumDurability = entry->at("forceMaximumDurability").get<bool>(),
		.bowsAndCrossbowsHaveDurability =
			entry->at("bowsAndCrossbowsHaveDurability").get<bool>(),
	};
}

constexpr bool PreventsLoss(
	std::uint32_t resistancePercent,
	std::uint32_t roll
) noexcept {
	return roll < std::min(resistancePercent, 100u);
}

// Returned in basis points: 400 = 4.00%, 1000 = 10.00%.
constexpr std::uint32_t EffectiveChanceBasisPoints(
	std::uint32_t vanillaChancePercent,
	std::uint32_t resistancePercent
) noexcept {
	return vanillaChancePercent * (100u - std::min(resistancePercent, 100u));
}

constexpr std::int32_t TargetEtherealMaxDurability(
	std::int32_t normalMaximum,
	std::uint32_t percent
) noexcept {
	if (normalMaximum <= 0) return normalMaximum;
	const auto clampedPercent = std::clamp(percent, 1u, 200u);
	const auto numerator = static_cast<std::int64_t>(normalMaximum) * clampedPercent;
	const auto scaled = clampedPercent < 100u
		? numerator / 100 + 1
		: (numerator + 50) / 100;
	return static_cast<std::int32_t>(std::clamp<std::int64_t>(scaled, 1, 255));
}

constexpr std::int32_t EncodeEtherealMaximumTarget(std::int32_t target) noexcept {
	return 2 * (std::clamp(target, 1, 255) - 1);
}

constexpr std::int32_t EncodeForVanillaEtherealHalving(
	std::int32_t normalMaximum,
	std::uint32_t percent
) noexcept {
	if (normalMaximum <= 0) return normalMaximum;
	return EncodeEtherealMaximumTarget(
		TargetEtherealMaxDurability(normalMaximum, percent));
}

constexpr std::int32_t ApplyVanillaEtherealHalving(std::int32_t value) noexcept {
	return value / 2 + 1;
}

} // namespace RuffnecKk::ItemDurability
