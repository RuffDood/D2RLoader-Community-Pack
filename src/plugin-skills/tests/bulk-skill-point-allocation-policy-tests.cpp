#include "bulk-skill-point-allocation-policy.h"

#include <json.hpp>

#include "../../../tests/test-check.h"
#include <fstream>
#include <stdexcept>

using namespace RuffnecKk::BulkSkillPointAllocation;

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
	static_assert(ResolveMode(false, false) == AllocationMode::Single);
	static_assert(ResolveMode(false, true) == AllocationMode::CtrlBatch);
	static_assert(ResolveMode(true, false) == AllocationMode::ShiftAll);
	static_assert(ResolveMode(true, true) == AllocationMode::CtrlBatch);
	static_assert(NativeSkillPacketExtra(AllocationMode::Single, 1) == 0);
	static_assert(NativeSkillPacketExtra(AllocationMode::CtrlBatch, 5) == 4);
	static_assert(NativeSkillPacketExtra(AllocationMode::CtrlBatch, 1'000) == 999);
	static_assert(NativeSkillPacketExtra(AllocationMode::ShiftAll, 1) == 0xFFFF);
	TEST_REQUIRE(!IsUsableLocalizedString(nullptr, "shiftConfirmation", "Missing string"));
	TEST_REQUIRE(!IsUsableLocalizedString("", "shiftConfirmation", "Missing string"));
	TEST_REQUIRE(!IsUsableLocalizedString(
		"shiftConfirmation", "shiftConfirmation", "Missing string"));
	TEST_REQUIRE(!IsUsableLocalizedString(
		"Missing string", "shiftConfirmation", "Missing string"));
	TEST_REQUIRE(!IsUsableLocalizedString(
		"없는 문자열", "shiftConfirmation", "없는 문자열"));
	TEST_REQUIRE(IsUsableLocalizedString(
		"Invest all skill points?", "shiftConfirmation", "Missing string"));
	TEST_REQUIRE(IsUsableLocalizedString(
		"모든 스킬 포인트를 투자하시겠습니까?",
		"shiftConfirmation",
		"없는 문자열"));

	const auto absent = ParseConfig(nlohmann::json::object());
	TEST_REQUIRE(!absent.enabled);
	TEST_REQUIRE(absent.skillPointsPerCtrlClick == 5);
	TEST_REQUIRE(!absent.confirmShiftAllocation);

	const auto enabled = ParseConfig(nlohmann::json::parse(R"json({
		"bulkSkillPointAllocation": {
			"enabled": true,
			"skillPointsPerCtrlClick": 25,
			"confirmShiftAllocation": true,
			"shiftConfirmationKey": "customKey",
			"shiftConfirmationFallback": "Custom fallback"
		}
	})json"));
	TEST_REQUIRE(enabled.enabled);
	TEST_REQUIRE(enabled.skillPointsPerCtrlClick == 25);
	TEST_REQUIRE(enabled.confirmShiftAllocation);
	TEST_REQUIRE(enabled.shiftConfirmationKey == "customKey");
	TEST_REQUIRE(enabled.shiftConfirmationFallback == "Custom fallback");

	ExpectInvalid([] { ParseConfig(nlohmann::json::array()); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"bulkSkillPointAllocation":true})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"bulkSkillPointAllocation":{}})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"bulkSkillPointAllocation":{"enabled":false,"skillPointsPerCtrlClick":0}})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"bulkSkillPointAllocation":{"enabled":false,"skillPointsPerCtrlClick":1001}})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"bulkSkillPointAllocation":{"enabled":false,"extra":false}})json")); });

	if (argc == 2) {
		std::ifstream stream(argv[1], std::ios::binary);
		TEST_REQUIRE(stream.good());
		const auto templateConfig = nlohmann::json::parse(
			stream, nullptr, true, true);
		const auto policy = ParseConfig(templateConfig.at("skills"));
		TEST_REQUIRE(!policy.enabled);
		TEST_REQUIRE(policy.skillPointsPerCtrlClick == 5);
		TEST_REQUIRE(!policy.confirmShiftAllocation);
	}

	return 0;
}
