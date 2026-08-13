#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ruffneck::extended_item_stats::tooltip_overlay {

struct Snapshot {
    bool active{};
    bool scrollable{};
    bool renderTextInOverlay{};
    std::size_t firstVisibleLine{};
    std::size_t visibleLineCount{};
    std::size_t totalLineCount{};
    std::size_t maximumTextColumns{};
    POINT anchorClient{};
    std::string visibleText;
    std::string completeText;
};

using SnapshotProvider = bool(*)(Snapshot&) noexcept;
using ScrollRatioHandler = void(*)(float) noexcept;
using ScrollLineHandler = bool(*)(std::int64_t) noexcept;
using PinHandler = void(*)(bool) noexcept;

void SetCallbacks(
    SnapshotProvider snapshotProvider,
    ScrollRatioHandler scrollRatioHandler,
    ScrollLineHandler scrollLineHandler,
    PinHandler pinHandler) noexcept;

bool Install(HMODULE module) noexcept;
bool Remove() noexcept;
bool IsReady() noexcept;
bool HitTestScreenPoint(POINT point) noexcept;
bool HasInteractionRegion() noexcept;
bool InteractionHitTestScreenPoint(POINT point) noexcept;
bool IsDragging() noexcept;
void ClearInteractionRegion() noexcept;
bool HandleMouseInput(WPARAM message, const MSLLHOOKSTRUCT& input) noexcept;

} // namespace ruffneck::extended_item_stats::tooltip_overlay
