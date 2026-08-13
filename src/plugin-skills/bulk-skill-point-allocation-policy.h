#pragma once

#include <json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace RuffnecKk::BulkSkillPointAllocation {

enum class AllocationMode : std::uint8_t {
	Single,
	CtrlBatch,
	ShiftAll,
};

inline constexpr std::uint32_t DefaultSkillPointsPerCtrlClick = 5;
inline constexpr std::uint32_t MaximumSkillPointsPerCtrlClick = 1'000;
inline constexpr std::uint16_t AssignAllSkillPointsExtra = 0xFFFF;
inline constexpr char DefaultShiftConfirmationLocalizationKey[] = "shiftConfirmation";
inline constexpr char DefaultShiftConfirmation[] =
	"Invest all currently usable skill points in this skill?";

struct Policy {
	bool enabled{};
	std::uint32_t skillPointsPerCtrlClick{DefaultSkillPointsPerCtrlClick};
	bool confirmShiftAllocation{};
	std::string shiftConfirmationKey{DefaultShiftConfirmationLocalizationKey};
	std::string shiftConfirmationFallback{DefaultShiftConfirmation};
};

constexpr AllocationMode ResolveMode(bool shiftPressed, bool ctrlPressed) noexcept {
	if (ctrlPressed) return AllocationMode::CtrlBatch;
	if (shiftPressed) return AllocationMode::ShiftAll;
	return AllocationMode::Single;
}

constexpr std::uint16_t NativeSkillPacketExtra(
	AllocationMode mode,
	std::uint32_t requestedPoints
) noexcept {
	if (mode == AllocationMode::ShiftAll) return AssignAllSkillPointsExtra;
	if (requestedPoints <= 1) return 0;
	return static_cast<std::uint16_t>(std::min(requestedPoints - 1, 0xFFFEU));
}

inline bool IsUsableLocalizedString(
	const char* localized,
	const char* requestedKey,
	const char* localizedMissingString
) noexcept {
	if (!localized || localized[0] == '\0') return false;
	if (requestedKey && std::strcmp(localized, requestedKey) == 0) return false;
	if (localizedMissingString
		&& std::strcmp(localized, localizedMissingString) == 0) return false;
	return true;
}

inline Policy ParseConfig(const nlohmann::json& skillsConfig) {
	if (!skillsConfig.is_object()) {
		throw std::invalid_argument("skills must be an object");
	}
	const auto entry = skillsConfig.find("bulkSkillPointAllocation");
	if (entry == skillsConfig.end()) return {};
	if (!entry->is_object()) {
		throw std::invalid_argument(
			"skills.bulkSkillPointAllocation must be an object");
	}
	for (const auto& [key, value] : entry->items()) {
		(void)value;
		if (key != "enabled"
			&& key != "skillPointsPerCtrlClick"
			&& key != "confirmShiftAllocation"
			&& key != "shiftConfirmationKey"
			&& key != "shiftConfirmationFallback") {
			throw std::invalid_argument(
				"skills.bulkSkillPointAllocation has unknown setting: " + key);
		}
	}
	if (!entry->contains("enabled") || !entry->at("enabled").is_boolean()) {
		throw std::invalid_argument(
			"skills.bulkSkillPointAllocation.enabled must be a boolean");
	}

	Policy policy{};
	policy.enabled = entry->at("enabled").get<bool>();
	if (entry->contains("skillPointsPerCtrlClick")) {
		const auto& value = entry->at("skillPointsPerCtrlClick");
		if (!value.is_number_unsigned() && !value.is_number_integer()) {
			throw std::invalid_argument(
				"skills.bulkSkillPointAllocation.skillPointsPerCtrlClick must be an integer");
		}
		const auto points = value.get<std::int64_t>();
		if (points < 1 || points > MaximumSkillPointsPerCtrlClick) {
			throw std::invalid_argument(
				"skills.bulkSkillPointAllocation.skillPointsPerCtrlClick must be 1 through 1000");
		}
		policy.skillPointsPerCtrlClick = static_cast<std::uint32_t>(points);
	}
	if (entry->contains("confirmShiftAllocation")) {
		if (!entry->at("confirmShiftAllocation").is_boolean()) {
			throw std::invalid_argument(
				"skills.bulkSkillPointAllocation.confirmShiftAllocation must be a boolean");
		}
		policy.confirmShiftAllocation = entry->at("confirmShiftAllocation").get<bool>();
	}
	if (entry->contains("shiftConfirmationKey")) {
		if (!entry->at("shiftConfirmationKey").is_string()) {
			throw std::invalid_argument(
				"skills.bulkSkillPointAllocation.shiftConfirmationKey must be a string");
		}
		policy.shiftConfirmationKey = entry->at("shiftConfirmationKey").get<std::string>();
	}
	if (entry->contains("shiftConfirmationFallback")) {
		if (!entry->at("shiftConfirmationFallback").is_string()) {
			throw std::invalid_argument(
				"skills.bulkSkillPointAllocation.shiftConfirmationFallback must be a string");
		}
		policy.shiftConfirmationFallback =
			entry->at("shiftConfirmationFallback").get<std::string>();
	}
	if (policy.shiftConfirmationKey.empty() || policy.shiftConfirmationKey.size() > 255) {
		throw std::invalid_argument(
			"skills.bulkSkillPointAllocation.shiftConfirmationKey must contain 1 through 255 UTF-8 bytes");
	}
	if (policy.shiftConfirmationFallback.empty()
		|| policy.shiftConfirmationFallback.size() > 1024) {
		throw std::invalid_argument(
			"skills.bulkSkillPointAllocation.shiftConfirmationFallback must contain 1 through 1024 UTF-8 bytes");
	}
	return policy;
}

} // namespace RuffnecKk::BulkSkillPointAllocation
