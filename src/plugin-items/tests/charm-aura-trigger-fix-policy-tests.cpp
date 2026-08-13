#include "charm-aura-trigger-fix-policy.h"

#include "../../../tests/test-check.h"
#include <exception>
#include <fstream>

using namespace RuffnecKk::CharmAuraTriggerFix;

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
	TEST_REQUIRE(IsEligible(true, 3, 0x10));
	TEST_REQUIRE(IsEligible(true, 3, 0x30));
	TEST_REQUIRE(!IsEligible(false, 3, 0x10));
	TEST_REQUIRE(!IsEligible(true, 6, 0x10));
	TEST_REQUIRE(!IsEligible(true, 7, 0x10));
	TEST_REQUIRE(!IsEligible(true, 3, 0));

	constexpr PackedStatRecord stats[]{
		{97u << 16U | 42u, 1},
		{151u << 16U | 99u, 12},
		{151u << 16U | 100u, 0},
	};
	static_assert(HasNonzeroStat(stats, 3, 97));
	static_assert(HasNonzeroStat(stats, 3, 151));
	static_assert(!HasNonzeroStat(stats, 3, 150));
	static_assert(!HasNonzeroStat(nullptr, 3, 151));

	const auto absent = ParseConfig(nlohmann::json::object());
	TEST_REQUIRE(!absent.enabled);

	const auto vanilla = nlohmann::json::parse(R"json({
		"charmAuraTriggerFix": {
			"enabled": false
		}
	})json");
	const auto policy = ParseConfig(vanilla);
	TEST_REQUIRE(!policy.enabled);

	ExpectInvalid([] { ParseConfig(nlohmann::json::array()); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"charmAuraTriggerFix":true})json")); });
	ExpectInvalid([&] {
		auto invalid = vanilla;
		invalid["charmAuraTriggerFix"]["enabled"] = 0;
		ParseConfig(invalid);
	});
	ExpectInvalid([&] {
		auto invalid = vanilla;
		invalid["charmAuraTriggerFix"]["extra"] = false;
		ParseConfig(invalid);
	});

	if (argc == 2) {
		std::ifstream stream(argv[1], std::ios::binary);
		TEST_REQUIRE(stream.good());
		const auto templateConfig = nlohmann::json::parse(
			stream, nullptr, true, true);
		const auto templatePolicy = ParseConfig(templateConfig.at("items"));
		TEST_REQUIRE(templatePolicy.enabled);
	}

	return 0;
}
