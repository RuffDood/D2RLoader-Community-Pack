#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ruffneck::extended_item_stats {

enum class TooltipLineOrder {
    TopToBottom,
    BottomToTop,
};

struct TooltipWindowOptions {
    std::size_t maxInputBytes{256 * 1024};
    std::size_t maxLines{4096};
    // D2R's native text layout stores one 0x3C-byte record per visible text
    // unit and does not handle a failed oversized allocation.  Zero disables
    // this secondary safety bound.
    std::size_t maxVisibleTextUnits{};
    std::size_t minimumScrollableLines{};
    TooltipLineOrder lineOrder{TooltipLineOrder::BottomToTop};
    bool showPosition{true};
};

struct TooltipWindowRequest {
    std::size_t firstVisibleLine{};
    float availableHeightPixels{};
    float originalHeightPixels{};
};

struct TooltipWindow {
    std::string text;
    std::size_t firstVisibleLine{};
    std::size_t visibleLineCount{};
    std::size_t totalLineCount{};
    bool overflow{};
    bool refused{};
};

struct TooltipSectionExpansion {
    std::string truncated;
    std::string expanded;
};

struct TooltipRefreshDecision {
    bool refreshNow{};
    std::uint32_t delayMilliseconds{};
};

// Coalesces arbitrarily dense input updates into at most one outstanding
// native tooltip rebuild. Callers provide the monotonic clock so the policy is
// deterministic in tests and independent from Win32.
class TooltipRefreshCoalescer {
public:
    bool Request() noexcept;
    TooltipRefreshDecision Decide(
        std::uint64_t nowMilliseconds,
        std::uint32_t minimumIntervalMilliseconds) const noexcept;
    void MarkRefreshed(std::uint64_t nowMilliseconds) noexcept;
    void Cancel() noexcept;
    void Reset() noexcept;
    bool Pending() const noexcept;

private:
    bool dirty_{};
    bool pending_{};
    std::uint64_t lastRefreshMilliseconds_{};
};

// Preserves sub-notch deltas emitted by high-resolution mouse wheels instead
// of discarding every event smaller than Win32's WHEEL_DELTA (120).
class WheelDeltaAccumulator {
public:
    std::int32_t Consume(std::int32_t delta) noexcept;
    void Reset() noexcept;

private:
    std::int32_t remainder_{};
};

using MeasureTooltipFn = float (*)(std::string_view text, void* context) noexcept;

std::uint32_t VanillaTooltipLineCapacity(
    std::uint32_t viewportHeightPixels,
    std::uint32_t maximumLines) noexcept;

std::size_t CountVisibleTooltipTextUnits(std::string_view text) noexcept;

std::size_t MaximumVisibleTooltipLineColumns(std::string_view text) noexcept;

std::string ExpandTooltipSections(
    std::string_view original,
    const std::vector<TooltipSectionExpansion>& expansions);

bool IsKnownTruncatedTooltipPass(
    std::string_view current,
    const std::vector<std::string>& knownTruncatedSections) noexcept;

std::string_view ReconcileTooltipGenerationText(
    std::string_view current,
    std::string_view cached,
    bool knownTruncatedPass) noexcept;

TooltipWindow BuildFittedTooltipWindow(
    std::string_view original,
    const TooltipWindowRequest& request,
    MeasureTooltipFn measure,
    void* measureContext,
    const TooltipWindowOptions& options = {});

std::size_t ScrollTooltipByLines(
    std::size_t firstVisibleLine,
    std::size_t totalLineCount,
    std::size_t visibleLineCount,
    std::int64_t delta) noexcept;

} // namespace ruffneck::extended_item_stats
