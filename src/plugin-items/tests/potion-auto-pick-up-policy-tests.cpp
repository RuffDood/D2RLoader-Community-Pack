#include "../potion-auto-pickup-policy.h"

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
    using namespace RuffnecKk::PotionAutoPickUp;

    const auto missing = ParseConfig(nlohmann::json::object());
    TEST_REQUIRE(!missing.enabled);
    TEST_REQUIRE(missing.distance == 4);
    TEST_REQUIRE(missing.interval == 3);
    TEST_REQUIRE(!missing.healing.policy.enabled);
    TEST_REQUIRE(missing.healing.policy.columnCount == 0);
    TEST_REQUIRE(missing.familyPriorityCount == 0);

    const auto skeleton = ParseConfig(nlohmann::json::parse(R"json({
        "potionAutoPickUp": {
            "enabled": false,
            "pickupDistance": 4,
            "minimumIntervalActions": 3,
            "familyPriority": [],
            "healing": {"enabled": false, "tiers": [], "columns": [], "overflowToInventory": false, "overflowTiers": [], "tierPriority": []},
            "mana": {"enabled": false, "tiers": [], "columns": [], "overflowToInventory": false, "overflowTiers": [], "tierPriority": []},
            "rejuvenation": {"enabled": false, "tiers": [], "columns": [], "overflowToInventory": false, "overflowTiers": [], "tierPriority": []},
            "diagnostics": {"enabled": false, "logScans": false}
        }
    })json"));
    TEST_REQUIRE(!skeleton.enabled);
    TEST_REQUIRE(!skeleton.healing.policy.Accepts(Classify("hp5")));
    TEST_REQUIRE(!skeleton.mana.policy.Accepts(Classify("mp5")));
    TEST_REQUIRE(!skeleton.rejuvenation.policy.Accepts(Classify("rvl")));

    const auto configured = ParseConfig(nlohmann::json::parse(R"json({
        "potionAutoPickUp": {
            "enabled": true,
            "pickupDistance": 4,
            "minimumIntervalActions": 2,
            "familyPriority": ["rejuvenation", "healing", "mana"],
            "healing": {
                "enabled": true,
                "tiers": ["hp5"],
                "columns": [1, 2],
                "overflowToInventory": false,
                "overflowTiers": ["hp5"],
                "tierPriority": ["hp5"]
            }
        }
    })json"));
    TEST_REQUIRE(configured.enabled);
    TEST_REQUIRE(configured.interval == 2);
    TEST_REQUIRE(configured.familyPriorityCount == 3);
    TEST_REQUIRE(configured.healing.policy.Accepts(Classify("hp5")));
    TEST_REQUIRE(configured.healing.policy.AllowsOverflow(Classify("hp5")));

    std::array<BeltSlot,16> fullBelt{};
    for (auto& slot : fullBelt) slot.occupied = true;
    TEST_REQUIRE(Route(
        configured.healing.policy, Classify("hp5"), fullBelt, 16, true
    ).destination == Destination::Inventory);
    TEST_REQUIRE(Route(
        configured.healing.policy, Classify("hp5"), fullBelt, 16, false
    ).destination == Destination::Ground);

    const auto legacy = ParseConfig(nlohmann::json::parse(R"json({
        "potionAutoPickUp": {
            "minimumIntervalFrames": 3,
            "healing": {
                "enabled": true,
                "tiers": ["hp4"],
                "overflowToInventory": true
            }
        }
    })json"));
    TEST_REQUIRE(legacy.interval == 3);
    TEST_REQUIRE(legacy.healing.policy.AllowsOverflow(Classify("hp4")));

    TEST_REQUIRE(Throws([] { ParseConfig(nlohmann::json::parse(
        R"json({"potionAutoPickUp":{"pickupDistance":5}})json")); }));
    TEST_REQUIRE(Throws([] { ParseConfig(nlohmann::json::parse(
        R"json({"potionAutoPickUp":{"minimumIntervalActions":1,"minimumIntervalFrames":1}})json")); }));
    TEST_REQUIRE(Throws([] { ParseConfig(nlohmann::json::parse(
        R"json({"potionAutoPickUp":{"healing":{"tiers":["mp5"]}}})json")); }));
    TEST_REQUIRE(Throws([] { ParseConfig(nlohmann::json::parse(
        R"json({"potionAutoPickUp":{"healing":{"columns":[1,1]}}})json")); }));
    return 0;
}
