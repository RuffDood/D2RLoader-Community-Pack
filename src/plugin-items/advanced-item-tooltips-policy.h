#pragma once

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RuffnecKk::AdvancedTooltips {

enum class PropertyRangeColor {
    ChronicleColor,
    BHDarkGreen,
};

enum class RangeDisplayMode {
    Always,
    HoldHotkey,
};

inline RangeDisplayMode ParseRangeDisplayMode(std::string_view value) {
    if (value == "Always") return RangeDisplayMode::Always;
    if (value == "HoldHotkey" || value == "HoldShift") {
        return RangeDisplayMode::HoldHotkey;
    }
    throw std::invalid_argument(
        "rangeDisplayMode must be Always or HoldHotkey (legacy HoldShift is also accepted)");
}

inline constexpr std::string_view RangeDisplayModeName(RangeDisplayMode value) noexcept {
    return value == RangeDisplayMode::Always
        ? std::string_view{"Always"}
        : std::string_view{"HoldHotkey"};
}

inline constexpr bool ShouldDisplayRanges(
    RangeDisplayMode mode, bool hotkeyDown) noexcept {
    return mode == RangeDisplayMode::Always || hotkeyDown;
}

struct HoldToDisplayHotkey {
    std::uint16_t virtualKey{0x10}; // VK_SHIFT
    std::string name{"Shift"};

    bool operator==(const HoldToDisplayHotkey&) const = default;
};

inline bool EqualsAsciiNoCase(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lhs = static_cast<unsigned char>(left[index]);
        const auto rhs = static_cast<unsigned char>(right[index]);
        if (std::tolower(lhs) != std::tolower(rhs)) return false;
    }
    return true;
}

inline HoldToDisplayHotkey ParseHoldToDisplayHotkey(std::string_view value) {
    struct NamedHotkey {
        std::string_view name;
        std::uint16_t virtualKey;
    };
    constexpr std::array namedHotkeys{
        NamedHotkey{"Shift", 0x10},
        NamedHotkey{"LeftShift", 0xA0},
        NamedHotkey{"RightShift", 0xA1},
        NamedHotkey{"Ctrl", 0x11},
        NamedHotkey{"LeftCtrl", 0xA2},
        NamedHotkey{"RightCtrl", 0xA3},
        NamedHotkey{"Alt", 0x12},
        NamedHotkey{"LeftAlt", 0xA4},
        NamedHotkey{"RightAlt", 0xA5},
        NamedHotkey{"Mouse4", 0x05},
        NamedHotkey{"Mouse5", 0x06},
    };
    for (const auto& hotkey : namedHotkeys) {
        if (EqualsAsciiNoCase(value, hotkey.name)) {
            return {hotkey.virtualKey, std::string(hotkey.name)};
        }
    }

    if (value.size() == 1) {
        const auto character = static_cast<unsigned char>(value.front());
        const auto upper = static_cast<char>(std::toupper(character));
        if ((upper >= 'A' && upper <= 'Z') || (upper >= '0' && upper <= '9')) {
            return {static_cast<std::uint16_t>(upper), std::string(1, upper)};
        }
    }

    if (value.size() >= 2 && value.size() <= 3
        && (value.front() == 'F' || value.front() == 'f')) {
        unsigned number{};
        for (std::size_t index = 1; index < value.size(); ++index) {
            const auto digit = value[index];
            if (digit < '0' || digit > '9') {
                number = 0;
                break;
            }
            number = number * 10 + static_cast<unsigned>(digit - '0');
        }
        if (number >= 1 && number <= 12) {
            return {
                static_cast<std::uint16_t>(0x70 + number - 1),
                "F" + std::to_string(number),
            };
        }
    }

    throw std::invalid_argument(
        "holdToDisplayHotkey must be Shift, LeftShift, RightShift, Ctrl, LeftCtrl, "
        "RightCtrl, Alt, LeftAlt, RightAlt, A-Z, 0-9, F1-F12, Mouse4, or Mouse5");
}

inline PropertyRangeColor ParsePropertyRangeColor(std::string_view value) {
    if (value == "ChronicleColor") return PropertyRangeColor::ChronicleColor;
    if (value == "BHDarkGreen") return PropertyRangeColor::BHDarkGreen;
    throw std::invalid_argument(
        "propertyRangeColor must be ChronicleColor or BHDarkGreen");
}

inline constexpr std::string_view PropertyRangeColorName(PropertyRangeColor value) noexcept {
    return value == PropertyRangeColor::ChronicleColor
        ? std::string_view{"ChronicleColor"}
        : std::string_view{"BHDarkGreen"};
}

inline constexpr char PropertyRangeColorCode(PropertyRangeColor value) noexcept {
    // Chronicle uses D2R's U color (teal/light blue). BH's legacy dark-green
    // palette entry is ':' and was used by the first plugin releases.
    return value == PropertyRangeColor::ChronicleColor ? 'U' : ':';
}

struct Config {
    bool enabled{false};
    bool showMaxSockets{true};
    bool showMaxSocketsOnSocketedItems{false};
    bool showBaseDefenseRange{true};
    bool showPropertyRanges{true};
    bool includeSocketedContributionsInRanges{false};
    PropertyRangeColor propertyRangeColor{PropertyRangeColor::ChronicleColor};
    RangeDisplayMode rangeDisplayMode{RangeDisplayMode::HoldHotkey};
    HoldToDisplayHotkey holdToDisplayHotkey{};
};

inline Config ParseConfig(const nlohmann::json& itemsConfig) {
    if (!itemsConfig.is_object()) {
        throw std::invalid_argument("items must be an object");
    }

    const auto feature = itemsConfig.find("advancedTooltips");
    if (feature == itemsConfig.end()) return {};
    if (!feature->is_object()) {
        throw std::invalid_argument("items.advancedTooltips must be an object");
    }
    const auto& root = *feature;

    constexpr std::array allowed{
        "enabled",
        "showMaxSockets",
        "showMaxSocketsOnSocketedItems",
        "showBaseDefenseRange",
        "showPropertyRanges",
        "includeSocketedContributionsInRanges",
        "_rangeDisplayModeHelp",
        "rangeDisplayMode",
        "_holdToDisplayHotkeyHelp",
        "holdToDisplayHotkey",
        "_propertyRangeColorHelp",
        "propertyRangeColor",
    };
    for (auto entry = root.begin(); entry != root.end(); ++entry) {
        if (std::find(allowed.begin(), allowed.end(), entry.key()) == allowed.end()) {
            throw std::invalid_argument(
                "items.advancedTooltips has unknown setting: " + entry.key());
        }
        if (entry.key() == "_propertyRangeColorHelp"
            || entry.key() == "_rangeDisplayModeHelp"
            || entry.key() == "_holdToDisplayHotkeyHelp") {
            if (!entry.value().is_string()) {
                throw std::invalid_argument(
                    "items.advancedTooltips." + entry.key() + " must be a string");
            }
            continue;
        }
        if (entry.key() == "propertyRangeColor"
            || entry.key() == "rangeDisplayMode"
            || entry.key() == "holdToDisplayHotkey") {
            if (!entry.value().is_string()) {
                throw std::invalid_argument(
                    "items.advancedTooltips." + entry.key() + " must be a string");
            }
            continue;
        }
        if (!entry.value().is_boolean()) {
            throw std::invalid_argument(
                "items.advancedTooltips." + entry.key() + " must be a boolean");
        }
    }

    Config config;
    config.enabled = root.value("enabled", config.enabled);
    config.showMaxSockets = root.value("showMaxSockets", config.showMaxSockets);
    config.showMaxSocketsOnSocketedItems = root.value(
        "showMaxSocketsOnSocketedItems", config.showMaxSocketsOnSocketedItems);
    config.showBaseDefenseRange = root.value(
        "showBaseDefenseRange", config.showBaseDefenseRange);
    config.showPropertyRanges = root.value("showPropertyRanges", config.showPropertyRanges);
    config.includeSocketedContributionsInRanges = root.value(
        "includeSocketedContributionsInRanges",
        config.includeSocketedContributionsInRanges);
    if (const auto entry = root.find("propertyRangeColor"); entry != root.end()) {
        config.propertyRangeColor = ParsePropertyRangeColor(entry->get<std::string>());
    }
    if (const auto entry = root.find("rangeDisplayMode"); entry != root.end()) {
        config.rangeDisplayMode = ParseRangeDisplayMode(entry->get<std::string>());
    }
    if (const auto entry = root.find("holdToDisplayHotkey"); entry != root.end()) {
        config.holdToDisplayHotkey = ParseHoldToDisplayHotkey(entry->get<std::string>());
    }
    return config;
}

} // namespace RuffnecKk::AdvancedTooltips
