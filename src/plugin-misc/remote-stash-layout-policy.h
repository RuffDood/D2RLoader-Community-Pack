#pragma once

#include <cstddef>
#include <cstdint>

namespace ruffneckk::remote_stash {

struct WidgetRect {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};
};

// The public inter-mod build never judges or rewrites the layout position.
// Any signed x/y is accepted; the mod-owned button only needs a usable size.
constexpr bool IsUsableLayoutOwnedButton(const WidgetRect& rect) noexcept {
    return rect.width > 0 && rect.height > 0;
}

constexpr bool IsExpectedEmbeddedMessage(
    const std::uintptr_t message,
    const std::uintptr_t expectedMessage
) noexcept {
    return message != 0 && expectedMessage != 0 && message == expectedMessage;
}

} // namespace ruffneckk::remote_stash
