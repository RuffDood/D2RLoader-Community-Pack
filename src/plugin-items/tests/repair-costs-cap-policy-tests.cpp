#include "repair-costs-cap-policy.h"

#include "../../../tests/test-check.h"
#include <cstdint>
#include <limits>

int main() {
	using namespace RuffnecKk::RepairCostsCap;

	TEST_REQUIRE(IsValidMaximumGold(0));
	TEST_REQUIRE(IsValidMaximumGold(MaximumGoldLimit));
	TEST_REQUIRE(!IsValidMaximumGold(-1));
	TEST_REQUIRE(!IsValidMaximumGold(MaximumGoldLimit + 1));
	TEST_REQUIRE(IsValidChance(0.0));
	TEST_REQUIRE(IsValidChance(0.10));
	TEST_REQUIRE(IsValidChance(1.0));
	TEST_REQUIRE(!IsValidChance(-0.01));
	TEST_REQUIRE(!IsValidChance(1.01));
	TEST_REQUIRE(ChanceToBasisPoints(0.0) == 0);
	TEST_REQUIRE(ChanceToBasisPoints(0.10) == 1'000);
	TEST_REQUIRE(ChanceToBasisPoints(1.0) == ChanceBasisPointScale);

	const RepairPolicy vanilla{
		.enabled = true,
		.maximumGold = std::numeric_limits<std::int32_t>::max(),
		.durabilityWearEnabled = true,
		.durabilityWearChance = 0.0,
	};
	TEST_REQUIRE(ApplyRepairCostCap(50'000, RepairTransactionType, vanilla) == 50'000);
	TEST_REQUIRE(ApplyRepairAllCap(30'000, vanilla) == 30'000);
	TEST_REQUIRE(!ShouldLoseMaximumDurability(true, vanilla.durabilityWearChance, 0));

	RepairPolicy policy{
		.enabled = true,
		.maximumGold = 5'000,
		.durabilityWearEnabled = true,
		.durabilityWearChance = 0.10,
	};
	TEST_REQUIRE(IsValidPolicy(policy));

	TEST_REQUIRE(ApplyRepairCostCap(2'500, RepairTransactionType, policy) == 2'500);
	TEST_REQUIRE(ApplyRepairCostCap(50'000, RepairTransactionType, policy) == 5'000);
	TEST_REQUIRE(ApplyRepairCostCap(10'000, 0, policy) == 10'000);
	TEST_REQUIRE(ApplyRepairCostCap(0, RepairTransactionType, policy) == 0);
	TEST_REQUIRE(ApplyRepairCostCap(-1, RepairTransactionType, policy) == -1);
	TEST_REQUIRE(ApplyRepairCostCap(
		std::numeric_limits<std::int32_t>::max(),
		RepairTransactionType,
		policy
	) == std::numeric_limits<std::int32_t>::max());

	TEST_REQUIRE(ApplyRepairAllCap(2'500, policy) == 2'500);
	TEST_REQUIRE(ApplyRepairAllCap(30'000, policy) == 5'000);
	TEST_REQUIRE(ApplyRepairAllCap(0, policy) == 0);
	TEST_REQUIRE(ApplyRepairAllCap(
		std::numeric_limits<std::int32_t>::max(),
		policy
	) == std::numeric_limits<std::int32_t>::max());

	policy.enabled = false;
	TEST_REQUIRE(ApplyRepairCostCap(50'000, RepairTransactionType, policy) == 50'000);
	TEST_REQUIRE(ApplyRepairAllCap(30'000, policy) == 30'000);
	policy.enabled = true;

	auto free = policy;
	free.maximumGold = 0;
	TEST_REQUIRE(ApplyRepairCostCap(50'000, RepairTransactionType, free) == 0);
	TEST_REQUIRE(ApplyRepairCostCap(50'000, 0, free) == 50'000);
	TEST_REQUIRE(ApplyRepairCostCap(0, RepairTransactionType, free) == 0);
	TEST_REQUIRE(ApplyRepairCostCap(
		std::numeric_limits<std::int32_t>::max(),
		RepairTransactionType,
		free
	) == std::numeric_limits<std::int32_t>::max());
	TEST_REQUIRE(ApplyRepairAllCap(30'000, free) == 0);
	TEST_REQUIRE(ApplyRepairAllCap(0, free) == 0);

	auto invalid = policy;
	invalid.maximumGold = -1;
	TEST_REQUIRE(!IsValidPolicy(invalid));
	TEST_REQUIRE(ApplyRepairCostCap(50'000, RepairTransactionType, invalid) == 50'000);
	TEST_REQUIRE(ApplyRepairAllCap(30'000, invalid) == 30'000);

	TEST_REQUIRE(GoldReduction(50'000, 5'000) == 45'000);
	TEST_REQUIRE(GoldReduction(5'000, 5'000) == 0);
	TEST_REQUIRE(GoldReduction(5'000, 10'000) == 0);
	TEST_REQUIRE(GoldReduction(-1, 0) == 0);

	TEST_REQUIRE(IsPhysicalRepairCandidate(0, 20));
	TEST_REQUIRE(IsPhysicalRepairCandidate(19, 20));
	TEST_REQUIRE(!IsPhysicalRepairCandidate(20, 20));
	TEST_REQUIRE(!IsPhysicalRepairCandidate(0, 1));
	TEST_REQUIRE(!IsPhysicalRepairCandidate(-1, 20));
	TEST_REQUIRE(DidPhysicalRepairSucceed(10, 20, 20));
	TEST_REQUIRE(!DidPhysicalRepairSucceed(10, 20, 19));
	TEST_REQUIRE(!DidPhysicalRepairSucceed(20, 20, 20));

	TEST_REQUIRE(!ShouldLoseMaximumDurability(false, 1.0, 0));
	TEST_REQUIRE(!ShouldLoseMaximumDurability(true, 0.0, 0));
	TEST_REQUIRE(ShouldLoseMaximumDurability(true, 0.10, 0));
	TEST_REQUIRE(ShouldLoseMaximumDurability(true, 0.10, 999));
	TEST_REQUIRE(!ShouldLoseMaximumDurability(true, 0.10, 1'000));
	TEST_REQUIRE(ShouldLoseMaximumDurability(true, 1.0, 9'999));
	TEST_REQUIRE(!ShouldLoseMaximumDurability(true, 1.0, 10'000));
	TEST_REQUIRE(ReducedMaximumDurability(20) == 19);
	TEST_REQUIRE(ReducedMaximumDurability(2) == 1);
	TEST_REQUIRE(ReducedMaximumDurability(1) == 1);
	return 0;
}
