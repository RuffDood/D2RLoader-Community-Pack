#include "cube-quick-move.h"
#include <plugin-shared.h>
#include "cube-quick-move-policy.h"

#include <Windows.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace RuffnecKk::CubeQuickMove {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t FindFreePositionRva = 0x3865B0;
constexpr std::uintptr_t GetItemDimensionsRva = 0x371850;
constexpr std::uintptr_t GetItemDataContextRva = 0x34A0E0;
constexpr std::uintptr_t BuildGridContextRva = 0x3C6D80;
constexpr std::uintptr_t ResolveOccupancyGridRva = 0x38B070;

struct CubeCallSite {
    const char* manifestId;
    std::uintptr_t rva;
    std::array<std::uint8_t, 5> expected;
};

constexpr auto MakeCallSite(const char* manifestId, std::uintptr_t rva) noexcept -> CubeCallSite {
    const auto displacement = static_cast<std::uint32_t>(
        FindFreePositionRva - (rva + 5)
    );
    return {
        manifestId,
        rva,
        {
            0xE8,
            static_cast<std::uint8_t>(displacement),
            static_cast<std::uint8_t>(displacement >> 8),
            static_cast<std::uint8_t>(displacement >> 16),
            static_cast<std::uint8_t>(displacement >> 24),
        }
    };
}

// Of 36 direct callers, nine are proven to pass only pages 0, 2 or 4. These
// are the complete 27 sites whose dynamic or explicit page can reach the Cube.
constexpr std::array<CubeCallSite, 27> CubeCallSites{{
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call0FA33D"), 0x0FA33D),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call15A25C"), 0x15A25C),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call15E760"), 0x15E760),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call15F94F"), 0x15F94F),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call2C7306"), 0x2C7306),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call38CC86"), 0x38CC86),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call40FFCB"), 0x40FFCB),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call417356"), 0x417356),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call42AB9F"), 0x42AB9F),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call471D62"), 0x471D62),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4A90C7"), 0x4A90C7),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4AB3B5"), 0x4AB3B5),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4AD94B"), 0x4AD94B),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4B4B5C"), 0x4B4B5C),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4B88ED"), 0x4B88ED),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4BBA73"), 0x4BBA73),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4C21D6"), 0x4C21D6),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4C4181"), 0x4C4181),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4C78D2"), 0x4C78D2),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4F2C8B"), 0x4F2C8B),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4FBC0E"), 0x4FBC0E),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call4FC395"), 0x4FC395),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call527DC2"), 0x527DC2),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call528053"), 0x528053),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call541230"), 0x541230),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call54128F"), 0x54128F),
    MakeCallSite(PSH_MANIFEST_SITE("misc.cubeQuickMoveBottomRight.call541DA7"), 0x541DA7),
}};

constexpr std::array<std::uint8_t, 32> FindFreePositionExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
    0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83,
    0xEC, 0x60, 0x48, 0x8B, 0x05, 0xFF, 0x4C, 0x64,
    0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45, 0xF0
};
constexpr std::array<std::uint8_t, 32> GetItemDimensionsExpected{
    0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C,
    0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20,
    0x49, 0x8B, 0xF0, 0x4C, 0x8B, 0xF2, 0x48, 0x8B,
    0xF9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0x5D
};
constexpr std::array<std::uint8_t, 24> GetItemDataContextExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x49, 0xC7, 0xFF, 0xFF, 0x84
};
constexpr std::array<std::uint8_t, 32> BuildGridContextExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
    0x48, 0x83, 0xEC, 0x30, 0x49, 0x8B, 0xE9, 0x41,
    0x8B, 0xD8, 0x8B, 0xFA, 0xE8, 0xEF, 0x9C, 0xF3
};
constexpr std::array<std::uint8_t, 32> ResolveOccupancyGridExpected{
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x57,
    0x48, 0x83, 0xEC, 0x30, 0x49, 0x8B, 0xF8, 0x48,
    0x39, 0x51, 0x28, 0x0F, 0x86, 0x01, 0x01, 0x00,
    0x00, 0x49, 0x89, 0x73, 0x18, 0x48, 0x8D, 0x71
};

using FindFreePositionFn = std::int32_t(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t*, std::int32_t*, std::uint8_t
) noexcept;
using GetItemDimensionsFn = void(__fastcall*)(
    void*, std::uint8_t*, std::uint8_t*, const char*, std::int32_t
) noexcept;
using GetItemDataContextFn = std::uint8_t(__fastcall*)(void*) noexcept;
using BuildGridContextFn = void(__fastcall*)(
    std::uint8_t, std::int32_t, std::int32_t, void*
) noexcept;
using ResolveOccupancyGridFn = void*(__fastcall*)(
    void*, std::int32_t, void*
) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
void* RelayStub{};
std::uint64_t RelayRva{};
Config Settings{};
FindFreePositionFn FindFreePosition{};
GetItemDimensionsFn GetItemDimensions{};
GetItemDataContextFn GetItemDataContext{};
BuildGridContextFn BuildGridContext{};
ResolveOccupancyGridFn ResolveOccupancyGrid{};
std::atomic<std::uint64_t> CubeCalls{};
std::atomic<std::uint64_t> PageThreeCalls{};
std::atomic<std::uint64_t> RedirectedPlacements{};
std::atomic<std::uint64_t> VanillaPlacements{};
std::atomic<std::uint64_t> SafeFallbacks{};
std::atomic<std::uint64_t> LastCallSiteRva{};
std::atomic<std::uint64_t> LastPageThreeCallSiteRva{};
std::atomic_bool FirstPlacementReported{};
std::atomic_bool FirstFallbackReported{};

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

bool ValidateRuntime() noexcept {
    for (const auto& callSite : CubeCallSites) {
        if (!Context->CheckExpectedBytes(
                callSite.rva,
                callSite.expected.data(),
                callSite.expected.size()
            )) {
            return false;
        }
    }
    return Context->CheckExpectedBytes(
            FindFreePositionRva,
            FindFreePositionExpected.data(),
            FindFreePositionExpected.size())
        && Context->CheckExpectedBytes(
            GetItemDimensionsRva,
            GetItemDimensionsExpected.data(),
            GetItemDimensionsExpected.size())
        && Context->CheckExpectedBytes(
            GetItemDataContextRva,
            GetItemDataContextExpected.data(),
            GetItemDataContextExpected.size())
        && Context->CheckExpectedBytes(
            BuildGridContextRva,
            BuildGridContextExpected.data(),
            BuildGridContextExpected.size())
        && Context->CheckExpectedBytes(
            ResolveOccupancyGridRva,
            ResolveOccupancyGridExpected.data(),
            ResolveOccupancyGridExpected.size());
}

bool ResolveBottomRight(
    void* occupancyGrid,
    std::uint8_t width,
    std::uint8_t height,
    std::int32_t* freeX,
    std::int32_t* freeY
) noexcept {
    if (!occupancyGrid) return false;
    auto* bytes = static_cast<std::uint8_t*>(occupancyGrid);
    const auto gridWidth = bytes[0x10];
    const auto gridHeight = bytes[0x11];
    const auto cells = *reinterpret_cast<std::uintptr_t**>(bytes + 0x18);
    return TryFindBottomRight(
        cells,
        gridWidth,
        gridHeight,
        width,
        height,
        freeX,
        freeY
    );
}

void* AllocateNear(void* hint, std::size_t size) noexcept {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity
    );
    const auto base = reinterpret_cast<std::uintptr_t>(hint)
        & ~(granularity - 1);

    for (std::uintptr_t delta = granularity;
         delta < 0x70000000ULL;
         delta += granularity) {
        if (base <= (std::numeric_limits<std::uintptr_t>::max)() - delta) {
            if (auto* memory = VirtualAlloc(
                    reinterpret_cast<void*>(base + delta),
                    size,
                    MEM_COMMIT | MEM_RESERVE,
                    PAGE_EXECUTE_READWRITE
                )) {
                return memory;
            }
        }
    }
    return nullptr;
}

bool InstallCallSiteRedirect(void* target) noexcept {
    constexpr std::size_t RelaySize = 14;
    auto* callSite = Base + CubeCallSites.front().rva;
    RelayStub = AllocateNear(callSite, RelaySize);
    if (!RelayStub) return false;

    auto* relay = static_cast<std::uint8_t*>(RelayStub);
    relay[0] = 0xFF;
    relay[1] = 0x25;
    relay[2] = 0x00;
    relay[3] = 0x00;
    relay[4] = 0x00;
    relay[5] = 0x00;
    const auto targetAddress = reinterpret_cast<std::uint64_t>(target);
    std::memcpy(relay + 6, &targetAddress, sizeof(targetAddress));
    FlushInstructionCache(GetCurrentProcess(), RelayStub, RelaySize);

    DWORD previousProtection{};
    if (!VirtualProtect(RelayStub, RelaySize, PAGE_EXECUTE_READ, &previousProtection)) {
        VirtualFree(RelayStub, 0, MEM_RELEASE);
        RelayStub = nullptr;
        return false;
    }

    const auto relayAddress = reinterpret_cast<std::uintptr_t>(RelayStub);
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(Base);
    if (relayAddress < baseAddress) {
        VirtualFree(RelayStub, 0, MEM_RELEASE);
        RelayStub = nullptr;
        return false;
    }

    RelayRva = static_cast<std::uint64_t>(relayAddress - baseAddress);
    for (const auto& site : CubeCallSites) {
        if (!PSh_ManifestPatchCallRel32(Context, site.manifestId,
                site.rva,
                site.expected.data(),
                static_cast<std::uint32_t>(site.expected.size()),
                RelayRva,
                static_cast<std::uint32_t>(site.expected.size())
            )) {
            return false;
        }
    }
    return true;
}

std::int32_t __fastcall HookFindFreePosition(
    void* inventory,
    void* item,
    std::int32_t inventoryRecordId,
    std::int32_t* freeX,
    std::int32_t* freeY,
    std::uint8_t page
) noexcept {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto callSiteRva = Base
        && returnAddress >= reinterpret_cast<std::uintptr_t>(Base) + 5
        ? returnAddress - reinterpret_cast<std::uintptr_t>(Base) - 5
        : 0;
    LastCallSiteRva.store(callSiteRva, std::memory_order_relaxed);

    const auto vanillaResult = FindFreePosition(
        inventory,
        item,
        inventoryRecordId,
        freeX,
        freeY,
        page
    );
    CubeCalls.fetch_add(1, std::memory_order_relaxed);
    if (page == CubePage) {
        PageThreeCalls.fetch_add(1, std::memory_order_relaxed);
        LastPageThreeCallSiteRva.store(callSiteRva, std::memory_order_relaxed);
    }

    if (!Settings.enabled || vanillaResult == 0 || page != CubePage
        || !inventory || !item || !freeX || !freeY) {
        VanillaPlacements.fetch_add(1, std::memory_order_relaxed);
        return vanillaResult;
    }

    const auto vanillaX = *freeX;
    const auto vanillaY = *freeY;
    std::uint8_t width{};
    std::uint8_t height{};

    __try {
        GetItemDimensions(item, &width, &height, "CubeQuickMove", 0);
        if (!ShouldRecomputeBottomRight(
                Settings.enabled,
                vanillaResult,
                page,
                width,
                height
            )) {
            VanillaPlacements.fetch_add(1, std::memory_order_relaxed);
            return vanillaResult;
        }

        alignas(8) std::array<std::uint8_t, 32> gridContext{};
        BuildGridContext(
            GetItemDataContext(item),
            inventoryRecordId,
            0,
            gridContext.data()
        );
        auto* occupancyGrid = ResolveOccupancyGrid(
            inventory,
            static_cast<std::int32_t>(page) + 2,
            gridContext.data()
        );
        if (ResolveBottomRight(occupancyGrid, width, height, freeX, freeY)) {
            const auto redirected = RedirectedPlacements.fetch_add(
                1,
                std::memory_order_relaxed
            ) + 1;
            if (Context && !FirstPlacementReported.exchange(
                    true,
                    std::memory_order_relaxed
                )) {
                char message[260]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "plugin-misc: Cube Quick Move by RuffnecKk first redirected call-site RVA 0x%llX: %ux%u at %d,%d (redirected=%llu).",
                    static_cast<unsigned long long>(callSiteRva),
                    static_cast<unsigned>(width),
                    static_cast<unsigned>(height),
                    *freeX,
                    *freeY,
                    static_cast<unsigned long long>(redirected)
                );
                Context->LogInfo(message);
            }
            return 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Restore the proven vanilla result below.
    }

    *freeX = vanillaX;
    *freeY = vanillaY;
    const auto fallbacks = SafeFallbacks.fetch_add(
        1,
        std::memory_order_relaxed
    ) + 1;
    if (Context && !FirstFallbackReported.exchange(
            true,
            std::memory_order_relaxed
        )) {
        char message[240]{};
        std::snprintf(
            message,
            sizeof(message),
            "plugin-misc: Cube Quick Move safe fallback for %ux%u on page %u; kept vanilla %d,%d (fallbacks=%llu).",
            static_cast<unsigned>(width),
            static_cast<unsigned>(height),
            static_cast<unsigned>(page),
            vanillaX,
            vanillaY,
            static_cast<unsigned long long>(fallbacks)
        );
        Context->LogWarn(message);
    }
    return vanillaResult;
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[500]{};
    std::snprintf(
        message,
        sizeof(message),
        "Cube Quick Move 0.1.3: enabled=%s; config=misc.cubeQuickMoveBottomRight; callSites=%llu; calls=%llu; page3=%llu; redirected=%llu; vanilla=%llu; safeFallbacks=%llu; lastCallSite=0x%llX; lastPage3CallSite=0x%llX.",
        Settings.enabled ? "true" : "false",
        static_cast<unsigned long long>(CubeCallSites.size()),
        static_cast<unsigned long long>(CubeCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PageThreeCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RedirectedPlacements.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(VanillaPlacements.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(SafeFallbacks.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(LastCallSiteRva.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(LastPageThreeCallSiteRva.load(std::memory_order_relaxed))
    );
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetCounters() noexcept {
    RelayRva = 0;
    CubeCalls.store(0, std::memory_order_relaxed);
    PageThreeCalls.store(0, std::memory_order_relaxed);
    RedirectedPlacements.store(0, std::memory_order_relaxed);
    VanillaPlacements.store(0, std::memory_order_relaxed);
    SafeFallbacks.store(0, std::memory_order_relaxed);
    LastCallSiteRva.store(0, std::memory_order_relaxed);
    LastPageThreeCallSiteRva.store(0, std::memory_order_relaxed);
    FirstPlacementReported.store(false, std::memory_order_relaxed);
    FirstFallbackReported.store(false, std::memory_order_relaxed);
}

} // namespace

bool Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& miscConfig
) noexcept {
    if (!context) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    ResetCounters();

    try {
        Settings = ParseConfig(miscConfig);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "plugin-misc: invalid misc.cubeQuickMoveBottomRight ("
        ) + exception.what() + ").";
        context->LogError(message.c_str());
        return false;
    }

	if (!PSh_RegisterConsoleCommand(context,
            "cube-quick-move",
            Status,
            "Show Cube quick-move placement status and counters."
        )) {
        context->LogWarn("plugin-misc: Cube Quick Move status command was not registered.");
    }

    if (!Settings.enabled) {
        context->LogInfo(
            "plugin-misc: Cube Quick Move 0.1.3 by RuffnecKk disabled; no redirects installed; config=misc.cubeQuickMoveBottomRight."
        );
        return true;
    }
    if (!Base) {
        context->LogError("plugin-misc: D2R executable base is unavailable.");
        return false;
    }
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError("plugin-misc: Cube Quick Move supports only D2R build 92777.");
        return false;
    }
    if (!ValidateRuntime()) {
        context->LogError(
            "plugin-misc: Cube Quick Move call-site or helper signature mismatch; feature refused."
        );
        return false;
    }

    FindFreePosition = At<FindFreePositionFn>(FindFreePositionRva);
    GetItemDimensions = At<GetItemDimensionsFn>(GetItemDimensionsRva);
    GetItemDataContext = At<GetItemDataContextFn>(GetItemDataContextRva);
    BuildGridContext = At<BuildGridContextFn>(BuildGridContextRva);
    ResolveOccupancyGrid = At<ResolveOccupancyGridFn>(ResolveOccupancyGridRva);

    if (!InstallCallSiteRedirect(reinterpret_cast<void*>(&HookFindFreePosition))) {
        context->LogError("plugin-misc: Cube Quick Move redirects failed.");
        return false;
    }

    char message[360]{};
    std::snprintf(
        message,
        sizeof(message),
        "plugin-misc: Cube Quick Move 0.1.3 by RuffnecKk active; %llu Cube-capable call-sites redirect through relay RVA 0x%llX; config=misc.cubeQuickMoveBottomRight.",
        static_cast<unsigned long long>(CubeCallSites.size()),
        static_cast<unsigned long long>(RelayRva)
    );
    context->LogInfo(message);
    return true;
}

void Unload() noexcept {
    // D2RLoader restores the registered rel32 patches. Keep the relay allocated
    // until process exit so an in-flight native call can never target freed code.
    Settings = {};
    FindFreePosition = nullptr;
    GetItemDimensions = nullptr;
    GetItemDataContext = nullptr;
    BuildGridContext = nullptr;
    ResolveOccupancyGrid = nullptr;
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::CubeQuickMove
