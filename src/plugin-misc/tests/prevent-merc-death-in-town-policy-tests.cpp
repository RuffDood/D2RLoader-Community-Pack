#include "prevent-merc-death-in-town-policy.h"

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
    using namespace RuffnecKk::PreventMercDeathInTown;

    TEST_REQUIRE(IsHirelingClass(271));
    TEST_REQUIRE(IsHirelingClass(338));
    TEST_REQUIRE(IsHirelingClass(359));
    TEST_REQUIRE(IsHirelingClass(560));
    TEST_REQUIRE(IsHirelingClass(561));
    TEST_REQUIRE(!IsHirelingClass(0));
    TEST_REQUIRE(!IsHirelingClass(270));
    TEST_REQUIRE(IsProjectedLethal(256, -256));
    TEST_REQUIRE(IsProjectedLethal(1, -2));
    TEST_REQUIRE(!IsProjectedLethal(256, -255));
    TEST_REQUIRE(!IsProjectedLethal(0, 0));
    TEST_REQUIRE(!IsProjectedLethal(1, 1));

    const auto missing = ParseConfig(nlohmann::json::object());
    TEST_REQUIRE(!missing.enabled);
    const auto enabled = ParseConfig(nlohmann::json::parse(
        R"json({"preventMercDeathInTown":{"enabled":true}})json"));
    TEST_REQUIRE(enabled.enabled);
    TEST_REQUIRE(Throws([] {
        ParseConfig(nlohmann::json::parse(
            R"json({"preventMercDeathInTown":{"enabled":true,"unknown":1}})json"));
    }));
    TEST_REQUIRE(Throws([] {
        ParseConfig(nlohmann::json::parse(
            R"json({"preventMercDeathInTown":{"enabled":1}})json"));
    }));

    TEST_REQUIRE(argc == 2);
    std::ifstream shippedConfig(argv[1]);
    TEST_REQUIRE(shippedConfig.is_open());
    const auto root = nlohmann::json::parse(shippedConfig, nullptr, true, true);
    const auto shipped = ParseConfig(root.at("misc"));
    TEST_REQUIRE(!shipped.enabled);
}
