#include "enhanced-damage-min-max-fix-policy.h"

#include <json.hpp>

#include "../../../tests/test-check.h"
#include <fstream>
#include <stdexcept>

using namespace RuffnecKk::EnhancedDamageMinMaxFix;

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
	static_assert(PackStat(ItemMaxDamagePercentStat) == 0x00110000);
	static_assert(PackStat(ItemMinDamagePercentStat) == 0x00120000);
	static_assert(PackStat(ItemMaxDamagePercentStat, 1) == 0x00110001);
	static_assert(IsEnhancedDamagePackedStat(0x00110000));
	static_assert(IsEnhancedDamagePackedStat(0x00120000));
	static_assert(!IsEnhancedDamagePackedStat(0x00110001));
	static_assert(!IsEnhancedDamagePackedStat(17));

	TEST_REQUIRE(!ParseConfig(nlohmann::json::object()).enabled);
	TEST_REQUIRE(!ParseConfig(nlohmann::json::parse(
		R"json({"enhancedDamageMinMaxFix":{"enabled":false}})json")).enabled);
	TEST_REQUIRE(ParseConfig(nlohmann::json::parse(
		R"json({"enhancedDamageMinMaxFix":{"enabled":true}})json")).enabled);

	ExpectInvalid([] { ParseConfig(nlohmann::json::array()); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"enhancedDamageMinMaxFix":true})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"enhancedDamageMinMaxFix":{}})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"enhancedDamageMinMaxFix":{"enabled":1}})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"enhancedDamageMinMaxFix":{"enabled":false,"extra":false}})json")); });

	TEST_REQUIRE(ShouldRestoreSuppressedUpdate(
		ItemUnitType,
		AddItemStatPercentOperation,
		PackStat(ItemMaxDamagePercentStat),
		false,
		510,
		0));
	TEST_REQUIRE(ShouldRestoreSuppressedUpdate(
		ItemUnitType,
		AddItemStatPercentOperation,
		PackStat(ItemMinDamagePercentStat),
		false,
		505,
		500));
	TEST_REQUIRE(!ShouldRestoreSuppressedUpdate(
		ItemUnitType,
		AddItemStatPercentOperation,
		PackStat(ItemMaxDamagePercentStat),
		true,
		510,
		0));
	TEST_REQUIRE(!ShouldRestoreSuppressedUpdate(
		ItemUnitType,
		AddItemStatPercentOperation,
		PackStat(ItemMaxDamagePercentStat),
		false,
		500,
		500));
	TEST_REQUIRE(!ShouldRestoreSuppressedUpdate(
		0,
		AddItemStatPercentOperation,
		PackStat(ItemMaxDamagePercentStat),
		false,
		510,
		0));
	TEST_REQUIRE(!ShouldRestoreSuppressedUpdate(
		ItemUnitType,
		12,
		PackStat(ItemMaxDamagePercentStat),
		false,
		510,
		0));

	if (argc == 2) {
		std::ifstream stream(argv[1], std::ios::binary);
		TEST_REQUIRE(stream.good());
		const auto templateConfig = nlohmann::json::parse(
			stream, nullptr, true, true);
		TEST_REQUIRE(ParseConfig(templateConfig.at("items")).enabled);
	}

	return 0;
}
