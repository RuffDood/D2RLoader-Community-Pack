#pragma once

#include <json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace RuffnecKk::QtyDisplayIssue {

inline constexpr std::uintptr_t QuantitySuppressionSignatureRva = 0x2BE103;
inline constexpr std::size_t QuantitySuppressionSignatureSize = 33;
inline constexpr std::size_t QuantitySuppressionBranchOffset = 21;

using QuantitySuppressionSignature =
	std::array<std::uint8_t, QuantitySuppressionSignatureSize>;

inline constexpr QuantitySuppressionSignature QuantitySuppressionExpected{
	0xBA, 0x00, 0x08, 0x00, 0x00,
	0x41, 0xB8, 0x2F, 0x07, 0x00, 0x00,
	0x48, 0x8B, 0xCB,
	0xE8, 0xBA, 0x01, 0x0B, 0x00,
	0x85, 0xC0,
	0x75, 0x0F,
	0x4C, 0x8B, 0xC7,
	0x48, 0x8D, 0x55, 0xD0,
	0x48, 0x8B, 0xCB,
};

struct Config {
	bool enabled{};
};

inline Config ParseConfig(const nlohmann::json& itemsConfig) {
	if (!itemsConfig.is_object()) {
		throw std::invalid_argument("items must be an object");
	}

	Config parsed{};
	const auto entry = itemsConfig.find("qtyDisplayIssue");
	if (entry == itemsConfig.end()) return parsed;
	if (!entry->is_object()) {
		throw std::invalid_argument("items.qtyDisplayIssue must be an object");
	}
	for (const auto& [key, value] : entry->items()) {
		(void)value;
		if (key != "enabled") {
			throw std::invalid_argument(
				"items.qtyDisplayIssue has unknown setting: " + key
			);
		}
	}
	if (!entry->contains("enabled") || !entry->at("enabled").is_boolean()) {
		throw std::invalid_argument(
			"items.qtyDisplayIssue.enabled must be a boolean"
		);
	}
	parsed.enabled = entry->at("enabled").get<bool>();
	return parsed;
}

inline QuantitySuppressionSignature BuildQuantitySuppressionPatch() noexcept {
	auto replacement = QuantitySuppressionExpected;
	replacement[QuantitySuppressionBranchOffset] = 0x90;
	replacement[QuantitySuppressionBranchOffset + 1] = 0x90;
	return replacement;
}

} // namespace RuffnecKk::QtyDisplayIssue
