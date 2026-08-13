#include "items-ethereal-policy.h"

#include <array>
#include "../../../tests/test-check.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {
struct Record {
	std::array<char, 4> code{};
	std::array<
		std::uint8_t,
		ruffneckk::plugin_items::ethereal::ItemTypeRecordStride - 4
	> padding{};
};
static_assert(sizeof(Record) == ruffneckk::plugin_items::ethereal::ItemTypeRecordStride);

template<class Callback>
bool Throws(Callback&& callback) {
	try {
		callback();
	} catch (const std::exception&) {
		return true;
	}
	return false;
}
}

int main(int argc, char** argv) {
	using namespace ruffneckk::plugin_items::ethereal;

	ItemTypeCode belt{};
	TEST_REQUIRE(NormalizeItemTypeCode(" BeLt ", belt));
	TEST_REQUIRE(belt.text[0] == 'b' && belt.text[3] == 't');

	ItemTypeCode gem{};
	TEST_REQUIRE(NormalizeItemTypeCode("gem", gem));
	TEST_REQUIRE(gem.bytes[3] == ' ');

	ItemTypeCode invalid{};
	TEST_REQUIRE(!NormalizeItemTypeCode("too-long", invalid));
	TEST_REQUIRE(!NormalizeItemTypeCode("a-b", invalid));

	std::array<Record, 3> records{};
	std::memcpy(records[0].code.data(), "armo", 4);
	std::memcpy(records[1].code.data(), "belt", 4);
	std::memcpy(records[2].code.data(), "gem ", 4);
	TEST_REQUIRE(FindItemTypeId(records.data(), records.size(), sizeof(Record), belt) == 1);
	TEST_REQUIRE(FindItemTypeId(records.data(), records.size(), sizeof(Record), gem) == 2);
	TEST_REQUIRE(FindItemTypeId(nullptr, records.size(), sizeof(Record), belt) == -1);
	TEST_REQUIRE(FindItemTypeId(records.data(), 4097, sizeof(Record), belt) == -1);

	const auto defaults = ParseConfig(nlohmann::json::object());
	TEST_REQUIRE(!defaults.enabled);
	TEST_REQUIRE(defaults.excludedItemTypeCount == 0);
	TEST_REQUIRE(defaults.chancePercent == VanillaChancePercent);
	TEST_REQUIRE(!HasExcludedItemTypes(defaults));
	TEST_REQUIRE(!HasDirectRulePatches(defaults));

	const auto configured = ParseConfig(nlohmann::json::parse(R"json(
		{
		  "magicItemsSpawnIdentified": false,
		  "etherealItemRules": {
			"enabled": true,
			"excludedItemTypes": ["belt", "BELT", "armo"],
			"chancePercent": 6,
			"allowSetItems": true,
			"allowIndestructibleItems": true
		  }
		}
	)json"));
	TEST_REQUIRE(configured.enabled);
	TEST_REQUIRE(configured.excludedItemTypeCount == 2);
	TEST_REQUIRE(configured.chancePercent == 6);
	TEST_REQUIRE(HasExcludedItemTypes(configured));
	TEST_REQUIRE(PatchChance(configured));
	TEST_REQUIRE(PatchSetItems(configured));
	TEST_REQUIRE(PatchIndestructibleItems(configured));
	TEST_REQUIRE(HasDirectRulePatches(configured));

	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"etherealExclusions":{"enabled":true}})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"etherealItemRules":{"enabled":true,"extra":1}})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"etherealItemRules":{"excludedItemTypes":["too-long"]}})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"etherealItemRules":{"chancePercent":-1}})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"etherealItemRules":{"chancePercent":101}})json"
		));
	}));

	TEST_REQUIRE(argc == 2);
	std::ifstream shippedConfig(argv[1]);
	TEST_REQUIRE(shippedConfig.is_open());
	const auto root = nlohmann::json::parse(shippedConfig, nullptr, true, true);
	TEST_REQUIRE(root.at("skills").at("selfHealParams").is_boolean());
	TEST_REQUIRE(root.at("skills").at("selfHealParams").get<bool>());
	const auto shipped = ParseConfig(root.at("items"));
	TEST_REQUIRE(!shipped.enabled);
	TEST_REQUIRE(shipped.excludedItemTypeCount == 0);
	TEST_REQUIRE(shipped.chancePercent == VanillaChancePercent);
	TEST_REQUIRE(!shipped.allowSetItems);
	TEST_REQUIRE(!shipped.allowIndestructibleItems);
	TEST_REQUIRE(!HasExcludedItemTypes(shipped));
	TEST_REQUIRE(!HasDirectRulePatches(shipped));
	return 0;
}
