#include "ground-item-label-limit-policy.h"

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
	using namespace RuffnecKk::GroundItemLabelLimit;

	TEST_REQUIRE(!IsSupportedLimit(32));
	TEST_REQUIRE(IsSupportedLimit(64));
	TEST_REQUIRE(IsSupportedLimit(128));
	TEST_REQUIRE(!IsSupportedLimit(256));
	TEST_REQUIRE(LabelArrayByteOffset(32) == 0x2880);
	TEST_REQUIRE(LabelArrayByteOffset(64) == 0x5100);
	TEST_REQUIRE(LabelArrayByteOffset(128) == 0xA200);

	const auto missing = ParseConfig(nlohmann::json::object());
	TEST_REQUIRE(!missing.enabled);
	TEST_REQUIRE(missing.limit == DefaultExpandedLimit);
	TEST_REQUIRE(EffectiveLimit(missing) == VanillaLimit);

	const auto disabled = ParseConfig(nlohmann::json::parse(
		R"json({"groundItemLabels":{"enabled":false,"limit":64}})json"
	));
	TEST_REQUIRE(!disabled.enabled);
	TEST_REQUIRE(EffectiveLimit(disabled) == VanillaLimit);

	const auto enabled64 = ParseConfig(nlohmann::json::parse(
		R"json({"groundItemLabels":{"enabled":true,"limit":64}})json"
	));
	TEST_REQUIRE(enabled64.enabled);
	TEST_REQUIRE(EffectiveLimit(enabled64) == 64);

	const auto enabled128 = ParseConfig(nlohmann::json::parse(
		R"json({"groundItemLabels":{"enabled":true,"limit":128}})json"
	));
	TEST_REQUIRE(enabled128.enabled);
	TEST_REQUIRE(EffectiveLimit(enabled128) == 128);

	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(R"json({"groundItemLabels":true})json"));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"groundItemLabels":{"enabled":1,"limit":64}})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"groundItemLabels":{"enabled":true,"limit":32}})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"groundItemLabels":{"enabled":true,"limit":256}})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"groundItemLabels":{"enabled":true,"limit":64,"extra":false}})json"
		));
	}));

	TEST_REQUIRE(argc == 2);
	std::ifstream shippedConfig(argv[1]);
	TEST_REQUIRE(shippedConfig.is_open());
	const auto root = nlohmann::json::parse(shippedConfig, nullptr, true, true);
	const auto shipped = ParseConfig(root.at("items"));
	TEST_REQUIRE(!shipped.enabled);
	TEST_REQUIRE(shipped.limit == DefaultExpandedLimit);
	TEST_REQUIRE(EffectiveLimit(shipped) == VanillaLimit);
	return 0;
}
