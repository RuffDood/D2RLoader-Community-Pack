#include "../magic-find-formula-policy.h"

#include <json.hpp>

#include "../../../tests/test-check.h"
#include <fstream>
#include <stdexcept>

using namespace RuffnecKk::MagicFindFormula;

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
	TEST_REQUIRE(ParseConfig(nlohmann::json::object()).mode == Mode::Vanilla);
	TEST_REQUIRE(ParseConfig(nlohmann::json::parse(
		R"json({"otherItemFeature":true})json")).mode == Mode::Vanilla);
	TEST_REQUIRE(ParseConfig(nlohmann::json::parse(
		R"json({"magicFindFormula":{}})json")).mode == Mode::Vanilla);
	TEST_REQUIRE(ParseConfig(nlohmann::json::parse(
		R"json({"magicFindFormula":{"mode":"vanilla"}})json")).mode == Mode::Vanilla);
	const auto linear = ParseConfig(nlohmann::json::parse(
		R"json({"magicFindFormula":{"mode":"linear"}})json"));
	TEST_REQUIRE(IsLinear(linear));
	TEST_REQUIRE(ModeName(linear.mode) == "linear");

	ExpectInvalid([] { ParseConfig(nlohmann::json::array()); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"magicFindFormula":true})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"magicFindFormula":{"mode":1}})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"magicFindFormula":{"mode":"legacy"}})json")); });
	ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
		R"json({"magicFindFormula":{"mode":"linear","extra":true}})json")); });

	if (argc == 2) {
		std::ifstream stream(argv[1], std::ios::binary);
		TEST_REQUIRE(stream.good());
		const auto templateConfig = nlohmann::json::parse(
			stream, nullptr, true, true);
		TEST_REQUIRE(ParseConfig(templateConfig.at("items")).mode == Mode::Vanilla);
	}
	return 0;
}
