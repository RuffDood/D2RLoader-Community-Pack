#include "larzuk-sockets.h"
#include <plugin-shared.h>
#include "larzuk-sockets-policy.h"

#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace RuffnecKk::ForceLarzukSockets {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t AddSocketsRva = 0x375560;
constexpr std::uintptr_t GetItemSeedRva = 0x36CC80;
constexpr std::uintptr_t GetItemQualityRva = 0x36CF60;
constexpr std::uintptr_t SetItemFlagRva = 0x36D8F0;
constexpr std::uintptr_t GetMaxSocketsRva = 0x36EAD0;
constexpr std::uintptr_t GetClassIdRva = 0x349860;
constexpr std::uintptr_t GetItemDataContextRva = 0x34A0E0;
constexpr std::uintptr_t GetItemsTxtRecordRva = 0x314110;
constexpr std::uintptr_t GetStatRva = 0x2F5020;
constexpr std::uintptr_t SetUnitStatRva = 0x2F7D10;
constexpr std::uintptr_t LarzukAddSocketsReturnRva = 0x4FD580;
constexpr std::size_t LarzukCallerGameOffset = 0x60;
constexpr std::size_t GameDifficultyOffset = 0x104;
constexpr std::size_t ItemsInventoryWidthOffset = 0x11E;
constexpr std::size_t ItemsInventoryHeightOffset = 0x11F;
constexpr std::uint32_t SocketedItemFlag = 0x800;
constexpr std::int32_t NumberOfSocketsStat = 0xC2;

struct ItemSeed {
    std::uint32_t low{};
    std::uint32_t high{};
};

using AddSocketsFn = void(__fastcall*)(void*, std::int32_t) noexcept;
using GetItemSeedFn = ItemSeed*(__fastcall*)(void*) noexcept;
using GetItemQualityFn = std::int32_t(__fastcall*)(void*) noexcept;
using SetItemFlagFn = void(__fastcall*)(void*, std::uint32_t, std::int32_t) noexcept;
using GetMaxSocketsFn = std::uint8_t(__fastcall*)(void*) noexcept;
using GetClassIdFn = std::uint32_t(__fastcall*)(void*, const char*, int) noexcept;
using GetItemDataContextFn = std::uint8_t(__fastcall*)(void*) noexcept;
using GetItemsTxtRecordFn = std::uint8_t*(__fastcall*)(std::uint8_t, std::int32_t) noexcept;
using GetStatFn = std::int32_t(__fastcall*)(void*, std::int32_t, std::uint32_t) noexcept;
using SetUnitStatFn = void(__fastcall*)(void*, std::int32_t, std::int32_t, std::uint32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
AddSocketsFn OriginalAddSockets{};
GetItemSeedFn GetItemSeed{};
GetItemQualityFn GetItemQuality{};
SetItemFlagFn SetItemFlag{};
GetMaxSocketsFn GetMaxSockets{};
GetClassIdFn GetClassId{};
GetItemDataContextFn GetItemDataContext{};
GetItemsTxtRecordFn GetItemsTxtRecord{};
GetStatFn GetStat{};
SetUnitStatFn SetUnitStat{};
std::atomic<std::uint64_t> ConfiguredRewards{};

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

std::uint32_t AdvanceItemRng(ItemSeed* seed) noexcept {
    const auto next = static_cast<std::uint64_t>(seed->low) * 0x6AC690C5ULL + seed->high;
    seed->low = static_cast<std::uint32_t>(next);
    seed->high = static_cast<std::uint32_t>(next >> 32);
    return seed->low;
}

void* LarzukCallerGame() noexcept {
    void* game{};
    const auto* returnSlot = reinterpret_cast<const std::uint8_t*>(_AddressOfReturnAddress());
    std::memcpy(&game, returnSlot + LarzukCallerGameOffset, sizeof(game));
    return game;
}

std::uint8_t LegalMaximum(void* item) noexcept {
    const auto classId = GetClassId(item, nullptr, 0);
    const auto dataContext = GetItemDataContext(item);
    const auto* itemsRecord = GetItemsTxtRecord(
        dataContext,
        static_cast<std::int32_t>(classId)
    );
    if (!itemsRecord) return 0;
    return EffectiveLegalMaximum(
        GetMaxSockets(item),
        itemsRecord[ItemsInventoryWidthOffset],
        itemsRecord[ItemsInventoryHeightOffset]
    );
}

__declspec(noinline) void __fastcall HookAddSockets(
    void* item,
    std::int32_t vanillaSockets
) noexcept {
    const auto returnRva = reinterpret_cast<std::uintptr_t>(_ReturnAddress())
        - reinterpret_cast<std::uintptr_t>(Base);
    if (returnRva != LarzukAddSocketsReturnRva || !item) {
        OriginalAddSockets(item, vanillaSockets);
        return;
    }

    void* game = LarzukCallerGame();
    if (!game || GetStat(item, NumberOfSocketsStat, 0) > 0) {
        OriginalAddSockets(item, vanillaSockets);
        return;
    }
    const auto difficulty = *(reinterpret_cast<const std::uint8_t*>(game) + GameDifficultyOffset);
    const auto quality = GetItemQuality(item);
    const auto* configured = FindRule(Settings.rules, difficulty, quality);
    if (!configured || !configured->has_value()) {
        OriginalAddSockets(item, vanillaSockets);
        return;
    }

    const auto legalMaximum = LegalMaximum(item);
    if (legalMaximum == 0) {
        OriginalAddSockets(item, vanillaSockets);
        return;
    }
    const auto rule = **configured;
    const auto clampedMinimum = std::min(rule.minSockets, legalMaximum);
    const auto clampedMaximum = std::min(rule.maxSockets, legalMaximum);
    std::uint32_t rawRoll{};
    if (clampedMinimum < clampedMaximum) {
        auto* seed = GetItemSeed(item);
        if (!seed) {
            OriginalAddSockets(item, vanillaSockets);
            return;
        }
        rawRoll = AdvanceItemRng(seed);
    }
    const auto sockets = ResolveSockets(rule, legalMaximum, rawRoll);
    if (sockets == 0) {
        OriginalAddSockets(item, vanillaSockets);
        return;
    }

    SetItemFlag(item, SocketedItemFlag, 1);
    SetUnitStat(item, NumberOfSocketsStat, sockets, 0);
    ++ConfiguredRewards;
}

bool ValidateNativeSignatures() noexcept {
    // GetItemsTxtRecord is deliberately not checked here: plugin-items may own
    // its entry hook for ranged durability while preserving the call contract.
    constexpr std::array<std::uint8_t, 16> expectedGetStat{
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57
    };
    constexpr std::array<std::uint8_t, 16> expectedSetUnitStat{
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56
    };
    constexpr std::array<std::uint8_t, 16> expectedGetClassId{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C
    };
    constexpr std::array<std::uint8_t, 16> expectedGetItemDataContext{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C
    };
    constexpr std::array<std::uint8_t, 16> expectedGetItemSeed{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0x3D
    };
    constexpr std::array<std::uint8_t, 16> expectedGetItemQuality{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0x5D
    };
    constexpr std::array<std::uint8_t, 16> expectedSetItemFlag{
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
        0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41
    };
    constexpr std::array<std::uint8_t, 16> expectedGetMaxSockets{
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
        0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48
    };

    const auto valid = Context->CheckExpectedBytes(
            GetStatRva, expectedGetStat.data(), expectedGetStat.size())
        && Context->CheckExpectedBytes(
            SetUnitStatRva, expectedSetUnitStat.data(), expectedSetUnitStat.size())
        && Context->CheckExpectedBytes(
            GetClassIdRva, expectedGetClassId.data(), expectedGetClassId.size())
        && Context->CheckExpectedBytes(
            GetItemDataContextRva,
            expectedGetItemDataContext.data(),
            expectedGetItemDataContext.size())
        && Context->CheckExpectedBytes(
            GetItemSeedRva, expectedGetItemSeed.data(), expectedGetItemSeed.size())
        && Context->CheckExpectedBytes(
            GetItemQualityRva,
            expectedGetItemQuality.data(),
            expectedGetItemQuality.size())
        && Context->CheckExpectedBytes(
            SetItemFlagRva, expectedSetItemFlag.data(), expectedSetItemFlag.size())
        && Context->CheckExpectedBytes(
            GetMaxSocketsRva,
            expectedGetMaxSockets.data(),
            expectedGetMaxSockets.size());
    if (!valid) {
        Context->LogError("ForceLarzukSockets: native helper signature mismatch; plugin refused.");
    }
    return valid;
}

bool InstallHook() noexcept {
    constexpr std::array<std::uint8_t, 24> expectedAddSockets{
        0x40, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x28,
        0x44, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x48, 0x85,
        0xC9, 0x74, 0x0A, 0xE8, 0x58, 0x64, 0xFD, 0xFF
    };
    if (!PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("quests.larzukSockets.addSockets"),
            AddSocketsRva,
            expectedAddSockets.data(),
            static_cast<std::uint32_t>(expectedAddSockets.size()),
            HookAddSockets,
            &OriginalAddSockets
        )) {
        Context->LogError("ForceLarzukSockets: ITEMS_AddSockets signature mismatch; hook refused.");
        return false;
    }
    return true;
}
} // namespace

bool Load(const D2RL::PluginContext* context, const nlohmann::json& questsConfig) noexcept {
    if (!context) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    if (!Base) {
        context->LogError("plugin-quests: D2R executable base is unavailable.");
        return false;
    }
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError("plugin-quests: ForceLarzukSockets supports only D2R build 92777.");
        return false;
    }

    try {
        Settings = ParseConfig(questsConfig);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "plugin-quests: invalid quests.larzukSockets ("
        ) + exception.what() + ").";
        context->LogError(message.c_str());
        return false;
    }

    const bool active = Settings.enabled && HasRules(Settings.rules);
    if (active && !ValidateNativeSignatures()) return false;

    GetItemSeed = At<GetItemSeedFn>(GetItemSeedRva);
    GetItemQuality = At<GetItemQualityFn>(GetItemQualityRva);
    SetItemFlag = At<SetItemFlagFn>(SetItemFlagRva);
    GetMaxSockets = At<GetMaxSocketsFn>(GetMaxSocketsRva);
    GetClassId = At<GetClassIdFn>(GetClassIdRva);
    GetItemDataContext = At<GetItemDataContextFn>(GetItemDataContextRva);
    GetItemsTxtRecord = At<GetItemsTxtRecordFn>(GetItemsTxtRecordRva);
    GetStat = At<GetStatFn>(GetStatRva);
    SetUnitStat = At<SetUnitStatFn>(SetUnitStatRva);

    if (active && !InstallHook()) return false;

    const auto message = std::string(
        "plugin-quests: ForceLarzukSockets 0.1.0 by RuffnecKk loaded"
    )
        + (active
            ? "; configured Larzuk hook active; config=quests.larzukSockets."
            : "; disabled or fully delegated to vanilla; hook not installed; config=quests.larzukSockets.");
    context->LogInfo(message.c_str());
    return true;
}

void Unload() noexcept {
    Settings = {};
    OriginalAddSockets = nullptr;
    GetItemSeed = nullptr;
    GetItemQuality = nullptr;
    SetItemFlag = nullptr;
    GetMaxSockets = nullptr;
    GetClassId = nullptr;
    GetItemDataContext = nullptr;
    GetItemsTxtRecord = nullptr;
    GetStat = nullptr;
    SetUnitStat = nullptr;
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::ForceLarzukSockets
