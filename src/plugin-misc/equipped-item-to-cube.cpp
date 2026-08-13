#include "equipped-item-to-cube.h"
#include <plugin-shared.h>
#include "equipped-item-to-cube-policy.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace RuffnecKk::EquippedItemToCube {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;

constexpr std::uintptr_t QueuePacketRva = 0x0EE2A0;
constexpr std::uintptr_t CanQuickMoveItemToCubeRva = 0x15A280;
constexpr std::uintptr_t TransferItemRva = 0x15F8B0;
constexpr std::uintptr_t ResolveHoveredUnitRva = 0x2A7810;
constexpr std::uintptr_t ActualEquippedClickHandlerRva = 0x2CACF0;
constexpr std::uintptr_t GetLocalDataContextRva = 0x08B2D0;
constexpr std::uintptr_t GetLocalPlayerRva = 0x09A480;
constexpr std::uintptr_t GetUnitInventoryRva = 0x34A360;
constexpr std::uintptr_t GetEquippedItemRva = 0x3886D0;

constexpr std::array<std::uint8_t, 32> QueuePacketExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
    0x48, 0x81, 0xEC, 0x30, 0x02, 0x00, 0x00, 0x48,
    0x8B, 0x05, 0x12, 0xD0, 0x8D, 0x02, 0x48, 0x33,
    0xC4, 0x48, 0x89, 0x84, 0x24, 0x20, 0x02, 0x00,
};
constexpr std::array<std::uint8_t, 32> ActualEquippedClickHandlerExpected{
    0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x55, 0x57,
    0x41, 0x55, 0x48, 0x83, 0xEC, 0x78, 0x48, 0x8B,
    0xD9, 0xE8, 0x0A, 0xCB, 0xFD, 0xFF, 0x41, 0xB8,
    0x1A, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x4D,
};
constexpr std::array<std::uint8_t, 32> CanQuickMoveItemToCubeExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0xB9, 0x19, 0x00, 0x00, 0x00, 0xE8, 0x6D,
    0x42, 0xF7, 0xFF, 0x84, 0xC0, 0x75, 0x17, 0xB9,
    0x18, 0x00, 0x00, 0x00, 0xE8, 0x5F, 0x42, 0xF7,
};
constexpr std::array<std::uint8_t, 32> TransferItemExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC,
    0x24, 0x08, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC,
    0xF8, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xF5,
};
constexpr std::array<std::uint8_t, 16> GetLocalDataContextExpected{
    0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 32> GetLocalPlayerExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85,
    0x00, 0x00, 0x00, 0x8B, 0xD9, 0x48, 0x89, 0x5C,
    0x24, 0x38, 0x48, 0x83, 0xFB, 0x08, 0x72, 0x19,
};
constexpr std::array<std::uint8_t, 32> GetUnitInventoryExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x70, 0xCC, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetEquippedItemExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x63, 0xFA, 0x48, 0x8B, 0xD9,
    0x48, 0x85, 0xC9, 0x75, 0x20, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0xED,
};

struct TransferPlacement {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint8_t valid{};
    std::uint8_t padding[3]{};
};
static_assert(sizeof(TransferPlacement) == 12);

using QueuePacketFn = void(__fastcall*)(const std::uint8_t*, std::int32_t) noexcept;
using ActualEquippedClickHandlerFn = void(__fastcall*)(void*, void*) noexcept;
using CanQuickMoveItemToCubeFn = bool(__fastcall*)(void*) noexcept;
using TransferItemFn = bool(__fastcall*)(
    void*, void*, std::uint8_t, std::uint8_t, std::uint8_t, void*) noexcept;
using ResolveHoveredUnitFn = void*(__fastcall*)(void*) noexcept;
using GetLocalDataContextFn = std::int32_t(__fastcall*)() noexcept;
using GetLocalPlayerFn = void*(__fastcall*)(std::int32_t) noexcept;
using GetUnitInventoryFn = void*(__fastcall*)(void*, const char*, int) noexcept;
using GetEquippedItemFn = void*(__fastcall*)(void*, std::uint32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
QueuePacketFn OriginalQueuePacket{};
ActualEquippedClickHandlerFn OriginalActualEquippedClickHandler{};
CanQuickMoveItemToCubeFn CanQuickMoveItemToCube{};
TransferItemFn TransferItem{};
ResolveHoveredUnitFn ResolveHoveredUnit{};
GetLocalDataContextFn GetLocalDataContext{};
GetLocalPlayerFn GetLocalPlayer{};
GetUnitInventoryFn GetUnitInventory{};
GetEquippedItemFn GetEquippedItem{};
thread_local bool RewriteArmed{};
thread_local std::uint32_t RewriteBodyLocation{};

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

bool ValidateRuntime() noexcept {
    // ResolveHoveredUnit is intentionally not revalidated here. plugin-items
    // owns its composable inline hook, so plugin-misc calls through that hook.
    return Context->CheckExpectedBytes(
            QueuePacketRva, QueuePacketExpected.data(), QueuePacketExpected.size())
        && Context->CheckExpectedBytes(
            ActualEquippedClickHandlerRva,
            ActualEquippedClickHandlerExpected.data(),
            ActualEquippedClickHandlerExpected.size())
        && Context->CheckExpectedBytes(
            CanQuickMoveItemToCubeRva,
            CanQuickMoveItemToCubeExpected.data(),
            CanQuickMoveItemToCubeExpected.size())
        && Context->CheckExpectedBytes(
            TransferItemRva, TransferItemExpected.data(), TransferItemExpected.size())
        && Context->CheckExpectedBytes(
            GetLocalDataContextRva,
            GetLocalDataContextExpected.data(),
            GetLocalDataContextExpected.size())
        && Context->CheckExpectedBytes(
            GetLocalPlayerRva,
            GetLocalPlayerExpected.data(),
            GetLocalPlayerExpected.size())
        && Context->CheckExpectedBytes(
            GetUnitInventoryRva,
            GetUnitInventoryExpected.data(),
            GetUnitInventoryExpected.size())
        && Context->CheckExpectedBytes(
            GetEquippedItemRva,
            GetEquippedItemExpected.data(),
            GetEquippedItemExpected.size());
}

class RewriteScope {
public:
    explicit RewriteScope(std::uint32_t bodyLocation) noexcept
        : previousArmed_(RewriteArmed),
          previousBodyLocation_(RewriteBodyLocation) {
        RewriteBodyLocation = bodyLocation;
        RewriteArmed = true;
    }

    ~RewriteScope() noexcept {
        RewriteArmed = previousArmed_;
        RewriteBodyLocation = previousBodyLocation_;
    }

private:
    bool previousArmed_{};
    std::uint32_t previousBodyLocation_{};
};

void __fastcall HookQueuePacket(
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    if (packet && size == static_cast<std::int32_t>(ItemTransferPacketSize)) {
        ItemTransferPacket inventoryPacket{};
        std::memcpy(inventoryPacket.data(), packet, inventoryPacket.size());
        if (ShouldRewriteCubeTransfer(
                RewriteArmed, inventoryPacket, RewriteBodyLocation)) {
            const auto equippedPacket = RewriteAsEquippedTransfer(
                inventoryPacket, RewriteBodyLocation);
            OriginalQueuePacket(equippedPacket.data(), size);
            return;
        }
    }
    OriginalQueuePacket(packet, size);
}

void __fastcall HookActualEquippedClickHandler(
    void* controller,
    void* eventState
) noexcept {
    const bool controlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (controlDown && controller) {
        std::uint32_t bodyLocation{};
        std::memcpy(
            &bodyLocation,
            static_cast<const std::uint8_t*>(controller) + 0x5D8,
            sizeof(bodyLocation));

        void* player = ResolveHoveredUnit(controller);
        void* localPlayer = GetLocalPlayer(GetLocalDataContext());
        void* inventory = player
            ? GetUnitInventory(player, __FILE__, __LINE__)
            : nullptr;
        void* item = inventory && IsEquippedBodyLocation(bodyLocation)
            ? GetEquippedItem(inventory, bodyLocation)
            : nullptr;

        if (player && player == localPlayer && item
            && CanQuickMoveItemToCube(item)) {
            TransferPlacement placement{};
            const RewriteScope rewriteScope(bodyLocation);
            if (TransferItem(item, player, 3, 0, 1, &placement)) return;
        }
    }
    OriginalActualEquippedClickHandler(controller, eventState);
}

bool InstallHooks() noexcept {
    if (!PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("misc.equippedItemToCube.queuePacket"),
            QueuePacketRva,
            QueuePacketExpected.data(),
            static_cast<std::uint32_t>(QueuePacketExpected.size()),
            HookQueuePacket,
            &OriginalQueuePacket)) {
        Context->LogError(
            "plugin-misc: Equipped Item to Cube outgoing packet hook refused.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("misc.equippedItemToCube.equippedClick"),
            ActualEquippedClickHandlerRva,
            ActualEquippedClickHandlerExpected.data(),
            static_cast<std::uint32_t>(ActualEquippedClickHandlerExpected.size()),
            HookActualEquippedClickHandler,
            &OriginalActualEquippedClickHandler)) {
        Context->LogError(
            "plugin-misc: Equipped Item to Cube equipped-slot hook refused.");
        return false;
    }
    return true;
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[260]{};
    std::snprintf(
        message,
        sizeof(message),
        "Equipped Item to Cube 0.2.0: enabled=%s; config=misc.equippedItemToCube.",
        Settings.enabled ? "true" : "false");
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Settings = {};
    OriginalQueuePacket = nullptr;
    OriginalActualEquippedClickHandler = nullptr;
    CanQuickMoveItemToCube = nullptr;
    TransferItem = nullptr;
    ResolveHoveredUnit = nullptr;
    GetLocalDataContext = nullptr;
    GetLocalPlayer = nullptr;
    GetUnitInventory = nullptr;
    GetEquippedItem = nullptr;
    RewriteArmed = false;
    RewriteBodyLocation = 0;
}

} // namespace

bool Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& miscConfig
) noexcept {
    if (!context) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    ResetState();

    try {
        Settings = ParseConfig(miscConfig);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "plugin-misc: invalid misc.equippedItemToCube (")
            + exception.what() + ").";
        context->LogError(message.c_str());
        return false;
    }

	if (!PSh_RegisterConsoleCommand(context,
            "equipped-item-to-cube",
            Status,
            "Show equipped-item Ctrl-click status.")) {
        context->LogWarn(
            "plugin-misc: Equipped Item to Cube status command was not registered.");
    }

    if (!Settings.enabled) {
        context->LogInfo(
            "plugin-misc: Equipped Item to Cube 0.2.0 by RuffnecKk disabled; no hooks installed; config=misc.equippedItemToCube.");
        return true;
    }
    if (!Base) {
        context->LogError("plugin-misc: D2R executable base is unavailable.");
        return false;
    }
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError(
            "plugin-misc: Equipped Item to Cube supports only D2R build 92777.");
        return false;
    }
    if (!ValidateRuntime()) {
        context->LogError(
            "plugin-misc: Equipped Item to Cube hook or helper signature mismatch; feature refused.");
        return false;
    }

    CanQuickMoveItemToCube = At<CanQuickMoveItemToCubeFn>(
        CanQuickMoveItemToCubeRva);
    TransferItem = At<TransferItemFn>(TransferItemRva);
    ResolveHoveredUnit = At<ResolveHoveredUnitFn>(ResolveHoveredUnitRva);
    GetLocalDataContext = At<GetLocalDataContextFn>(GetLocalDataContextRva);
    GetLocalPlayer = At<GetLocalPlayerFn>(GetLocalPlayerRva);
    GetUnitInventory = At<GetUnitInventoryFn>(GetUnitInventoryRva);
    GetEquippedItem = At<GetEquippedItemFn>(GetEquippedItemRva);

    if (!InstallHooks()) return false;

    context->LogInfo(
        "plugin-misc: Equipped Item to Cube 0.2.0 by RuffnecKk active; Ctrl-left-click equipped transfers target the Cube; config=misc.equippedItemToCube.");
    return true;
}

void Unload() noexcept {
    ResetState();
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::EquippedItemToCube
