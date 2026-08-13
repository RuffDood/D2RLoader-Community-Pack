#pragma once

#include <json.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace RuffnecKk::CubeQuickMove {

inline constexpr std::uint8_t CubePage = 3;

struct Config {
    bool enabled{};
};

inline Config ParseConfig(const nlohmann::json& miscConfig) {
    if (!miscConfig.is_object()) {
        throw std::invalid_argument("misc must be an object");
    }

    Config parsed{};
    const auto entry = miscConfig.find("cubeQuickMoveBottomRight");
    if (entry == miscConfig.end()) return parsed;
    if (!entry->is_object()) {
        throw std::invalid_argument(
            "misc.cubeQuickMoveBottomRight must be an object"
        );
    }
    for (const auto& [key, value] : entry->items()) {
        (void)value;
        if (key != "enabled") {
            throw std::invalid_argument(
                "misc.cubeQuickMoveBottomRight contains unknown key '" + key + "'"
            );
        }
    }
    if (entry->contains("enabled")) {
        if (!entry->at("enabled").is_boolean()) {
            throw std::invalid_argument(
                "misc.cubeQuickMoveBottomRight.enabled must be a boolean"
            );
        }
        parsed.enabled = entry->at("enabled").get<bool>();
    }
    return parsed;
}

constexpr bool ShouldRecomputeBottomRight(
    bool enabled,
    std::int32_t vanillaResult,
    std::uint8_t page,
    std::uint8_t width,
    std::uint8_t height
) noexcept {
    return enabled
        && vanillaResult != 0
        && page == CubePage
        && width != 0
        && height > 1;
}

inline bool TryFindBottomRight(
    const std::uintptr_t* cells,
    std::uint8_t gridWidth,
    std::uint8_t gridHeight,
    std::uint8_t itemWidth,
    std::uint8_t itemHeight,
    std::int32_t* freeX,
    std::int32_t* freeY
) noexcept {
    if (!cells || !freeX || !freeY || itemWidth == 0 || itemHeight == 0
        || itemWidth > gridWidth || itemHeight > gridHeight) {
        return false;
    }

    const auto lastX = static_cast<std::int32_t>(gridWidth - itemWidth);
    const auto lastY = static_cast<std::int32_t>(gridHeight - itemHeight);
    for (auto x = lastX; x >= 0; --x) {
        for (auto y = lastY; y >= 0; --y) {
            bool free = true;
            for (std::int32_t row{}; row < itemHeight && free; ++row) {
                for (std::int32_t column{}; column < itemWidth; ++column) {
                    const auto index = static_cast<std::size_t>(y + row)
                        * gridWidth
                        + static_cast<std::size_t>(x + column);
                    if (cells[index] != 0) {
                        free = false;
                        break;
                    }
                }
            }
            if (free) {
                *freeX = x;
                *freeY = y;
                return true;
            }
        }
    }
    return false;
}

} // namespace RuffnecKk::CubeQuickMove
