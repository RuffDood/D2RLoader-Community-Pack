#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <json.hpp>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RuffnecKk::MassIdentify {

inline constexpr std::size_t RequestPacketSize = 21;
inline constexpr std::uint8_t CainIdentifyOpcode = 0x34;
inline constexpr std::uint32_t RequestMarker = 0x3144494Du; // "MID1"
inline constexpr std::uint32_t RequestGuard = 0x314B4B52u;  // "RKK1"
inline constexpr std::uint32_t IdentifyTomeCode = 0x206B6269u; // "ibk "
inline constexpr std::uint32_t IdentifiedItemFlag = 0x00000010u;
inline constexpr std::uint32_t QuantityStat = 70;
inline constexpr std::uint8_t InventoryPage = 0;
inline constexpr std::uint8_t CubePage = 3;
inline constexpr std::uint8_t StashPage = 4;
inline constexpr std::uint8_t InvalidInventoryPage = 0xFF;
inline constexpr std::size_t ItemDataInventoryPageOffset = 0x55;

enum class TargetContainer : std::uint8_t {
    Inventory,
    Cube,
    PersonalStash,
    SharedStash,
};

struct TargetSelection {
    bool includeCube{false};
    bool includePersonalStash{false};
    bool includeSharedStash{false};
};

struct Config {
    bool enabled{true};
    bool freeIdentification{false};
    TargetSelection targets{};
};

inline Config ParseConfig(const nlohmann::json& itemsConfig) {
    if (!itemsConfig.is_object()) {
        throw std::invalid_argument("items must be an object");
    }
    const auto entry = itemsConfig.find("massIdentify");
    const auto config = entry == itemsConfig.end()
        ? nlohmann::json::object()
        : *entry;
    if (!config.is_object()) {
        throw std::invalid_argument("items.massIdentify must be an object");
    }
    for (const auto& [key, value] : config.items()) {
        (void)value;
        if (key != "enabled" && key != "freeIdentification"
                && key != "includeCube"
                && key != "includePersonalStash"
                && key != "includeSharedStash") {
            throw std::invalid_argument(
                "items.massIdentify contains unknown setting: " + key);
        }
    }
    for (const auto* key : {
            "enabled",
            "freeIdentification",
            "includeCube",
            "includePersonalStash",
            "includeSharedStash"}) {
        if (config.contains(key) && !config.at(key).is_boolean()) {
            throw std::invalid_argument(
                std::string("items.massIdentify.") + key
                + " must be a boolean");
        }
    }
    Config result{};
    result.enabled = config.value("enabled", true);
    result.freeIdentification = config.value("freeIdentification", false);
    result.targets.includeCube = config.value("includeCube", false);
    result.targets.includePersonalStash = config.value(
        "includePersonalStash", false);
    result.targets.includeSharedStash = config.value(
        "includeSharedStash", false);
    return result;
}

constexpr bool IncludesTarget(
        const TargetSelection& selection,
        TargetContainer container) noexcept {
    switch (container) {
    case TargetContainer::Inventory:
        return true;
    case TargetContainer::Cube:
        return selection.includeCube;
    case TargetContainer::PersonalStash:
        return selection.includePersonalStash;
    case TargetContainer::SharedStash:
        return selection.includeSharedStash;
    }
    return false;
}

using RequestPacket = std::array<std::uint8_t, RequestPacketSize>;

constexpr std::uint32_t ReadU32(
        const std::uint8_t* bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

constexpr void WriteU32(
        RequestPacket& packet, std::size_t offset,
        std::uint32_t value) noexcept {
    packet[offset] = static_cast<std::uint8_t>(value);
    packet[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    packet[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    packet[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

constexpr RequestPacket MakeRequest(std::uint32_t tomeGuid) noexcept {
    RequestPacket packet{};
    packet[0] = CainIdentifyOpcode;
    WriteU32(packet, 1, tomeGuid);
    WriteU32(packet, 5, RequestMarker);
    WriteU32(packet, 9, RequestGuard);
    return packet;
}

constexpr bool IsPrivateRequest(
        const std::uint8_t* packet, std::int32_t size) noexcept {
    return packet
        && size == static_cast<std::int32_t>(RequestPacketSize)
        && packet[0] == CainIdentifyOpcode
        && ReadU32(packet, 5) == RequestMarker
        && ReadU32(packet, 9) == RequestGuard;
}

constexpr bool IsSupportedInventoryPage(std::uint8_t page) noexcept {
    return page == InventoryPage || page == CubePage;
}

constexpr bool IsMassIdentifyTargetPage(std::uint8_t page) noexcept {
    return page == InventoryPage || page == CubePage || page == StashPage;
}

inline std::uint8_t ReadInventoryPageFromItemData(
        const void* itemData) noexcept {
    if (!itemData) return InvalidInventoryPage;
    const auto* bytes = static_cast<const std::uint8_t*>(itemData);
    return bytes[ItemDataInventoryPageOffset];
}

constexpr bool ShouldCaptureGesture(
        bool enabled,
        bool shiftDown,
        bool rightClick,
        bool cursorEmpty,
        std::int32_t unitType,
        std::uint32_t itemCode) noexcept {
    return enabled
        && shiftDown
        && rightClick
        && cursorEmpty
        && unitType == 4
        && itemCode == IdentifyTomeCode;
}

inline std::string AddMassIdTooltipLine(
        std::string tooltip, std::string_view localizedText) {
    if (localizedText.empty() || tooltip.find(localizedText) != std::string::npos) {
        return tooltip;
    }
    if (!tooltip.empty() && tooltip.back() != '\n') tooltip.push_back('\n');
    tooltip.append(localizedText);
    return tooltip;
}

constexpr std::int32_t IdentificationBudget(
        bool freeIdentification, std::int32_t quantity) noexcept {
    if (freeIdentification) return std::numeric_limits<std::int32_t>::max();
    return quantity > 0 ? quantity : 0;
}

} // namespace RuffnecKk::MassIdentify
