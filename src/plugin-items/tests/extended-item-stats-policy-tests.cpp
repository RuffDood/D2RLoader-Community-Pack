#include "extended-item-stats-policy.h"

#include "../../../tests/test-check.h"
#include <fstream>
#include <json.hpp>

using namespace RuffnecKk::ExtendedItemStats;

int main(int argc, char** argv) {
	TEST_REQUIRE(ItemTransportEnabled);
	TEST_REQUIRE(ScrollBarEnabledByDefault);
	const char owner[] = "tooltip";
	const char duplicate[] = "tooltip";
	TEST_REQUIRE(!ShouldSuppressSecondaryNativeTooltip(
		false, true, owner, duplicate));
	TEST_REQUIRE(!ShouldSuppressSecondaryNativeTooltip(
		true, false, owner, duplicate));
	TEST_REQUIRE(!ShouldSuppressSecondaryNativeTooltip(
		true, true, nullptr, duplicate));
	TEST_REQUIRE(!ShouldSuppressSecondaryNativeTooltip(
		true, true, owner, owner));
	TEST_REQUIRE(ShouldSuppressSecondaryNativeTooltip(
		true, true, owner, duplicate));

	TEST_REQUIRE(argc == 2);
	std::ifstream stream(argv[1], std::ios::binary);
	TEST_REQUIRE(stream.good());
	const auto root = nlohmann::json::parse(stream, nullptr, true, true);
	TEST_REQUIRE(root.at("items").is_object());
	TEST_REQUIRE(!root.at("items").contains("extendedItemStats"));
	return 0;
}
