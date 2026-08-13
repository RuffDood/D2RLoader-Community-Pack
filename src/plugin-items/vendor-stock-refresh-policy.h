#pragma once

#include <json.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace RuffnecKk::VendorStockRefresh {

struct Config {
    bool enabled{};
};

constexpr std::uint8_t NormalVendorMode = 2;
constexpr std::uint8_t GambleVendorMode = 3;
constexpr std::uint32_t NormalRefreshAction = 0x56535246u; // "VSRF"
constexpr std::uint32_t VanillaNormalVendorAction = 1u;
constexpr std::uint32_t VanillaGambleRefreshAction = 2u;

struct WidgetRect {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};
};

struct WidgetPosition {
    bool valid{};
    std::int32_t x{};
    std::int32_t y{};
};

constexpr bool HasUsableSize(const WidgetRect& rect) noexcept {
    return rect.width > 0 && rect.height > 0;
}

constexpr WidgetPosition CenterBelow(
    const WidgetRect& anchor,
    const WidgetRect& widget
) noexcept {
    if (!HasUsableSize(anchor) || !HasUsableSize(widget)) return {};

    const auto x = static_cast<std::int64_t>(anchor.x)
        + (static_cast<std::int64_t>(anchor.width) - widget.width) / 2;
    const auto gap = (static_cast<std::int64_t>(widget.height) + 5) / 6;
    const auto y = static_cast<std::int64_t>(anchor.y) + anchor.height + gap;
    if (x < std::numeric_limits<std::int32_t>::min()
        || x > std::numeric_limits<std::int32_t>::max()
        || y < std::numeric_limits<std::int32_t>::min()
        || y > std::numeric_limits<std::int32_t>::max()) {
        return {};
    }
    return {
        .valid = true,
        .x = static_cast<std::int32_t>(x),
        .y = static_cast<std::int32_t>(y),
    };
}

constexpr WidgetRect UnionRect(
    const WidgetRect& first,
    const WidgetRect& second
) noexcept {
    if (!HasUsableSize(first)) return second;
    if (!HasUsableSize(second)) return first;

    const auto left = first.x < second.x ? first.x : second.x;
    const auto top = first.y < second.y ? first.y : second.y;
    const auto firstRight = static_cast<std::int64_t>(first.x) + first.width;
    const auto secondRight = static_cast<std::int64_t>(second.x) + second.width;
    const auto firstBottom = static_cast<std::int64_t>(first.y) + first.height;
    const auto secondBottom = static_cast<std::int64_t>(second.y) + second.height;
    const auto right = firstRight > secondRight ? firstRight : secondRight;
    const auto bottom = firstBottom > secondBottom ? firstBottom : secondBottom;
    if (right - left > std::numeric_limits<std::int32_t>::max()
        || bottom - top > std::numeric_limits<std::int32_t>::max()) {
        return {};
    }
    return {
        .x = left,
        .y = top,
        .width = static_cast<std::int32_t>(right - left),
        .height = static_cast<std::int32_t>(bottom - top),
    };
}

constexpr bool ShouldArmNormalRefresh(
    bool enabled,
    bool hasRefreshMarker,
    std::uint8_t requestedMode,
    bool hasVendorEntry,
    bool vendorInventoryFilled
) noexcept {
    return enabled
        && hasRefreshMarker
        && requestedMode == NormalVendorMode
        && hasVendorEntry
        && vendorInventoryFilled;
}

constexpr std::uint32_t RefreshActionForPanel(bool isGambling) noexcept {
    return isGambling ? VanillaGambleRefreshAction : NormalRefreshAction;
}

constexpr bool ShouldShowNormalRefresh(bool enabled, bool isGambling) noexcept {
    return enabled && !isGambling;
}

inline Config ParseConfig(const nlohmann::json& itemsConfig) {
    if (!itemsConfig.is_object()) {
        throw std::invalid_argument("items must be an object");
    }
    Config parsed{};
    const auto entry = itemsConfig.find("vendorStockRefresh");
    if (entry == itemsConfig.end()) return parsed;
    if (!entry->is_object()) {
        throw std::invalid_argument("items.vendorStockRefresh must be an object");
    }
    for (const auto& [key, value] : entry->items()) {
        (void)value;
        if (key != "enabled") {
            throw std::invalid_argument(
                "items.vendorStockRefresh contains unknown key '" + key + "'");
        }
    }
    if (entry->contains("enabled") && !entry->at("enabled").is_boolean()) {
        throw std::invalid_argument(
            "items.vendorStockRefresh.enabled must be a boolean");
    }
    parsed.enabled = entry->value("enabled", false);
    return parsed;
}

} // namespace RuffnecKk::VendorStockRefresh
