#include "qty-display-issue-policy.h"

#include <algorithm>
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
	using namespace RuffnecKk::QtyDisplayIssue;

	const auto missing = ParseConfig(nlohmann::json::object());
	TEST_REQUIRE(!missing.enabled);

	const auto disabled = ParseConfig(nlohmann::json::parse(
		R"json({"qtyDisplayIssue":{"enabled":false}})json"
	));
	TEST_REQUIRE(!disabled.enabled);

	const auto enabled = ParseConfig(nlohmann::json::parse(
		R"json({"qtyDisplayIssue":{"enabled":true}})json"
	));
	TEST_REQUIRE(enabled.enabled);

	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"qtyDisplayIssue":true})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"qtyDisplayIssue":{"enabled":1}})json"
		));
	}));
	TEST_REQUIRE(Throws([] {
		ParseConfig(nlohmann::json::parse(
			R"json({"qtyDisplayIssue":{"enabled":true,"format":"custom"}})json"
		));
	}));

	const auto replacement = BuildQuantitySuppressionPatch();
	static_assert(QuantitySuppressionSignatureSize == 33);
	static_assert(QuantitySuppressionBranchOffset == 21);
	TEST_REQUIRE(QuantitySuppressionExpected[QuantitySuppressionBranchOffset] == 0x75);
	TEST_REQUIRE(QuantitySuppressionExpected[QuantitySuppressionBranchOffset + 1] == 0x0F);
	TEST_REQUIRE(replacement[QuantitySuppressionBranchOffset] == 0x90);
	TEST_REQUIRE(replacement[QuantitySuppressionBranchOffset + 1] == 0x90);
	const auto changedBytes = std::count_if(
		QuantitySuppressionExpected.begin(),
		QuantitySuppressionExpected.end(),
		[&, index = std::size_t{}](std::uint8_t byte) mutable {
			return byte != replacement[index++];
		}
	);
	TEST_REQUIRE(changedBytes == 2);

	TEST_REQUIRE(argc == 2);
	std::ifstream shippedConfig(argv[1]);
	TEST_REQUIRE(shippedConfig.is_open());
	const auto root = nlohmann::json::parse(shippedConfig, nullptr, true, true);
	const auto shipped = ParseConfig(root.at("items"));
	TEST_REQUIRE(shipped.enabled);
	return 0;
}
