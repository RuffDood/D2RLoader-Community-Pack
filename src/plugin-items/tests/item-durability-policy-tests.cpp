#include "item-durability-policy.h"

#include "../../../tests/test-check.h"
#include <exception>
#include <fstream>

using namespace RuffnecKk::ItemDurability;

template<class Callback>
void ExpectInvalid(Callback&& callback) {
	bool rejected = false;
	try {
		callback();
	} catch (const std::exception&) {
		rejected = true;
	}
	TEST_REQUIRE(rejected);
}

int main(int argc, char** argv) {
	static_assert(IsBowOrCrossbowItemTypeCode(PackItemTypeCode('b', 'o', 'w')));
	static_assert(IsBowOrCrossbowItemTypeCode(PackItemTypeCode('x', 'b', 'o', 'w')));
	static_assert(!IsBowOrCrossbowItemTypeCode(PackItemTypeCode('a', 'b', 'o', 'w')));

	TEST_REQUIRE(!PreventsLoss(0, 0));
	TEST_REQUIRE(PreventsLoss(50, 49));
	TEST_REQUIRE(!PreventsLoss(50, 50));
	TEST_REQUIRE(PreventsLoss(100, 99));
	TEST_REQUIRE(EffectiveChanceBasisPoints(4, 0) == 400);
	TEST_REQUIRE(EffectiveChanceBasisPoints(4, 50) == 200);
	TEST_REQUIRE(EffectiveChanceBasisPoints(10, 75) == 250);
	TEST_REQUIRE(EffectiveChanceBasisPoints(10, 100) == 0);

	TEST_REQUIRE(TargetEtherealMaxDurability(20, 25) == 6);
	TEST_REQUIRE(TargetEtherealMaxDurability(20, 50) == 11);
	TEST_REQUIRE(TargetEtherealMaxDurability(20, 75) == 16);
	TEST_REQUIRE(ApplyVanillaEtherealHalving(
		EncodeForVanillaEtherealHalving(20, 50)) == 11);
	TEST_REQUIRE(TargetEtherealMaxDurability(30, 100) == 30);
	TEST_REQUIRE(TargetEtherealMaxDurability(20, 200) == 40);
	TEST_REQUIRE(TargetEtherealMaxDurability(500, 200) == 255);
	TEST_REQUIRE(ApplyVanillaEtherealHalving(EncodeEtherealMaximumTarget(255)) == 255);

	const auto absent = ParseConfig(nlohmann::json::object());
	TEST_REQUIRE(!absent.enabled);
	TEST_REQUIRE(absent.etherealMaximumPercent == 50);

	const auto vanilla = nlohmann::json::parse(R"json({
		"itemDurability": {
			"enabled": false,
			"normalResistancePercent": 0,
			"etherealResistancePercent": 0,
			"etherealMaximumPercent": 50,
			"forceMaximumDurability": false,
			"bowsAndCrossbowsHaveDurability": false
		}
	})json");
	const auto policy = ParseConfig(vanilla);
	TEST_REQUIRE(!policy.enabled);
	TEST_REQUIRE(policy.normalResistancePercent == 0);
	TEST_REQUIRE(policy.etherealResistancePercent == 0);
	TEST_REQUIRE(policy.etherealMaximumPercent == 50);
	TEST_REQUIRE(!policy.forceMaximumDurability);
	TEST_REQUIRE(!policy.bowsAndCrossbowsHaveDurability);

	ExpectInvalid([] { ParseConfig(nlohmann::json::array()); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"itemDurability":true})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"itemDurability":{"enabled":false}})json")); });
	ExpectInvalid([&] {
		auto invalid = vanilla;
		invalid["itemDurability"]["normalResistancePercent"] = 101;
		ParseConfig(invalid);
	});
	ExpectInvalid([&] {
		auto invalid = vanilla;
		invalid["itemDurability"]["etherealResistancePercent"] = -1;
		ParseConfig(invalid);
	});
	ExpectInvalid([&] {
		auto invalid = vanilla;
		invalid["itemDurability"]["etherealMaximumPercent"] = 0;
		ParseConfig(invalid);
	});
	ExpectInvalid([&] {
		auto invalid = vanilla;
		invalid["itemDurability"]["enabled"] = 1;
		ParseConfig(invalid);
	});
	ExpectInvalid([&] {
		auto invalid = vanilla;
		invalid["itemDurability"]["extra"] = false;
		ParseConfig(invalid);
	});

	if (argc == 2) {
		std::ifstream stream(argv[1], std::ios::binary);
		TEST_REQUIRE(stream.good());
		const auto templateConfig = nlohmann::json::parse(
			stream, nullptr, true, true);
		const auto templatePolicy = ParseConfig(templateConfig.at("items"));
		TEST_REQUIRE(!templatePolicy.enabled);
		TEST_REQUIRE(templatePolicy.normalResistancePercent == 0);
		TEST_REQUIRE(templatePolicy.etherealResistancePercent == 0);
		TEST_REQUIRE(templatePolicy.etherealMaximumPercent == 50);
		TEST_REQUIRE(!templatePolicy.forceMaximumDurability);
		TEST_REQUIRE(!templatePolicy.bowsAndCrossbowsHaveDurability);
	}

	return 0;
}
