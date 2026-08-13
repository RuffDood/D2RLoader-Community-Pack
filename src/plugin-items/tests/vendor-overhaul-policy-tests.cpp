#include "items-private.h"

#include "../../../tests/test-check.h"
#include <cstdint>

int main()
{
	TEST_REQUIRE(PSh_Items_IsValidVendorLevelScale(0));
	TEST_REQUIRE(PSh_Items_IsValidVendorLevelScale(1));
	TEST_REQUIRE(PSh_Items_IsValidVendorLevelScale(16));
	TEST_REQUIRE(PSh_Items_IsValidVendorLevelScale(128));
	TEST_REQUIRE(!PSh_Items_IsValidVendorLevelScale(-1));
	TEST_REQUIRE(!PSh_Items_IsValidVendorLevelScale(3));
	TEST_REQUIRE(!PSh_Items_IsValidVendorLevelScale(127));

	TEST_REQUIRE(!PSh_Items_ShouldGenerateRareVendorItem(false, 0, 1024));
	TEST_REQUIRE(!PSh_Items_ShouldGenerateRareVendorItem(true, 0, 0));
	TEST_REQUIRE(PSh_Items_ShouldGenerateRareVendorItem(true, 0, 1024));
	TEST_REQUIRE(!PSh_Items_ShouldGenerateRareVendorItem(true, 1, 1024));
	TEST_REQUIRE(!PSh_Items_ShouldGenerateRareVendorItem(true, 1023, 1024));
	TEST_REQUIRE(PSh_Items_ShouldGenerateRareVendorItem(true, 1024, 1024));
	TEST_REQUIRE(PSh_Items_ShouldGenerateRareVendorItem(true, 17, 1));

	return 0;
}
