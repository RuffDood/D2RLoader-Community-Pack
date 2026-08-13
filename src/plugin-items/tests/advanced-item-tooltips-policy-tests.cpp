#include "../advanced-item-tooltips-policy.h"

#include "../../../tests/test-check.h"

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

int main() {
    using namespace RuffnecKk::AdvancedTooltips;

    const auto missing = ParseConfig(nlohmann::json::object());
    TEST_REQUIRE(!missing.enabled);
    TEST_REQUIRE(missing.rangeDisplayMode == RangeDisplayMode::HoldHotkey);
    TEST_REQUIRE(missing.holdToDisplayHotkey.name == "Shift");
    TEST_REQUIRE(missing.holdToDisplayHotkey.virtualKey == 0x10);
    TEST_REQUIRE(!ShouldDisplayRanges(missing.rangeDisplayMode, false));
    TEST_REQUIRE(ShouldDisplayRanges(missing.rangeDisplayMode, true));

    const auto configured = ParseConfig(nlohmann::json::parse(R"json({
        "advancedTooltips": {
            "enabled": true,
            "showMaxSockets": false,
            "showMaxSocketsOnSocketedItems": true,
            "showBaseDefenseRange": false,
            "showPropertyRanges": false,
            "includeSocketedContributionsInRanges": true,
            "propertyRangeColor": "BHDarkGreen",
            "rangeDisplayMode": "Always",
            "holdToDisplayHotkey": "F12"
        }
    })json"));
    TEST_REQUIRE(configured.enabled);
    TEST_REQUIRE(!configured.showMaxSockets);
    TEST_REQUIRE(configured.showMaxSocketsOnSocketedItems);
    TEST_REQUIRE(!configured.showBaseDefenseRange);
    TEST_REQUIRE(!configured.showPropertyRanges);
    TEST_REQUIRE(configured.includeSocketedContributionsInRanges);
    TEST_REQUIRE(configured.propertyRangeColor == PropertyRangeColor::BHDarkGreen);
    TEST_REQUIRE(configured.rangeDisplayMode == RangeDisplayMode::Always);
    TEST_REQUIRE(configured.holdToDisplayHotkey.name == "F12");
    TEST_REQUIRE(configured.holdToDisplayHotkey.virtualKey == 0x7B);

    const auto legacy = ParseConfig(nlohmann::json::parse(
        R"json({"advancedTooltips":{"rangeDisplayMode":"HoldShift"}})json"));
    TEST_REQUIRE(legacy.rangeDisplayMode == RangeDisplayMode::HoldHotkey);
    TEST_REQUIRE(legacy.holdToDisplayHotkey.name == "Shift");

    TEST_REQUIRE(Throws([] { ParseConfig(nlohmann::json::parse(
        R"json({"advancedTooltips":true})json")); }));
    TEST_REQUIRE(Throws([] { ParseConfig(nlohmann::json::parse(
        R"json({"advancedTooltips":{"enabled":1}})json")); }));
    TEST_REQUIRE(Throws([] { ParseConfig(nlohmann::json::parse(
        R"json({"advancedTooltips":{"holdToDisplayHotkey":"Space"}})json")); }));
    return 0;
}
