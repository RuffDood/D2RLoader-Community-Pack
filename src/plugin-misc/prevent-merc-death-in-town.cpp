#include "prevent-merc-death-in-town.h"
#include "prevent-merc-death-in-town-policy.h"

#include <plugin-shared.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace RuffnecKk::PreventMercDeathInTown {
namespace {
constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t ApplyMonsterStatRegenRva = 0x448C00;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t GetUnitBaseStatRva = 0x2F48C0;
constexpr std::uintptr_t GetUnitBaseStatSignatureRva = GetUnitBaseStatRva + 5;
constexpr std::uintptr_t CheckLifeStateMaskRva = 0x335E80;
constexpr std::uintptr_t GetUnitRoomRva = 0x34B440;
constexpr std::uintptr_t IsRoomInTownRva = 0x2F0750;
constexpr std::uintptr_t SetEventRva = 0x48B720;
constexpr std::ptrdiff_t GameFrameOffset = 0x170;
constexpr std::int32_t HitpointsStat = 6;
constexpr std::int32_t HitpointRegenStat = 74;
constexpr std::int32_t StatRegenEvent = 3;
constexpr std::array<std::uint8_t, 32> ApplyMonsterStatRegenExpected{
    0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC, 0x90,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xB6, 0x26,
    0x58, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44,
    0x24, 0x70, 0x48, 0x8B, 0xFA, 0x45, 0x33, 0xC0
};
constexpr std::array<std::uint8_t, 32> GetUnitStatExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8,
    0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9
};
constexpr std::array<std::uint8_t, 50> GetUnitBaseStatExpected{
    0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74,
    0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x0F, 0xB7, 0xE8, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
    0x48, 0x85, 0xC9, 0x75, 0x2A, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x00,
    0xD2, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC,
    0x33, 0xC0
};
constexpr std::array<std::uint8_t, 32> CheckLifeStateMaskExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0xE8, 0x52, 0x42, 0x01, 0x00, 0x0F, 0xB6,
    0xC8, 0xE8, 0xFA, 0xAB, 0xFC, 0xFF, 0x48, 0x8B,
    0xCB, 0x48, 0x8B, 0x90, 0xD0, 0x03, 0x00, 0x00
};
constexpr std::array<std::uint8_t, 32> GetUnitRoomExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x54, 0xA7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01
};
constexpr std::array<std::uint8_t, 32> IsRoomInTownExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x07, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3,
    0x48, 0x8B, 0x49, 0x18, 0xE8, 0x57, 0x08, 0x07,
    0x00, 0x8B, 0xC8, 0x48, 0x83, 0xC4, 0x28, 0xE9
};
constexpr std::array<std::uint8_t, 32> SetEventExpected{
    0x48, 0x83, 0xEC, 0x48, 0x8B, 0x84, 0x24, 0x80,
    0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x38, 0x8B,
    0x44, 0x24, 0x78, 0x89, 0x44, 0x24, 0x30, 0x8B,
    0x44, 0x24, 0x70, 0x89, 0x44, 0x24, 0x28, 0x48
};

using ApplyMonsterStatRegenFn = void(*)(void*, void*, std::int32_t, std::int32_t) noexcept;
using GetUnitStatFn = std::int32_t(*)(void*, std::int32_t, std::int32_t) noexcept;
using CheckLifeStateMaskFn = std::int32_t(*)(void*) noexcept;
using GetUnitRoomFn = void*(*)(void*) noexcept;
using IsRoomInTownFn = std::int32_t(*)(void*) noexcept;
using SetEventFn = void(*)(
    void*, void*, std::int32_t, std::int32_t, std::int32_t, std::int32_t
) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
ApplyMonsterStatRegenFn OriginalApplyMonsterStatRegen{};
GetUnitStatFn GetUnitStat{};
GetUnitStatFn GetUnitBaseStat{};
CheckLifeStateMaskFn CheckLifeStateMask{};
GetUnitRoomFn GetUnitRoom{};
IsRoomInTownFn IsRoomInTown{};
SetEventFn ScheduleEvent{};
std::atomic<std::uint64_t> PreventedDeaths{};

template <typename T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

bool ValidateRuntime() noexcept {
    // Item Durability may own the entry hook at GetUnitBaseStat. Validate its
    // untouched body and call through the live, composable entry instead.
    return Context->CheckExpectedBytes(
            ApplyMonsterStatRegenRva,
            ApplyMonsterStatRegenExpected.data(),
            ApplyMonsterStatRegenExpected.size())
        && Context->CheckExpectedBytes(
            GetUnitStatRva,
            GetUnitStatExpected.data(),
            GetUnitStatExpected.size())
        && Context->CheckExpectedBytes(
            GetUnitBaseStatSignatureRva,
            GetUnitBaseStatExpected.data(),
            GetUnitBaseStatExpected.size())
        && Context->CheckExpectedBytes(
            CheckLifeStateMaskRva,
            CheckLifeStateMaskExpected.data(),
            CheckLifeStateMaskExpected.size())
        && Context->CheckExpectedBytes(
            GetUnitRoomRva,
            GetUnitRoomExpected.data(),
            GetUnitRoomExpected.size())
        && Context->CheckExpectedBytes(
            IsRoomInTownRva,
            IsRoomInTownExpected.data(),
            IsRoomInTownExpected.size())
        && Context->CheckExpectedBytes(
            SetEventRva,
            SetEventExpected.data(),
            SetEventExpected.size());
}

bool IsLethalHirelingTickInTown(void* game, void* unit) noexcept {
    if (!game || !unit) return false;
    __try {
        const auto& view = *static_cast<const D2UnitStrc*>(unit);
        if (PSh_UnitType(view) != D2UnitType::Monster
            || !IsHirelingClass(PSh_UnitClassId(view))) {
            return false;
        }

        auto regen = GetUnitStat(unit, HitpointRegenStat, 0);
        if (CheckLifeStateMask(unit)) {
            regen -= GetUnitBaseStat(unit, HitpointRegenStat, 0);
        }
        const auto hitpoints = GetUnitStat(unit, HitpointsStat, 0);
        if (!IsProjectedLethal(hitpoints, regen)) return false;

        auto* room = GetUnitRoom(unit);
        return room && IsRoomInTown(room) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void HookApplyMonsterStatRegen(
    void* game, void* unit, std::int32_t a3, std::int32_t a4
) noexcept {
    if (!IsLethalHirelingTickInTown(game, unit)) {
        OriginalApplyMonsterStatRegen(game, unit, a3, a4);
        return;
    }

    __try {
        const auto frame = *reinterpret_cast<const std::int32_t*>(
            static_cast<const std::uint8_t*>(game) + GameFrameOffset
        );
        ScheduleEvent(game, unit, StatRegenEvent, frame + 1, 0, 0);
        PreventedDeaths.fetch_add(1, std::memory_order_relaxed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OriginalApplyMonsterStatRegen(game, unit, a3, a4);
    }
}

auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
    -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[320]{};
    std::snprintf(
        message,
        sizeof(message),
        "Prevent Merc Death in Town 0.1.0 (plugin-misc): enabled=%s; prevented lethal ticks=%llu.",
        Settings.enabled ? "true" : "false",
        static_cast<unsigned long long>(PreventedDeaths.load(std::memory_order_relaxed))
    );
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}
} // namespace

bool Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& miscConfig
) noexcept {
    if (!context) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    Settings = {};
    PreventedDeaths.store(0, std::memory_order_relaxed);
    if (!Base) return false;
    if (context->modDataVersionBuild != 0 && context->modDataVersionBuild != SupportedBuild) {
        context->LogError(
            "plugin-misc: Prevent Merc Death in Town supports only D2R build 92777.");
        return false;
    }
    try {
        Settings = ParseConfig(miscConfig);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "plugin-misc: invalid misc.preventMercDeathInTown (")
            + exception.what() + ").";
        context->LogError(message.c_str());
        return false;
    }

    if (Settings.enabled) {
        if (!ValidateRuntime()) {
            context->LogError(
                "plugin-misc: Prevent Merc Death in Town runtime signature mismatch.");
            return false;
        }
        GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
        GetUnitBaseStat = At<GetUnitStatFn>(GetUnitBaseStatRva);
        CheckLifeStateMask = At<CheckLifeStateMaskFn>(CheckLifeStateMaskRva);
        GetUnitRoom = At<GetUnitRoomFn>(GetUnitRoomRva);
        IsRoomInTown = At<IsRoomInTownFn>(IsRoomInTownRva);
        ScheduleEvent = At<SetEventFn>(SetEventRva);
        if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("misc.preventMercDeathInTown.applyMonsterStatRegen"),
            ApplyMonsterStatRegenRva,
            ApplyMonsterStatRegenExpected.data(),
            static_cast<std::uint32_t>(ApplyMonsterStatRegenExpected.size()),
            HookApplyMonsterStatRegen,
            &OriginalApplyMonsterStatRegen
        )) {
            context->LogError(
                "plugin-misc: Prevent Merc Death in Town stat-regen hook failed.");
            return false;
        }
    }

	if (!PSh_RegisterConsoleCommand(context,
            "prevent-merc-death-in-town",
            Status,
            "Show the persistent-damage protection status."
        )) {
        context->LogWarn(
            "plugin-misc: Prevent Merc Death in Town status command was not registered.");
    }
    context->LogInfo(
        Settings.enabled
            ? "plugin-misc: Prevent Merc Death in Town 0.1.0 by RuffnecKk active; config=misc.preventMercDeathInTown."
            : "plugin-misc: Prevent Merc Death in Town 0.1.0 by RuffnecKk disabled; no hook installed; config=misc.preventMercDeathInTown."
    );
    return true;
}

void Unload() noexcept {
    ScheduleEvent = nullptr;
    IsRoomInTown = nullptr;
    GetUnitRoom = nullptr;
    CheckLifeStateMask = nullptr;
    GetUnitBaseStat = nullptr;
    GetUnitStat = nullptr;
    OriginalApplyMonsterStatRegen = nullptr;
    Settings = {};
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::PreventMercDeathInTown
