#include "extended-item-stats-tooltip.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace ruffneck::extended_item_stats {
namespace {

struct LineRange {
    std::size_t begin{};
    std::size_t end{};
};

struct ColorCodeRange {
    std::size_t begin{};
    std::size_t end{};
};

std::vector<LineRange> SplitLines(std::string_view text, std::size_t maxLines) {
    std::vector<LineRange> lines;
    if (text.empty()) return lines;
    lines.reserve(std::min(maxLines, static_cast<std::size_t>(128)));

    std::size_t start{};
    while (start < text.size()) {
        if (lines.size() == maxLines) return {};
        const auto separator = text.find('\n', start);
        const auto end = separator == std::string_view::npos ? text.size() : separator;
        lines.push_back({start, end});
        if (separator == std::string_view::npos) break;
        start = separator + 1;
    }
    return lines;
}

std::string PositionLine(
    std::size_t firstVisibleLine,
    std::size_t visibleLineCount,
    std::size_t totalLineCount) {
    return "[Lines " + std::to_string(firstVisibleLine + 1) + "-"
        + std::to_string(firstVisibleLine + visibleLineCount) + " of "
        + std::to_string(totalLineCount) + "]";
}

ColorCodeRange LastColorCodeBefore(
    std::string_view text,
    std::size_t end) noexcept {
    ColorCodeRange last{};
    end = std::min(end, text.size());
    for (std::size_t index = 0; index < end;) {
        if (index + 3 < end
            && static_cast<unsigned char>(text[index]) == 0xC3
            && static_cast<unsigned char>(text[index + 1]) == 0xBF
            && text[index + 2] == 'c') {
            last = {index, index + 4};
            index += 4;
            continue;
        }
        if (index + 2 < end
            && static_cast<unsigned char>(text[index]) == 0xFF
            && text[index + 1] == 'c') {
            last = {index, index + 3};
            index += 3;
            continue;
        }
        // D2R 3.2 converts the legacy `\xFFcN` marker to the UTF-8 private-use
        // sequence U+E07E followed by the color digit.  A window cut from the
        // middle of a tooltip must restore that active color explicitly.
        if (index + 3 < end
            && static_cast<unsigned char>(text[index]) == 0xEE
            && static_cast<unsigned char>(text[index + 1]) == 0x81
            && static_cast<unsigned char>(text[index + 2]) == 0xBE) {
            last = {index, index + 4};
            index += 4;
            continue;
        }
        ++index;
    }
    return last;
}

std::string BuildCandidate(
    std::string_view original,
    const std::vector<LineRange>& rawLines,
    std::size_t firstVisibleLine,
    std::size_t visibleLineCount,
    const TooltipWindowOptions& options) {
    const auto totalLineCount = rawLines.size();
    std::size_t rawBegin{};
    std::size_t rawEnd{};
    if (options.lineOrder == TooltipLineOrder::BottomToTop) {
        rawBegin = totalLineCount - firstVisibleLine - visibleLineCount;
        rawEnd = totalLineCount - firstVisibleLine;
    } else {
        rawBegin = firstVisibleLine;
        rawEnd = firstVisibleLine + visibleLineCount;
    }

    const auto position = options.showPosition
        ? PositionLine(firstVisibleLine, visibleLineCount, totalLineCount)
        : std::string{};
    const auto inheritedColor = LastColorCodeBefore(
        original, rawLines[rawBegin].begin);
    std::string candidate;
    candidate.reserve(original.size());
    if (!position.empty() && options.lineOrder == TooltipLineOrder::BottomToTop) {
        candidate.append(position);
        candidate.push_back('\n');
    }
    if (inheritedColor.end > inheritedColor.begin) {
        candidate.append(original.substr(
            inheritedColor.begin,
            inheritedColor.end - inheritedColor.begin));
    }
    for (auto index = rawBegin; index < rawEnd; ++index) {
        const auto& line = rawLines[index];
        candidate.append(original.substr(line.begin, line.end - line.begin));
        if (index + 1 < rawEnd) candidate.push_back('\n');
    }
    if (!position.empty() && options.lineOrder == TooltipLineOrder::TopToBottom) {
        candidate.push_back('\n');
        candidate.append(position);
    }
    return candidate;
}

bool IsValidHeight(float height) noexcept {
    return std::isfinite(height) && height >= 0.0F;
}

std::size_t VisibleTextUnits(std::string_view text) noexcept {
    std::size_t units{};
    for (std::size_t index = 0; index < text.size();) {
        // Color changes are parsed by D2R but do not emit a shaped glyph.
        if (index + 3 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xC3
            && static_cast<unsigned char>(text[index + 1]) == 0xBF
            && text[index + 2] == 'c') {
            index += 4;
            continue;
        }
        if (index + 2 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xFF
            && text[index + 1] == 'c') {
            index += 3;
            continue;
        }
        if (index + 3 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xEE
            && static_cast<unsigned char>(text[index + 1]) == 0x81
            && static_cast<unsigned char>(text[index + 2]) == 0xBE) {
            index += 4;
            continue;
        }

        const auto byte = static_cast<unsigned char>(text[index]);
        if ((byte & 0xC0U) != 0x80U) ++units;
        ++index;
    }
    return units;
}

std::size_t VisibleLineColumns(std::string_view text) noexcept {
    std::size_t maximum{};
    std::size_t current{};
    for (std::size_t index{}; index < text.size();) {
        if (text[index] == '\n') {
            maximum = std::max(maximum, current);
            current = 0;
            ++index;
            continue;
        }
        if (index + 3 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xC3
            && static_cast<unsigned char>(text[index + 1]) == 0xBF
            && text[index + 2] == 'c') {
            index += 4;
            continue;
        }
        if (index + 2 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xFF
            && text[index + 1] == 'c') {
            index += 3;
            continue;
        }
        if (index + 3 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xEE
            && static_cast<unsigned char>(text[index + 1]) == 0x81
            && static_cast<unsigned char>(text[index + 2]) == 0xBE) {
            index += 4;
            continue;
        }

        const auto byte = static_cast<unsigned char>(text[index]);
        if ((byte & 0xC0U) != 0x80U) ++current;
        ++index;
    }
    return std::max(maximum, current);
}

bool FitsTextLayoutBudget(
    std::string_view text,
    std::size_t maximumUnits) noexcept {
    return maximumUnits == 0 || VisibleTextUnits(text) <= maximumUnits;
}

std::string TruncateToTextLayoutBudget(
    std::string_view text,
    std::size_t maximumUnits) {
    if (maximumUnits == 0 || FitsTextLayoutBudget(text, maximumUnits)) {
        return std::string(text);
    }

    constexpr std::string_view Omission = "...";
    const auto contentUnits = maximumUnits > Omission.size()
        ? maximumUnits - Omission.size()
        : maximumUnits;
    std::size_t units{};
    std::size_t end{};
    for (std::size_t index = 0; index < text.size() && units < contentUnits;) {
        if (index + 3 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xC3
            && static_cast<unsigned char>(text[index + 1]) == 0xBF
            && text[index + 2] == 'c') {
            index += 4;
            end = index;
            continue;
        }
        if (index + 2 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xFF
            && text[index + 1] == 'c') {
            index += 3;
            end = index;
            continue;
        }
        if (index + 3 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xEE
            && static_cast<unsigned char>(text[index + 1]) == 0x81
            && static_cast<unsigned char>(text[index + 2]) == 0xBE) {
            index += 4;
            end = index;
            continue;
        }

        std::size_t width = 1;
        const auto byte = static_cast<unsigned char>(text[index]);
        if ((byte & 0xE0U) == 0xC0U) width = 2;
        else if ((byte & 0xF0U) == 0xE0U) width = 3;
        else if ((byte & 0xF8U) == 0xF0U) width = 4;
        width = std::min(width, text.size() - index);

        ++units;
        index += width;
        end = index;
    }

    std::string truncated(text.substr(0, end));
    if (maximumUnits > Omission.size()) truncated.append(Omission);
    return truncated;
}

} // namespace

std::uint32_t VanillaTooltipLineCapacity(
    std::uint32_t viewportHeightPixels,
    std::uint32_t maximumLines) noexcept {
    if (viewportHeightPixels == 0 || maximumLines == 0) return 0;

    // Vanilla clamps framed text to the viewport minus a 31-pixel footer.
    // D2R's native item-tooltip pitch is 1/40 of the viewport height
    // (54 pixels at the governed 3840x2160 runtime).
    constexpr float NativeFooterPixels = 31.0F;
    const auto linePitch = static_cast<float>(viewportHeightPixels) / 40.0F;
    const auto available = std::max(
        0.0F,
        static_cast<float>(viewportHeightPixels) - NativeFooterPixels);
    const auto capacity = static_cast<std::uint32_t>(available / linePitch);
    return std::clamp(capacity, 1U, maximumLines);
}

std::size_t CountVisibleTooltipTextUnits(std::string_view text) noexcept {
    return VisibleTextUnits(text);
}

std::size_t MaximumVisibleTooltipLineColumns(std::string_view text) noexcept {
    return VisibleLineColumns(text);
}

std::int32_t WheelDeltaAccumulator::Consume(std::int32_t delta) noexcept {
    constexpr std::int32_t WheelDelta = 120;
    const auto combined = static_cast<std::int64_t>(remainder_) + delta;
    const auto notches = combined / WheelDelta;
    remainder_ = static_cast<std::int32_t>(combined % WheelDelta);
    return static_cast<std::int32_t>(notches);
}

void WheelDeltaAccumulator::Reset() noexcept {
    remainder_ = 0;
}

std::string ExpandTooltipSections(
    std::string_view original,
    const std::vector<TooltipSectionExpansion>& expansions) {
    std::string result(original);
    for (const auto& expansion : expansions) {
        if (expansion.truncated.empty()
            || expansion.expanded.size() <= expansion.truncated.size()
            || !expansion.expanded.starts_with(expansion.truncated)) {
            continue;
        }
        const auto position = result.find(expansion.truncated);
        if (position != std::string::npos) {
            result.replace(position, expansion.truncated.size(), expansion.expanded);
            continue;
        }

        // The native builder can splice a fixed item sentence between the
        // pre-existing first stat line and the remainder appended by
        // ITEMS_GetStatsDescription.  In that layout the complete truncated
        // buffer is absent from the final tooltip, but its long suffix is
        // still byte-exact.  Replace only that anchored suffix and preserve
        // the native sentence already placed before it.
        const auto minimumMatch = std::min<std::size_t>(
            512, std::max<std::size_t>(1, expansion.truncated.size() / 2));

        // Find both layouts before choosing: the native builder may splice a
        // sentence into the middle (long suffix survives), or drop the final
        // few bytes before appending its next sentence (long prefix survives).
        std::size_t prefixLength{};
        std::size_t prefixPosition{};
        for (std::size_t matchLength = expansion.truncated.size() - 1;
             matchLength >= minimumMatch;
             --matchLength) {
            const std::string_view prefix(
                expansion.truncated.data(), matchLength);
            const auto candidatePosition = result.find(prefix);
            if (candidatePosition == std::string::npos) continue;
            prefixLength = matchLength;
            prefixPosition = candidatePosition;
            break;
        }

        std::size_t suffixOffset{};
        std::size_t suffixLength{};
        std::size_t suffixPosition{};
        for (std::size_t offset = 1;
             offset + minimumMatch <= expansion.truncated.size();
             ++offset) {
            const std::string_view suffix(
                expansion.truncated.data() + offset,
                expansion.truncated.size() - offset);
            const auto candidatePosition = result.find(suffix);
            if (candidatePosition == std::string::npos) continue;
            suffixOffset = offset;
            suffixLength = suffix.size();
            suffixPosition = candidatePosition;
            break;
        }
        if (prefixLength > suffixLength) {
            result.replace(prefixPosition, prefixLength, expansion.expanded);
        } else if (suffixLength != 0) {
            result.replace(
                suffixPosition,
                suffixLength,
                std::string_view(expansion.expanded).substr(suffixOffset));
        }
    }
    return result;
}

bool IsKnownTruncatedTooltipPass(
    std::string_view current,
    const std::vector<std::string>& knownTruncatedSections) noexcept {
    return std::any_of(
        knownTruncatedSections.begin(),
        knownTruncatedSections.end(),
        [current](const std::string& section) {
            return !section.empty() && current.find(section) != std::string_view::npos;
        });
}

std::string_view ReconcileTooltipGenerationText(
    std::string_view current,
    std::string_view cached,
    bool knownTruncatedPass) noexcept {
    return knownTruncatedPass && !cached.empty() ? cached : current;
}

TooltipWindow BuildFittedTooltipWindow(
    std::string_view original,
    const TooltipWindowRequest& request,
    MeasureTooltipFn measure,
    void* measureContext,
    const TooltipWindowOptions& options) {
    TooltipWindow result{
        .text = std::string(original),
        .firstVisibleLine = 0,
        .visibleLineCount = 0,
        .totalLineCount = 0,
        .overflow = false,
        .refused = false,
    };

    if (original.empty()) return result;
    if (original.size() > options.maxInputBytes || options.maxLines == 0) {
        result.refused = true;
        return result;
    }

    const auto lines = SplitLines(original, options.maxLines);
    if (lines.empty()) {
        result.refused = true;
        return result;
    }
    result.totalLineCount = lines.size();
    result.visibleLineCount = lines.size();

    if (!IsValidHeight(request.availableHeightPixels)
        || !IsValidHeight(request.originalHeightPixels)
        || request.availableHeightPixels <= 0.0F) {
        result.refused = true;
        return result;
    }

    const auto exceedsHeight =
        request.originalHeightPixels > request.availableHeightPixels;
    const auto exceedsTextLayoutBudget =
        !FitsTextLayoutBudget(original, options.maxVisibleTextUnits);
    if (!exceedsTextLayoutBudget
        && options.minimumScrollableLines != 0
        && lines.size() < options.minimumScrollableLines) {
        return result;
    }
    if (!exceedsHeight && !exceedsTextLayoutBudget) {
        return result;
    }
    result.overflow = true;
    if (!measure) {
        result.refused = true;
        return result;
    }

    const auto first = std::min(request.firstVisibleLine, lines.size() - 1);
    const auto availableLines = lines.size() - first;
    std::size_t low = 1;
    std::size_t high = availableLines;
    std::size_t best{};
    std::string bestText;
    while (low <= high) {
        const auto count = low + ((high - low) / 2);
        auto candidate = BuildCandidate(original, lines, first, count, options);
        if (!FitsTextLayoutBudget(candidate, options.maxVisibleTextUnits)) {
            if (count == 1) break;
            high = count - 1;
            continue;
        }
        const auto measuredHeight = measure(candidate, measureContext);
        if (!IsValidHeight(measuredHeight)) {
            result.refused = true;
            return result;
        }
        if (measuredHeight <= request.availableHeightPixels) {
            best = count;
            bestText = std::move(candidate);
            low = count + 1;
        } else {
            if (count == 1) break;
            high = count - 1;
        }
    }

    if (best == 0) {
        if (options.maxVisibleTextUnits != 0) {
            const auto singleLine = BuildCandidate(
                original, lines, first, 1, options);
            if (!FitsTextLayoutBudget(
                    singleLine, options.maxVisibleTextUnits)) {
                result.text = TruncateToTextLayoutBudget(
                    singleLine, options.maxVisibleTextUnits);
                result.firstVisibleLine = first;
                result.visibleLineCount = 1;
                return result;
            }
        }
        result.refused = true;
        return result;
    }
    result.text = std::move(bestText);
    result.firstVisibleLine = first;
    result.visibleLineCount = best;
    return result;
}

std::size_t ScrollTooltipByLines(
    std::size_t firstVisibleLine,
    std::size_t totalLineCount,
    std::size_t visibleLineCount,
    std::int64_t delta) noexcept {
    if (totalLineCount == 0 || visibleLineCount >= totalLineCount) return 0;
    const auto maximum = totalLineCount - std::max<std::size_t>(visibleLineCount, 1);
    const auto current = std::min(firstVisibleLine, maximum);
    if (delta >= 0) {
        const auto amount = static_cast<std::uint64_t>(delta);
        if (amount >= maximum - current) return maximum;
        return current + static_cast<std::size_t>(amount);
    }
    const auto amount = delta == std::numeric_limits<std::int64_t>::min()
        ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(-delta);
    if (amount >= current) return 0;
    return current - static_cast<std::size_t>(amount);
}

bool TooltipRefreshCoalescer::Request() noexcept {
    dirty_ = true;
    if (pending_) return false;
    pending_ = true;
    return true;
}

TooltipRefreshDecision TooltipRefreshCoalescer::Decide(
    std::uint64_t nowMilliseconds,
    std::uint32_t minimumIntervalMilliseconds) const noexcept {
    if (!pending_ || !dirty_) return {};
    if (lastRefreshMilliseconds_ == 0
        || nowMilliseconds < lastRefreshMilliseconds_) {
        return {.refreshNow = true};
    }
    const auto elapsed = nowMilliseconds - lastRefreshMilliseconds_;
    if (elapsed >= minimumIntervalMilliseconds) {
        return {.refreshNow = true};
    }
    return {
        .refreshNow = false,
        .delayMilliseconds = static_cast<std::uint32_t>(
            minimumIntervalMilliseconds - elapsed),
    };
}

void TooltipRefreshCoalescer::MarkRefreshed(
    std::uint64_t nowMilliseconds) noexcept {
    dirty_ = false;
    pending_ = false;
    lastRefreshMilliseconds_ = nowMilliseconds;
}

void TooltipRefreshCoalescer::Cancel() noexcept {
    dirty_ = false;
    pending_ = false;
}

void TooltipRefreshCoalescer::Reset() noexcept {
    Cancel();
    lastRefreshMilliseconds_ = 0;
}

bool TooltipRefreshCoalescer::Pending() const noexcept {
    return pending_;
}

} // namespace ruffneck::extended_item_stats
