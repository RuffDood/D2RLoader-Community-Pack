#pragma once

#include <json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace RuffnecKk::EquippedItemToCube {

struct Config {
    bool enabled{};
};

inline Config ParseConfig(const nlohmann::json& miscConfig) {
    if (!miscConfig.is_object()) {
        throw std::invalid_argument("misc must be an object");
    }
    Config parsed{};
    const auto entry = miscConfig.find("equippedItemToCube");
    if (entry == miscConfig.end()) return parsed;
    if (!entry->is_object()) {
        throw std::invalid_argument("misc.equippedItemToCube must be an object");
    }
    for (const auto& [key, value] : entry->items()) {
        (void)value;
        if (key != "enabled") {
            throw std::invalid_argument(
                "misc.equippedItemToCube contains unknown key '" + key + "'"
            );
        }
    }
    if (entry->contains("enabled")) {
        if (!entry->at("enabled").is_boolean()) {
            throw std::invalid_argument(
                "misc.equippedItemToCube.enabled must be a boolean"
            );
        }
        parsed.enabled = entry->at("enabled").get<bool>();
    }
    return parsed;
}

inline constexpr std::size_t ItemTransferPacketSize = 21;
inline constexpr std::uint8_t InventoryTransferOpcode = 0x54;
inline constexpr std::uint8_t EquippedTransferOpcode = 0x58;
inline constexpr std::uint32_t SelfTargetGuid = 0xFFFFFFFFu;
inline constexpr std::uint32_t CubeInventoryPage = 3;
inline constexpr std::uint32_t BodyLocationCount = 11;

using ItemTransferPacket = std::array<std::uint8_t, ItemTransferPacketSize>;

constexpr std::uint32_t ReadU32(
    const ItemTransferPacket& packet,
    std::size_t offset
) noexcept {
    return static_cast<std::uint32_t>(packet[offset])
        | (static_cast<std::uint32_t>(packet[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(packet[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(packet[offset + 3]) << 24);
}

constexpr void WriteU32(
    ItemTransferPacket& packet,
    std::size_t offset,
    std::uint32_t value
) noexcept {
    packet[offset] = static_cast<std::uint8_t>(value);
    packet[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    packet[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    packet[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

constexpr bool IsEquippedBodyLocation(std::uint32_t bodyLocation) noexcept {
    return bodyLocation > 0 && bodyLocation < BodyLocationCount;
}

constexpr bool ShouldRewriteCubeTransfer(
    bool rewriteArmed,
    const ItemTransferPacket& packet,
    std::uint32_t bodyLocation
) noexcept {
    return rewriteArmed
        && IsEquippedBodyLocation(bodyLocation)
        && packet[0] == InventoryTransferOpcode
        && ReadU32(packet, 13) == CubeInventoryPage;
}

constexpr ItemTransferPacket RewriteAsEquippedTransfer(
    const ItemTransferPacket& inventoryPacket,
    std::uint32_t bodyLocation
) noexcept {
    ItemTransferPacket equippedPacket{};
    equippedPacket[0] = EquippedTransferOpcode;
    WriteU32(equippedPacket, 1, ReadU32(inventoryPacket, 1));
    WriteU32(equippedPacket, 5, SelfTargetGuid);
    WriteU32(equippedPacket, 9, bodyLocation);
    WriteU32(equippedPacket, 13, ReadU32(inventoryPacket, 13));
    WriteU32(equippedPacket, 17, ReadU32(inventoryPacket, 17));
    return equippedPacket;
}

} // namespace RuffnecKk::EquippedItemToCube
