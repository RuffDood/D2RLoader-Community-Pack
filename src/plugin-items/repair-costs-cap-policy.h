#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Repair Costs Cap 1.4.0 gameplay policy by RuffnecKk.
namespace RuffnecKk::RepairCostsCap {

inline constexpr std::int32_t RepairTransactionType = 3;
inline constexpr std::int64_t MaximumGoldLimit = std::numeric_limits<std::int32_t>::max();
inline constexpr std::uint32_t ChanceBasisPointScale = 10'000;

struct RepairPolicy {
	bool enabled{};
	std::int32_t maximumGold{std::numeric_limits<std::int32_t>::max()};
	bool durabilityWearEnabled{};
	double durabilityWearChance{};
};

inline bool IsValidMaximumGold(std::int64_t value) noexcept {
	return value >= 0 && value <= MaximumGoldLimit;
}

inline bool IsValidChance(double value) noexcept {
	return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

inline std::uint32_t ChanceToBasisPoints(double chance) noexcept {
	if (!IsValidChance(chance)) return 0;
	return static_cast<std::uint32_t>(std::llround(
		chance * static_cast<double>(ChanceBasisPointScale)
	));
}

inline bool IsValidPolicy(const RepairPolicy& policy) noexcept {
	return IsValidMaximumGold(policy.maximumGold)
		&& IsValidChance(policy.durabilityWearChance);
}

inline bool IsPhysicalRepairCandidate(
	std::int32_t durabilityBeforeRepair,
	std::int32_t maximumDurabilityBeforeRepair
) noexcept {
	return maximumDurabilityBeforeRepair > 1
		&& durabilityBeforeRepair >= 0
		&& durabilityBeforeRepair < maximumDurabilityBeforeRepair;
}

inline bool DidPhysicalRepairSucceed(
	std::int32_t durabilityBeforeRepair,
	std::int32_t maximumDurabilityBeforeRepair,
	std::int32_t durabilityAfterRepair
) noexcept {
	return IsPhysicalRepairCandidate(durabilityBeforeRepair, maximumDurabilityBeforeRepair)
		&& durabilityAfterRepair >= maximumDurabilityBeforeRepair;
}

inline bool ShouldLoseMaximumDurability(
	bool enabled,
	double chance,
	std::uint32_t roll
) noexcept {
	if (!enabled || !IsValidChance(chance) || roll >= ChanceBasisPointScale) return false;
	return roll < ChanceToBasisPoints(chance);
}

inline std::int32_t ReducedMaximumDurability(std::int32_t maximumDurability) noexcept {
	return maximumDurability > 1 ? maximumDurability - 1 : maximumDurability;
}

inline std::int32_t ApplyRepairCostCap(
	std::int32_t vanillaCost,
	std::int32_t transactionType,
	const RepairPolicy& policy
) noexcept {
	if (!policy.enabled || transactionType != RepairTransactionType) return vanillaCost;
	if (!IsValidPolicy(policy)) return vanillaCost;
	if (vanillaCost <= 0 || vanillaCost == std::numeric_limits<std::int32_t>::max()) {
		return vanillaCost;
	}
	return std::min(vanillaCost, policy.maximumGold);
}

inline std::int32_t ApplyRepairAllCap(
	std::int32_t adjustedItemTotal,
	const RepairPolicy& policy
) noexcept {
	if (!policy.enabled || !IsValidPolicy(policy)) return adjustedItemTotal;
	if (adjustedItemTotal <= 0
		|| adjustedItemTotal == std::numeric_limits<std::int32_t>::max()) {
		return adjustedItemTotal;
	}
	return std::min(adjustedItemTotal, policy.maximumGold);
}

inline std::uint64_t GoldReduction(std::int32_t before, std::int32_t after) noexcept {
	if (before <= after || before <= 0 || after < 0) return 0;
	return static_cast<std::uint64_t>(
		static_cast<std::int64_t>(before) - static_cast<std::int64_t>(after));
}

} // namespace RuffnecKk::RepairCostsCap
