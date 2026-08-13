#include "../mass-identify-policy.h"

#include "../../../tests/test-check.h"

#include <array>
#include <exception>
#include <fstream>

using namespace RuffnecKk::MassIdentify;

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
    const auto defaults = ParseConfig(nlohmann::json::object());
    TEST_REQUIRE(defaults.enabled);
    TEST_REQUIRE(!defaults.freeIdentification);
    TEST_REQUIRE(IncludesTarget(
        defaults.targets, TargetContainer::Inventory));
    TEST_REQUIRE(!IncludesTarget(defaults.targets, TargetContainer::Cube));
    TEST_REQUIRE(!IncludesTarget(
        defaults.targets, TargetContainer::PersonalStash));
    TEST_REQUIRE(!IncludesTarget(
        defaults.targets, TargetContainer::SharedStash));

    const auto allContainers = ParseConfig(nlohmann::json::parse(R"json({
        "massIdentify": {
            "enabled": true,
            "freeIdentification": true,
            "includeCube": true,
            "includePersonalStash": true,
            "includeSharedStash": true
        }
    })json"));
    TEST_REQUIRE(allContainers.enabled);
    TEST_REQUIRE(allContainers.freeIdentification);
    TEST_REQUIRE(IncludesTarget(
        allContainers.targets, TargetContainer::Cube));
    TEST_REQUIRE(IncludesTarget(
        allContainers.targets, TargetContainer::PersonalStash));
    TEST_REQUIRE(IncludesTarget(
        allContainers.targets, TargetContainer::SharedStash));

    ExpectInvalid([] { ParseConfig(nlohmann::json::array()); });
    ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
        R"json({"massIdentify":true})json")); });
    ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
        R"json({"massIdentify":{"enabled":1}})json")); });
    ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
        R"json({"massIdentify":{"freeIdentification":"no"}})json")); });
    ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
        R"json({"massIdentify":{"includeCube":0}})json")); });
    ExpectInvalid([] { ParseConfig(nlohmann::json::parse(
        R"json({"massIdentify":{"unknown":false}})json")); });

    const auto packet = MakeRequest(0x12345678u);
    TEST_REQUIRE(IsPrivateRequest(
        packet.data(), static_cast<std::int32_t>(packet.size())));
    TEST_REQUIRE(ReadU32(packet.data(), 1) == 0x12345678u);
    TEST_REQUIRE(!IsPrivateRequest(packet.data(), 20));

    std::array<std::uint8_t, ItemDataInventoryPageOffset + 1> itemData{};
    itemData[ItemDataInventoryPageOffset] = StashPage;
    TEST_REQUIRE(ReadInventoryPageFromItemData(itemData.data()) == StashPage);
    TEST_REQUIRE(ReadInventoryPageFromItemData(nullptr) == InvalidInventoryPage);

    TEST_REQUIRE(ShouldCaptureGesture(
        true, true, true, true, 4, IdentifyTomeCode));
    TEST_REQUIRE(!ShouldCaptureGesture(
        true, false, true, true, 4, IdentifyTomeCode));
    TEST_REQUIRE(!ShouldCaptureGesture(
        true, true, true, false, 4, IdentifyTomeCode));

    TEST_REQUIRE(IdentificationBudget(false, 0) == 0);
    TEST_REQUIRE(IdentificationBudget(false, 7) == 7);
    TEST_REQUIRE(IdentificationBudget(true, 0)
        == std::numeric_limits<std::int32_t>::max());

    if (argc == 2) {
        std::ifstream stream(argv[1], std::ios::binary);
        TEST_REQUIRE(stream.good());
        const auto templateConfig = nlohmann::json::parse(
            stream, nullptr, true, true);
        const auto shipped = ParseConfig(templateConfig.at("items"));
        TEST_REQUIRE(shipped.enabled);
        TEST_REQUIRE(!shipped.freeIdentification);
        TEST_REQUIRE(!shipped.targets.includeCube);
        TEST_REQUIRE(!shipped.targets.includePersonalStash);
        TEST_REQUIRE(!shipped.targets.includeSharedStash);
    }

    return 0;
}
