#pragma once

#include <json.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

namespace RuffnecKk::MagicFindFormula {

enum class Mode {
	Vanilla,
	Linear,
};

struct Config {
	Mode mode{Mode::Vanilla};
};

inline Config ParseConfig(const nlohmann::json& itemsConfig) {
	if (!itemsConfig.is_object()) {
		throw std::invalid_argument("items must be an object");
	}
	const auto entry = itemsConfig.find("magicFindFormula");
	if (entry == itemsConfig.end()) return {};
	if (!entry->is_object()) {
		throw std::invalid_argument(
			"items.magicFindFormula must be an object");
	}
	for (const auto& [key, value] : entry->items()) {
		(void)value;
		if (key != "mode") {
			throw std::invalid_argument(
				"items.magicFindFormula has unknown setting: " + key);
		}
	}
	if (!entry->contains("mode")) return {};
	if (!entry->at("mode").is_string()) {
		throw std::invalid_argument(
			"items.magicFindFormula.mode must be a string");
	}
	const auto mode = entry->at("mode").get<std::string>();
	if (mode == "vanilla") return {.mode = Mode::Vanilla};
	if (mode == "linear") return {.mode = Mode::Linear};
	throw std::invalid_argument(
		"items.magicFindFormula.mode must be 'vanilla' or 'linear'");
}

inline constexpr std::string_view ModeName(Mode mode) noexcept {
	return mode == Mode::Linear ? "linear" : "vanilla";
}

inline constexpr bool IsLinear(const Config& config) noexcept {
	return config.mode == Mode::Linear;
}

} // namespace RuffnecKk::MagicFindFormula
