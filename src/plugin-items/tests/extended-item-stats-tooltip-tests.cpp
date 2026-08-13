#include "extended-item-stats-tooltip.h"
#include "../../../tests/test-check.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace ruffneck::extended_item_stats;

namespace {

float MeasureRows(std::string_view text, void* context) noexcept {
    const auto lineHeight = *static_cast<const float*>(context);
    if (text.empty()) return 0.0F;
    std::size_t rows = 1;
    for (const auto character : text) {
        if (character == '\n') ++rows;
    }
    return static_cast<float>(rows) * lineHeight;
}

std::string BottomToTopTooltip(std::size_t lines) {
    std::string text;
    for (auto index = lines; index > 0; --index) {
        if (!text.empty()) text.push_back('\n');
        text += "stat-" + std::to_string(index - 1);
    }
    return text;
}

std::string LongBottomToTopTooltip(
    std::size_t lines,
    std::size_t charactersPerLine) {
    std::string text;
    for (auto index = lines; index > 0; --index) {
        if (!text.empty()) text.push_back('\n');
        text.append(charactersPerLine, static_cast<char>('A' + (index % 26)));
    }
    return text;
}

} // namespace

int main() {
    TEST_REQUIRE(VanillaTooltipLineCapacity(1080, 200) == 38);
    TEST_REQUIRE(VanillaTooltipLineCapacity(1440, 200) == 39);
    TEST_REQUIRE(VanillaTooltipLineCapacity(2160, 200) == 39);
    TEST_REQUIRE(VanillaTooltipLineCapacity(2160, 20) == 20);
    TEST_REQUIRE(VanillaTooltipLineCapacity(0, 200) == 0);
    TEST_REQUIRE(CountVisibleTooltipTextUnits("abc\n123") == 7);
    TEST_REQUIRE(CountVisibleTooltipTextUnits(
        "\xEE\x81\xBE" "3Blue") == 4);
    TEST_REQUIRE(MaximumVisibleTooltipLineColumns(
        "short\n\xEE\x81\xBE" "3longest affix") == 13);
    WheelDeltaAccumulator wheel;
    TEST_REQUIRE(wheel.Consume(30) == 0);
    TEST_REQUIRE(wheel.Consume(30) == 0);
    TEST_REQUIRE(wheel.Consume(60) == 1);
    TEST_REQUIRE(wheel.Consume(-240) == -2);
    TEST_REQUIRE(wheel.Consume(-90) == 0);
    TEST_REQUIRE(wheel.Consume(-30) == -1);
    wheel.Reset();
    TEST_REQUIRE(wheel.Consume(119) == 0);
    TEST_REQUIRE(wheel.Consume(1) == 1);

    const std::string truncatedStats =
        "stat one\nstat two without a terminator";
    const std::string expandedStats =
        "stat one\nstat two without a terminator\nstat three\nstat four\n";
    const std::string truncatedTooltip =
        "footer\n" + truncatedStats + "Can be inserted into socketed items\nitem name";
    const auto expandedTooltip = ExpandTooltipSections(
        truncatedTooltip,
        {{truncatedStats, expandedStats}});
    TEST_REQUIRE(expandedTooltip ==
        "footer\n" + expandedStats + "Can be inserted into socketed items\nitem name");
    TEST_REQUIRE(ExpandTooltipSections(truncatedTooltip, {}) == truncatedTooltip);
    TEST_REQUIRE(ExpandTooltipSections(
        truncatedTooltip,
        {{"missing", "missing\nreplacement"}}) == truncatedTooltip);

    const std::vector<std::string> knownTruncatedSections{
        "native truncated",
    };
    TEST_REQUIRE(IsKnownTruncatedTooltipPass(
        "footer\nnative truncated\nitem name", knownTruncatedSections));
    TEST_REQUIRE(!IsKnownTruncatedTooltipPass(
        "footer\nrerolled stats\nitem name", knownTruncatedSections));
    TEST_REQUIRE(ReconcileTooltipGenerationText(
        "footer\nnative truncated\nitem name",
        "footer\nnative truncated\nfull stat block\nitem name",
        true) ==
        "footer\nnative truncated\nfull stat block\nitem name");
    TEST_REQUIRE(ReconcileTooltipGenerationText(
        "new", "old tooltip that is much longer", false) == "new");
    TEST_REQUIRE(ReconcileTooltipGenerationText(
        "new affix 02", "old affix 01", false) == "new affix 02");
    TEST_REQUIRE(ReconcileTooltipGenerationText(
        "new complete stat block", "old", false) == "new complete stat block");

    const std::string longFirstStat(540, 'A');
    const std::string longRemainingStats(540, 'B');
    const std::string longExtraStats(540, 'C');
    const std::string nativeSentence = "Can be inserted into socketed items\n";
    const std::string interleavedTruncated =
        longFirstStat + "\n" + longRemainingStats;
    const std::string interleavedExpanded =
        interleavedTruncated + "\n" + longExtraStats;
    const std::string interleavedTooltip =
        "item name\n" + longFirstStat + nativeSentence + longRemainingStats;
    TEST_REQUIRE(ExpandTooltipSections(
        interleavedTooltip,
        {{interleavedTruncated, interleavedExpanded}}) ==
        "item name\n" + longFirstStat + nativeSentence
            + longRemainingStats + "\n" + longExtraStats);

    const std::string clippedTooltip =
        "color" + interleavedTruncated.substr(0, interleavedTruncated.size() - 3)
        + nativeSentence + "item name\n";
    TEST_REQUIRE(ExpandTooltipSections(
        clippedTooltip,
        {{interleavedTruncated, interleavedExpanded}}) ==
        "color" + interleavedExpanded + nativeSentence + "item name\n");

    constexpr float lineHeight = 16.0F;
    const std::string vanilla = "\xC3\xBF" "c1Damage +10\n" "\xC3\xBF" "c4Test Item";
    const auto unchanged = BuildFittedTooltipWindow(
        vanilla,
        {.firstVisibleLine = 0, .availableHeightPixels = 64.0F, .originalHeightPixels = 32.0F},
        MeasureRows,
        const_cast<float*>(&lineHeight));
    TEST_REQUIRE(!unchanged.overflow);
    TEST_REQUIRE(!unchanged.refused);
    TEST_REQUIRE(unchanged.text == vanilla);

    const auto ordinary = BottomToTopTooltip(13);
    TooltipWindowOptions extremeOnly{};
    extremeOnly.minimumScrollableLines = 30;
    const auto ordinaryUnchanged = BuildFittedTooltipWindow(
        ordinary,
        {.firstVisibleLine = 0, .availableHeightPixels = 12.0F,
            .originalHeightPixels = 13.0F},
        MeasureRows,
        const_cast<float*>(&lineHeight),
        extremeOnly);
    TEST_REQUIRE(!ordinaryUnchanged.overflow);
    TEST_REQUIRE(!ordinaryUnchanged.refused);
    TEST_REQUIRE(ordinaryUnchanged.text == ordinary);

    const std::string coloredBottomToTop =
        "\xC3\xBF" "c3Affix three\n"
        "Affix two\n"
        "Affix one\n"
        "\xC3\xBF" "c0Item title";
    const auto coloredWindow = BuildFittedTooltipWindow(
        coloredBottomToTop,
        {.firstVisibleLine = 0, .availableHeightPixels = 3.0F,
            .originalHeightPixels = 4.0F},
        [](std::string_view text, void*) noexcept {
            return static_cast<float>(std::count(text.begin(), text.end(), '\n') + 1);
        },
        nullptr);
    TEST_REQUIRE(coloredWindow.overflow);
    TEST_REQUIRE(!coloredWindow.refused);
    TEST_REQUIRE(coloredWindow.text.starts_with(
        "[Lines 1-2 of 4]\n\xC3\xBF" "c3Affix one\n"));
    TEST_REQUIRE(coloredWindow.text.ends_with("\xC3\xBF" "c0Item title"));

    const std::string d2rColoredBottomToTop =
        "\xEE\x81\xBE" "3Affix three\n"
        "Affix two\n"
        "Affix one\n"
        "\xEE\x81\xBE" "0Can be inserted into socketed items\n"
        "\xEE\x81\xBE" "3Jewel";
    const auto d2rColoredWindow = BuildFittedTooltipWindow(
        d2rColoredBottomToTop,
        {.firstVisibleLine = 0, .availableHeightPixels = 3.0F,
            .originalHeightPixels = 5.0F},
        [](std::string_view text, void*) noexcept {
            return static_cast<float>(std::count(text.begin(), text.end(), '\n') + 1);
        },
        nullptr,
        {.showPosition = false});
    TEST_REQUIRE(d2rColoredWindow.overflow);
    TEST_REQUIRE(!d2rColoredWindow.refused);
    TEST_REQUIRE(d2rColoredWindow.text.starts_with(
        "\xEE\x81\xBE" "3Affix one\n"));
    TEST_REQUIRE(d2rColoredWindow.text.ends_with(
        "\xEE\x81\xBE" "3Jewel"));

    const auto huge = BottomToTopTooltip(1019);
    const auto top = BuildFittedTooltipWindow(
        huge,
        {.firstVisibleLine = 0, .availableHeightPixels = 160.0F,
            .originalHeightPixels = 1019.0F * lineHeight},
        MeasureRows,
        const_cast<float*>(&lineHeight));
    TEST_REQUIRE(top.overflow);
    TEST_REQUIRE(!top.refused);
    TEST_REQUIRE(top.totalLineCount == 1019);
    TEST_REQUIRE(top.visibleLineCount == 9);
    TEST_REQUIRE(top.firstVisibleLine == 0);
    TEST_REQUIRE(top.text.starts_with("[Lines 1-9 of 1019]\nstat-8\n"));
    TEST_REQUIRE(top.text.ends_with("stat-0"));
    TEST_REQUIRE(MeasureRows(top.text, const_cast<float*>(&lineHeight)) == 160.0F);

    const auto next = ScrollTooltipByLines(
        top.firstVisibleLine, top.totalLineCount, top.visibleLineCount, 3);
    TEST_REQUIRE(next == 3);
    const auto scrolled = BuildFittedTooltipWindow(
        huge,
        {.firstVisibleLine = next, .availableHeightPixels = 160.0F,
            .originalHeightPixels = 1019.0F * lineHeight},
        MeasureRows,
        const_cast<float*>(&lineHeight));
    TEST_REQUIRE(scrolled.text.starts_with("[Lines 4-12 of 1019]\nstat-11\n"));
    TEST_REQUIRE(scrolled.text.ends_with("stat-3"));

    const auto lastPage = ScrollTooltipByLines(0, 1019, 9, 100000);
    TEST_REQUIRE(lastPage == 1010);
    const auto bottom = BuildFittedTooltipWindow(
        huge,
        {.firstVisibleLine = lastPage, .availableHeightPixels = 160.0F,
            .originalHeightPixels = 1019.0F * lineHeight},
        MeasureRows,
        const_cast<float*>(&lineHeight));
    TEST_REQUIRE(bottom.text.starts_with("[Lines 1011-1019 of 1019]\nstat-1018\n"));
    TEST_REQUIRE(bottom.text.ends_with("stat-1010"));

    TooltipWindowOptions layoutBounded{};
    layoutBounded.maxVisibleTextUnits = 1024;
    layoutBounded.showPosition = false;

    // The native vanilla-height viewport remains untouched for short lines.
    const auto fortyShortLines = BottomToTopTooltip(40);
    const auto shortPage = BuildFittedTooltipWindow(
        fortyShortLines,
        {.firstVisibleLine = 0, .availableHeightPixels = 39.0F * lineHeight,
            .originalHeightPixels = 40.0F * lineHeight},
        MeasureRows,
        const_cast<float*>(&lineHeight),
        layoutBounded);
    TEST_REQUIRE(shortPage.overflow);
    TEST_REQUIRE(!shortPage.refused);
    TEST_REQUIRE(shortPage.visibleLineCount == 39);

    // Long affixes dynamically lower the row count so every page stays below
    // D2R's 64-KiB native layout allocation ceiling.
    const auto fortyLongLines = LongBottomToTopTooltip(40, 120);
    const auto vanillaHeightLongPage = BuildFittedTooltipWindow(
        fortyLongLines,
        {.firstVisibleLine = 0, .availableHeightPixels = 39.0F * lineHeight,
            .originalHeightPixels = 40.0F * lineHeight},
        MeasureRows,
        const_cast<float*>(&lineHeight),
        {.maxVisibleTextUnits = 0, .showPosition = false});
    TEST_REQUIRE(vanillaHeightLongPage.overflow);
    TEST_REQUIRE(!vanillaHeightLongPage.refused);
    TEST_REQUIRE(vanillaHeightLongPage.visibleLineCount == 39);
    TEST_REQUIRE(CountVisibleTooltipTextUnits(vanillaHeightLongPage.text) > 1024);

    const auto longPage = BuildFittedTooltipWindow(
        fortyLongLines,
        {.firstVisibleLine = 0, .availableHeightPixels = 39.0F * lineHeight,
            .originalHeightPixels = 40.0F * lineHeight},
        MeasureRows,
        const_cast<float*>(&lineHeight),
        layoutBounded);
    TEST_REQUIRE(longPage.overflow);
    TEST_REQUIRE(!longPage.refused);
    TEST_REQUIRE(longPage.visibleLineCount == 8);
    TEST_REQUIRE(longPage.text.size() <= 1024);

    for (std::size_t offset = 0; offset < 40; ++offset) {
        const auto page = BuildFittedTooltipWindow(
            fortyLongLines,
            {.firstVisibleLine = offset,
                .availableHeightPixels = 39.0F * lineHeight,
                .originalHeightPixels = 40.0F * lineHeight},
            MeasureRows,
            const_cast<float*>(&lineHeight),
            layoutBounded);
        TEST_REQUIRE(page.overflow);
        TEST_REQUIRE(!page.refused);
        TEST_REQUIRE(page.visibleLineCount >= 1);
        TEST_REQUIRE(page.text.size() <= 1024);
    }

    // The workload budget also protects a tooltip that fits vertically but
    // contains unusually wide lines.
    const auto eightVeryLongLines = LongBottomToTopTooltip(8, 200);
    const auto widePage = BuildFittedTooltipWindow(
        eightVeryLongLines,
        {.firstVisibleLine = 0, .availableHeightPixels = 39.0F * lineHeight,
            .originalHeightPixels = 8.0F * lineHeight},
        MeasureRows,
        const_cast<float*>(&lineHeight),
        layoutBounded);
    TEST_REQUIRE(widePage.overflow);
    TEST_REQUIRE(!widePage.refused);
    TEST_REQUIRE(widePage.visibleLineCount == 5);
    TEST_REQUIRE(widePage.text.size() <= 1024);

    TooltipWindowOptions singleLineBounded{};
    singleLineBounded.maxVisibleTextUnits = 32;
    singleLineBounded.showPosition = false;
    const auto oversizedSingleLine = BuildFittedTooltipWindow(
        std::string(200, 'X'),
        {.firstVisibleLine = 0, .availableHeightPixels = 39.0F * lineHeight,
            .originalHeightPixels = lineHeight},
        MeasureRows,
        const_cast<float*>(&lineHeight),
        singleLineBounded);
    TEST_REQUIRE(oversizedSingleLine.overflow);
    TEST_REQUIRE(!oversizedSingleLine.refused);
    TEST_REQUIRE(oversizedSingleLine.visibleLineCount == 1);
    TEST_REQUIRE(oversizedSingleLine.text == std::string(29, 'X') + "...");

    TooltipWindowOptions bounded{};
    bounded.maxLines = 1000;
    const auto refused = BuildFittedTooltipWindow(
        huge,
        {.firstVisibleLine = 0, .availableHeightPixels = 160.0F,
            .originalHeightPixels = 1019.0F * lineHeight},
        MeasureRows,
        const_cast<float*>(&lineHeight),
        bounded);
    TEST_REQUIRE(refused.refused);
    TEST_REQUIRE(refused.text == huge);

    TEST_REQUIRE(ScrollTooltipByLines(5, 100, 10, -3) == 2);
    TEST_REQUIRE(ScrollTooltipByLines(5, 100, 10, -1000) == 0);
    TEST_REQUIRE(ScrollTooltipByLines(5, 100, 100, 3) == 0);

    TooltipRefreshCoalescer refreshes;
    TEST_REQUIRE(refreshes.Request());
    TEST_REQUIRE(!refreshes.Request());
    auto decision = refreshes.Decide(1000, 33);
    TEST_REQUIRE(decision.refreshNow);
    TEST_REQUIRE(decision.delayMilliseconds == 0);
    refreshes.MarkRefreshed(1000);
    TEST_REQUIRE(!refreshes.Pending());

    TEST_REQUIRE(refreshes.Request());
    decision = refreshes.Decide(1008, 33);
    TEST_REQUIRE(!decision.refreshNow);
    TEST_REQUIRE(decision.delayMilliseconds == 25);
    for (auto request = 0; request < 500; ++request) {
        TEST_REQUIRE(!refreshes.Request());
    }
    decision = refreshes.Decide(1032, 33);
    TEST_REQUIRE(!decision.refreshNow);
    TEST_REQUIRE(decision.delayMilliseconds == 1);
    decision = refreshes.Decide(1033, 33);
    TEST_REQUIRE(decision.refreshNow);
    refreshes.MarkRefreshed(1033);

    TEST_REQUIRE(refreshes.Request());
    refreshes.Cancel();
    TEST_REQUIRE(!refreshes.Pending());
    decision = refreshes.Decide(5000, 33);
    TEST_REQUIRE(!decision.refreshNow);
    TEST_REQUIRE(decision.delayMilliseconds == 0);

    refreshes.Reset();
    TEST_REQUIRE(refreshes.Request());
    decision = refreshes.Decide(1, 33);
    TEST_REQUIRE(decision.refreshNow);
    return 0;
}
