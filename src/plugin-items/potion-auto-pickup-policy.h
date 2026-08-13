#pragma once
#include <json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RuffnecKk::PotionAutoPickUp {
enum class Family : std::uint8_t { Healing, Mana, Rejuvenation, Unknown };
struct Item { std::string_view code; Family family; std::uint8_t tier; };
inline constexpr std::array Items{
    Item{"hp1",Family::Healing,1}, Item{"hp2",Family::Healing,2}, Item{"hp3",Family::Healing,3}, Item{"hp4",Family::Healing,4}, Item{"hp5",Family::Healing,5},
    Item{"mp1",Family::Mana,1}, Item{"mp2",Family::Mana,2}, Item{"mp3",Family::Mana,3}, Item{"mp4",Family::Mana,4}, Item{"mp5",Family::Mana,5},
    Item{"rvs",Family::Rejuvenation,1}, Item{"rvl",Family::Rejuvenation,2},
};
inline constexpr std::uint32_t PackItemCode(std::string_view code) noexcept {
    std::uint32_t packed=0x20202020;
    for(std::size_t index=0;index<4 && index<code.size();++index) {
        const auto shift=static_cast<std::uint32_t>(index*8);
        packed=(packed & ~(0xFFu<<shift))
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[index]))<<shift);
    }
    return packed;
}
inline constexpr Item Classify(std::string_view code) noexcept {
    for (const auto& item : Items) if (item.code == code) return item;
    return {code, Family::Unknown, 0};
}
struct Policy {
    bool enabled{};
    std::array<bool,6> tiers{};
    std::array<std::uint8_t,4> columns{};
    std::uint8_t columnCount{};
    std::array<bool,6> overflowTiers{};
    constexpr bool Accepts(Item item) const noexcept { return enabled && item.family != Family::Unknown && item.tier < tiers.size() && tiers[item.tier]; }
    constexpr bool AllowsOverflow(Item item) const noexcept { return Accepts(item) && item.tier < overflowTiers.size() && overflowTiers[item.tier]; }
};
struct BeltSlot { bool occupied{}; Family family{Family::Unknown}; };
struct RoutingToken {
    static constexpr std::uint32_t InvalidGuid=std::numeric_limits<std::uint32_t>::max();
    std::uint32_t itemGuid{InvalidGuid};
    constexpr bool Matches(std::uint32_t actualGuid) const noexcept {
        return itemGuid!=InvalidGuid && itemGuid==actualGuid;
    }
    constexpr void Reset() noexcept { itemGuid=InvalidGuid; }
};
enum class Destination : std::int8_t { Ground=-1, Inventory=0, Column1=1, Column2=2, Column3=3, Column4=4 };
struct RouteResult { Destination destination{Destination::Ground}; std::int8_t beltSlot{-1}; };
inline constexpr std::int8_t ChooseBeltSlot(
    const Policy& policy,
    Item item,
    const std::array<BeltSlot,16>& slots,
    std::uint8_t capacity) noexcept {
    if (!policy.Accepts(item)) return -1;
    if (capacity < 4 || capacity > slots.size() || capacity % 4 != 0) return -1;
    const auto rows = static_cast<std::uint8_t>(capacity / 4);
    for (std::uint8_t index=0; index<policy.columnCount; ++index) {
        const auto column = policy.columns[index];
        if (column < 1 || column > 4) continue;
        const auto bottom = static_cast<std::uint8_t>(column - 1);
        if (!slots[bottom].occupied || slots[bottom].family != item.family) continue;
        for (std::uint8_t row=1; row<rows; ++row) {
            const auto slot = static_cast<std::uint8_t>(bottom + row * 4);
            if (!slots[slot].occupied) return static_cast<std::int8_t>(slot);
        }
    }
    for (std::uint8_t index=0; index<policy.columnCount; ++index) {
        const auto column = policy.columns[index];
        if (column < 1 || column > 4) continue;
        const auto bottom = static_cast<std::uint8_t>(column - 1);
        if (!slots[bottom].occupied) return static_cast<std::int8_t>(bottom);
    }
    return -1;
}
inline constexpr RouteResult Route(
    const Policy& policy,
    Item item,
    const std::array<BeltSlot,16>& slots,
    std::uint8_t capacity,
    bool inventoryHasRoom) noexcept {
    const auto slot = ChooseBeltSlot(policy,item,slots,capacity);
    if (slot >= 0) {
        return {static_cast<Destination>((slot % 4) + 1),slot};
    }
    if (policy.AllowsOverflow(item) && inventoryHasRoom) return {Destination::Inventory,-1};
    return {Destination::Ground,-1};
}

struct FamilyConfig {
    Policy policy{};
    bool legacyOverflow{};
    bool explicitOverflowTiers{};
    std::array<std::uint8_t,5> tierPriority{};
    std::uint8_t tierPriorityCount{};
};

struct Config {
    bool enabled{};
    std::uint32_t distance{4};
    std::uint32_t interval{3};
    FamilyConfig healing{};
    FamilyConfig mana{};
    FamilyConfig rejuvenation{};
    std::array<Family,3> familyPriority{};
    std::uint8_t familyPriorityCount{};
};

inline const Item* FindItem(std::string_view code, Family family) noexcept {
    for (const auto& item : Items) {
        if (item.family == family && item.code == code) return &item;
    }
    return nullptr;
}

inline void RequireKnownSettings(
    const nlohmann::json& object,
    std::initializer_list<std::string_view> known,
    std::string_view path) {
    if (!object.is_object()) {
        throw std::invalid_argument(std::string(path) + " must be an object");
    }
    for (const auto& [key, value] : object.items()) {
        (void)value;
        bool accepted{};
        for (const auto candidate : known) {
            if (key == candidate) {
                accepted = true;
                break;
            }
        }
        if (!accepted) {
            throw std::invalid_argument(
                std::string(path) + " has unknown setting: " + key);
        }
    }
}

inline bool ReadOptionalBoolean(
    const nlohmann::json& object,
    std::string_view key,
    bool fallback,
    std::string_view path) {
    const auto entry = object.find(std::string(key));
    if (entry == object.end()) return fallback;
    if (!entry->is_boolean()) {
        throw std::invalid_argument(
            std::string(path) + "." + std::string(key) + " must be a boolean");
    }
    return entry->get<bool>();
}

inline std::uint32_t ReadOptionalUnsigned(
    const nlohmann::json& object,
    std::string_view key,
    std::uint32_t fallback,
    std::uint32_t minimum,
    std::uint32_t maximum,
    std::string_view path) {
    const auto entry = object.find(std::string(key));
    if (entry == object.end()) return fallback;
    if (!entry->is_number_unsigned() && !entry->is_number_integer()) {
        throw std::invalid_argument(
            std::string(path) + "." + std::string(key) + " must be an integer");
    }
    std::int64_t value{};
    if (entry->is_number_unsigned()) {
        const auto parsed = entry->get<std::uint64_t>();
        if (parsed > maximum) {
            throw std::invalid_argument(
                std::string(path) + "." + std::string(key) + " is out of range");
        }
        value = static_cast<std::int64_t>(parsed);
    } else {
        value = entry->get<std::int64_t>();
    }
    if (value < minimum || value > maximum) {
        throw std::invalid_argument(
            std::string(path) + "." + std::string(key) + " is out of range");
    }
    return static_cast<std::uint32_t>(value);
}

inline void ParseTierSet(
    const nlohmann::json& values,
    Family family,
    std::array<bool,6>& output,
    std::string_view path) {
    if (!values.is_array()) {
        throw std::invalid_argument(std::string(path) + " must be an array of item codes");
    }
    output.fill(false);
    for (const auto& value : values) {
        if (!value.is_string()) {
            throw std::invalid_argument(
                std::string(path) + " must contain only item-code strings");
        }
        const auto code = value.get<std::string>();
        const auto* item = FindItem(code, family);
        if (!item) {
            throw std::invalid_argument(
                std::string(path) + " contains unsupported item code: " + code);
        }
        if (output[item->tier]) {
            throw std::invalid_argument(
                std::string(path) + " contains duplicate item code: " + code);
        }
        output[item->tier] = true;
    }
}

inline void ParseColumns(
    const nlohmann::json& values,
    FamilyConfig& output,
    std::string_view path) {
    if (!values.is_array()) {
        throw std::invalid_argument(std::string(path) + " must be an array");
    }
    output.policy.columns.fill(0);
    output.policy.columnCount = 0;
    std::array<bool,4> seen{};
    for (const auto& value : values) {
        if (!value.is_number_integer()) {
            throw std::invalid_argument(
                std::string(path) + " must contain only integers from 1 through 4");
        }
        const auto column = value.get<std::int64_t>();
        if (column < 1 || column > 4 || seen[static_cast<std::size_t>(column - 1)]) {
            throw std::invalid_argument(
                std::string(path) + " must contain unique integers from 1 through 4");
        }
        seen[static_cast<std::size_t>(column - 1)] = true;
        output.policy.columns[output.policy.columnCount++] =
            static_cast<std::uint8_t>(column);
    }
}

inline void ParseTierPriority(
    const nlohmann::json& values,
    Family family,
    FamilyConfig& output,
    std::string_view path) {
    if (!values.is_array()) {
        throw std::invalid_argument(std::string(path) + " must be an array of item codes");
    }
    output.tierPriority.fill(0);
    output.tierPriorityCount = 0;
    std::array<bool,6> seen{};
    for (const auto& value : values) {
        if (!value.is_string()) {
            throw std::invalid_argument(
                std::string(path) + " must contain only item-code strings");
        }
        const auto code = value.get<std::string>();
        const auto* item = FindItem(code, family);
        if (!item || seen[item->tier]
            || output.tierPriorityCount >= output.tierPriority.size()) {
            throw std::invalid_argument(
                std::string(path) + " contains an unsupported or duplicate item code: " + code);
        }
        seen[item->tier] = true;
        output.tierPriority[output.tierPriorityCount++] = item->tier;
    }
}

inline void FinalizeOverflow(FamilyConfig& config) noexcept {
    if (config.explicitOverflowTiers) return;
    for (std::size_t tier = 0; tier < config.policy.overflowTiers.size(); ++tier) {
        config.policy.overflowTiers[tier] =
            config.legacyOverflow && config.policy.tiers[tier];
    }
}

inline FamilyConfig ParseFamilyConfig(
    const nlohmann::json& object,
    Family family,
    std::string_view path) {
    RequireKnownSettings(object, {
        "enabled", "tiers", "columns", "overflowToInventory",
        "overflowTiers", "tierPriority"
    }, path);
    FamilyConfig parsed{};
    parsed.policy.enabled = ReadOptionalBoolean(object, "enabled", false, path);
    if (const auto tiers = object.find("tiers"); tiers != object.end()) {
        ParseTierSet(*tiers, family, parsed.policy.tiers,
            std::string(path) + ".tiers");
    }
    if (const auto columns = object.find("columns"); columns != object.end()) {
        ParseColumns(*columns, parsed, std::string(path) + ".columns");
    }
    parsed.legacyOverflow = ReadOptionalBoolean(
        object, "overflowToInventory", false, path);
    if (const auto overflow = object.find("overflowTiers"); overflow != object.end()) {
        ParseTierSet(*overflow, family, parsed.policy.overflowTiers,
            std::string(path) + ".overflowTiers");
        parsed.explicitOverflowTiers = true;
    }
    if (const auto priority = object.find("tierPriority"); priority != object.end()) {
        ParseTierPriority(*priority, family, parsed,
            std::string(path) + ".tierPriority");
    }
    FinalizeOverflow(parsed);
    return parsed;
}

inline void ParseFamilyPriority(const nlohmann::json& values, Config& output) {
    if (!values.is_array()) {
        throw std::invalid_argument(
            "items.potionAutoPickUp.familyPriority must be an array");
    }
    output.familyPriority.fill(Family::Unknown);
    output.familyPriorityCount = 0;
    std::array<bool,3> seen{};
    for (const auto& value : values) {
        if (!value.is_string()) {
            throw std::invalid_argument(
                "items.potionAutoPickUp.familyPriority must contain family names");
        }
        const auto name = value.get<std::string>();
        Family family{Family::Unknown};
        std::size_t index{};
        if (name == "healing") family = Family::Healing;
        else if (name == "mana") { family = Family::Mana; index = 1; }
        else if (name == "rejuvenation") { family = Family::Rejuvenation; index = 2; }
        else {
            throw std::invalid_argument(
                "items.potionAutoPickUp.familyPriority contains unknown family: " + name);
        }
        if (seen[index]) {
            throw std::invalid_argument(
                "items.potionAutoPickUp.familyPriority contains duplicate family: " + name);
        }
        seen[index] = true;
        output.familyPriority[output.familyPriorityCount++] = family;
    }
}

inline Config ParseConfig(const nlohmann::json& itemsConfig) {
    if (!itemsConfig.is_object()) {
        throw std::invalid_argument("items must be an object");
    }
    const auto feature = itemsConfig.find("potionAutoPickUp");
    if (feature == itemsConfig.end()) return {};
    constexpr std::string_view path{"items.potionAutoPickUp"};
    RequireKnownSettings(*feature, {
        "enabled", "pickupDistance", "minimumIntervalActions",
        "minimumIntervalFrames", "familyPriority", "healing", "mana",
        "rejuvenation"
    }, path);

    Config parsed{};
    parsed.enabled = ReadOptionalBoolean(*feature, "enabled", false, path);
    parsed.distance = ReadOptionalUnsigned(
        *feature, "pickupDistance", 4, 1, 4, path);
    if (feature->contains("minimumIntervalActions")
        && feature->contains("minimumIntervalFrames")) {
        throw std::invalid_argument(
            "items.potionAutoPickUp cannot define both minimumIntervalActions "
            "and legacy minimumIntervalFrames");
    }
    parsed.interval = feature->contains("minimumIntervalFrames")
        ? ReadOptionalUnsigned(*feature, "minimumIntervalFrames", 3, 1, 25, path)
        : ReadOptionalUnsigned(*feature, "minimumIntervalActions", 3, 1, 25, path);
    if (const auto priority = feature->find("familyPriority");
        priority != feature->end()) {
        ParseFamilyPriority(*priority, parsed);
    }
    if (const auto family = feature->find("healing"); family != feature->end()) {
        parsed.healing = ParseFamilyConfig(
            *family, Family::Healing, "items.potionAutoPickUp.healing");
    }
    if (const auto family = feature->find("mana"); family != feature->end()) {
        parsed.mana = ParseFamilyConfig(
            *family, Family::Mana, "items.potionAutoPickUp.mana");
    }
    if (const auto family = feature->find("rejuvenation");
        family != feature->end()) {
        parsed.rejuvenation = ParseFamilyConfig(
            *family, Family::Rejuvenation, "items.potionAutoPickUp.rejuvenation");
    }
    return parsed;
}
} // namespace RuffnecKk::PotionAutoPickUp
