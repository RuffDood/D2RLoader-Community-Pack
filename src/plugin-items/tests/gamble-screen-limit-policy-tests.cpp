#include "gamble-screen-limit-policy.h"

#include "../../../tests/test-check.h"
#include <fstream>
#include <stdexcept>

namespace {

template<class Callback>
bool Throws(Callback&& callback) {
	try {
		callback();
	} catch (const std::exception&) {
		return true;
	}
	return false;
}

} // namespace

int main(int argc, char** argv) {
	using namespace RuffnecKk::GambleScreenLimit;

	const auto missing = ParseConfig(nlohmann::json::object());
	TEST_REQUIRE(!missing.enabled);
	TEST_REQUIRE(EffectiveLimit(missing) == VanillaLimit);

	const auto disabled = ParseConfig(nlohmann::json::parse(
		R"json({"gambleScreenLimit":{"enabled":false}})json"
	));
	TEST_REQUIRE(!disabled.enabled);
	TEST_REQUIRE(EffectiveLimit(disabled) == VanillaLimit);

	const auto enabled = ParseConfig(nlohmann::json::parse(
		R"json({"gambleScreenLimit":{"enabled":true}})json"
	));
	TEST_REQUIRE(enabled.enabled);
	TEST_REQUIRE(EffectiveLimit(enabled) == ExpandedLimit);

	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"gambleScreenLimit":true})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"gambleScreenLimit":{"enabled":1}})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"gambleScreenLimit":{"enabled":true,"itemLimit":64}})json"
		));
	}));

	TEST_REQUIRE(argc == 2);
	std::ifstream shippedConfig(argv[1]);
	TEST_REQUIRE(shippedConfig.is_open());
	const auto root = nlohmann::json::parse(shippedConfig, nullptr, true, true);
	const auto shipped = ParseConfig(root.at("items"));
	TEST_REQUIRE(!shipped.enabled);
	TEST_REQUIRE(EffectiveLimit(shipped) == VanillaLimit);
	return 0;
}
