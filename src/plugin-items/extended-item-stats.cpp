#include <D2RLPlugin/api.h>
#include <plugin-shared.h>
#include "extended-item-stats.h"
#include "extended-item-stats-policy.h"
#include "extended-item-stats-transport.h"
#include "extended-item-stats-tooltip.h"
#include "extended-item-stats-overlay.h"

#include <Windows.h>
#include <Xinput.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using ruffneck::extended_item_stats::AcceptResult;
using ruffneck::extended_item_stats::DefaultFrameBytes;
using ruffneck::extended_item_stats::FragmentItem;
using ruffneck::extended_item_stats::BuildFittedTooltipWindow;
using ruffneck::extended_item_stats::ExpandTooltipSections;
using ruffneck::extended_item_stats::IsKnownTruncatedTooltipPass;
using ruffneck::extended_item_stats::MaximumVisibleTooltipLineColumns;
using ruffneck::extended_item_stats::ReconcileTooltipGenerationText;
using ruffneck::extended_item_stats::Reassembler;
using ruffneck::extended_item_stats::ScrollTooltipByLines;
using ruffneck::extended_item_stats::TooltipRefreshCoalescer;
using ruffneck::extended_item_stats::TooltipSectionExpansion;
using ruffneck::extended_item_stats::VanillaTooltipLineCapacity;
using ruffneck::extended_item_stats::WheelDeltaAccumulator;
using ruffneck::extended_item_stats::TooltipWindowOptions;
using ruffneck::extended_item_stats::TransportOptions;
using RuffnecKk::ExtendedItemStats::ShouldSuppressSecondaryNativeTooltip;

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t DispatchItemAction9CRva = 0x12E2C0;
constexpr std::uintptr_t DispatchItemAction9DRva = 0x12E490;
constexpr std::uintptr_t DecodeItemRva = 0x374BF0;
constexpr std::uintptr_t ReadItemMetadataRva = 0x374FF0;
constexpr std::uintptr_t SerializeItemRva = 0x375EE0;
constexpr std::uintptr_t QueueServerPacketRva = 0x4817F0;
constexpr std::uintptr_t BuildItemTooltipRva = 0x2BD480;
constexpr std::uintptr_t GetStatsDescriptionRva = 0x2DC4B0;
constexpr std::uintptr_t ResolveHoveredUnitRva = 0x2A7810;
constexpr std::uintptr_t ResolveHoveredWidgetRva = 0x2A89C0;
constexpr std::uintptr_t EnsureStringCapacityRva = 0x076210;
constexpr std::uintptr_t QueueTextLayoutRva = 0x880160;
constexpr std::uintptr_t MeasureTextLayoutRva = 0x909560;
constexpr std::uintptr_t UiScaleRva = 0x8460F0;
constexpr std::uintptr_t NativeWidthRva = 0x07F510;
constexpr std::uintptr_t NativeHeightRva = 0x07F4A0;
constexpr std::uintptr_t ClampTextLayoutRva = 0x8DA750;
constexpr std::size_t SaveItemBufferBytes = 0x4000;
constexpr std::size_t Packet9CHeaderBytes = 8;
constexpr std::size_t Packet9DHeaderBytes = 13;
constexpr std::size_t MaximumPacketBytes = 0xFC;
constexpr std::array<std::uint8_t, 4> FrameMagic{'E', 'I', 'T', '1'};
constexpr UINT ControllerPollMilliseconds = 60;
constexpr UINT TooltipRefreshMessage = WM_APP + 0x455;
constexpr std::uint32_t TooltipRefreshMinimumIntervalMilliseconds = 33;
constexpr std::size_t NativeTextLayoutElementBytes = 0x3C;
constexpr std::size_t NativeTextLayoutAllocationBudgetBytes = 60 * 1024;
constexpr std::size_t MaximumVisibleTooltipTextUnits =
    NativeTextLayoutAllocationBudgetBytes / NativeTextLayoutElementBytes;
constexpr std::size_t NativeTextLayoutRecordBytes = 0x2E8;

struct NativeRect {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
};

struct FixedPolicy {
    std::uint32_t maxItemBytes{0x1000};
    std::uint32_t maxInFlightTransfers{32};
    std::uint32_t reassemblyTimeoutMs{5000};
    bool logDiagnostics{false};
    bool scrollableTooltips{true};
    std::uint32_t tooltipMaxLines{4096};
    std::uint32_t tooltipMaxTextBytes{256 * 1024};
    std::uint32_t mouseWheelLines{1};
    bool keyboardScrolling{true};
    bool controllerScrolling{true};
};

using SerializeItemFn = std::uint32_t(__fastcall*)(
    void*, std::uint8_t*, std::uint32_t, std::uint32_t, std::uint32_t,
    std::uint32_t, void*) noexcept;
using QueueServerPacketFn = void(__fastcall*)(
    void*, const std::uint8_t*, std::size_t) noexcept;
using DispatchItemActionFn = void(__fastcall*)(const std::uint8_t*) noexcept;
using ReadItemMetadataFn = std::uint64_t(__fastcall*)(
    std::uint8_t, const std::uint8_t*, std::size_t, std::uint32_t, void*) noexcept;
using DecodeItemFn = std::size_t(__fastcall*)(
    void*, const std::uint8_t*, std::size_t, std::uint32_t, void*,
    std::uint32_t, void*) noexcept;
using BuildItemTooltipFn = void*(__fastcall*)(
    void*, void*, void*, void*, std::uint64_t, std::uint64_t, std::uint64_t,
    std::uint64_t, std::uint64_t) noexcept;
using GetStatsDescriptionFn = void(__fastcall*)(
    void*, char*, std::uint32_t, int, int, int, unsigned, int, void*, void*) noexcept;
using EnsureStringCapacityFn = void(__fastcall*)(void*, std::size_t) noexcept;
using ResolveHoveredUnitFn = void*(__fastcall*)(void*) noexcept;
using ResolveHoveredWidgetFn = void*(__fastcall*)(void*, std::uint64_t) noexcept;
using QueueTextLayoutFn = void(__fastcall*)(
    void*, const char*, const NativeRect*, const void*, const char*) noexcept;
using MeasureTextLayoutFn = void(__fastcall*)(
    const char*, const void*, std::int32_t*, float,
    const std::int32_t*) noexcept;
using UiScaleFn = float(__fastcall*)() noexcept;
using NativeExtentFn = std::int32_t(__fastcall*)() noexcept;
using ClampTextLayoutFn = void(__fastcall*)(void*, const NativeRect*) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
constexpr FixedPolicy Settings{};
SerializeItemFn OriginalSerializeItem{};
QueueServerPacketFn OriginalQueueServerPacket{};
DispatchItemActionFn OriginalDispatchItemAction9C{};
DispatchItemActionFn OriginalDispatchItemAction9D{};
ReadItemMetadataFn OriginalReadItemMetadata{};
DecodeItemFn OriginalDecodeItem{};
BuildItemTooltipFn OriginalBuildItemTooltip{};
GetStatsDescriptionFn OriginalGetStatsDescription{};
EnsureStringCapacityFn EnsureStringCapacity{};
ResolveHoveredUnitFn OriginalResolveHoveredUnit{};
ResolveHoveredWidgetFn OriginalResolveHoveredWidget{};
QueueTextLayoutFn OriginalQueueTextLayout{};
MeasureTextLayoutFn MeasureTextLayout{};
UiScaleFn UiScale{};
NativeExtentFn NativeWidth{};
NativeExtentFn NativeHeight{};
ClampTextLayoutFn ClampTextLayout{};
std::unique_ptr<Reassembler> ItemReassembler;
std::mutex ReassemblerMutex;
std::atomic<std::uint32_t> NextTransferId{1};

std::atomic<std::uint64_t> OversizedSerializations{};
std::atomic<std::uint64_t> FragmentedItemsSent{};
std::atomic<std::uint64_t> FramesSent{};
std::atomic<std::uint64_t> FramesReceived{};
std::atomic<std::uint64_t> ItemsReassembled{};
std::atomic<std::uint64_t> TransfersRejected{};
std::atomic<std::uint64_t> TooltipsWindowed{};
std::atomic<std::uint64_t> TooltipScrolls{};
std::atomic<std::uint64_t> TooltipHookCalls{};
std::atomic<std::uint64_t> StatBlocksExpanded{};
std::atomic<std::uint32_t> StatsDescriptionProbeLogs{};
std::atomic<std::uint32_t> TooltipProbeLogs{};
std::atomic<bool> TooltipColorMapLogged{};
bool TooltipHookInstalled{};
bool TransportEnabled{};
bool ScrollBarEnabled{};
std::atomic<bool> FeatureEnabled{};
HWND GameWindow{};
std::atomic<void*> LastTooltipPanel{};
std::atomic<void*> LastHoveredUnit{};
std::atomic<void*> LastHoveredWidget{};
HANDLE TooltipInputThread{};
DWORD TooltipInputThreadId{};
std::atomic<bool> TooltipInputReady{};
std::atomic<bool> TooltipInputFailed{};
std::atomic<bool> TooltipInputStopping{};
TooltipRefreshCoalescer TooltipRefreshSchedule{};
std::mutex TooltipRefreshMutex;

enum class TooltipActivationPhase {
    Armed,
    WaitForClear,
    WaitForHover,
};

struct TooltipState {
    void* item{};
    void* unit{};
    void* panel{};
    void* widget{};
    std::size_t firstVisibleLine{};
    std::size_t visibleLineCount{};
    std::size_t totalLineCount{};
    std::size_t maximumTextColumns{};
    std::uint64_t lastBuildMs{};
    POINT anchorClient{};
    bool anchorCaptured{};
    bool overflow{};
    bool renderTextInOverlay{};
    std::string visibleText;
    std::string completeText;
    std::string nativeVisibleText;
    std::int32_t nativeFixedWidth{};
    std::vector<std::string> knownTruncatedStatBlocks;
};

TooltipState ActiveTooltip{};
TooltipActivationPhase ActiveTooltipPhase{TooltipActivationPhase::Armed};
void* SuppressedTooltipUnit{};
std::mutex TooltipMutex;
WORD PreviousControllerButtons{};
SHORT PreviousRightStickY{};
WheelDeltaAccumulator MouseWheelDeltas{};
thread_local std::vector<TooltipSectionExpansion> PendingStatExpansions;
thread_local std::vector<char> StatsDescriptionScratch;

HWND FindGameWindow() noexcept;

std::uint32_t NativeTooltipVisibleLines() noexcept {
    GameWindow = GameWindow ? GameWindow : FindGameWindow();
    if (!GameWindow) return 0;

    RECT client{};
    if (!GetClientRect(GameWindow, &client)) return 0;
    const auto height = client.bottom - client.top;
    if (height <= 0) return 0;

    return VanillaTooltipLineCapacity(
        static_cast<std::uint32_t>(height),
        Settings.tooltipMaxLines);
}

struct PendingSendState {
    bool active{};
    bool rejected{};
    std::uint32_t transferId{};
    std::vector<std::uint8_t> itemBytes;

    void Reset() noexcept {
        active = false;
        rejected = false;
        transferId = 0;
        itemBytes.clear();
    }
};

struct ReplayState {
    bool active{};
    const std::uint8_t* bitstream{};
    std::size_t itemBytes{};
};

thread_local PendingSendState PendingSend;
thread_local ReplayState Replay;
thread_local std::array<std::uint8_t, SaveItemBufferBytes> SerializationScratch{};

TransportOptions ActiveTransportOptions() noexcept {
    return {
        .frameBytes = DefaultFrameBytes,
        .maxItemBytes = Settings.maxItemBytes,
        .maxInFlightTransfers = Settings.maxInFlightTransfers,
        .reassemblyTimeoutMs = Settings.reassemblyTimeoutMs,
    };
}

void Diagnostic(const char* message) noexcept {
    if (Settings.logDiagnostics && Context) Context->LogInfo(message);
}

bool IsAccessible(const void* address, std::size_t size, bool writable = false) noexcept {
    if (!address || size == 0) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory)
        || memory.State != MEM_COMMIT
        || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    if (writable && (memory.Protect & (PAGE_READONLY | PAGE_EXECUTE_READ)) != 0) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return begin <= regionEnd && size <= regionEnd - begin;
}

float MeasureTooltipRows(std::string_view text, void*) noexcept {
    if (text.empty()) return 0.0F;
    std::size_t rows = 1;
    for (const auto character : text) {
        if (character == '\n') ++rows;
    }
    return static_cast<float>(rows);
}

std::size_t CountTooltipRows(std::string_view text) noexcept {
    return static_cast<std::size_t>(MeasureTooltipRows(text, nullptr));
}

void LogTooltipProbe(
    const char* stage,
    std::size_t inputBytes = 0,
    std::size_t inputRows = 0,
    std::size_t outputBytes = 0,
    std::size_t visibleRows = 0,
    bool overflow = false,
    bool refused = false) noexcept {
    if (!Settings.logDiagnostics || !Context
        || TooltipProbeLogs.fetch_add(1) >= 16) return;
    char message[320]{};
    std::snprintf(
        message,
        sizeof(message),
            "ExtendedItemStats 0.3.17 tooltip probe: stage=%s; input=%zu bytes/%zu rows; output=%zu bytes/%zu visible rows; vanilla capacity=%u; overflow=%s; refused=%s.",
        stage,
        inputBytes,
        inputRows,
        outputBytes,
        visibleRows,
        NativeTooltipVisibleLines(),
        overflow ? "true" : "false",
        refused ? "true" : "false");
    Context->LogWarn(message);
}

void LogTooltipColorMap(std::string_view text, std::size_t rows) noexcept {
    if (!Settings.logDiagnostics
        || rows <= NativeTooltipVisibleLines()
        || TooltipColorMapLogged.exchange(true)) {
        return;
    }
    std::size_t start{};
    std::size_t rawLine{};
    while (start < text.size()) {
        const auto separator = text.find('\n', start);
        const auto end = separator == std::string_view::npos ? text.size() : separator;
        std::string escaped;
        escaped.reserve(std::min<std::size_t>(end - start, 160) * 4);
        for (auto index = start; index < end && index - start < 160; ++index) {
            const auto value = static_cast<unsigned char>(text[index]);
            if (value >= 0x20 && value <= 0x7E && value != '\\') {
                escaped.push_back(static_cast<char>(value));
            } else {
                char encoded[5]{};
                std::snprintf(encoded, sizeof(encoded), "\\x%02X", value);
                escaped.append(encoded);
            }
        }
        char message[850]{};
        std::snprintf(
            message,
            sizeof(message),
            "ExtendedItemStats tooltip raw line: raw=%zu; display=%zu; bytes=%zu; text=%s",
            rawLine,
            rows - rawLine,
            end - start,
            escaped.c_str());
        if (Context) Context->LogInfo(message);
        ++rawLine;
        if (separator == std::string_view::npos) break;
        start = separator + 1;
    }
}

struct CapturedTooltipText {
    std::string text;
    std::vector<std::string> truncatedStatBlocks;
    bool expanded{};
};

CapturedTooltipText ExpandCapturedStatBlocks(
    std::string_view tooltip,
    void*) {
    // Captures are scoped by HookBuildItemTooltip on the same thread.  The
    // first argument of ITEMS_GetStatsDescription is not guaranteed to be the
    // same pointer identity as the outer tooltip builder's item argument.
    if (PendingStatExpansions.empty()) {
        return {.text = std::string(tooltip)};
    }
    auto expansions = std::move(PendingStatExpansions);
    PendingStatExpansions.clear();
    CapturedTooltipText captured{
        .text = ExpandTooltipSections(tooltip, expansions),
    };
    if (captured.text != tooltip) {
        captured.expanded = true;
        captured.truncatedStatBlocks.reserve(expansions.size());
        for (const auto& expansion : expansions) {
            if (!expansion.truncated.empty()) {
                captured.truncatedStatBlocks.push_back(expansion.truncated);
            }
        }
        StatBlocksExpanded.fetch_add(expansions.size());
    }
    return captured;
}

void* TransformScrollableTooltip(void* result, void* item, void* unit = nullptr) noexcept {
    if (!Settings.scrollableTooltips || !result || !item
        || !IsAccessible(result, 24)) {
        LogTooltipProbe("invalid-input");
        return result;
    }

    {
        std::lock_guard lock(TooltipMutex);
        if (ActiveTooltipPhase != TooltipActivationPhase::Armed) {
            PendingStatExpansions.clear();
            return result;
        }
    }

    try {
        const auto* object = static_cast<const std::uint8_t*>(result);
        const auto* data = *reinterpret_cast<char* const*>(object);
        const auto length = *reinterpret_cast<const std::size_t*>(object + 8);
        if (length == 0 || length > Settings.tooltipMaxTextBytes
            || !IsAccessible(data, length + 1)) {
            LogTooltipProbe("invalid-string", length);
            return result;
        }

        const std::string vanilla(data, length);
        const auto capturedText = ExpandCapturedStatBlocks(vanilla, item);
        const auto& currentText = capturedText.text;
        std::string original;
        std::size_t firstVisibleLine{};
        {
            std::lock_guard lock(TooltipMutex);
            // The cursor can leave the tooltip while the native builder is
            // still finishing an earlier pass. Never let that stale pass
            // recreate the active overlay after ClearActiveTooltip() moved
            // the state machine out of Armed.
            if (ActiveTooltipPhase != TooltipActivationPhase::Armed) {
                PendingStatExpansions.clear();
                return result;
            }
            if (ActiveTooltip.item != item) {
                ActiveTooltip = {};
                ActiveTooltip.item = item;
            }
            // A single native hover can rebuild the same tooltip more than
            // once. Retain the full cached description only when this pass
            // contains a truncated stat block captured for that exact content
            // generation. A Cube reroll can mutate the item in place, so its
            // pointer alone must never preserve a different, longer tooltip.
            const auto knownTruncatedPass = !capturedText.expanded
                && IsKnownTruncatedTooltipPass(
                    currentText, ActiveTooltip.knownTruncatedStatBlocks);
            const auto selectedText = ReconcileTooltipGenerationText(
                currentText, ActiveTooltip.completeText, knownTruncatedPass);
            const auto contentGenerationChanged =
                !ActiveTooltip.completeText.empty()
                && selectedText != ActiveTooltip.completeText;
            if (contentGenerationChanged) {
                ActiveTooltip.firstVisibleLine = 0;
                ActiveTooltip.anchorCaptured = false;
                ActiveTooltip.nativeFixedWidth = 0;
            }
            if (selectedText != ActiveTooltip.completeText) {
                ActiveTooltip.completeText.assign(selectedText);
            }
            if (capturedText.expanded) {
                ActiveTooltip.knownTruncatedStatBlocks =
                    capturedText.truncatedStatBlocks;
            } else if (!knownTruncatedPass
                && contentGenerationChanged) {
                ActiveTooltip.knownTruncatedStatBlocks.clear();
            }
            original = ActiveTooltip.completeText;
            ActiveTooltip.unit = unit ? unit : LastHoveredUnit.load();
            ActiveTooltip.panel = LastTooltipPanel.load();
            if (auto* widget = LastHoveredWidget.load()) {
                ActiveTooltip.widget = widget;
            }
            firstVisibleLine = ActiveTooltip.firstVisibleLine;
        }
        const auto rows = CountTooltipRows(original);
        LogTooltipColorMap(original, rows);
        const auto maximumColumns = MaximumVisibleTooltipLineColumns(original);

        TooltipWindowOptions options{
            .maxInputBytes = Settings.tooltipMaxTextBytes,
            .maxLines = Settings.tooltipMaxLines,
            // Keep every page inside D2R's proven native text-layout budget.
            // This deliberately lowers the row count for unusually wide
            // affixes so every page stays on the same native renderer.
            .maxVisibleTextUnits = MaximumVisibleTooltipTextUnits,
            .minimumScrollableLines = 0,
            .lineOrder = ruffneck::extended_item_stats::TooltipLineOrder::BottomToTop,
            .showPosition = false,
        };
        const auto visibleLineCapacity = NativeTooltipVisibleLines();
        const auto window = BuildFittedTooltipWindow(
            original,
            {
                .firstVisibleLine = firstVisibleLine,
                .availableHeightPixels = static_cast<float>(visibleLineCapacity),
                .originalHeightPixels = static_cast<float>(rows),
            },
            MeasureTooltipRows,
            nullptr,
            options);
        constexpr bool renderTextInOverlay = false;

        {
            std::lock_guard lock(TooltipMutex);
            // The fitted window is computed without holding TooltipMutex.
            // Revalidate ownership before publishing it: a mouse-leave or a
            // different hovered item may have invalidated this build.
            if (ActiveTooltipPhase != TooltipActivationPhase::Armed
                || ActiveTooltip.item != item) {
                return result;
            }
            ActiveTooltip.item = item;
            ActiveTooltip.firstVisibleLine = window.firstVisibleLine;
            ActiveTooltip.visibleLineCount = window.visibleLineCount;
            ActiveTooltip.totalLineCount = window.totalLineCount;
            ActiveTooltip.maximumTextColumns = maximumColumns;
            ActiveTooltip.lastBuildMs = GetTickCount64();
            ActiveTooltip.overflow = window.overflow && !window.refused;
            ActiveTooltip.renderTextInOverlay = renderTextInOverlay;
            ActiveTooltip.visibleText = renderTextInOverlay
                ? window.text
                : std::string{};
            if (!ActiveTooltip.anchorCaptured) {
                GameWindow = GameWindow ? GameWindow : FindGameWindow();
                POINT anchor{};
                if (GameWindow && GetCursorPos(&anchor)
                    && ScreenToClient(GameWindow, &anchor)) {
                    ActiveTooltip.anchorClient = anchor;
                    ActiveTooltip.anchorCaptured = true;
                }
            }
        }
        if (window.refused) {
            LogTooltipProbe(
                "refused",
                length,
                rows,
                window.text.size(),
                window.visibleLineCount,
                window.overflow,
                window.refused);
            return result;
        }

        const auto transformed = window.overflow ? window.text : original;
        {
            std::lock_guard lock(TooltipMutex);
            if (ActiveTooltipPhase == TooltipActivationPhase::Armed
                && ActiveTooltip.item == item) {
                ActiveTooltip.nativeVisibleText = transformed;
            }
        }
        if (transformed == vanilla) {
            LogTooltipProbe(
                "unchanged",
                length,
                rows,
                transformed.size(),
                window.visibleLineCount,
                window.overflow,
                false);
            return result;
        }

        if (transformed.size() > length) EnsureStringCapacity(result, transformed.size());
        auto* destination = *reinterpret_cast<char**>(result);
        if (!IsAccessible(destination, transformed.size() + 1, true)) {
            LogTooltipProbe(
                "destination-refused",
                length,
                rows,
                transformed.size(),
                window.visibleLineCount,
                window.overflow,
                true);
            return result;
        }
        std::memcpy(destination, transformed.c_str(), transformed.size() + 1);
        const auto windowLength = transformed.size();
        std::memcpy(static_cast<std::uint8_t*>(result) + 8,
            &windowLength, sizeof(windowLength));
        if (window.overflow) ++TooltipsWindowed;
        LogTooltipProbe(
            window.overflow ? "applied" : "stats-expanded",
            length,
            rows,
            transformed.size(),
            window.visibleLineCount,
            window.overflow,
            window.refused);
    } catch (...) {
        if (Context) Context->LogError(
            "ExtendedItemStats: scrollable tooltip transformation failed safely.");
    }
    return result;
}

std::uint32_t __fastcall HookSerializeItem(
    void* item,
    std::uint8_t* destination,
    std::uint32_t capacity,
    std::uint32_t mode,
    std::uint32_t argument5,
    std::uint32_t argument6,
    void* serializationContext) noexcept {
    if (capacity != 0xF4 || PendingSend.active) {
        return OriginalSerializeItem(
            item, destination, capacity, mode, argument5, argument6, serializationContext);
    }

    try {
        const auto serializedBytes = OriginalSerializeItem(
            item,
            SerializationScratch.data(),
            static_cast<std::uint32_t>(SerializationScratch.size()),
            mode,
            argument5,
            argument6,
            serializationContext);
        if (serializedBytes > 0 && serializedBytes <= DefaultFrameBytes) {
            std::memcpy(destination, SerializationScratch.data(), serializedBytes);
            return serializedBytes;
        }

        PendingSend.Reset();
        PendingSend.active = true;
        PendingSend.transferId = NextTransferId.fetch_add(1, std::memory_order_relaxed);
        if (serializedBytes == 0 || serializedBytes > Settings.maxItemBytes) {
            PendingSend.rejected = true;
        } else {
            PendingSend.itemBytes.assign(
                SerializationScratch.begin(),
                SerializationScratch.begin() + serializedBytes);
            ++OversizedSerializations;
        }
        return 0;
    } catch (...) {
        PendingSend.Reset();
        PendingSend.active = true;
        PendingSend.rejected = true;
        ++TransfersRejected;
        return 0;
    }
}

void __fastcall HookQueueServerPacket(
    void* client,
    const std::uint8_t* packet,
    std::size_t packetBytes) noexcept {
    const auto is9C = packet && packetBytes == Packet9CHeaderBytes
        && packet[0] == 0x9C && packet[2] == Packet9CHeaderBytes;
    const auto is9D = packet && packetBytes == Packet9DHeaderBytes
        && packet[0] == 0x9D && packet[2] == Packet9DHeaderBytes;
    if (!PendingSend.active || (!is9C && !is9D)) {
        if (PendingSend.active) {
            PendingSend.Reset();
            ++TransfersRejected;
            Diagnostic("ExtendedItemStats: oversized serialization was not followed by an item-action packet.");
        }
        OriginalQueueServerPacket(client, packet, packetBytes);
        return;
    }

    const auto headerBytes = is9D ? Packet9DHeaderBytes : Packet9CHeaderBytes;
    if (PendingSend.rejected) {
        PendingSend.Reset();
        ++TransfersRejected;
        Diagnostic("ExtendedItemStats: oversized or invalid item packet was suppressed.");
        return;
    }

    try {
        const auto frames = FragmentItem(
            PendingSend.itemBytes, PendingSend.transferId, ActiveTransportOptions());
        for (const auto& frame : frames) {
            std::vector<std::uint8_t> output(headerBytes + frame.size());
            std::memcpy(output.data(), packet, headerBytes);
            std::memcpy(output.data() + headerBytes, frame.data(), frame.size());
            output[2] = static_cast<std::uint8_t>(output.size());
            OriginalQueueServerPacket(client, output.data(), output.size());
            ++FramesSent;
        }
        ++FragmentedItemsSent;
        if (Settings.logDiagnostics) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "ExtendedItemStats: sent item transfer %u (%zu bytes in %zu frames).",
                PendingSend.transferId,
                PendingSend.itemBytes.size(),
                frames.size());
            Diagnostic(message);
        }
        PendingSend.Reset();
    } catch (...) {
        PendingSend.Reset();
        ++TransfersRejected;
        Diagnostic("ExtendedItemStats: item fragmentation failed and the packet was suppressed.");
    }
}

bool IsExtendedFrame(std::span<const std::uint8_t> payload) noexcept {
    return payload.size() >= FrameMagic.size()
        && std::equal(FrameMagic.begin(), FrameMagic.end(), payload.begin());
}

struct ReplayGuard {
    ReplayGuard(const std::uint8_t* bitstream, std::size_t itemBytes) noexcept {
        Replay = {.active = true, .bitstream = bitstream, .itemBytes = itemBytes};
    }
    ~ReplayGuard() { Replay = {}; }
};

bool ProcessExtendedFrame(
    const std::uint8_t* packet,
    std::size_t headerBytes,
    DispatchItemActionFn original) noexcept {
    if (!packet || packet[2] < headerBytes || packet[2] > MaximumPacketBytes) {
        return false;
    }
    const auto packetBytes = static_cast<std::size_t>(packet[2]);
    const std::span payload(packet + headerBytes, packetBytes - headerBytes);
    if (!IsExtendedFrame(payload)) return false;

    if (Settings.logDiagnostics) {
        char message[160]{};
        std::snprintf(
            message,
            sizeof(message),
            "ExtendedItemStats: received %zu-byte item frame in packet 0x%02X.",
            payload.size(),
            packet[0]);
        Diagnostic(message);
    }

    try {
        std::array<std::uint8_t, Packet9DHeaderBytes> envelope{};
        std::copy_n(packet, headerBytes, envelope.begin());
        envelope[2] = 0;
        AcceptResult result;
        {
            std::lock_guard lock(ReassemblerMutex);
            result = ItemReassembler->Accept(
                payload,
                "d2r-server",
                std::span(envelope.data(), headerBytes),
                GetTickCount64());
        }
        ++FramesReceived;
        if (result.status == AcceptResult::Status::Pending) {
            Diagnostic("ExtendedItemStats: item transfer is waiting for more frames.");
            return true;
        }

        std::vector<std::uint8_t> replayPacket(headerBytes + result.itemBytes.size());
        std::copy_n(packet, headerBytes, replayPacket.begin());
        replayPacket[2] = static_cast<std::uint8_t>(headerBytes);
        std::copy(result.itemBytes.begin(), result.itemBytes.end(),
            replayPacket.begin() + headerBytes);
        Diagnostic("ExtendedItemStats: replaying a complete item through the native handler.");
        ReplayGuard replay(replayPacket.data() + headerBytes, result.itemBytes.size());
        original(replayPacket.data());
        ++ItemsReassembled;
        if (Settings.logDiagnostics) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "ExtendedItemStats: reassembled item transfer %u (%zu bytes from %zu frames).",
                result.transferId,
                result.itemBytes.size(),
                result.chunkCount);
            Diagnostic(message);
        }
        return true;
    } catch (...) {
        ++TransfersRejected;
        Diagnostic("ExtendedItemStats: malformed or inconsistent item fragment was consumed.");
        return true;
    }
}

void __fastcall HookDispatchItemAction9C(const std::uint8_t* packet) noexcept {
    if (ProcessExtendedFrame(
            packet, Packet9CHeaderBytes, OriginalDispatchItemAction9C)) {
        return;
    }
    OriginalDispatchItemAction9C(packet);
}

void __fastcall HookDispatchItemAction9D(const std::uint8_t* packet) noexcept {
    if (ProcessExtendedFrame(
            packet, Packet9DHeaderBytes, OriginalDispatchItemAction9D)) {
        return;
    }
    OriginalDispatchItemAction9D(packet);
}

std::uint64_t __fastcall HookReadItemMetadata(
    std::uint8_t dataContext,
    const std::uint8_t* bitstream,
    std::size_t itemBytes,
    std::uint32_t mode,
    void* output) noexcept {
    if (Replay.active) {
        bitstream = Replay.bitstream;
        itemBytes = Replay.itemBytes;
    }
    return OriginalReadItemMetadata(dataContext, bitstream, itemBytes, mode, output);
}

std::size_t __fastcall HookDecodeItem(
    void* item,
    const std::uint8_t* bitstream,
    std::size_t itemBytes,
    std::uint32_t mode,
    void* socketedItemCount,
    std::uint32_t version,
    void* failure) noexcept {
    if (Replay.active) {
        bitstream = Replay.bitstream;
        itemBytes = Replay.itemBytes;
        Diagnostic("ExtendedItemStats: native item decoder received the reassembled length.");
    }
    return OriginalDecodeItem(
        item, bitstream, itemBytes, mode, socketedItemCount, version, failure);
}

std::uintptr_t CallerRva(void* returnAddress) noexcept {
    if (!Base || !returnAddress) return 0;
    const auto caller = reinterpret_cast<std::uintptr_t>(returnAddress);
    const auto base = reinterpret_cast<std::uintptr_t>(Base);
    return caller >= base ? caller - base : 0;
}

void* __fastcall HookResolveHoveredUnit(void* panel) noexcept {
    const auto caller = CallerRva(_ReturnAddress());
    auto* resolved = OriginalResolveHoveredUnit(panel);
    POINT cursor{};
    const auto cursorKnown = GetCursorPos(&cursor) != FALSE;
    bool clearOverlay{};
    {
        std::lock_guard lock(TooltipMutex);
        if (ActiveTooltipPhase == TooltipActivationPhase::Armed
            && ActiveTooltip.item && ActiveTooltip.unit
            && ActiveTooltip.panel == panel
            && resolved != ActiveTooltip.unit) {
            // Only retain the previous unit over empty UI space. A genuinely
            // resolved second item must take ownership immediately.
            if (!resolved && (ruffneck::extended_item_stats::tooltip_overlay::
                    IsDragging()
                || (cursorKnown
                    && ruffneck::extended_item_stats::tooltip_overlay::
                        InteractionHitTestScreenPoint(cursor)))) {
                resolved = ActiveTooltip.unit;
            } else {
                // A null resolver result proves that the old hover has
                // already ended. The next non-null result is therefore a
                // fresh hover and may arm immediately; waiting for another
                // null pass would strand the next tooltip in vanilla mode.
                if (!resolved) {
                    SuppressedTooltipUnit = ActiveTooltip.unit;
                    ActiveTooltipPhase = TooltipActivationPhase::WaitForHover;
                }
                ActiveTooltip = {};
                clearOverlay = true;
            }
        }
        if (caller == 0x2C89AA) {
            LastTooltipPanel = panel;
            if (ActiveTooltipPhase == TooltipActivationPhase::WaitForClear) {
                LastHoveredUnit = nullptr;
                if (!resolved) {
                    ActiveTooltipPhase = TooltipActivationPhase::WaitForHover;
                } else if (resolved != SuppressedTooltipUnit) {
                    ActiveTooltipPhase = TooltipActivationPhase::Armed;
                    SuppressedTooltipUnit = nullptr;
                    LastHoveredUnit = resolved;
                }
            } else if (ActiveTooltipPhase == TooltipActivationPhase::WaitForHover) {
                LastHoveredUnit = nullptr;
                if (resolved) {
                    ActiveTooltipPhase = TooltipActivationPhase::Armed;
                    SuppressedTooltipUnit = nullptr;
                    LastHoveredUnit = resolved;
                }
            } else {
                LastHoveredUnit = resolved;
                // Once a tooltip has been published, its panel remains the
                // sole native owner. Other open item panels (notably the
                // Personal stash beside Inventory) are still polled by D2R;
                // letting those foreign polls replace the owner leaves their
                // cached +0x558 text eligible for a second draw.
                if (!ActiveTooltip.item || ActiveTooltip.panel == panel) {
                    ActiveTooltip.panel = panel;
                    ActiveTooltip.unit = resolved;
                }
            }
        }
    }
    if (clearOverlay) {
        ruffneck::extended_item_stats::tooltip_overlay::ClearInteractionRegion();
    }
    return resolved;
}

void* __fastcall HookResolveHoveredWidget(
    void* panel,
    std::uint64_t argument) noexcept {
    const auto caller = CallerRva(_ReturnAddress());
    auto* resolved = OriginalResolveHoveredWidget(panel, argument);
    POINT cursor{};
    const auto cursorKnown = GetCursorPos(&cursor) != FALSE;
    {
        std::lock_guard lock(TooltipMutex);
        if (ActiveTooltipPhase == TooltipActivationPhase::Armed
            && ActiveTooltip.item && ActiveTooltip.widget
            && ActiveTooltip.panel == panel
            && (ruffneck::extended_item_stats::tooltip_overlay::IsDragging()
                || (cursorKnown
                    && ruffneck::extended_item_stats::tooltip_overlay::
                        InteractionHitTestScreenPoint(cursor)))) {
            // Keep the exact native tooltip widget while the pointer travels
            // through the tooltip-to-scrollbar interaction region. Unit
            // ownership is resolved independently and clears stale items.
            resolved = ActiveTooltip.widget;
        }
        if (caller == 0x2C8B75) {
            if (ActiveTooltipPhase == TooltipActivationPhase::Armed) {
                LastHoveredWidget = resolved;
                if (ActiveTooltip.item && ActiveTooltip.panel == panel && resolved) {
                    ActiveTooltip.widget = resolved;
                }
            } else {
                LastHoveredWidget = nullptr;
            }
        }
    }
    return resolved;
}

void __fastcall HookGetStatsDescription(
    void* item,
    char* buffer,
    std::uint32_t bufferSize,
    int a4,
    int a5,
    int a6,
    unsigned a7,
    int a8,
    void* a9,
    void* a10) noexcept {
    std::string initial;
    if (item && buffer && bufferSize >= 2
        && bufferSize <= Settings.tooltipMaxTextBytes) {
        try {
            const auto initialLength = strnlen_s(buffer, bufferSize);
            if (initialLength < bufferSize) initial.assign(buffer, initialLength);
        } catch (...) {
            initial.clear();
        }
    }

    OriginalGetStatsDescription(
        item, buffer, bufferSize, a4, a5, a6, a7, a8, a9, a10);
    if (!item || !buffer || bufferSize < 2
        || bufferSize >= Settings.tooltipMaxTextBytes) {
        return;
    }

    try {
        const auto truncatedLength = strnlen_s(buffer, bufferSize);
        if (truncatedLength >= bufferSize || truncatedLength < 512) return;

        StatsDescriptionScratch.assign(Settings.tooltipMaxTextBytes, '\0');
        if (!initial.empty()) {
            std::memcpy(
                StatsDescriptionScratch.data(),
                initial.data(),
                std::min(initial.size(), StatsDescriptionScratch.size() - 1));
        }
        OriginalGetStatsDescription(
            item,
            StatsDescriptionScratch.data(),
            static_cast<std::uint32_t>(StatsDescriptionScratch.size()),
            a4, a5, a6, a7, a8, a9, a10);

        const auto expandedLength = strnlen_s(
            StatsDescriptionScratch.data(), StatsDescriptionScratch.size());
        if (Settings.logDiagnostics && Context
            && StatsDescriptionProbeLogs.fetch_add(1) < 8) {
            char message[240]{};
            std::snprintf(
                message,
                sizeof(message),
                "ExtendedItemStats 0.3.17 stat-buffer probe: caller-size=%u; truncated=%zu; expanded=%zu.",
                bufferSize,
                truncatedLength,
                expandedLength);
            Context->LogWarn(message);
        }
        if (expandedLength <= truncatedLength
            || expandedLength >= StatsDescriptionScratch.size()) {
            return;
        }
        const std::string_view truncated(buffer, truncatedLength);
        const std::string_view expanded(
            StatsDescriptionScratch.data(), expandedLength);
        if (!expanded.starts_with(truncated)) return;

        if (PendingStatExpansions.size() < 16) {
            PendingStatExpansions.push_back({
                std::string(truncated),
                std::string(expanded),
            });
        }
    } catch (...) {
        if (Context) Context->LogWarn(
            "ExtendedItemStats: full item-stat description capture failed safely.");
    }
}

std::int32_t MeasureNativeTextWidth(
    std::string_view text,
    const void* style) noexcept {
    if (!MeasureTextLayout || !UiScale || !style || text.empty()
        || text.size() > MaximumVisibleTooltipTextUnits * 4) {
        return 0;
    }
    try {
        std::string terminated(text);
        std::int32_t measured[2]{};
        constexpr std::int32_t limits[2]{
            std::numeric_limits<std::int32_t>::max(),
            std::numeric_limits<std::int32_t>::max(),
        };
        MeasureTextLayout(
            terminated.c_str(), style, measured, UiScale(), limits);
        return std::max(measured[0], 0);
    } catch (...) {
        return 0;
    }
}

std::int32_t MeasureWidestNativeTooltipLine(
    std::string_view text,
    const void* style) noexcept {
    std::int32_t widest{};
    for (std::size_t begin{}; begin <= text.size();) {
        const auto separator = text.find('\n', begin);
        const auto end = separator == std::string_view::npos
            ? text.size()
            : separator;
        widest = std::max(
            widest,
            MeasureNativeTextWidth(text.substr(begin, end - begin), style));
        if (separator == std::string_view::npos) break;
        begin = separator + 1;
    }
    return widest;
}

void __fastcall HookQueueTextLayout(
    void* component,
    const char* text,
    const NativeRect* requestedRect,
    const void* style,
    const char* secondaryText) noexcept {
    std::string visibleText;
    std::string completeText;
    void* item{};
    std::int32_t cachedFixedWidth{};
    bool suppressSecondaryNativeTooltip{};
    if (text && IsAccessible(text, 1)) {
        const auto length = strnlen_s(text, Settings.tooltipMaxTextBytes);
        if (length < Settings.tooltipMaxTextBytes) {
            std::lock_guard lock(TooltipMutex);
            const auto overflowActive =
                ActiveTooltipPhase == TooltipActivationPhase::Armed
                && ActiveTooltip.overflow
                && !ActiveTooltip.renderTextInOverlay
                && length == ActiveTooltip.nativeVisibleText.size()
                && std::memcmp(
                    text, ActiveTooltip.nativeVisibleText.data(), length) == 0;
            if (overflowActive) {
                const char* ownerText{};
                auto* panel = static_cast<std::uint8_t*>(ActiveTooltip.panel);
                if (panel && IsAccessible(panel + 0x558, sizeof(ownerText))) {
                    ownerText = *reinterpret_cast<char* const*>(panel + 0x558);
                }
                suppressSecondaryNativeTooltip =
                    ShouldSuppressSecondaryNativeTooltip(
                        true,
                        true,
                        ownerText,
                        text);
                if (!suppressSecondaryNativeTooltip) {
                    visibleText = ActiveTooltip.nativeVisibleText;
                    completeText = ActiveTooltip.completeText;
                    item = ActiveTooltip.item;
                    cachedFixedWidth = ActiveTooltip.nativeFixedWidth;
                }
            }
        }
    }

    // UI_ITEM_TOOLTIP_Render passes panel+0x558 directly into the native text
    // renderer. Matching content from a different pointer is therefore the
    // stale second panel, not another line of the active tooltip.
    if (suppressSecondaryNativeTooltip) return;

    std::size_t recordCountBefore{};
    if (item && component
        && IsAccessible(static_cast<std::uint8_t*>(component) + 0x168, 24)) {
        recordCountBefore = *reinterpret_cast<const std::size_t*>(
            static_cast<const std::uint8_t*>(component) + 0x170);
    }
    OriginalQueueTextLayout(
        component, text, requestedRect, style, secondaryText);
    if (!item || visibleText.empty() || completeText.empty() || !component
        || !style || !ClampTextLayout || !NativeWidth || !NativeHeight
        || !IsAccessible(static_cast<std::uint8_t*>(component) + 0x168, 24)) {
        return;
    }

    auto* componentBytes = static_cast<std::uint8_t*>(component);
    auto* records = *reinterpret_cast<std::uint8_t**>(componentBytes + 0x168);
    const auto recordCount = *reinterpret_cast<const std::size_t*>(
        componentBytes + 0x170);
    if (!records || recordCount != recordCountBefore + 1
        || recordCount > std::numeric_limits<std::size_t>::max()
            / NativeTextLayoutRecordBytes) {
        return;
    }
    auto* record = records + (recordCount - 1) * NativeTextLayoutRecordBytes;
    if (!IsAccessible(record, NativeTextLayoutRecordBytes, true)) return;

    auto& primaryWidth = *reinterpret_cast<std::int32_t*>(record + 0x68);
    auto& secondaryWidth = *reinterpret_cast<std::int32_t*>(record + 0x78);
    const auto currentWidth = std::max(primaryWidth, secondaryWidth);
    auto targetWidth = cachedFixedWidth;
    if (targetWidth <= 0) {
        const auto pageWidth = MeasureNativeTextWidth(visibleText, style);
        const auto completeWidth = MeasureWidestNativeTooltipLine(
            completeText, style);
        targetWidth = currentWidth;
        if (pageWidth > 0 && completeWidth > pageWidth) {
            targetWidth += completeWidth - pageWidth;
        }
        std::lock_guard lock(TooltipMutex);
        if (ActiveTooltipPhase == TooltipActivationPhase::Armed
            && ActiveTooltip.item == item
            && ActiveTooltip.completeText == completeText) {
            ActiveTooltip.nativeFixedWidth = targetWidth;
        }
    }
    if (targetWidth <= currentWidth) return;

    const auto nativeWidth = NativeWidth();
    const auto nativeHeight = NativeHeight();
    if (nativeWidth <= 0 || nativeHeight <= 0
        || !IsAccessible(componentBytes + 0xA40, sizeof(std::int32_t))) {
        return;
    }
    const auto scale = UiScale ? UiScale() : 1.0F;
    const auto margin = std::max(
        0,
        static_cast<std::int32_t>(scale
            * *reinterpret_cast<const std::int32_t*>(componentBytes + 0xA40)));
    NativeRect bounds{
        margin,
        margin,
        std::max(1, nativeWidth - margin * 2),
        std::max(1, nativeHeight - margin * 2),
    };
    targetWidth = std::min(targetWidth, bounds.right);
    if (targetWidth <= currentWidth) return;

    auto& primaryLeft = *reinterpret_cast<std::int32_t*>(record + 0x60);
    auto& secondaryLeft = *reinterpret_cast<std::int32_t*>(record + 0x70);
    const auto center = primaryLeft + currentWidth / 2;
    primaryWidth = targetWidth;
    secondaryWidth = targetWidth;
    primaryLeft = center - targetWidth / 2;
    secondaryLeft = primaryLeft;
    ClampTextLayout(record, &bounds);
}

void* __fastcall HookBuildItemTooltip(
    void* output,
    void* a2,
    void* a3,
    void* item,
    std::uint64_t a5,
    std::uint64_t a6,
    std::uint64_t a7,
    std::uint64_t a8,
    std::uint64_t a9) noexcept {
    ++TooltipHookCalls;
    PendingStatExpansions.clear();
    auto* result = OriginalBuildItemTooltip(output, a2, a3, item, a5, a6, a7, a8, a9);
    return TransformScrollableTooltip(result, item, a2);
}

bool TooltipIsActive() noexcept {
    std::lock_guard lock(TooltipMutex);
    return ActiveTooltip.overflow || ActiveTooltip.renderTextInOverlay;
}

bool CurrentProcessOwnsForegroundWindow() noexcept {
    const auto foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD processId{};
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

void RefreshTooltipNow() noexcept {
    GameWindow = FindGameWindow();
    if (!GameWindow) return;

    // This is the exact native invalidation path proven by the standalone
    // 0.3.17 oracle. D2R rebuilds an item tooltip only after real cursor
    // activity; WM_MOUSEMOVE alone can leave the previous native page alive.
    INPUT moves[2]{};
    moves[0].type = INPUT_MOUSE;
    moves[0].mi.dx = 1;
    moves[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    moves[1].type = INPUT_MOUSE;
    moves[1].mi.dx = -1;
    moves[1].mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(2, moves, sizeof(INPUT));

    POINT cursor{};
    if (GetCursorPos(&cursor) && ScreenToClient(GameWindow, &cursor)) {
        PostMessageW(GameWindow, WM_MOUSEMOVE, 0, MAKELPARAM(cursor.x, cursor.y));
    }
    InvalidateRect(GameWindow, nullptr, FALSE);
}

void RequestTooltipRefresh() noexcept {
    bool wakeInputThread{};
    {
        std::lock_guard lock(TooltipRefreshMutex);
        wakeInputThread = TooltipRefreshSchedule.Request();
    }
    if (!wakeInputThread) return;
    if (TooltipInputThreadId != 0
        && PostThreadMessageW(TooltipInputThreadId, TooltipRefreshMessage, 0, 0)) {
        return;
    }
    {
        std::lock_guard lock(TooltipRefreshMutex);
        TooltipRefreshSchedule.MarkRefreshed(GetTickCount64());
    }
    RefreshTooltipNow();
}

void ProcessQueuedTooltipRefresh(UINT_PTR& timer) noexcept {
    if (timer != 0) {
        KillTimer(nullptr, timer);
        timer = 0;
    }

    const auto now = GetTickCount64();
    bool refreshNow{};
    std::uint32_t delayMilliseconds{};
    {
        std::lock_guard lock(TooltipRefreshMutex);
        const auto decision = TooltipRefreshSchedule.Decide(
            now, TooltipRefreshMinimumIntervalMilliseconds);
        refreshNow = decision.refreshNow;
        delayMilliseconds = decision.delayMilliseconds;
        if (refreshNow) TooltipRefreshSchedule.MarkRefreshed(now);
    }

    if (refreshNow) {
        RefreshTooltipNow();
        return;
    }
    if (delayMilliseconds == 0) return;

    timer = SetTimer(nullptr, 0, delayMilliseconds, nullptr);
    if (timer != 0) return;

    // A timer failure must not strand the most recent scroll position. One
    // synchronous rebuild is safe because no second native refresh is pending.
    {
        std::lock_guard lock(TooltipRefreshMutex);
        TooltipRefreshSchedule.MarkRefreshed(GetTickCount64());
    }
    RefreshTooltipNow();
}

void CancelQueuedTooltipRefresh() noexcept {
    std::lock_guard lock(TooltipRefreshMutex);
    TooltipRefreshSchedule.Cancel();
}

bool ScrollActiveTooltip(std::int64_t delta) noexcept {
    bool changed{};
    {
        std::lock_guard lock(TooltipMutex);
        if (!ActiveTooltip.overflow) {
            return false;
        }
        const auto next = ScrollTooltipByLines(
            ActiveTooltip.firstVisibleLine,
            ActiveTooltip.totalLineCount,
            ActiveTooltip.visibleLineCount,
            delta);
        changed = next != ActiveTooltip.firstVisibleLine;
        ActiveTooltip.firstVisibleLine = next;
        ActiveTooltip.lastBuildMs = GetTickCount64();
    }
    if (changed) {
        ++TooltipScrolls;
        RequestTooltipRefresh();
    }
    return changed;
}

void ScrollActiveTooltipToRatio(float ratio) noexcept {
    bool changed{};
    {
        std::lock_guard lock(TooltipMutex);
        if (!ActiveTooltip.overflow
            || ActiveTooltip.visibleLineCount >= ActiveTooltip.totalLineCount) {
            return;
        }
        const auto maximum = ActiveTooltip.totalLineCount
            - ActiveTooltip.visibleLineCount;
        const auto bounded = std::clamp(ratio, 0.0F, 1.0F);
        const auto next = static_cast<std::size_t>(std::llround(
            bounded * static_cast<float>(maximum)));
        changed = next != ActiveTooltip.firstVisibleLine;
        ActiveTooltip.firstVisibleLine = next;
        ActiveTooltip.lastBuildMs = GetTickCount64();
    }
    if (changed) {
        ++TooltipScrolls;
        RequestTooltipRefresh();
    }
}

void ClearActiveTooltip(bool cursorAlreadyOutside = false) noexcept {
    {
        std::lock_guard lock(TooltipMutex);
        SuppressedTooltipUnit = ActiveTooltip.unit;
        ActiveTooltip = {};
        ActiveTooltipPhase = cursorAlreadyOutside
            ? TooltipActivationPhase::WaitForHover
            : TooltipActivationPhase::WaitForClear;
    }
    LastTooltipPanel = nullptr;
    LastHoveredUnit = nullptr;
    LastHoveredWidget = nullptr;
    CancelQueuedTooltipRefresh();
    ruffneck::extended_item_stats::tooltip_overlay::ClearInteractionRegion();
}

bool ProvideTooltipOverlaySnapshot(
    ruffneck::extended_item_stats::tooltip_overlay::Snapshot& snapshot) noexcept {
    try {
        std::lock_guard lock(TooltipMutex);
        snapshot.active = ActiveTooltip.overflow
            || ActiveTooltip.renderTextInOverlay;
        snapshot.scrollable = ActiveTooltip.overflow;
        snapshot.renderTextInOverlay = ActiveTooltip.renderTextInOverlay;
        snapshot.firstVisibleLine = ActiveTooltip.firstVisibleLine;
        snapshot.visibleLineCount = ActiveTooltip.visibleLineCount;
        snapshot.totalLineCount = ActiveTooltip.totalLineCount;
        snapshot.maximumTextColumns = ActiveTooltip.maximumTextColumns;
        snapshot.anchorClient = ActiveTooltip.anchorClient;
        snapshot.visibleText = ActiveTooltip.visibleText;
        snapshot.completeText = ActiveTooltip.completeText;
        return snapshot.active;
    } catch (...) {
        snapshot = {};
        return false;
    }
}

std::int64_t ActiveTooltipPageDelta(bool down) noexcept {
    std::lock_guard lock(TooltipMutex);
    const auto page = std::max<std::size_t>(ActiveTooltip.visibleLineCount, 2) - 1;
    const auto bounded = static_cast<std::int64_t>(std::min<std::size_t>(
        page, static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())));
    return down ? bounded : -bounded;
}

void PollController() noexcept {
    if (!Settings.controllerScrolling || !TooltipIsActive()) {
        PreviousControllerButtons = 0;
        PreviousRightStickY = 0;
        return;
    }
    XINPUT_STATE state{};
    if (XInputGetState(0, &state) != ERROR_SUCCESS) {
        PreviousControllerButtons = 0;
        PreviousRightStickY = 0;
        return;
    }

    const auto buttons = state.Gamepad.wButtons;
    const auto pressed = static_cast<WORD>(buttons & ~PreviousControllerButtons);
    if ((pressed & XINPUT_GAMEPAD_DPAD_UP) != 0) ScrollActiveTooltip(-1);
    if ((pressed & XINPUT_GAMEPAD_DPAD_DOWN) != 0) ScrollActiveTooltip(1);
    if ((pressed & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0) {
        ScrollActiveTooltip(ActiveTooltipPageDelta(false));
    }
    if ((pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0) {
        ScrollActiveTooltip(ActiveTooltipPageDelta(true));
    }

    constexpr SHORT StickThreshold = 20000;
    const auto stick = state.Gamepad.sThumbRY;
    if (stick > StickThreshold && PreviousRightStickY <= StickThreshold) {
        ScrollActiveTooltip(-1);
    } else if (stick < -StickThreshold && PreviousRightStickY >= -StickThreshold) {
        ScrollActiveTooltip(1);
    }
    PreviousControllerButtons = buttons;
    PreviousRightStickY = stick;
}

LRESULT CALLBACK TooltipMouseHook(
    int code,
    WPARAM message,
    LPARAM parameter) noexcept {
    if (TooltipInputStopping.load(std::memory_order_acquire)
        || !FeatureEnabled.load(std::memory_order_acquire)) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    if (code == HC_ACTION && CurrentProcessOwnsForegroundWindow()) {
        const auto* input = reinterpret_cast<const MSLLHOOKSTRUCT*>(parameter);
        const auto active = TooltipIsActive();
        const auto hasRegion = ruffneck::extended_item_stats::tooltip_overlay::
            HasInteractionRegion();
        const auto overScrollbar = hasRegion
            && ruffneck::extended_item_stats::tooltip_overlay::
                HitTestScreenPoint(input->pt);
        const auto handled = (active || hasRegion)
            && ruffneck::extended_item_stats::tooltip_overlay::
                HandleMouseInput(message, *input);

        bool wheelHandled{};
        if (active && message == WM_MOUSEWHEEL) {
            const auto notches = MouseWheelDeltas.Consume(
                static_cast<SHORT>(HIWORD(input->mouseData)));
            if (notches != 0) {
                wheelHandled = ScrollActiveTooltip(
                    -static_cast<std::int64_t>(notches)
                        * Settings.mouseWheelLines);
            }
        }
        if (hasRegion
            && message == WM_MOUSEMOVE
            && !ruffneck::extended_item_stats::tooltip_overlay::IsDragging()
            && !ruffneck::extended_item_stats::tooltip_overlay::
                InteractionHitTestScreenPoint(input->pt)) {
            ClearActiveTooltip(true);
        } else if (active
            && (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN
                || message == WM_MBUTTONDOWN)
            && !overScrollbar) {
            // Clicking another panel or stash tab is a hard ownership change;
            // never leave the old overlay alive behind it.
            ClearActiveTooltip(true);
        } else if (message == WM_LBUTTONUP
            && !ruffneck::extended_item_stats::tooltip_overlay::IsDragging()
            && hasRegion
            && !ruffneck::extended_item_stats::tooltip_overlay::
                InteractionHitTestScreenPoint(input->pt)) {
            ClearActiveTooltip(true);
        }
        if (handled || wheelHandled) return 1;
        if (!active) MouseWheelDeltas.Reset();
    }
    return CallNextHookEx(nullptr, code, message, parameter);
}

LRESULT CALLBACK TooltipKeyboardHook(
    int code,
    WPARAM message,
    LPARAM parameter) noexcept {
    if (TooltipInputStopping.load(std::memory_order_acquire)
        || !FeatureEnabled.load(std::memory_order_acquire)) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    if (code == HC_ACTION && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
        && CurrentProcessOwnsForegroundWindow() && TooltipIsActive()) {
        const auto* input = reinterpret_cast<const KBDLLHOOKSTRUCT*>(parameter);
        if (input->vkCode == VK_ESCAPE) {
            ClearActiveTooltip();
            return CallNextHookEx(nullptr, code, message, parameter);
        }
        if (!Settings.keyboardScrolling) {
            return CallNextHookEx(nullptr, code, message, parameter);
        }
        switch (input->vkCode) {
        case VK_PRIOR: ScrollActiveTooltip(ActiveTooltipPageDelta(false)); break;
        case VK_NEXT: ScrollActiveTooltip(ActiveTooltipPageDelta(true)); break;
        default: break;
        }
    }
    return CallNextHookEx(nullptr, code, message, parameter);
}

HWND FindGameWindow() noexcept {
    struct Search {
        DWORD processId{};
        HWND window{};
        std::int64_t area{};
    } search{GetCurrentProcessId()};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(parameter);
        DWORD processId{};
        GetWindowThreadProcessId(window, &processId);
        if (processId != state.processId || !IsWindowVisible(window)
            || GetWindow(window, GW_OWNER) != nullptr) {
            return TRUE;
        }
        RECT client{};
        if (!GetClientRect(window, &client)) return TRUE;
        const auto area = static_cast<std::int64_t>(client.right - client.left)
            * static_cast<std::int64_t>(client.bottom - client.top);
        if (area > state.area) {
            state.window = window;
            state.area = area;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

bool InstallTooltipInput() noexcept {
    TooltipInputReady = false;
    TooltipInputFailed = false;
    TooltipInputStopping.store(false, std::memory_order_release);
    {
        std::lock_guard lock(TooltipRefreshMutex);
        TooltipRefreshSchedule.Reset();
    }
    HMODULE workerModule{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&InstallTooltipInput),
            &workerModule)) {
        return false;
    }
    TooltipInputThread = CreateThread(
        nullptr,
        0,
        [](void* parameter) noexcept -> DWORD {
            const auto module = static_cast<HMODULE>(parameter);
            MSG message{};
            PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
            const auto mouseHook = SetWindowsHookExW(
                WH_MOUSE_LL, TooltipMouseHook, module, 0);
            const auto keyboardHook = SetWindowsHookExW(
                WH_KEYBOARD_LL, TooltipKeyboardHook, module, 0);
            const auto controllerTimer = SetTimer(
                nullptr, 0, ControllerPollMilliseconds, nullptr);
            UINT_PTR tooltipRefreshTimer{};
            if (!mouseHook || !keyboardHook || !controllerTimer) {
                if (controllerTimer) KillTimer(nullptr, controllerTimer);
                if (keyboardHook) UnhookWindowsHookEx(keyboardHook);
                if (mouseHook) UnhookWindowsHookEx(mouseHook);
                TooltipInputFailed = true;
                TooltipInputReady = true;
                FreeLibraryAndExitThread(module, 1);
            }

            TooltipInputReady = true;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (message.message == WM_TIMER && message.wParam == controllerTimer) {
                    PollController();
                } else if (message.message == TooltipRefreshMessage) {
                    ProcessQueuedTooltipRefresh(tooltipRefreshTimer);
                } else if (message.message == WM_TIMER
                    && tooltipRefreshTimer != 0
                    && message.wParam == tooltipRefreshTimer) {
                    ProcessQueuedTooltipRefresh(tooltipRefreshTimer);
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            if (tooltipRefreshTimer) KillTimer(nullptr, tooltipRefreshTimer);
            KillTimer(nullptr, controllerTimer);
            UnhookWindowsHookEx(keyboardHook);
            UnhookWindowsHookEx(mouseHook);
            FreeLibraryAndExitThread(module, 0);
        },
        workerModule,
        0,
        &TooltipInputThreadId);
    if (!TooltipInputThread) {
        FreeLibrary(workerModule);
        return false;
    }

    for (unsigned attempt = 0; attempt < 200 && !TooltipInputReady.load(); ++attempt) {
        Sleep(10);
    }
    if (!TooltipInputReady.load() || TooltipInputFailed.load()) {
        TooltipInputStopping.store(true, std::memory_order_release);
        if (TooltipInputThreadId != 0) {
            PostThreadMessageW(TooltipInputThreadId, WM_QUIT, 0, 0);
        }
        const auto wait = WaitForSingleObject(TooltipInputThread, 2000);
        if (wait == WAIT_OBJECT_0) {
            CloseHandle(TooltipInputThread);
            TooltipInputThread = nullptr;
            TooltipInputThreadId = 0;
        }
        return false;
    }
    return true;
}

bool RemoveTooltipInput() noexcept {
    TooltipInputStopping.store(true, std::memory_order_release);
    if (!TooltipInputThread) return true;
    if (TooltipInputThreadId != 0) {
        PostThreadMessageW(TooltipInputThreadId, WM_QUIT, 0, 0);
    }
    const auto wait = WaitForSingleObject(TooltipInputThread, 2000);
    if (wait != WAIT_OBJECT_0) {
        if (Context) {
            Context->LogError(
                "ExtendedItemStats: tooltip input worker did not stop; its module reference is retained for safety.");
        }
        return false;
    }
    CloseHandle(TooltipInputThread);
    TooltipInputThread = nullptr;
    TooltipInputThreadId = 0;
    TooltipInputReady = false;
    TooltipInputFailed = false;
    GameWindow = nullptr;
    {
        std::lock_guard lock(TooltipRefreshMutex);
        TooltipRefreshSchedule.Reset();
    }
    return true;
}

bool InstallHooks(bool installTransport) noexcept {
    constexpr std::array<std::uint8_t, 32> dispatch9CExpected{
        0x40,0x53,0x48,0x81,0xEC,0x50,0x01,0x00,0x00,0x48,0x8B,0x05,0xF8,0xCF,0x89,0x02,
        0x48,0x33,0xC4,0x48,0x89,0x84,0x24,0x40,0x01,0x00,0x00,0x48,0x8B,0xD9,0x33,0xD2};
    constexpr std::array<std::uint8_t, 32> dispatch9DExpected{
        0x40,0x53,0x48,0x81,0xEC,0x60,0x01,0x00,0x00,0x48,0x8B,0x05,0x28,0xCE,0x89,0x02,
        0x48,0x33,0xC4,0x48,0x89,0x84,0x24,0x50,0x01,0x00,0x00,0x48,0x8B,0xD9,0x33,0xD2};
    constexpr std::array<std::uint8_t, 14> decodeExpected{
        0x40,0x55,0x53,0x41,0x55,0x41,0x57,0x48,0x8B,0xEC,0x48,0x83,0xEC,0x68};
    constexpr std::array<std::uint8_t, 27> metadataExpected{
        0x48,0x83,0xEC,0x38,0x48,0x8B,0x44,0x24,0x60,0xC7,0x44,0x24,0x28,0x69,0x00,0x00,
        0x00,0x48,0x89,0x44,0x24,0x20,0xE8,0x05,0x00,0x00,0x00};
    constexpr std::array<std::uint8_t, 26> serializeExpected{
        0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x60,0x48,0x8B,0xD9,0x41,0x8B,0xF9,
        0x48,0x8D,0x4C,0x24,0x38,0xE8,0x26,0x57,0x6A,0x00};
    constexpr std::array<std::uint8_t, 22> queueExpected{
        0x40,0x55,0x56,0x57,0x48,0x83,0xEC,0x40,0x49,0x8B,0xE8,0x48,0x8B,0xF2,0x48,0x8B,
        0xF9,0x48,0x85,0xC9,0x75,0x1F};
    constexpr std::array<std::uint8_t, 32> hoveredUnitExpected{
        0x48,0x83,0xEC,0x28,0x44,0x8B,0x81,0xC4,0x05,0x00,0x00,0x41,0x83,0xF8,0xFF,
        0x75,0x10,0x83,0xB9,0xC8,0x05,0x00,0x00,0x06,0x75,0x07,0x33,0xC0,0x48,0x83,0xC4,0x28};
    constexpr std::array<std::uint8_t, 32> hoveredWidgetExpected{
        0x40,0x57,0x41,0x56,0x48,0x83,0xEC,0x28,0x48,0x89,0x5C,0x24,0x40,0x4C,0x8B,0xF2,
        0x48,0x89,0x74,0x24,0x20,0x48,0x8B,0xD9,0x33,0xFF,0x48,0xC7,0x44,0x24,0x50,0xFF};
    constexpr std::array<std::uint8_t, 32> statsDescriptionExpected{
        0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,
        0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8D,0xAC,0x24,0xC0,0xFB,0xFF,0xFF,
        0x48,0x81,0xEC,0x40,0x05,0x00,0x00,0x48};
    constexpr std::array<std::uint8_t, 32> queueTextLayoutExpected{
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,
        0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,
        0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,
        0xEC,0x40,0x4C,0x8D,0xB1,0x68,0x01,0x00};

    if (installTransport) {
        const auto transportInstalled = PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.extendedItemStats.serializeItem"),
            SerializeItemRva, serializeExpected.data(), serializeExpected.size(),
            HookSerializeItem, &OriginalSerializeItem)
            && PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.extendedItemStats.queueServerPacket"),
                QueueServerPacketRva, queueExpected.data(), queueExpected.size(),
                HookQueueServerPacket, &OriginalQueueServerPacket)
            && PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.extendedItemStats.readItemMetadata"),
                ReadItemMetadataRva, metadataExpected.data(), metadataExpected.size(),
                HookReadItemMetadata, &OriginalReadItemMetadata)
            && PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.extendedItemStats.decodeItem"),
                DecodeItemRva, decodeExpected.data(), decodeExpected.size(),
                HookDecodeItem, &OriginalDecodeItem)
            && PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.extendedItemStats.dispatch9C"),
                DispatchItemAction9CRva, dispatch9CExpected.data(), dispatch9CExpected.size(),
                HookDispatchItemAction9C, &OriginalDispatchItemAction9C)
            && PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.extendedItemStats.dispatch9D"),
                DispatchItemAction9DRva, dispatch9DExpected.data(), dispatch9DExpected.size(),
                HookDispatchItemAction9D, &OriginalDispatchItemAction9D);
        if (!transportInstalled) return false;
    }
    if (!Settings.scrollableTooltips) return true;

    const auto hoverTrackingInstalled = PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.extendedItemStats.resolveHoveredUnit"),
        ResolveHoveredUnitRva,
        hoveredUnitExpected.data(),
        static_cast<std::uint32_t>(hoveredUnitExpected.size()),
        HookResolveHoveredUnit,
        &OriginalResolveHoveredUnit)
        && PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.extendedItemStats.resolveHoveredWidget"),
            ResolveHoveredWidgetRva,
            hoveredWidgetExpected.data(),
            static_cast<std::uint32_t>(hoveredWidgetExpected.size()),
            HookResolveHoveredWidget,
            &OriginalResolveHoveredWidget);
    if (!hoverTrackingInstalled) return false;

    const auto statCaptureInstalled = PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.extendedItemStats.getStatsDescription"),
        GetStatsDescriptionRva,
        statsDescriptionExpected.data(),
        static_cast<std::uint32_t>(statsDescriptionExpected.size()),
        HookGetStatsDescription,
        &OriginalGetStatsDescription);
    if (!statCaptureInstalled) return false;

    constexpr std::array<std::uint8_t, 32> tooltipExpected{
        0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,
        0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,
        0x24,0xF8,0xB1,0xFF,0xFF,0xB8,0x08,0x4F,
        0x00,0x00,0xE8,0x41,0x3C,0x01,0x01,0x48};
    TooltipHookInstalled = PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.extendedItemStats.buildItemTooltip"),
        BuildItemTooltipRva,
        tooltipExpected.data(),
        static_cast<std::uint32_t>(tooltipExpected.size()),
        HookBuildItemTooltip,
        &OriginalBuildItemTooltip);
    if (!TooltipHookInstalled) return false;

    return PSh_ManifestInstallInlineHook(
        Context,
        PSH_MANIFEST_SITE("items.extendedItemStats.queueTextLayout"),
        QueueTextLayoutRva,
        queueTextLayoutExpected.data(),
        static_cast<std::uint32_t>(queueTextLayoutExpected.size()),
        HookQueueTextLayout,
        &OriginalQueueTextLayout);
}

auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
    -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "ExtendedItemStats 0.3.17: item transport=%s; max item bytes=%u; max in-flight=%u; timeout=%u ms; tooltip=%s/%u vanilla lines; scroll bar=%s; tooltip owner=%s; hook calls=%llu; expanded stat blocks=%llu; windowed=%llu; scrolls=%llu; oversized=%llu; fragmented=%llu; frames sent=%llu; frames received=%llu; reassembled=%llu; rejected=%llu.",
        TransportEnabled ? "enabled" : "disabled",
        Settings.maxItemBytes,
        Settings.maxInFlightTransfers,
        Settings.reassemblyTimeoutMs,
        Settings.scrollableTooltips ? "enabled" : "disabled",
        NativeTooltipVisibleLines(),
        !ScrollBarEnabled
            ? "disabled"
            : (ruffneck::extended_item_stats::tooltip_overlay::IsReady()
                ? "ready"
                : "pending"),
        TooltipHookInstalled ? "plugin-items" : "none",
        static_cast<unsigned long long>(TooltipHookCalls.load()),
        static_cast<unsigned long long>(StatBlocksExpanded.load()),
        static_cast<unsigned long long>(TooltipsWindowed.load()),
        static_cast<unsigned long long>(TooltipScrolls.load()),
        static_cast<unsigned long long>(OversizedSerializations.load()),
        static_cast<unsigned long long>(FragmentedItemsSent.load()),
        static_cast<unsigned long long>(FramesSent.load()),
        static_cast<unsigned long long>(FramesReceived.load()),
        static_cast<unsigned long long>(ItemsReassembled.load()),
        static_cast<unsigned long long>(TransfersRejected.load()));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}
} // namespace

extern "C" __declspec(dllexport) bool __cdecl
ExtendedItemStatsOwnsTooltipPipeline() noexcept {
    return FeatureEnabled.load(std::memory_order_acquire) && TooltipHookInstalled;
}

extern "C" __declspec(dllexport) void* __cdecl
ExtendedItemStatsTransformTooltip(void* result, void* item) noexcept {
    return FeatureEnabled.load(std::memory_order_acquire)
        ? TransformScrollableTooltip(result, item)
        : result;
}

bool RuffnecKk::ExtendedItemStats::Load(
    const D2RL::PluginContext* context) noexcept {
    if (!context) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    if (!Base) {
        context->LogError("ExtendedItemStats: D2R executable base is unavailable.");
        return false;
    }
    EnsureStringCapacity = reinterpret_cast<EnsureStringCapacityFn>(
        Base + EnsureStringCapacityRva);
    MeasureTextLayout = reinterpret_cast<MeasureTextLayoutFn>(
        Base + MeasureTextLayoutRva);
    UiScale = reinterpret_cast<UiScaleFn>(Base + UiScaleRva);
    NativeWidth = reinterpret_cast<NativeExtentFn>(Base + NativeWidthRva);
    NativeHeight = reinterpret_cast<NativeExtentFn>(Base + NativeHeightRva);
    ClampTextLayout = reinterpret_cast<ClampTextLayoutFn>(
        Base + ClampTextLayoutRva);
    if (context->modDataVersionBuild != 0 && context->modDataVersionBuild != SupportedBuild) {
        context->LogError("ExtendedItemStats: only D2R build 92777 is supported.");
        return false;
    }
    TransportEnabled = ItemTransportEnabled;
    ScrollBarEnabled = ScrollBarEnabledByDefault;
    try {
        ItemReassembler = std::make_unique<Reassembler>(ActiveTransportOptions());
    } catch (const std::exception& exception) {
        const auto message = std::string("ExtendedItemStats: transport initialization failed (")
            + exception.what() + ").";
        context->LogError(message.c_str());
        return false;
    }
    if (!InstallHooks(TransportEnabled)) {
        context->LogError("ExtendedItemStats: native signature or hook installation failed; plugin refused.");
        return false;
    }
    FeatureEnabled.store(true, std::memory_order_release);
    if (Settings.scrollableTooltips && !InstallTooltipInput()) {
        context->LogWarn(
            "ExtendedItemStats: tooltip input listener could not start; scrolling input is unavailable.");
    }
    if (Settings.scrollableTooltips && ScrollBarEnabled) {
        ruffneck::extended_item_stats::tooltip_overlay::SetCallbacks(
            ProvideTooltipOverlaySnapshot,
            ScrollActiveTooltipToRatio,
            ScrollActiveTooltip,
            nullptr);
        HMODULE pluginModule{};
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&InstallHooks),
            &pluginModule);
        if (!pluginModule
            || !ruffneck::extended_item_stats::tooltip_overlay::Install(pluginModule)) {
            context->LogWarn(
                "ExtendedItemStats: native-styled tooltip scrollbar could not start; text scrolling remains available.");
        }
    }
	if (!PSh_RegisterConsoleCommand(context,
            "extended-item-stats", Status, "Show Extended Item Stats status.")) {
        context->LogWarn("ExtendedItemStats: status command could not be registered.");
    }
    const auto message = std::string("ExtendedItemStats 0.3.17 active; item transport=")
        + (TransportEnabled ? "enabled" : "disabled")
        + "; scroll bar="
        + (ScrollBarEnabled ? "enabled" : "disabled")
        + "; tooltip owner="
        + (TooltipHookInstalled ? "plugin-items" : "none")
        + "; built-in patch.";
    context->LogInfo(message.c_str());
    return true;
}

void RuffnecKk::ExtendedItemStats::Unload() noexcept {
    FeatureEnabled.store(false, std::memory_order_release);
    const auto overlayStopped =
        ruffneck::extended_item_stats::tooltip_overlay::Remove();
    const auto inputStopped = RemoveTooltipInput();
    if (!overlayStopped || !inputStopped) {
        if (Context) {
            Context->LogError(
                "ExtendedItemStats: asynchronous workers did not stop cleanly; state and module references are retained for safety.");
        }
        return;
    }
    {
        std::lock_guard lock(ReassemblerMutex);
        ItemReassembler.reset();
    }
    PendingSend.Reset();
    Replay = {};
    {
        std::lock_guard lock(TooltipMutex);
        ActiveTooltip = {};
        ActiveTooltipPhase = TooltipActivationPhase::Armed;
        SuppressedTooltipUnit = nullptr;
    }
    TooltipHookInstalled = false;
    TransportEnabled = false;
    ScrollBarEnabled = false;
    OriginalSerializeItem = nullptr;
    OriginalQueueServerPacket = nullptr;
    OriginalDispatchItemAction9C = nullptr;
    OriginalDispatchItemAction9D = nullptr;
    OriginalReadItemMetadata = nullptr;
    OriginalDecodeItem = nullptr;
    OriginalBuildItemTooltip = nullptr;
    OriginalQueueTextLayout = nullptr;
    OriginalGetStatsDescription = nullptr;
    PendingStatExpansions.clear();
    StatsDescriptionScratch.clear();
    LastTooltipPanel = nullptr;
    LastHoveredUnit = nullptr;
    LastHoveredWidget = nullptr;
    OriginalResolveHoveredUnit = nullptr;
    OriginalResolveHoveredWidget = nullptr;
    MeasureTextLayout = nullptr;
    UiScale = nullptr;
    NativeWidth = nullptr;
    NativeHeight = nullptr;
    ClampTextLayout = nullptr;
    Base = nullptr;
    Context = nullptr;
}
