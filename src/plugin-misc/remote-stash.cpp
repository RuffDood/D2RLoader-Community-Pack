#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <D2RLPlugin/api.h>
#include <plugin-shared.h>
#include "remote-stash.h"
#include "remote-stash-policy.h"
#include "remote-stash-layout-policy.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using ruffneckk::remote_stash::CompanionInventoryCloseDecision;
using ruffneckk::remote_stash::ExactModifiersMatch;
using ruffneckk::remote_stash::HotkeyConfig;
using ruffneckk::remote_stash::HotkeyDispatch;
using ruffneckk::remote_stash::IsMouseHotkey;
using ruffneckk::remote_stash::IsExpectedEmbeddedMessage;
using ruffneckk::remote_stash::IsUsableLayoutOwnedButton;
using ruffneckk::remote_stash::ParseHotkeyConfig;
using ruffneckk::remote_stash::ResolveHotkeyDispatch;
using ruffneckk::remote_stash::ResolveCompanionInventoryClose;
using ruffneckk::remote_stash::ResolveRemoteStashTransitionFlag;
using ruffneckk::remote_stash::ShouldSuppressHotkeyMouseReset;
using ruffneckk::remote_stash::ShouldRestoreIndependentInventory;
using ruffneckk::remote_stash::ShouldSuppressMovementInventoryClose;
using ruffneckk::remote_stash::ShouldSuppressRemoteStashClose;
using ruffneckk::remote_stash::WidgetRect;

constexpr std::uint32_t SupportedBuild = 92777;
constexpr WPARAM DispatchRequestCookie = 0x5253484B; // RSHK

constexpr std::uintptr_t ConfigurePlayerInventoryRva = 0x22BA70;
constexpr std::uintptr_t DispatchUiMessageRva = 0x843D90;
constexpr std::uintptr_t ButtonDispatchUiMessageCallRva = 0x8F1069;
constexpr std::uintptr_t QueueOutgoingPacketRva = 0xEE2A0;
constexpr std::uintptr_t SharedGoldDepositRva = 0x14F5330;
constexpr std::uintptr_t SharedGoldDepositCallRva = 0x14F5895;
constexpr std::uintptr_t RemoveItemHandlerRva = 0x4AA100;
constexpr std::uintptr_t InsertItemHandlerRva = 0x4BFF30;
constexpr std::uintptr_t SharedDepositHandlerRva = 0x4C5570;
constexpr std::uintptr_t SharedWithdrawalHandlerRva = 0x4C6480;
constexpr std::uintptr_t ValidateItemPacketStateRva = 0x474700;
constexpr std::uintptr_t GoldButtonHandlerRva = 0x4BA580;
constexpr std::uintptr_t GoldRangeGateRva = 0x4BA617;
constexpr std::uintptr_t GoldRangeBypassRva = 0x4BA6A1;
constexpr std::uintptr_t SendServerUiRva = 0x480650;
constexpr std::uintptr_t GetClientFromPlayerRva = 0x48FDE0;
constexpr std::uintptr_t RemoveServerUnitRva = 0x43EC10;
constexpr std::uintptr_t GetLocalDataContextRva = 0x8B2D0;
constexpr std::uintptr_t GetLocalPlayerRva = 0x9A480;
constexpr std::uintptr_t IsRoomInTownRva = 0x2F0750;
constexpr std::uintptr_t TransferItemToInventoryPageRva = 0x15F8B0;
constexpr std::uintptr_t GetUiStateRva = 0xCE500;
constexpr std::uintptr_t OpenInterfaceStateRva = 0xCD7C0;
constexpr std::uintptr_t CloseInterfaceStateRva = 0xC7D30;
constexpr std::uintptr_t MovementUiCloseRva = 0xC8240;
constexpr std::uintptr_t MovementUiCloseCallRva = 0x102590;
constexpr std::uintptr_t StashInterfaceTransitionRva = 0x11FB80;
constexpr std::uintptr_t StashInterfaceTransitionCallRva = 0xC1F01;
constexpr std::uintptr_t ResetMouseInputStateRva = 0x8D510;
constexpr std::uintptr_t ResetMouseInputStateWithFinalizeRva = 0x8D540;
constexpr std::uintptr_t MouseInputState0Rva = 0x2A23454;
constexpr std::uintptr_t MouseInputState1Rva = 0x2A23478;
constexpr std::uintptr_t MouseInputState2Rva = 0x2A236EA;
constexpr std::uintptr_t MouseInputState3Rva = 0x2A23480;
constexpr std::uintptr_t MouseInputState4Rva = 0x2A23484;
constexpr std::uintptr_t MouseInputState5Rva = 0x2A236EB;
constexpr std::uintptr_t MarkUiDirtyRva = 0x843FC0;
constexpr std::uintptr_t FindTopLevelPanelRva = 0x846170;
constexpr std::uintptr_t FindWidgetRva = 0x856220;
constexpr std::uintptr_t GetWidgetRectRva = 0x8562A0;

// These two return addresses are the only client stash lifecycle checks that
// may observe the synthetic remote session as a town-backed stash session.
constexpr std::uintptr_t ClientStashTownCheckReturnRva = 0x259137;
constexpr std::uintptr_t ClientStashCleanupTownCheckReturnRva = 0x25A122;
constexpr std::uintptr_t QuickMoveItemInitTownCheckReturnRva = 0xFEE3B;
constexpr std::uintptr_t QuickMoveItemPlacementTownCheckReturnRva = 0xFF1DC;
constexpr std::uintptr_t QuickMoveStashUiStateReturnRva = 0x15F982;
constexpr std::uintptr_t QuickMoveToStashItemPacketStateReturnRva = 0x473F80;
constexpr std::uintptr_t QuickMoveFromStashItemPacketStateReturnRva = 0x474257;
constexpr std::uintptr_t ServerUiCloseStashReturnRva = 0x1F0AD8;
constexpr std::uintptr_t StashPanelCloseButtonReturnRva = 0x23C150;
constexpr std::uintptr_t GeneralUiTeardownStashReturnRva = 0x22A9FE;
constexpr std::size_t WidgetRectOffset = 0x70;
constexpr std::size_t WidgetVisibleOffset = 0x51;
constexpr std::size_t ButtonOnClickMessageOffset = 0x558;
constexpr std::int32_t StashInterfaceState = 0x18;
constexpr std::int32_t InventoryInterfaceState = 1;
constexpr std::uint64_t CompanionInventoryCloseWindowMs = 2000;
constexpr std::uint64_t HotkeyOpenTransitionWindowMs = 2000;

constexpr std::array<std::uint8_t, 32> ConfigurePlayerInventoryExpected{
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x55,
    0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D,
    0x6C, 0x24, 0x90, 0x48, 0x81, 0xEC, 0x70, 0x01,
    0x00, 0x00, 0x48, 0x8B, 0x05, 0x37, 0xF8, 0x79
};
constexpr std::array<std::uint8_t, 32> DispatchUiMessageExpected{
    0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x20,
    0x4C, 0x89, 0x7C, 0x24, 0x58, 0x4C, 0x8B, 0xF9,
    0xE8, 0x7B, 0x1C, 0xA6, 0x00, 0x0F, 0xB6, 0x90,
    0x18, 0x01, 0x00, 0x00, 0x84, 0xD2, 0x74, 0x6F
};
constexpr std::array<std::uint8_t, 32> InsertItemHandlerExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48,
    0x8D, 0x6C, 0x24, 0xF0, 0x48, 0x81, 0xEC, 0x10,
    0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x7E, 0xB3,
    0x50, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45
};
constexpr std::array<std::uint8_t, 32> RemoveItemHandlerExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48,
    0x8D, 0x6C, 0x24, 0xF0, 0x48, 0x81, 0xEC, 0x10,
    0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xAE, 0x11,
    0x52, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45
};
constexpr std::array<std::uint8_t, 32> SharedDepositHandlerExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x48,
    0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xF0,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x3E, 0x5D,
    0x50, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45
};
constexpr std::array<std::uint8_t, 32> SharedWithdrawalHandlerExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x48,
    0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xF0,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x2E, 0x4E,
    0x50, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45
};
constexpr std::array<std::uint8_t, 32> ValidateItemPacketStateExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0xD9, 0x41, 0x0F, 0xB6, 0xF0, 0x48, 0x8B,
    0x09, 0x48, 0x8B, 0xFA, 0xE8, 0xAF, 0x72, 0xED
};
constexpr std::array<std::uint8_t, 32> GoldButtonHandlerExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x57, 0x48,
    0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0x00,
    0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x2E, 0x0D,
    0x51, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45
};
constexpr std::array<std::uint8_t, 15> GoldRangeGateExpected{
    0x44, 0x39, 0x7D, 0xBF, 0x0F, 0x86, 0x39, 0x0E,
    0x00, 0x00, 0x48, 0x85, 0xFF, 0x75, 0x7B
};
constexpr std::array<std::uint8_t, 16> SendServerUiExpected{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0xC6, 0x44,
    0x24, 0x50, 0x77, 0x48, 0x8B, 0xF9, 0x88, 0x54
};
constexpr std::array<std::uint8_t, 16> GetClientFromPlayerExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x74, 0x1E, 0xE8, 0xDD
};
constexpr std::array<std::uint8_t, 16> RemoveServerUnitExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0xDA
};
constexpr std::array<std::uint8_t, 16> GetLocalDataContextExpected{
    0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC
};
constexpr std::array<std::uint8_t, 16> GetLocalPlayerExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85
};
constexpr std::array<std::uint8_t, 16> IsRoomInTownExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x07, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3
};
constexpr std::array<std::uint8_t, 16> TransferItemToInventoryPageExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC
};
constexpr std::array<std::uint8_t, 15> GetUiStateExpected{
    0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x96, 0xC8,
    0x95, 0x02, 0x0F, 0xB6, 0x04, 0x08, 0xC3
};
constexpr std::array<std::uint8_t, 68> OpenInterfaceStateExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41,
    0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC,
    0x24, 0x40, 0xFD, 0xFF, 0xFF, 0x48, 0x81, 0xEC,
    0xC0, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xE5,
    0xDA, 0x8F, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89,
    0x85, 0xB0, 0x02, 0x00, 0x00, 0x48, 0x63, 0xD9,
    0x4C, 0x8D, 0x25, 0x09, 0x28, 0xF3, 0xFF, 0x44,
    0x0F, 0xB6, 0xF2, 0x45, 0x0F, 0xB6, 0xBC, 0x1C,
    0xA0, 0xAD, 0xA2, 0x02,
};
constexpr std::array<std::uint8_t, 64> CloseInterfaceStateExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xE0, 0xFD,
    0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x20, 0x03, 0x00,
    0x00, 0x48, 0x8B, 0x05, 0x70, 0x35, 0x90, 0x02,
    0x48, 0x33, 0xC4, 0x48, 0x89, 0x85, 0x10, 0x02,
    0x00, 0x00, 0x48, 0x63, 0xD9, 0x4C, 0x8D, 0x25,
    0x94, 0x82, 0xF3, 0xFF, 0x0F, 0xB6, 0xF2, 0x46,
};
constexpr std::array<std::uint8_t, 20> StashInterfaceTransitionExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x0F, 0xB6, 0xFA, 0x8B, 0xD9, 0xE8,
    0xAC, 0x75, 0x72, 0x00,
};
constexpr std::array<std::uint8_t, 32> ResetMouseInputStateExpected{
    0x33, 0xC0, 0x89, 0x05, 0x3C, 0x5F, 0x99, 0x02,
    0x89, 0x05, 0x5A, 0x5F, 0x99, 0x02, 0x88, 0x05,
    0xC6, 0x61, 0x99, 0x02, 0x89, 0x05, 0x56, 0x5F,
    0x99, 0x02, 0x89, 0x05, 0x54, 0x5F, 0x99, 0x02,
};
constexpr std::array<std::uint8_t, 32> ResetMouseInputStateWithFinalizeExpected{
    0x8B, 0x0D, 0xBE, 0x61, 0x99, 0x02, 0x33, 0xC0,
    0x89, 0x05, 0x06, 0x5F, 0x99, 0x02, 0x89, 0x05,
    0x24, 0x5F, 0x99, 0x02, 0x88, 0x05, 0x90, 0x61,
    0x99, 0x02, 0x89, 0x05, 0x20, 0x5F, 0x99, 0x02,
};
constexpr std::array<std::uint8_t, 20> MarkUiDirtyExpected{
    0x48, 0x8B, 0x05, 0xA9, 0xC1, 0xBF, 0x02, 0x48,
    0x85, 0xC0, 0x74, 0x07, 0xC6, 0x80, 0xB8, 0x00,
    0x00, 0x00, 0x01, 0xC3,
};
constexpr std::array<std::uint8_t, 22> FindTopLevelPanelExpected{
    0x48, 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0xF6, 0x9F,
    0xBF, 0x02, 0x48, 0x85, 0xC9, 0x0F, 0x85, 0xDD,
    0x95, 0x05, 0x00, 0x33, 0xC0, 0xC3
};
constexpr std::array<std::uint8_t, 32> FindWidgetExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x59, 0x58, 0x48, 0x8B, 0xF2, 0x48, 0x8B,
    0x41, 0x60, 0x48, 0x8D, 0x3C, 0xC3, 0x48, 0x3B
};
constexpr std::array<std::uint8_t, 32> GetWidgetRectExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x80, 0x79,
    0x52, 0x00, 0x48, 0x8B, 0xDA, 0x74, 0x2A, 0x48,
    0x8B, 0x49, 0x30, 0x48, 0x8D, 0x54, 0x24, 0x20,
    0xE8, 0xE3, 0xFF, 0xFF, 0xFF, 0x33, 0xC0, 0x48
};

struct RelativeCallSite {
    const char* manifestId{};
    std::uintptr_t rva{};
    std::array<std::uint8_t, 5> expected{};
};

constexpr std::array<RelativeCallSite, 1> ButtonDispatchUiMessageCallSites{{
    {PSH_MANIFEST_SITE("misc.remoteStash.buttonDispatchCall"),
        ButtonDispatchUiMessageCallRva, {0xE8, 0x22, 0x2D, 0xF5, 0xFF}},
}};
constexpr std::array<RelativeCallSite, 1> StashInterfaceTransitionCallSites{{
    {PSH_MANIFEST_SITE("misc.remoteStash.stashTransitionCall"),
        StashInterfaceTransitionCallRva, {0xE8, 0x7A, 0xDC, 0x05, 0x00}},
}};
constexpr std::array<RelativeCallSite, 1> MovementUiCloseCallSites{{
    {PSH_MANIFEST_SITE("misc.remoteStash.movementUiCloseCall"),
        MovementUiCloseCallRva, {0xE8, 0xAB, 0x5C, 0xFC, 0xFF}},
}};
constexpr std::array<RelativeCallSite, 1> SharedGoldDepositCallSites{{
    {PSH_MANIFEST_SITE("misc.remoteStash.sharedGoldDepositCall"),
        SharedGoldDepositCallRva, {0xE8, 0x96, 0xFA, 0xFF, 0xFF}},
}};
constexpr std::array<RelativeCallSite, 4> IsRoomInTownCallSites{{
    {PSH_MANIFEST_SITE("misc.remoteStash.townCheck.clientLifecycle"),
        0x259132, {0xE8, 0x19, 0x76, 0x09, 0x00}},
    {PSH_MANIFEST_SITE("misc.remoteStash.townCheck.clientCleanup"),
        0x25A11D, {0xE8, 0x2E, 0x66, 0x09, 0x00}},
    {PSH_MANIFEST_SITE("misc.remoteStash.townCheck.quickMoveInit"),
        0x0FEE36, {0xE8, 0x15, 0x19, 0x1F, 0x00}},
    {PSH_MANIFEST_SITE("misc.remoteStash.townCheck.quickMovePlacement"),
        0x0FF1D7, {0xE8, 0x74, 0x15, 0x1F, 0x00}},
}};
constexpr std::array<RelativeCallSite, 8> TransferItemToInventoryPageCallSites{{
    {PSH_MANIFEST_SITE("misc.remoteStash.transferItem.15E382"),
        0x15E382, {0xE8, 0x29, 0x15, 0x00, 0x00}},
    {PSH_MANIFEST_SITE("misc.remoteStash.transferItem.1F6674"),
        0x1F6674, {0xE8, 0x37, 0x92, 0xF6, 0xFF}},
    {PSH_MANIFEST_SITE("misc.remoteStash.transferItem.2A9D68"),
        0x2A9D68, {0xE8, 0x43, 0x5B, 0xEB, 0xFF}},
    {PSH_MANIFEST_SITE("misc.remoteStash.transferItem.2AAB0D"),
        0x2AAB0D, {0xE8, 0x9E, 0x4D, 0xEB, 0xFF}},
    {PSH_MANIFEST_SITE("misc.remoteStash.transferItem.2ABB09"),
        0x2ABB09, {0xE8, 0xA2, 0x3D, 0xEB, 0xFF}},
    {PSH_MANIFEST_SITE("misc.remoteStash.transferItem.2C6055"),
        0x2C6055, {0xE8, 0x56, 0x98, 0xE9, 0xFF}},
    {PSH_MANIFEST_SITE("misc.remoteStash.transferItem.2C6265"),
        0x2C6265, {0xE8, 0x46, 0x96, 0xE9, 0xFF}},
    {PSH_MANIFEST_SITE("misc.remoteStash.transferItem.2C7479"),
        0x2C7479, {0xE8, 0x32, 0x84, 0xE9, 0xFF}},
}};

using ConfigurePlayerInventoryFn = void(__fastcall*)(void* panel) noexcept;
using DispatchUiMessageFn = void(__fastcall*)(void* message) noexcept;
using QueueOutgoingPacketFn = void(__fastcall*)(const std::uint8_t* packet, std::int32_t size) noexcept;
using SharedGoldDepositFn = std::int32_t(__fastcall*)(
    void* panel,
    std::int32_t amount
) noexcept;
using ServerPacketHandlerFn = std::int32_t(__fastcall*)(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept;
using ValidateItemPacketStateFn = bool(__fastcall*)(
    void* transaction,
    const void* packetState,
    bool bypassStashProximity
) noexcept;
using SendServerUiFn = void(__fastcall*)(void* client, std::uint8_t action) noexcept;
using GetClientFromPlayerFn = void*(__fastcall*)(void* player) noexcept;
using RemoveServerUnitFn = void(__fastcall*)(void* game, void* unit) noexcept;
using GetLocalDataContextFn = std::int32_t(__fastcall*)() noexcept;
using GetLocalPlayerFn = void*(__fastcall*)(std::int32_t context) noexcept;
using IsRoomInTownFn = std::int32_t(__fastcall*)(void* room) noexcept;
using TransferItemToInventoryPageFn = bool(__fastcall*)(
    void* item,
    void* destinationUnit,
    std::uint8_t inventoryPage,
    std::uint8_t destinationKind,
    bool transferMode,
    void* placementOut
) noexcept;
using GetUiStateFn = std::uint8_t(__fastcall*)(std::int32_t state) noexcept;
using OpenInterfaceStateFn = bool(__fastcall*)(
    std::int32_t state,
    bool secondary
) noexcept;
using CloseInterfaceStateFn = void(__fastcall*)(
    std::int32_t state,
    bool secondary
) noexcept;
using MovementUiCloseFn = void(__fastcall*)(
    std::int32_t closeMode,
    std::int32_t secondary
) noexcept;
using StashInterfaceTransitionFn = void(__fastcall*)(
    std::int32_t mode,
    bool transitionFlag
) noexcept;
using ResetMouseInputStateFn = void(__fastcall*)() noexcept;
using MarkUiDirtyFn = void(__fastcall*)() noexcept;
using FindTopLevelPanelFn = void*(__fastcall*)(const char* name) noexcept;
using FindWidgetFn = void*(__fastcall*)(void* panel, const char* name) noexcept;
using GetWidgetRectFn = WidgetRect*(__fastcall*)(
    void* widget,
    WidgetRect* rectOut
) noexcept;
using SetWidgetBoolFn = void(__fastcall*)(void* widget, bool value) noexcept;
using UiMessageInterceptorFn = bool(__fastcall*)(void* message) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
ConfigurePlayerInventoryFn OriginalConfigurePlayerInventory{};
DispatchUiMessageFn OriginalDispatchUiMessage{};
QueueOutgoingPacketFn QueueOutgoingPacket{};
SharedGoldDepositFn OriginalSharedGoldDeposit{};
ServerPacketHandlerFn OriginalRemoveItemHandler{};
ServerPacketHandlerFn OriginalInsertItemHandler{};
ServerPacketHandlerFn OriginalSharedDepositHandler{};
ServerPacketHandlerFn OriginalSharedWithdrawalHandler{};
ValidateItemPacketStateFn OriginalValidateItemPacketState{};
ServerPacketHandlerFn OriginalGoldButtonHandler{};
SendServerUiFn SendServerUi{};
GetClientFromPlayerFn GetClientFromPlayer{};
RemoveServerUnitFn OriginalRemoveServerUnit{};
GetLocalDataContextFn GetLocalDataContext{};
GetLocalPlayerFn GetLocalPlayer{};
IsRoomInTownFn OriginalIsRoomInTown{};
TransferItemToInventoryPageFn OriginalTransferItemToInventoryPage{};
GetUiStateFn OriginalGetUiState{};
OpenInterfaceStateFn OriginalOpenInterfaceState{};
CloseInterfaceStateFn OriginalCloseInterfaceState{};
MovementUiCloseFn OriginalMovementUiClose{};
StashInterfaceTransitionFn OriginalStashInterfaceTransition{};
ResetMouseInputStateFn OriginalResetMouseInputState{};
ResetMouseInputStateFn OriginalResetMouseInputStateWithFinalize{};
MarkUiDirtyFn MarkUiDirty{};
FindTopLevelPanelFn FindTopLevelPanel{};
FindWidgetFn FindWidget{};
GetWidgetRectFn GetWidgetRect{};

HotkeyConfig HotkeySettings{};
std::string LoadedConfigPath{"built-in disabled defaults"};
HANDLE InputThread{};
DWORD InputThreadId{};
std::atomic_bool InputThreadReady{};
std::atomic_bool InputThreadFailed{};
std::atomic_bool InputStopping{};
std::atomic_bool UiDispatchReady{};
std::atomic_bool HotkeyPressed{};
std::atomic_bool HotkeyCaptured{};
std::atomic_bool HotkeyRequestPending{};
std::atomic<std::uint64_t> HotkeyAcceptedRequests{};
std::atomic<std::uint64_t> HotkeyCoalescedRequests{};
std::atomic<std::uint64_t> HotkeyDispatchedRequests{};
std::atomic<std::uint64_t> HotkeyRefusedRequests{};
std::atomic<std::uint64_t> HotkeyFailedRequests{};
std::atomic<std::uint64_t> CompanionInventoryCloseDeadline{};
std::atomic<std::uint64_t> CompanionInventoryCloseTickets{};
std::atomic<std::uint64_t> CompanionInventoryCloses{};
std::atomic<std::uint64_t> CompanionInventoryCloseExpirations{};
std::atomic<std::uint64_t> HotkeyOpenTransitionDeadline{};
std::atomic<std::uint64_t> HotkeyOpenTransitionTickets{};
std::atomic<std::uint64_t> HotkeyOpenTransitionApplications{};
std::atomic<std::uint64_t> HotkeyOpenTransitionExpirations{};
std::atomic<std::uint64_t> HotkeyMouseResetSuppressions{};
std::atomic<std::uint64_t> HotkeyMouseStateRestorations{};
std::atomic<std::uint64_t> IndependentInventoryRestores{};
std::atomic_bool UiReadyReported{};
HHOOK UiMessageHookHandle{};
DWORD UiThreadId{};
UINT DispatchRequestMessage{};

std::atomic<std::uint64_t> DynamicPlacements{};
std::atomic<std::uint64_t> PlacementFailures{};
std::atomic<std::uint64_t> OpenRequests{};
std::atomic<std::uint64_t> CloseRequests{};
std::atomic<std::uint64_t> ServerSessionsOpened{};
std::atomic<std::uint64_t> ServerSessionsClosed{};
std::atomic<std::uint64_t> ServerSessionsPruned{};
std::atomic<std::uint64_t> RemoteItemOperations{};
std::atomic<std::uint64_t> RemoteItemFailures{};
std::atomic<std::uint64_t> MaxRemoteItemOperationMs{};
std::atomic<std::uint64_t> RemoteStashProximityBypasses{};
std::atomic<std::uint64_t> RemoteTownBypasses{};
std::atomic<std::uint64_t> RemoteSharedTransferOperations{};
std::atomic<std::uint64_t> RemoteSharedTransferFailures{};
std::atomic<std::uint64_t> RemoteGoldTransactions{};
std::atomic<std::uint64_t> RemoteGoldFailures{};
std::atomic<std::uint64_t> RemoteQuickMoveUiBypasses{};
std::atomic<std::uint64_t> RemoteAutomaticCloseSuppressions{};
std::atomic<std::uint64_t> RemoteGeneralUiCloseSuppressions{};
std::atomic<std::uint64_t> RemoteExplicitClosePasses{};
std::atomic<std::uint64_t> RemoteMovementInventoryCloseSuppressions{};
std::atomic<std::uint64_t> RemoteQuickMoveWithdrawalDeadline{};
std::atomic_bool PlacementSuccessReported{};
std::atomic_bool PlacementFailureReported{};
std::atomic<void*> RemoteStashMessage{};
std::atomic<UiMessageInterceptorFn> ExternalUiMessageInterceptor{};
std::atomic_bool BrokerReady{};

struct RemoteSession {
    void* game{};
};

std::mutex RemoteSessionsMutex;
std::unordered_map<void*, RemoteSession> RemoteSessions;
std::atomic_bool RemoteClientSessionActive{};
std::atomic_bool RemoteClientUiObservedOpen{};
thread_local bool RemoteItemScope{};
thread_local bool RemoteGoldScope{};
thread_local bool RemoteHotkeyOpenTransitionScope{};
thread_local bool RemoteMovementUiCloseScope{};
void* GoldRangeStub{};
void* GoldRangeTrampoline{};
void* CallSiteRelayPage{};

constexpr std::array<std::uint8_t, 17> RemoteOpenRequest{
    0x18,
    0x52, 0x53, 0x54, 0x41,
    0x53, 0x48, 0x52, 0x55,
    0x46, 0x46, 0x4E, 0x45,
    0x43, 0x4B, 0x4B, 0x21,
};
constexpr std::array<std::uint8_t, 17> RemoteCloseRequest{
    0x18,
    0x52, 0x53, 0x54, 0x41,
    0x53, 0x48, 0x52, 0x55,
    0x46, 0x46, 0x4E, 0x45,
    0x43, 0x4B, 0x4B, 0x22,
};
constexpr std::uint8_t OpenStashUiAction = 0x10;

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

template<std::size_t Size>
bool Matches(
    std::uintptr_t rva,
    const std::array<std::uint8_t, Size>& expected
) noexcept {
    return std::memcmp(Base + rva, expected.data(), Size) == 0;
}

bool IsExecutableAddress(const void* address) noexcept {
    if (!address) return false;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(address, &region, sizeof(region)) == 0
        || region.State != MEM_COMMIT) {
        return false;
    }
    const auto protection = region.Protect & 0xFF;
    return protection == PAGE_EXECUTE
        || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
}

template<std::size_t Count>
bool MatchesAll(const std::array<RelativeCallSite, Count>& sites) noexcept {
    for (const auto& site : sites) {
        if (std::memcmp(Base + site.rva, site.expected.data(), site.expected.size()) != 0) {
            return false;
        }
    }
    return true;
}

bool LoadConfig(const nlohmann::json& miscConfig) noexcept {
    HotkeySettings = {};
    LoadedConfigPath = "misc.remoteStash defaults";
    try {
        if (!miscConfig.is_object()) {
            throw std::invalid_argument("misc must be an object");
        }
        const auto entry = miscConfig.find("remoteStash");
        HotkeySettings = entry == miscConfig.end()
            ? HotkeyConfig{}
            : ParseHotkeyConfig(*entry);
        LoadedConfigPath = "misc.remoteStash";
        return true;
    } catch (const std::exception& exception) {
        if (Context) {
            const auto message = std::string(
                "plugin-misc: invalid misc.remoteStash (")
                + exception.what() + ").";
            Context->LogError(message.c_str());
        }
        return false;
    }
}

bool ValidateRuntime() noexcept {
    return Matches(ConfigurePlayerInventoryRva, ConfigurePlayerInventoryExpected)
        // QueueOutgoingPacket is a composable live entry. PluginPack's Equipped
        // Item to Cube may own its prologue, while RemoteStash calls through it.
        && IsExecutableAddress(Base + QueueOutgoingPacketRva)
        && MatchesAll(SharedGoldDepositCallSites)
        && IsExecutableAddress(Base + SharedGoldDepositRva)
        && Matches(RemoveItemHandlerRva, RemoveItemHandlerExpected)
        && Matches(InsertItemHandlerRva, InsertItemHandlerExpected)
        && Matches(SharedDepositHandlerRva, SharedDepositHandlerExpected)
        && Matches(SharedWithdrawalHandlerRva, SharedWithdrawalHandlerExpected)
        && Matches(ValidateItemPacketStateRva, ValidateItemPacketStateExpected)
        && Matches(GoldButtonHandlerRva, GoldButtonHandlerExpected)
        && Matches(GoldRangeGateRva, GoldRangeGateExpected)
        && Matches(SendServerUiRva, SendServerUiExpected)
        && Matches(GetClientFromPlayerRva, GetClientFromPlayerExpected)
        && Matches(RemoveServerUnitRva, RemoveServerUnitExpected)
        && Matches(GetLocalDataContextRva, GetLocalDataContextExpected)
        && Matches(GetLocalPlayerRva, GetLocalPlayerExpected)
        && Matches(IsRoomInTownRva, IsRoomInTownExpected)
        && Matches(
            TransferItemToInventoryPageRva,
            TransferItemToInventoryPageExpected
        )
        && Matches(GetUiStateRva, GetUiStateExpected)
        && Matches(CloseInterfaceStateRva, CloseInterfaceStateExpected)
        && Matches(FindWidgetRva, FindWidgetExpected)
        && Matches(GetWidgetRectRva, GetWidgetRectExpected)
        && MatchesAll(ButtonDispatchUiMessageCallSites)
        && MatchesAll(IsRoomInTownCallSites)
        && MatchesAll(TransferItemToInventoryPageCallSites);
}

bool ValidateHotkeyRuntime() noexcept {
    return Matches(FindTopLevelPanelRva, FindTopLevelPanelExpected)
        && Matches(OpenInterfaceStateRva, OpenInterfaceStateExpected)
        && Matches(
            StashInterfaceTransitionRva,
            StashInterfaceTransitionExpected
        )
        && Matches(
            ResetMouseInputStateRva,
            ResetMouseInputStateExpected
        )
        && Matches(
            ResetMouseInputStateWithFinalizeRva,
            ResetMouseInputStateWithFinalizeExpected
        )
        && MatchesAll(StashInterfaceTransitionCallSites)
        && Matches(MarkUiDirtyRva, MarkUiDirtyExpected);
}

bool IsRemoteControlRequest(
    const std::uint8_t* packet,
    std::int32_t size,
    const std::array<std::uint8_t, 17>& request
) noexcept {
    return packet
        && size == static_cast<std::int32_t>(request.size())
        && std::memcmp(packet, request.data(), request.size()) == 0;
}

bool IsRemoteOpenRequest(
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    return IsRemoteControlRequest(packet, size, RemoteOpenRequest);
}

bool IsRemoteCloseRequest(
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    return IsRemoteControlRequest(packet, size, RemoteCloseRequest);
}

bool HasRemoteSession(void* game, void* player) noexcept {
    if (!game || !player) return false;
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        const auto found = RemoteSessions.find(player);
        return found != RemoteSessions.end() && found->second.game == game;
    } catch (...) {
        return false;
    }
}

bool CloseRemoteSession(void* game, void* player) noexcept {
    if (!game || !player) return false;
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        const auto found = RemoteSessions.find(player);
        if (found != RemoteSessions.end() && found->second.game == game) {
            RemoteSessions.erase(found);
            ServerSessionsClosed.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    } catch (...) {
    }
    return false;
}

void UpdateMaximum(
    std::atomic<std::uint64_t>& maximum,
    std::uint64_t candidate
) noexcept {
    auto current = maximum.load(std::memory_order_relaxed);
    while (candidate > current
        && !maximum.compare_exchange_weak(
            current,
            candidate,
            std::memory_order_relaxed,
            std::memory_order_relaxed
        )) {
    }
}

void RecordRemoteItemOperation(
    std::int32_t result,
    std::uint64_t elapsedMs
) noexcept {
    RemoteItemOperations.fetch_add(1, std::memory_order_relaxed);
    if (result != 0) {
        RemoteItemFailures.fetch_add(1, std::memory_order_relaxed);
    }
    UpdateMaximum(MaxRemoteItemOperationMs, elapsedMs);
}

void DeactivateRemoteClientSession(bool notifyServer) noexcept {
    const auto wasActive = RemoteClientSessionActive.exchange(
        false,
        std::memory_order_acq_rel
    );
    RemoteClientUiObservedOpen.store(false, std::memory_order_release);
    RemoteQuickMoveWithdrawalDeadline.store(0, std::memory_order_release);
    CompanionInventoryCloseDeadline.store(0, std::memory_order_release);
    HotkeyOpenTransitionDeadline.store(0, std::memory_order_release);
    if (!notifyServer || !wasActive || !QueueOutgoingPacket) return;

    __try {
        QueueOutgoingPacket(
            RemoteCloseRequest.data(),
            static_cast<std::int32_t>(RemoteCloseRequest.size())
        );
        CloseRequests.fetch_add(1, std::memory_order_relaxed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

std::size_t RemoteSessionCount() noexcept {
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        return RemoteSessions.size();
    } catch (...) {
        return 0;
    }
}

bool TrySendStashUi(void* player, std::uint8_t action) noexcept {
    if (!player || !GetClientFromPlayer || !SendServerUi) return false;
    __try {
        auto* client = GetClientFromPlayer(player);
        if (!client) return false;
        SendServerUi(client, action);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool OpenRemoteSession(void* game, void* player) noexcept {
    if (!game || !player) return false;
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        RemoteSessions.insert_or_assign(player, RemoteSession{game});
    } catch (...) {
        return false;
    }

    if (!TrySendStashUi(player, OpenStashUiAction)) {
        CloseRemoteSession(game, player);
        return false;
    }

    ServerSessionsOpened.fetch_add(1, std::memory_order_relaxed);
    return true;
}

std::int32_t __fastcall HookInsertItemHandler(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    if (IsRemoteOpenRequest(packet, size)) {
        if (OpenRemoteSession(game, player)) return 0;
        if (Context) {
            Context->LogError("RemoteStash: server could not open the remote session.");
        }
        return 1;
    }
    if (IsRemoteCloseRequest(packet, size)) {
        CloseRemoteSession(game, player);
        return 0;
    }

    const auto remote = HasRemoteSession(game, player);
    const auto previousScope = RemoteItemScope;
    RemoteItemScope = remote;
    const auto started = GetTickCount64();
    const auto result = OriginalInsertItemHandler(game, player, packet, size);
    const auto elapsed = GetTickCount64() - started;
    RemoteItemScope = previousScope;
    if (remote) {
        RecordRemoteItemOperation(result, elapsed);
    }
    return result;
}

std::int32_t __fastcall HookRemoveItemHandler(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    const auto remote = HasRemoteSession(game, player);
    const auto previousScope = RemoteItemScope;
    RemoteItemScope = remote;
    const auto started = GetTickCount64();
    const auto result = OriginalRemoveItemHandler(game, player, packet, size);
    const auto elapsed = GetTickCount64() - started;
    RemoteItemScope = previousScope;
    if (remote) {
        RecordRemoteItemOperation(result, elapsed);
    }
    return result;
}

std::int32_t ForwardSharedTransfer(
    ServerPacketHandlerFn original,
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    const auto remote = HasRemoteSession(game, player);
    const auto previousScope = RemoteItemScope;
    RemoteItemScope = remote;
    const auto started = GetTickCount64();
    const auto result = original(game, player, packet, size);
    const auto elapsed = GetTickCount64() - started;
    RemoteItemScope = previousScope;
    if (!remote) return result;

    UpdateMaximum(MaxRemoteItemOperationMs, elapsed);
    RemoteSharedTransferOperations.fetch_add(1, std::memory_order_relaxed);
    if (result != 0) {
        RemoteSharedTransferFailures.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

std::int32_t __fastcall HookSharedDepositHandler(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    return ForwardSharedTransfer(
        OriginalSharedDepositHandler,
        game,
        player,
        packet,
        size
    );
}

std::int32_t __fastcall HookSharedWithdrawalHandler(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    return ForwardSharedTransfer(
        OriginalSharedWithdrawalHandler,
        game,
        player,
        packet,
        size
    );
}

bool __fastcall HookValidateItemPacketState(
    void* transaction,
    const void* packetState,
    bool bypassStashProximity
) noexcept {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    bool remoteStashState{};
    std::int32_t page{-1};
    if (packetState) {
        __try {
            std::memcpy(
                &page,
                static_cast<const std::uint8_t*>(packetState) + 4,
                sizeof(page)
            );
            remoteStashState = page == 4
                && (RemoteItemScope
                    || (RemoteClientSessionActive.load(std::memory_order_acquire)
                        && (returnAddress == reinterpret_cast<std::uintptr_t>(
                                Base + QuickMoveToStashItemPacketStateReturnRva
                            )
                            || returnAddress == reinterpret_cast<std::uintptr_t>(
                                Base + QuickMoveFromStashItemPacketStateReturnRva
                            ))));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            page = -1;
            remoteStashState = false;
        }
    }

    if (remoteStashState && !bypassStashProximity) {
        RemoteStashProximityBypasses.fetch_add(1, std::memory_order_relaxed);
        bypassStashProximity = true;
    }

    return OriginalValidateItemPacketState(
        transaction,
        packetState,
        bypassStashProximity
    );
}

bool IsRemoteClientStashTownCallsite(std::uintptr_t returnAddress) noexcept {
    if (!RemoteClientSessionActive.load(std::memory_order_acquire)) return false;
    if (returnAddress == reinterpret_cast<std::uintptr_t>(
            Base + ClientStashTownCheckReturnRva
        )
        || returnAddress == reinterpret_cast<std::uintptr_t>(
            Base + ClientStashCleanupTownCheckReturnRva
        )) {
        return true;
    }

    const auto quickMoveWithdrawalActive = GetTickCount64()
        <= RemoteQuickMoveWithdrawalDeadline.load(std::memory_order_acquire);
    return quickMoveWithdrawalActive
        && (returnAddress == reinterpret_cast<std::uintptr_t>(
                Base + QuickMoveItemInitTownCheckReturnRva
            )
            || returnAddress == reinterpret_cast<std::uintptr_t>(
                Base + QuickMoveItemPlacementTownCheckReturnRva
            ));
}

std::int32_t __fastcall HookIsRoomInTown(void* room) noexcept {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (IsRemoteClientStashTownCallsite(returnAddress)) {
        RemoteTownBypasses.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }

    return OriginalIsRoomInTown(room);
}

struct UnitProbe {
    std::uint32_t type{0xFFFFFFFF};
    std::uint32_t classId{0xFFFFFFFF};
    std::uint32_t unitId{0xFFFFFFFF};
};

UnitProbe ProbeUnit(const void* unit) noexcept {
    UnitProbe probe{};
    if (!unit) return probe;
    __try {
        const auto* bytes = static_cast<const std::uint8_t*>(unit);
        std::memcpy(&probe.type, bytes, sizeof(probe.type));
        std::memcpy(&probe.classId, bytes + 4, sizeof(probe.classId));
        std::memcpy(&probe.unitId, bytes + 8, sizeof(probe.unitId));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return UnitProbe{};
    }
    return probe;
}

bool IsLocalPlayer(void* player) noexcept {
    if (!player || !GetLocalDataContext || !GetLocalPlayer) return false;
    const auto playerProbe = ProbeUnit(player);
    if (playerProbe.type != 0 || playerProbe.unitId == 0xFFFFFFFF) return false;

    void* localPlayer{};
    __try {
        localPlayer = GetLocalPlayer(GetLocalDataContext());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    const auto localProbe = ProbeUnit(localPlayer);
    return localProbe.type == 0 && localProbe.unitId == playerProbe.unitId;
}

bool LocalPlayerIsAvailable() noexcept {
    if (!GetLocalDataContext || !GetLocalPlayer) return false;
    void* player{};
    __try {
        player = GetLocalPlayer(GetLocalDataContext());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return ProbeUnit(player).type == 0;
}

void __fastcall HookRemoveServerUnit(void* game, void* unit) noexcept {
    if (ProbeUnit(unit).type != 0) {
        OriginalRemoveServerUnit(game, unit);
        return;
    }
    const auto session = HasRemoteSession(game, unit);
    const auto localPlayer = session && IsLocalPlayer(unit);
    if (session && CloseRemoteSession(game, unit)) {
        ServerSessionsPruned.fetch_add(1, std::memory_order_relaxed);
        if (localPlayer) {
            DeactivateRemoteClientSession(false);
        }
    }
    OriginalRemoveServerUnit(game, unit);
}

bool __fastcall HookTransferItemToInventoryPage(
    void* item,
    void* destinationUnit,
    std::uint8_t inventoryPage,
    std::uint8_t destinationKind,
    bool transferMode,
    void* placementOut
) noexcept {
    const auto remoteWithdrawal =
        RemoteClientSessionActive.load(std::memory_order_acquire)
        && inventoryPage == 0
        && destinationKind == 4;
    if (remoteWithdrawal) {
        RemoteQuickMoveWithdrawalDeadline.store(
            GetTickCount64() + 1000,
            std::memory_order_release
        );
    }
    const auto result = OriginalTransferItemToInventoryPage(
        item,
        destinationUnit,
        inventoryPage,
        destinationKind,
        transferMode,
        placementOut
    );
    return result;
}

std::uint8_t __fastcall HookGetUiState(std::int32_t state) noexcept {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto result = OriginalGetUiState(state);
    if (state != 0x16
        || !RemoteClientSessionActive.load(std::memory_order_acquire)) {
        return result;
    }

    if (result != 0) {
        RemoteClientUiObservedOpen.store(true, std::memory_order_release);
    } else {
        const auto observedOpen = RemoteClientUiObservedOpen.load(
            std::memory_order_acquire
        );
        if (observedOpen) {
            DeactivateRemoteClientSession(true);
        }
    }

    if (returnAddress == reinterpret_cast<std::uintptr_t>(
            Base + QuickMoveStashUiStateReturnRva
        )) {
        RemoteQuickMoveUiBypasses.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

void ProcessCompanionInventoryCloseAfterOpen(std::int32_t state) noexcept {
    const auto now = GetTickCount64();
    const auto deadline = CompanionInventoryCloseDeadline.load(
        std::memory_order_acquire
    );
    const auto decision = ResolveCompanionInventoryClose(
        deadline,
        state == StashInterfaceState,
        now
    );
    if (decision == CompanionInventoryCloseDecision::Wait) return;

    if (decision == CompanionInventoryCloseDecision::Expire) {
        auto expected = deadline;
        if (CompanionInventoryCloseDeadline.compare_exchange_strong(
                expected,
                0,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            )) {
            CompanionInventoryCloseExpirations.fetch_add(
                1,
                std::memory_order_relaxed
            );
        }
        return;
    }

    __try {
        if (OriginalGetUiState(StashInterfaceState) == 0) return;
        auto expected = deadline;
        if (!CompanionInventoryCloseDeadline.compare_exchange_strong(
                expected,
                0,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            )) {
            return;
        }
        if (OriginalGetUiState(InventoryInterfaceState) != 0) {
            OriginalCloseInterfaceState(InventoryInterfaceState, false);
            MarkUiDirty();
            CompanionInventoryCloses.fetch_add(1, std::memory_order_relaxed);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CompanionInventoryCloseDeadline.store(0, std::memory_order_release);
    }
}

bool __fastcall HookOpenInterfaceState(
    std::int32_t state,
    bool secondary
) noexcept {
    const auto transitionNow = GetTickCount64();
    const auto transitionDeadline = HotkeyOpenTransitionDeadline.load(
        std::memory_order_acquire
    );
    const auto transitionDecision = ResolveCompanionInventoryClose(
        transitionDeadline,
        state == StashInterfaceState,
        transitionNow
    );
    bool scopedHotkeyTransition = false;
    if (transitionDecision != CompanionInventoryCloseDecision::Wait) {
        auto expected = transitionDeadline;
        if (HotkeyOpenTransitionDeadline.compare_exchange_strong(
                expected,
                0,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            )) {
            if (transitionDecision == CompanionInventoryCloseDecision::Close) {
                scopedHotkeyTransition = true;
            } else {
                HotkeyOpenTransitionExpirations.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
            }
        }
    }

    const auto previousTransitionScope = RemoteHotkeyOpenTransitionScope;
    RemoteHotkeyOpenTransitionScope =
        previousTransitionScope || scopedHotkeyTransition;
    const auto result = OriginalOpenInterfaceState(state, secondary);

    // Opening the native stash also opens the inventory state. RemoteStash
    // closes that companion state immediately so only the stash remains
    // visible. Keep the scoped mouse-reset suppression active through that
    // close: both native transitions call the same input-reset routine.
    ProcessCompanionInventoryCloseAfterOpen(state);
    RemoteHotkeyOpenTransitionScope = previousTransitionScope;
    return result;
}

void __fastcall HookStashInterfaceTransition(
    std::int32_t mode,
    bool transitionFlag
) noexcept {
    const auto resolvedFlag = ResolveRemoteStashTransitionFlag(
        RemoteHotkeyOpenTransitionScope && mode == 2,
        transitionFlag
    );
    if (resolvedFlag != transitionFlag) {
        HotkeyOpenTransitionApplications.fetch_add(1, std::memory_order_relaxed);
    }
    OriginalStashInterfaceTransition(mode, resolvedFlag);
}

void __fastcall HookResetMouseInputState() noexcept {
    if (ShouldSuppressHotkeyMouseReset(RemoteHotkeyOpenTransitionScope)) {
        HotkeyMouseResetSuppressions.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    OriginalResetMouseInputState();
}

struct MouseInputStateSnapshot {
    std::uint32_t value0{};
    std::uint32_t value1{};
    std::uint8_t value2{};
    std::uint32_t value3{};
    std::uint32_t value4{};
    std::uint8_t value5{};
};

MouseInputStateSnapshot CaptureMouseInputState() noexcept {
    MouseInputStateSnapshot snapshot{};
    std::memcpy(&snapshot.value0, Base + MouseInputState0Rva, sizeof(snapshot.value0));
    std::memcpy(&snapshot.value1, Base + MouseInputState1Rva, sizeof(snapshot.value1));
    std::memcpy(&snapshot.value2, Base + MouseInputState2Rva, sizeof(snapshot.value2));
    std::memcpy(&snapshot.value3, Base + MouseInputState3Rva, sizeof(snapshot.value3));
    std::memcpy(&snapshot.value4, Base + MouseInputState4Rva, sizeof(snapshot.value4));
    std::memcpy(&snapshot.value5, Base + MouseInputState5Rva, sizeof(snapshot.value5));
    return snapshot;
}

void RestoreMouseInputState(const MouseInputStateSnapshot& snapshot) noexcept {
    std::memcpy(Base + MouseInputState0Rva, &snapshot.value0, sizeof(snapshot.value0));
    std::memcpy(Base + MouseInputState1Rva, &snapshot.value1, sizeof(snapshot.value1));
    std::memcpy(Base + MouseInputState2Rva, &snapshot.value2, sizeof(snapshot.value2));
    std::memcpy(Base + MouseInputState3Rva, &snapshot.value3, sizeof(snapshot.value3));
    std::memcpy(Base + MouseInputState4Rva, &snapshot.value4, sizeof(snapshot.value4));
    std::memcpy(Base + MouseInputState5Rva, &snapshot.value5, sizeof(snapshot.value5));
}

void __fastcall HookResetMouseInputStateWithFinalize() noexcept {
    if (!ShouldSuppressHotkeyMouseReset(RemoteHotkeyOpenTransitionScope)) {
        OriginalResetMouseInputStateWithFinalize();
        return;
    }

    const auto snapshot = CaptureMouseInputState();
    OriginalResetMouseInputStateWithFinalize();
    RestoreMouseInputState(snapshot);
    HotkeyMouseStateRestorations.fetch_add(1, std::memory_order_relaxed);
}

bool IsExplicitRemoteStashClose(std::uintptr_t returnAddress) noexcept {
    return returnAddress == reinterpret_cast<std::uintptr_t>(
               Base + ServerUiCloseStashReturnRva
           )
        || returnAddress == reinterpret_cast<std::uintptr_t>(
               Base + StashPanelCloseButtonReturnRva
           );
}

void __fastcall HookMovementUiClose(
    std::int32_t closeMode,
    std::int32_t secondary
) noexcept {
    const auto previousScope = RemoteMovementUiCloseScope;
    RemoteMovementUiCloseScope = true;
    OriginalMovementUiClose(closeMode, secondary);
    RemoteMovementUiCloseScope = previousScope;
}

void __fastcall HookCloseInterfaceState(
    std::int32_t state,
    bool secondary
) noexcept {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto explicitClose = IsExplicitRemoteStashClose(returnAddress);
    const auto escapeIsDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (ShouldSuppressMovementInventoryClose(
            RemoteClientSessionActive.load(std::memory_order_acquire),
            OriginalGetUiState
                && OriginalGetUiState(StashInterfaceState) != 0,
            state == InventoryInterfaceState,
            RemoteMovementUiCloseScope
        )) {
        RemoteMovementInventoryCloseSuppressions.fetch_add(
            1,
            std::memory_order_relaxed
        );
        return;
    }
    if (ShouldSuppressRemoteStashClose(
            RemoteClientSessionActive.load(std::memory_order_acquire),
            state == StashInterfaceState,
            explicitClose,
            escapeIsDown
        )) {
        RemoteAutomaticCloseSuppressions.fetch_add(1, std::memory_order_relaxed);
        if (returnAddress == reinterpret_cast<std::uintptr_t>(
                Base + GeneralUiTeardownStashReturnRva
            )) {
            RemoteGeneralUiCloseSuppressions.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    if (state == StashInterfaceState && (explicitClose || escapeIsDown)) {
        RemoteExplicitClosePasses.fetch_add(1, std::memory_order_relaxed);
    }
    OriginalCloseInterfaceState(state, secondary);
}

bool ShouldBypassGoldRange() noexcept {
    return RemoteGoldScope;
}

std::int32_t __fastcall HookGoldButtonHandler(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    // D2R 3.2 routes stash-gold operations through the modern 0x27 handler.
    // Its final dword is a signed gold delta, not the legacy 18/19/20 action
    // value that older implementations assumed. Scope the handler itself; the
    // injected bypass still applies only at its stash-object proximity gate.
    const auto remote = packet
        && size == 17
        && packet[0] == 0x27
        && HasRemoteSession(game, player);
    const auto previousScope = RemoteGoldScope;
    RemoteGoldScope = remote;
    const auto result = OriginalGoldButtonHandler(game, player, packet, size);
    RemoteGoldScope = previousScope;

    if (!remote) return result;
    RemoteGoldTransactions.fetch_add(1, std::memory_order_relaxed);
    if (result != 0) {
        RemoteGoldFailures.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

bool CreateGoldRangeStubImmediate() noexcept {
    constexpr std::size_t StubSize = 50;
    constexpr std::size_t HelperAddressOffset = 6;
    constexpr std::size_t BypassAddressOffset = 26;
    constexpr std::size_t TrampolineAddressOffset = 39;

    GoldRangeStub = VirtualAlloc(
        nullptr,
        StubSize,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE
    );
    if (!GoldRangeStub) return false;

    std::array<std::uint8_t, StubSize> stub{
        0x48, 0x83, 0xEC, 0x20,
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,
        0xFF, 0xD0,
        0x48, 0x83, 0xC4, 0x20,
        0x84, 0xC0,
        0x74, 0x0D,
        0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
        0x41, 0xFF, 0xE3,
        0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
        0x41, 0xFF, 0xE3,
    };
    const auto helper = reinterpret_cast<std::uintptr_t>(&ShouldBypassGoldRange);
    const auto bypass = reinterpret_cast<std::uintptr_t>(Base + GoldRangeBypassRva);
    std::memcpy(stub.data() + HelperAddressOffset, &helper, sizeof(helper));
    std::memcpy(stub.data() + BypassAddressOffset, &bypass, sizeof(bypass));
    std::memcpy(GoldRangeStub, stub.data(), stub.size());

    if (!PSh_InstallGuardedInlineHook(
            Context,
            GoldRangeGateRva,
            GoldRangeGateExpected.data(),
            static_cast<std::uint32_t>(GoldRangeGateExpected.size()),
            GoldRangeStub,
            &GoldRangeTrampoline
        ) || !GoldRangeTrampoline) {
        VirtualFree(GoldRangeStub, 0, MEM_RELEASE);
        GoldRangeStub = nullptr;
        GoldRangeTrampoline = nullptr;
        return false;
    }

    const auto trampoline = reinterpret_cast<std::uintptr_t>(GoldRangeTrampoline);
    std::memcpy(
        static_cast<std::uint8_t*>(GoldRangeStub) + TrampolineAddressOffset,
        &trampoline,
        sizeof(trampoline)
    );
    FlushInstructionCache(GetCurrentProcess(), GoldRangeStub, StubSize);

    DWORD previousProtection{};
    if (!VirtualProtect(GoldRangeStub, StubSize, PAGE_EXECUTE_READ, &previousProtection)) {
        return false;
    }
    return true;
}

bool CreateGoldRangeStub() noexcept {
    constexpr auto ManifestId =
        PSH_MANIFEST_SITE("misc.remoteStash.goldRangeGate");
    if (!PSh_HookTransactionIsCollecting()
        || !PSh_ManifestSiteIsValid(
            Context,
            ManifestId,
            GoldRangeGateRva,
            GoldRangeGateExpected.data(),
            static_cast<std::uint32_t>(GoldRangeGateExpected.size()))) {
        return false;
    }
    return PSh_HookTransactionEnqueue(
        ManifestId,
        true,
        []() noexcept { return CreateGoldRangeStubImmediate(); });
}

std::int32_t __fastcall HookSharedGoldDeposit(
    void* panel,
    std::int32_t amount
) noexcept {
    auto resolvedAmount = amount;
    if (panel && RemoteClientSessionActive.load(std::memory_order_acquire)) {
        __try {
            auto* modal = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(panel) + 0x510
            );
            if (modal && modal[0x14E8] != 0) {
                std::int32_t requestedAmount{};
                std::memcpy(
                    &requestedAmount,
                    modal + 0x14E4,
                    sizeof(requestedAmount)
                );
                if (requestedAmount > 0) {
                    resolvedAmount = requestedAmount;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return OriginalSharedGoldDeposit
        ? OriginalSharedGoldDeposit(panel, resolvedAmount)
        : 2;
}

void* FindNamedWidget(void* panel, const char* name) noexcept {
    if (!panel || !name) return nullptr;
    __try {
        return FindWidget(panel, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool TryReadTopLevelPanelVisibility(const char* name, bool& visible) noexcept {
    visible = false;
    if (!FindTopLevelPanel || !name) return false;
    __try {
        auto* widget = FindTopLevelPanel(name);
        if (widget) {
            visible = *(static_cast<std::uint8_t*>(widget) + WidgetVisibleOffset) != 0;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool KnownInputIsBlocked() noexcept {
    constexpr const char* blockers[]{
        "ChatPanel",
        "TextInputModal",
        "DropGoldModal",
        "ConfirmationModal",
        "ButtonBindingModal",
        "KeyBindingDefaultsModal",
        "AddFriendModal",
        "LootFilterRenameProfileModal",
        "LootFilterExportProfileModal",
        "LootFilterDeleteProfileModal",
        "LootFilterNewProfileModal",
        "LootFilterImportProfileModal",
        "LootFilterRenameRuleModal",
        "LootFilterCopyRuleModal",
        "LootFilterDeleteRuleModal",
    };
    for (const auto* name : blockers) {
        bool visible{};
        if (!TryReadTopLevelPanelVisibility(name, visible) || visible) return true;
    }
    return false;
}

bool ReadWidgetRect(void* widget, WidgetRect& rect) noexcept {
    if (!widget) return false;
    __try {
        WidgetRect current{};
        if (GetWidgetRect(widget, &current) != &current) return false;
        rect = current;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SetWidgetState(void* widget, bool value) noexcept {
    if (!widget) return;
    __try {
        auto** vtable = *reinterpret_cast<void***>(widget);
        auto setEnabled = reinterpret_cast<SetWidgetBoolFn>(vtable[9]);
        auto setVisible = reinterpret_cast<SetWidgetBoolFn>(vtable[10]);
        setEnabled(widget, value);
        setVisible(widget, value);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (Context) {
            Context->LogError("RemoteStash: button state update failed.");
        }
    }
}

void ReportPlacementFailure(const char* reason) noexcept {
    PlacementFailures.fetch_add(1, std::memory_order_relaxed);
    if (!Context || PlacementFailureReported.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    char message[240]{};
    std::snprintf(
        message,
        sizeof(message),
        "RemoteStash: layout-owned button binding failed (%s); button stays hidden.",
        reason
    );
    Context->LogWarn(message);
}

void __fastcall HookConfigurePlayerInventory(void* panel) noexcept {
    RemoteStashMessage.store(nullptr, std::memory_order_release);
    OriginalConfigurePlayerInventory(panel);
    if (!panel) return;

    auto* button = FindNamedWidget(panel, "remote_stash");
    if (!button) {
        ReportPlacementFailure("remote_stash was not found");
        return;
    }

    WidgetRect buttonRect{};
    if (!ReadWidgetRect(button, buttonRect)
        || !IsUsableLayoutOwnedButton(buttonRect)) {
        SetWidgetState(button, false);
        ReportPlacementFailure("remote_stash has no usable layout rectangle");
        return;
    }

    SetWidgetState(button, true);
    RemoteStashMessage.store(
        static_cast<std::uint8_t*>(button) + ButtonOnClickMessageOffset,
        std::memory_order_release
    );
    DynamicPlacements.fetch_add(1, std::memory_order_relaxed);
    if (Context && !PlacementSuccessReported.exchange(true, std::memory_order_relaxed)) {
        char message[220]{};
        std::snprintf(
            message,
            sizeof(message),
            "RemoteStash: layout-owned button bound at %d,%d with size %dx%d.",
            buttonRect.x,
            buttonRect.y,
            buttonRect.width,
            buttonRect.height
        );
        Context->LogInfo(message);
    }
}

bool IsCurrentRemoteStashMessage(void* message) noexcept {
    return IsExpectedEmbeddedMessage(
        reinterpret_cast<std::uintptr_t>(message),
        reinterpret_cast<std::uintptr_t>(
            RemoteStashMessage.load(std::memory_order_acquire)
        )
    );
}

bool TryQueueRemoteOpenRequest(
    bool closeCompanionInventory = false,
    bool smoothHotkeyTransition = false
) noexcept {
    CompanionInventoryCloseDeadline.store(0, std::memory_order_release);
    HotkeyOpenTransitionDeadline.store(0, std::memory_order_release);
    if (!QueueOutgoingPacket || !LocalPlayerIsAvailable()) return false;
    __try {
        const auto now = GetTickCount64();
        if (closeCompanionInventory) {
            CompanionInventoryCloseDeadline.store(
                now + CompanionInventoryCloseWindowMs,
                std::memory_order_release
            );
            CompanionInventoryCloseTickets.fetch_add(1, std::memory_order_relaxed);
        }
        if (smoothHotkeyTransition) {
            HotkeyOpenTransitionDeadline.store(
                now + HotkeyOpenTransitionWindowMs,
                std::memory_order_release
            );
            HotkeyOpenTransitionTickets.fetch_add(1, std::memory_order_relaxed);
        }
        RemoteClientUiObservedOpen.store(false, std::memory_order_release);
        RemoteClientSessionActive.store(true, std::memory_order_release);
        QueueOutgoingPacket(
            RemoteOpenRequest.data(),
            static_cast<std::int32_t>(RemoteOpenRequest.size())
        );
        OpenRequests.fetch_add(1, std::memory_order_relaxed);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DeactivateRemoteClientSession(false);
        if (Context) {
            Context->LogError("RemoteStash: server open request could not be queued.");
        }
        return false;
    }
}

bool TryQueueRemoteCloseRequest() noexcept {
    if (!QueueOutgoingPacket || !LocalPlayerIsAvailable()) return false;
    __try {
        QueueOutgoingPacket(
            RemoteCloseRequest.data(),
            static_cast<std::int32_t>(RemoteCloseRequest.size())
        );
        DeactivateRemoteClientSession(false);
        CloseRequests.fetch_add(1, std::memory_order_relaxed);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (Context) {
            Context->LogError("RemoteStash: server close request could not be queued.");
        }
        return false;
    }
}

bool TryCloseStashUiFromHotkey() noexcept {
    if (!OriginalGetUiState || !OriginalOpenInterfaceState
        || !OriginalCloseInterfaceState || !MarkUiDirty) {
        return false;
    }
    __try {
        const auto inventoryWasOpen =
            OriginalGetUiState(InventoryInterfaceState) != 0;
        OriginalCloseInterfaceState(StashInterfaceState, false);
        const auto inventoryIsOpen =
            OriginalGetUiState(InventoryInterfaceState) != 0;
        if (ShouldRestoreIndependentInventory(
                inventoryWasOpen,
                inventoryIsOpen
            )) {
            if (!OriginalOpenInterfaceState(InventoryInterfaceState, false)
                || OriginalGetUiState(InventoryInterfaceState) == 0) {
                if (Context) {
                    Context->LogError(
                        "RemoteStash: independent inventory could not be restored after hotkey close."
                    );
                }
                return false;
            }
            IndependentInventoryRestores.fetch_add(1, std::memory_order_relaxed);
        }
        MarkUiDirty();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (Context) {
            Context->LogError("RemoteStash: native UI close failed.");
        }
        return false;
    }
}

bool TryOpenStashUiFromHotkey() noexcept {
    if (!OriginalGetUiState || !OriginalOpenInterfaceState) return false;
    __try {
        if (OriginalGetUiState(StashInterfaceState) != 0) return true;

        // Open synchronously on D2R's UI thread, like the native panel
        // hotkeys. The authoritative remote request is queued first; when its
        // 0x77/0x10 response arrives, the native client handler sees the
        // already-visible stash panel and leaves it untouched.
        if (!HookOpenInterfaceState(StashInterfaceState, false)
            || OriginalGetUiState(StashInterfaceState) == 0) {
            if (Context) {
                Context->LogError(
                    "RemoteStash: immediate hotkey UI open failed."
                );
            }
            return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (Context) {
            Context->LogError(
                "RemoteStash: immediate hotkey UI open raised an exception."
            );
        }
        return false;
    }
}

bool __fastcall InterceptUiMessage(void* message) noexcept {
    if (!IsCurrentRemoteStashMessage(message)) return false;
    (void)TryQueueRemoteOpenRequest();
    return true;
}

bool TryExternalUiMessageInterceptor(void* message) noexcept {
    const auto interceptor = ExternalUiMessageInterceptor.load(std::memory_order_acquire);
    if (!interceptor) return false;
    __try {
        return interceptor(message);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool __fastcall InterceptUiMessageChain(void* message) noexcept {
    return TryExternalUiMessageInterceptor(message) || InterceptUiMessage(message);
}

void __fastcall HookDispatchUiMessage(void* message) noexcept {
    if (InterceptUiMessageChain(message)) return;
    OriginalDispatchUiMessage(message);
}

bool CurrentProcessOwnsForegroundWindow() noexcept {
    const auto foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD processId{};
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
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

bool ModifierDown(int virtualKey) noexcept {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

void ReplayRefusedHotkey() noexcept {
    std::array<INPUT, 2> input{};
    if (!IsMouseHotkey(HotkeySettings.hotkey)) {
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wVk = static_cast<WORD>(HotkeySettings.hotkey.virtualKey);
        input[1] = input[0];
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    } else {
        input[0].type = INPUT_MOUSE;
        input[1].type = INPUT_MOUSE;
        switch (HotkeySettings.hotkey.virtualKey) {
        case VK_MBUTTON:
            input[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            input[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
            break;
        case VK_XBUTTON1:
        case VK_XBUTTON2:
            input[0].mi.dwFlags = MOUSEEVENTF_XDOWN;
            input[1].mi.dwFlags = MOUSEEVENTF_XUP;
            input[0].mi.mouseData = HotkeySettings.hotkey.virtualKey == VK_XBUTTON1
                ? XBUTTON1 : XBUTTON2;
            input[1].mi.mouseData = input[0].mi.mouseData;
            break;
        default:
            return;
        }
    }
    // The low-level hook explicitly ignores injected input, so a request that
    // the UI thread refuses is replayed once to preserve D2R's normal action.
    if (SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT))
            != input.size() && Context) {
        Context->LogWarn(
            "RemoteStash: a refused hotkey could not be replayed to the game.");
    }
}

void ProcessQueuedHotkeyRequest() noexcept {
    if (!HotkeyRequestPending.exchange(false, std::memory_order_acq_rel)) return;
    const auto dispatch = ResolveHotkeyDispatch(
        RemoteClientSessionActive.load(std::memory_order_acquire),
        KnownInputIsBlocked()
    );
    if (dispatch == HotkeyDispatch::Refuse) {
        HotkeyRefusedRequests.fetch_add(1, std::memory_order_relaxed);
        ReplayRefusedHotkey();
        return;
    }
    const auto closing = dispatch == HotkeyDispatch::Close;
    bool closeCompanionInventory = false;
    if (!closing && OriginalGetUiState) {
        __try {
            closeCompanionInventory =
                OriginalGetUiState(InventoryInterfaceState) == 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            closeCompanionInventory = false;
        }
    }
    const auto queued = closing
        ? TryQueueRemoteCloseRequest()
        : TryQueueRemoteOpenRequest(closeCompanionInventory, true);
    if (queued && closing && !TryCloseStashUiFromHotkey()) {
        HotkeyFailedRequests.fetch_add(1, std::memory_order_relaxed);
        ReplayRefusedHotkey();
        return;
    }
    if (queued && !closing && !TryOpenStashUiFromHotkey()) {
        (void)TryQueueRemoteCloseRequest();
        HotkeyFailedRequests.fetch_add(1, std::memory_order_relaxed);
        ReplayRefusedHotkey();
        return;
    }
    if (queued) {
        HotkeyDispatchedRequests.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    HotkeyFailedRequests.fetch_add(1, std::memory_order_relaxed);
    ReplayRefusedHotkey();
}

void ResetUiMessageHook() noexcept {
    UiDispatchReady.store(false, std::memory_order_release);
    if (UiMessageHookHandle) UnhookWindowsHookEx(UiMessageHookHandle);
    UiMessageHookHandle = nullptr;
    UiThreadId = 0;
}

LRESULT CALLBACK UiMessageHook(
    int code,
    WPARAM removeMode,
    LPARAM parameter
) noexcept {
    if (code >= 0 && removeMode == PM_REMOVE && parameter) {
        auto* message = reinterpret_cast<MSG*>(parameter);
        if (message->message == DispatchRequestMessage
            && message->wParam == DispatchRequestCookie) {
            message->message = WM_NULL;
            message->wParam = 0;
            message->lParam = 0;
            ProcessQueuedHotkeyRequest();
        }
    }
    return CallNextHookEx(UiMessageHookHandle, code, removeMode, parameter);
}

bool EnsureUiMessageHook(HMODULE module) noexcept {
    if (UiMessageHookHandle) return true;
    const auto window = FindGameWindow();
    if (!window || DispatchRequestMessage == 0) return false;
    const auto threadId = GetWindowThreadProcessId(window, nullptr);
    if (threadId == 0) return false;
    const auto hook = SetWindowsHookExW(
        WH_GETMESSAGE,
        UiMessageHook,
        module,
        threadId
    );
    if (!hook) return false;
    UiMessageHookHandle = hook;
    UiThreadId = threadId;
    UiDispatchReady.store(true, std::memory_order_release);
    if (Context && !UiReadyReported.exchange(true, std::memory_order_relaxed)) {
        char message[190]{};
        std::snprintf(
            message,
            sizeof(message),
            "RemoteStash: UI-thread hotkey handoff ready on thread %lu for %s input.",
            static_cast<unsigned long>(threadId),
            IsMouseHotkey(HotkeySettings.hotkey) ? "mouse" : "keyboard"
        );
        Context->LogInfo(message);
    }
    return true;
}

bool QueueHotkeyRequest() noexcept {
    if (!CurrentProcessOwnsForegroundWindow()) return false;
    if (!ExactModifiersMatch(
            HotkeySettings.hotkey,
            ModifierDown(VK_CONTROL),
            ModifierDown(VK_SHIFT),
            ModifierDown(VK_MENU)
        )) {
        return false;
    }

    bool noRequest{};
    if (!HotkeyRequestPending.compare_exchange_strong(
            noRequest,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        )) {
        HotkeyCoalescedRequests.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    if (!UiDispatchReady.load(std::memory_order_acquire)) {
        HotkeyRequestPending.store(false, std::memory_order_release);
        HotkeyFailedRequests.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!PostThreadMessageW(
            UiThreadId,
            DispatchRequestMessage,
            DispatchRequestCookie,
            0
        )) {
        HotkeyRequestPending.store(false, std::memory_order_release);
        HotkeyFailedRequests.fetch_add(1, std::memory_order_relaxed);
        ResetUiMessageHook();
        return false;
    }
    HotkeyAcceptedRequests.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool HandleInputTransition(bool isDown, bool isUp, bool injected) noexcept {
    if (InputStopping.load(std::memory_order_acquire)) return false;
    if (isUp) {
        HotkeyPressed.store(false, std::memory_order_release);
        const auto captured = HotkeyCaptured.exchange(false, std::memory_order_acq_rel);
        return captured
            && CurrentProcessOwnsForegroundWindow();
    }
    if (!isDown || injected || !CurrentProcessOwnsForegroundWindow()) return false;
    const auto modifiersMatch = ExactModifiersMatch(
        HotkeySettings.hotkey,
        ModifierDown(VK_CONTROL),
        ModifierDown(VK_SHIFT),
        ModifierDown(VK_MENU)
    );
    if (!modifiersMatch) return false;

    const auto firstDown = !HotkeyPressed.exchange(true, std::memory_order_acq_rel);
    if (!firstDown) {
        return HotkeyCaptured.load(std::memory_order_acquire);
    }
    // Capture only a request that was actually handed to the game UI thread.
    // Focus/modifier mismatches and unavailable handoff paths remain native.
    const auto accepted = QueueHotkeyRequest();
    HotkeyCaptured.store(accepted, std::memory_order_release);
    return accepted;
}

LRESULT CALLBACK KeyboardHook(
    int code,
    WPARAM message,
    LPARAM parameter
) noexcept {
    if (code != HC_ACTION || !parameter) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    const auto* input = reinterpret_cast<const KBDLLHOOKSTRUCT*>(parameter);
    if (IsMouseHotkey(HotkeySettings.hotkey)
        || input->vkCode != HotkeySettings.hotkey.virtualKey) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }

    const auto isDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const auto isUp = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!isDown && !isUp) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    if (HandleInputTransition(
            isDown,
            isUp,
            (input->flags & LLKHF_INJECTED) != 0
        )) {
        return 1;
    }
    return CallNextHookEx(nullptr, code, message, parameter);
}

LRESULT CALLBACK MouseHook(
    int code,
    WPARAM message,
    LPARAM parameter
) noexcept {
    if (code != HC_ACTION || !parameter || !IsMouseHotkey(HotkeySettings.hotkey)) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    const auto* input = reinterpret_cast<const MSLLHOOKSTRUCT*>(parameter);
    bool matches{};
    bool isDown{};
    bool isUp{};
    switch (HotkeySettings.hotkey.virtualKey) {
    case VK_MBUTTON:
        matches = message == WM_MBUTTONDOWN || message == WM_MBUTTONUP;
        isDown = message == WM_MBUTTONDOWN;
        isUp = message == WM_MBUTTONUP;
        break;
    case VK_XBUTTON1:
    case VK_XBUTTON2: {
        const auto expected = HotkeySettings.hotkey.virtualKey == VK_XBUTTON1
            ? XBUTTON1
            : XBUTTON2;
        matches = (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP)
            && HIWORD(input->mouseData) == expected;
        isDown = message == WM_XBUTTONDOWN;
        isUp = message == WM_XBUTTONUP;
        break;
    }
    default:
        break;
    }
    if (!matches) return CallNextHookEx(nullptr, code, message, parameter);
    if (HandleInputTransition(
            isDown,
            isUp,
            (input->flags & LLMHF_INJECTED) != 0
        )) {
        return 1;
    }
    return CallNextHookEx(nullptr, code, message, parameter);
}

DWORD WINAPI InputThreadProc(void* parameter) noexcept {
    const auto module = static_cast<HMODULE>(parameter);
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    DispatchRequestMessage = RegisterWindowMessageW(
        L"RuffnecKk.RemoteStash.Hotkey.Dispatch"
    );
    const auto inputHook = SetWindowsHookExW(
        IsMouseHotkey(HotkeySettings.hotkey) ? WH_MOUSE_LL : WH_KEYBOARD_LL,
        IsMouseHotkey(HotkeySettings.hotkey) ? MouseHook : KeyboardHook,
        module,
        0
    );
    const auto uiHookTimer = SetTimer(nullptr, 0, 100, nullptr);
    if (!inputHook || !uiHookTimer || DispatchRequestMessage == 0) {
        if (uiHookTimer) KillTimer(nullptr, uiHookTimer);
        if (inputHook) UnhookWindowsHookEx(inputHook);
        InputThreadFailed.store(true, std::memory_order_release);
        InputThreadReady.store(true, std::memory_order_release);
        FreeLibraryAndExitThread(module, 1);
    }

    EnsureUiMessageHook(module);
    InputThreadReady.store(true, std::memory_order_release);
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_TIMER && message.wParam == uiHookTimer
            && !UiDispatchReady.load(std::memory_order_acquire)) {
            EnsureUiMessageHook(module);
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    KillTimer(nullptr, uiHookTimer);
    ResetUiMessageHook();
    UnhookWindowsHookEx(inputHook);
    FreeLibraryAndExitThread(module, 0);
}

bool StartInput() noexcept {
    InputThreadReady.store(false, std::memory_order_relaxed);
    InputThreadFailed.store(false, std::memory_order_relaxed);
    InputStopping.store(false, std::memory_order_release);
    HMODULE workerModule{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&InputThreadProc),
            &workerModule)) {
        return false;
    }
    InputThread = CreateThread(
        nullptr,
        0,
        InputThreadProc,
        workerModule,
        0,
        &InputThreadId
    );
    if (!InputThread) {
        FreeLibrary(workerModule);
        return false;
    }
    for (unsigned attempt = 0;
         attempt < 200 && !InputThreadReady.load(std::memory_order_acquire);
         ++attempt) {
        Sleep(10);
    }
    return InputThreadReady.load(std::memory_order_acquire)
        && !InputThreadFailed.load(std::memory_order_acquire);
}

bool StopInput() noexcept {
    InputStopping.store(true, std::memory_order_release);
    if (InputThreadId != 0) PostThreadMessageW(InputThreadId, WM_QUIT, 0, 0);
    if (InputThread) {
        const auto wait = WaitForSingleObject(InputThread, 3000);
        if (wait != WAIT_OBJECT_0) {
            if (Context) {
                Context->LogError(
                    "RemoteStash: hotkey input worker did not stop; its module reference is retained for safety."
                );
            }
            return false;
        }
        CloseHandle(InputThread);
    }
    InputThread = nullptr;
    InputThreadId = 0;
    DispatchRequestMessage = 0;
    return true;
}

void* AllocateCallSiteRelayPageNear(void* hint) noexcept {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity
    );
    const auto base = reinterpret_cast<std::uintptr_t>(hint) & ~(granularity - 1);

    for (std::uintptr_t delta = granularity;
         delta < 0x70000000ULL;
         delta += granularity) {
        if (base > std::numeric_limits<std::uintptr_t>::max() - delta) break;
        if (auto* memory = VirtualAlloc(
                reinterpret_cast<void*>(base + delta),
                systemInfo.dwPageSize,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE
            )) {
            return memory;
        }
    }
    return nullptr;
}

bool WriteAbsoluteJumpRelay(
    std::uint8_t* destination,
    const void* target
) noexcept {
    if (!destination || !target) return false;
    constexpr std::size_t RelaySize = 14;
    std::array<std::uint8_t, RelaySize> relay{
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const auto targetAddress = reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(relay.data() + 6, &targetAddress, sizeof(targetAddress));
    std::memcpy(destination, relay.data(), relay.size());
    return true;
}

template<std::size_t Count>
bool PatchCallSites(
    const std::array<RelativeCallSite, Count>& sites,
    std::uintptr_t relayRva
) noexcept {
    for (const auto& site : sites) {
        if (!PSh_ManifestPatchCallRel32(
                Context,
                site.manifestId,
                site.rva,
                site.expected.data(),
                static_cast<std::uint32_t>(site.expected.size()),
                relayRva,
                static_cast<std::uint32_t>(site.expected.size())
            )) {
            return false;
        }
    }
    return true;
}

bool InstallComposableCallSiteRedirects() noexcept {
    constexpr std::size_t RelayStride = 16;
    constexpr std::size_t RelayCount = 6;
    constexpr std::size_t RelayBytes = RelayStride * RelayCount;

    CallSiteRelayPage = AllocateCallSiteRelayPageNear(
        Base + ButtonDispatchUiMessageCallRva
    );
    if (!CallSiteRelayPage) return false;

    auto* relays = static_cast<std::uint8_t*>(CallSiteRelayPage);
    if (!WriteAbsoluteJumpRelay(relays, reinterpret_cast<const void*>(&HookDispatchUiMessage))
        || !WriteAbsoluteJumpRelay(
            relays + RelayStride,
            reinterpret_cast<const void*>(&HookIsRoomInTown)
        )
        || !WriteAbsoluteJumpRelay(
            relays + RelayStride * 2,
            reinterpret_cast<const void*>(&HookTransferItemToInventoryPage)
        )
        || (HotkeySettings.enabled && !WriteAbsoluteJumpRelay(
            relays + RelayStride * 3,
            reinterpret_cast<const void*>(&HookStashInterfaceTransition)
        ))
        || !WriteAbsoluteJumpRelay(
            relays + RelayStride * 4,
            reinterpret_cast<const void*>(&HookMovementUiClose)
        )
        || !WriteAbsoluteJumpRelay(
            relays + RelayStride * 5,
            reinterpret_cast<const void*>(&HookSharedGoldDeposit)
        )) {
        VirtualFree(CallSiteRelayPage, 0, MEM_RELEASE);
        CallSiteRelayPage = nullptr;
        return false;
    }

    DWORD previousProtection{};
    if (!VirtualProtect(
            CallSiteRelayPage,
            RelayBytes,
            PAGE_EXECUTE_READ,
            &previousProtection
        )) {
        VirtualFree(CallSiteRelayPage, 0, MEM_RELEASE);
        CallSiteRelayPage = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), CallSiteRelayPage, RelayBytes);

    const auto relayAddress = reinterpret_cast<std::uintptr_t>(CallSiteRelayPage);
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(Base);
    if (relayAddress < baseAddress) return false;
    const auto relayRva = relayAddress - baseAddress;
    const auto coreRedirectsInstalled =
        PatchCallSites(ButtonDispatchUiMessageCallSites, relayRva)
        && PatchCallSites(IsRoomInTownCallSites, relayRva + RelayStride)
        && PatchCallSites(
            TransferItemToInventoryPageCallSites,
            relayRva + RelayStride * 2
        )
        && PatchCallSites(
            MovementUiCloseCallSites,
            relayRva + RelayStride * 4
        )
        && PatchCallSites(
            SharedGoldDepositCallSites,
            relayRva + RelayStride * 5
        );
    if (!coreRedirectsInstalled) return false;
    return !HotkeySettings.enabled
        || PatchCallSites(
            StashInterfaceTransitionCallSites,
            relayRva + RelayStride * 3
        );
}

auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
    -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[1600]{};
    std::snprintf(
        message,
        sizeof(message),
        "RemoteStash 1.2.1 (plugin-misc): hotkeyEnabled=%s; hotkey=%s; hotkeyInput=%s; "
        "hotkeyUiDispatch=%s; config=%s; hotkeyAccepted=%llu; "
        "hotkeyCoalesced=%llu; hotkeyDispatched=%llu; hotkeyRefused=%llu; "
        "hotkeyFailed=%llu; companionCloseTickets=%llu; "
        "companionInventoryCloses=%llu; companionCloseExpirations=%llu; "
        "hotkeyTransitionTickets=%llu; hotkeyTransitionApplications=%llu; "
        "hotkeyTransitionExpirations=%llu; hotkeyMouseResetSuppressions=%llu; "
        "independentInventoryRestores=%llu; "
        "layoutBindings=%llu; bindingFailures=%llu; "
        "clientOpenRequests=%llu; clientCloseRequests=%llu; sessionsOpened=%llu; "
        "sessionsClosed=%llu; sessionsPruned=%llu; activeSessions=%llu; "
        "remoteItemOps=%llu; remoteItemFailures=%llu; maxRemoteItemMs=%llu; "
        "remoteGoldOps=%llu; "
        "remoteGoldFailures=%llu; stashProximityBypasses=%llu; "
        "clientTownBypasses=%llu; sharedTransferOps=%llu; "
        "sharedTransferFailures=%llu; quickMoveUiBypasses=%llu; "
        "automaticCloseSuppressions=%llu; generalUiCloseSuppressions=%llu; "
        "movementInventoryCloseSuppressions=%llu; explicitClosePasses=%llu.",
        HotkeySettings.enabled ? "true" : "false",
        HotkeySettings.hotkeyText.c_str(),
        IsMouseHotkey(HotkeySettings.hotkey) ? "mouse" : "keyboard",
        UiDispatchReady.load(std::memory_order_acquire) ? "ready" : "disabled",
        LoadedConfigPath.c_str(),
        static_cast<unsigned long long>(
            HotkeyAcceptedRequests.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyCoalescedRequests.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyDispatchedRequests.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyRefusedRequests.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyFailedRequests.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            CompanionInventoryCloseTickets.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            CompanionInventoryCloses.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            CompanionInventoryCloseExpirations.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyOpenTransitionTickets.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyOpenTransitionApplications.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyOpenTransitionExpirations.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyMouseResetSuppressions.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            IndependentInventoryRestores.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(DynamicPlacements.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PlacementFailures.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(OpenRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(CloseRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ServerSessionsOpened.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ServerSessionsClosed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ServerSessionsPruned.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RemoteSessionCount()),
        static_cast<unsigned long long>(RemoteItemOperations.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RemoteItemFailures.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(MaxRemoteItemOperationMs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RemoteGoldTransactions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RemoteGoldFailures.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RemoteStashProximityBypasses.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(RemoteTownBypasses.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RemoteSharedTransferOperations.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            RemoteSharedTransferFailures.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            RemoteQuickMoveUiBypasses.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            RemoteAutomaticCloseSuppressions.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            RemoteGeneralUiCloseSuppressions.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            RemoteMovementInventoryCloseSuppressions.load(
                std::memory_order_relaxed
            )
        ),
        static_cast<unsigned long long>(
            RemoteExplicitClosePasses.load(std::memory_order_relaxed)
        )
    );
    command->plugin->WriteConsoleMessage(message);
    char inputDiagnostic[144]{};
    std::snprintf(
        inputDiagnostic,
        sizeof(inputDiagnostic),
        "RemoteStash input: hotkeyMouseResetSuppressions=%llu; "
        "hotkeyMouseStateRestorations=%llu",
        static_cast<unsigned long long>(
            HotkeyMouseResetSuppressions.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyMouseStateRestorations.load(std::memory_order_relaxed)
        )
    );
    command->plugin->WriteConsoleMessage(inputDiagnostic);
    return D2RL::ConsoleCommandResult::Handled;
}
} // namespace

namespace RuffnecKk::RemoteStash {

bool Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& miscConfig
) noexcept {
    if (!context) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    DynamicPlacements.store(0, std::memory_order_relaxed);
    PlacementFailures.store(0, std::memory_order_relaxed);
    OpenRequests.store(0, std::memory_order_relaxed);
    CloseRequests.store(0, std::memory_order_relaxed);
    ServerSessionsOpened.store(0, std::memory_order_relaxed);
    ServerSessionsClosed.store(0, std::memory_order_relaxed);
    ServerSessionsPruned.store(0, std::memory_order_relaxed);
    RemoteItemOperations.store(0, std::memory_order_relaxed);
    RemoteItemFailures.store(0, std::memory_order_relaxed);
    MaxRemoteItemOperationMs.store(0, std::memory_order_relaxed);
    RemoteStashProximityBypasses.store(0, std::memory_order_relaxed);
    RemoteTownBypasses.store(0, std::memory_order_relaxed);
    RemoteSharedTransferOperations.store(0, std::memory_order_relaxed);
    RemoteSharedTransferFailures.store(0, std::memory_order_relaxed);
    RemoteGoldTransactions.store(0, std::memory_order_relaxed);
    RemoteGoldFailures.store(0, std::memory_order_relaxed);
    RemoteQuickMoveUiBypasses.store(0, std::memory_order_relaxed);
    HotkeySettings = {};
    LoadedConfigPath = "built-in disabled defaults";
    InputThread = nullptr;
    InputThreadId = 0;
    InputThreadReady.store(false, std::memory_order_relaxed);
    InputThreadFailed.store(false, std::memory_order_relaxed);
    InputStopping.store(false, std::memory_order_relaxed);
    UiDispatchReady.store(false, std::memory_order_relaxed);
    HotkeyPressed.store(false, std::memory_order_relaxed);
    HotkeyCaptured.store(false, std::memory_order_relaxed);
    HotkeyRequestPending.store(false, std::memory_order_relaxed);
    HotkeyAcceptedRequests.store(0, std::memory_order_relaxed);
    HotkeyCoalescedRequests.store(0, std::memory_order_relaxed);
    HotkeyDispatchedRequests.store(0, std::memory_order_relaxed);
    HotkeyRefusedRequests.store(0, std::memory_order_relaxed);
    HotkeyFailedRequests.store(0, std::memory_order_relaxed);
    CompanionInventoryCloseDeadline.store(0, std::memory_order_relaxed);
    CompanionInventoryCloseTickets.store(0, std::memory_order_relaxed);
    CompanionInventoryCloses.store(0, std::memory_order_relaxed);
    CompanionInventoryCloseExpirations.store(0, std::memory_order_relaxed);
    HotkeyOpenTransitionDeadline.store(0, std::memory_order_relaxed);
    HotkeyOpenTransitionTickets.store(0, std::memory_order_relaxed);
    HotkeyOpenTransitionApplications.store(0, std::memory_order_relaxed);
    HotkeyOpenTransitionExpirations.store(0, std::memory_order_relaxed);
    HotkeyMouseResetSuppressions.store(0, std::memory_order_relaxed);
    HotkeyMouseStateRestorations.store(0, std::memory_order_relaxed);
    IndependentInventoryRestores.store(0, std::memory_order_relaxed);
    UiReadyReported.store(false, std::memory_order_relaxed);
    UiMessageHookHandle = nullptr;
    UiThreadId = 0;
    DispatchRequestMessage = 0;
    FindTopLevelPanel = nullptr;
    OriginalOpenInterfaceState = nullptr;
    OriginalCloseInterfaceState = nullptr;
    OriginalMovementUiClose = nullptr;
    OriginalStashInterfaceTransition = nullptr;
    OriginalResetMouseInputState = nullptr;
    OriginalResetMouseInputStateWithFinalize = nullptr;
    MarkUiDirty = nullptr;
    PlacementSuccessReported.store(false, std::memory_order_relaxed);
    PlacementFailureReported.store(false, std::memory_order_relaxed);
    RemoteStashMessage.store(nullptr, std::memory_order_relaxed);
    ExternalUiMessageInterceptor.store(nullptr, std::memory_order_relaxed);
    BrokerReady.store(false, std::memory_order_relaxed);
    RemoteClientSessionActive.store(false, std::memory_order_relaxed);
    RemoteClientUiObservedOpen.store(false, std::memory_order_relaxed);
    RemoteQuickMoveWithdrawalDeadline.store(0, std::memory_order_relaxed);
    RemoteAutomaticCloseSuppressions.store(0, std::memory_order_relaxed);
    RemoteGeneralUiCloseSuppressions.store(0, std::memory_order_relaxed);
    RemoteExplicitClosePasses.store(0, std::memory_order_relaxed);
    RemoteMovementInventoryCloseSuppressions.store(
        0,
        std::memory_order_relaxed
    );
    RemoteItemScope = false;
    RemoteGoldScope = false;
    RemoteHotkeyOpenTransitionScope = false;
    RemoteMovementUiCloseScope = false;
    GoldRangeStub = nullptr;
    GoldRangeTrampoline = nullptr;
    CallSiteRelayPage = nullptr;
    OriginalSharedGoldDeposit = nullptr;
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        RemoteSessions.clear();
    } catch (...) {
        return false;
    }

    if (!Base || !LoadConfig(miscConfig)) return false;
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError("RemoteStash: only D2R build 92777 is supported.");
        return false;
    }
    if (!ValidateRuntime()) {
        context->LogError("RemoteStash: 92777 native signature mismatch; plugin refused.");
        return false;
    }
    if (HotkeySettings.enabled && !ValidateHotkeyRuntime()) {
        context->LogError(
            "RemoteStash: 92777 hotkey UI/input signature mismatch; plugin refused."
        );
        return false;
    }

    if (HotkeySettings.enabled) {
        FindTopLevelPanel = At<FindTopLevelPanelFn>(FindTopLevelPanelRva);
        MarkUiDirty = At<MarkUiDirtyFn>(MarkUiDirtyRva);
        OriginalStashInterfaceTransition = At<StashInterfaceTransitionFn>(
            StashInterfaceTransitionRva
        );
    }
    FindWidget = At<FindWidgetFn>(FindWidgetRva);
    GetWidgetRect = At<GetWidgetRectFn>(GetWidgetRectRva);
    SendServerUi = At<SendServerUiFn>(SendServerUiRva);
    GetClientFromPlayer = At<GetClientFromPlayerFn>(GetClientFromPlayerRva);
    GetLocalDataContext = At<GetLocalDataContextFn>(GetLocalDataContextRva);
    GetLocalPlayer = At<GetLocalPlayerFn>(GetLocalPlayerRva);
    QueueOutgoingPacket = At<QueueOutgoingPacketFn>(QueueOutgoingPacketRva);
    OriginalDispatchUiMessage = At<DispatchUiMessageFn>(DispatchUiMessageRva);
    OriginalSharedGoldDeposit = At<SharedGoldDepositFn>(
        SharedGoldDepositRva
    );
    OriginalIsRoomInTown = At<IsRoomInTownFn>(IsRoomInTownRva);
    OriginalTransferItemToInventoryPage = At<TransferItemToInventoryPageFn>(
        TransferItemToInventoryPageRva
    );
    OriginalMovementUiClose = At<MovementUiCloseFn>(MovementUiCloseRva);
    // The pack's Bulk Skill Point Allocation feature owns the dispatcher
    // prologue. RemoteStash reaches the current live dispatcher through its
    // unique button call site instead of installing a competing hook.
    if (!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.configurePlayerInventory"),
            ConfigurePlayerInventoryRva,
            ConfigurePlayerInventoryExpected.data(),
            static_cast<std::uint32_t>(ConfigurePlayerInventoryExpected.size()),
            HookConfigurePlayerInventory,
            &OriginalConfigurePlayerInventory
        )) {
        context->LogError("RemoteStash: inventory-panel hook failed.");
        return false;
    }
    if (!InstallComposableCallSiteRedirects()) {
        context->LogError(
            "RemoteStash: composable UI, town-state, quick-move, or shared-gold "
            "call-site redirect failed."
        );
        return false;
    }
    BrokerReady.store(true, std::memory_order_release);

    if (!CreateGoldRangeStub()) {
        context->LogError("RemoteStash: scoped stash-range hook failed.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.getUiState"),
            GetUiStateRva,
            GetUiStateExpected.data(),
            static_cast<std::uint32_t>(GetUiStateExpected.size()),
            HookGetUiState,
            &OriginalGetUiState
        )) {
        context->LogError("RemoteStash: scoped quick-move UI-state hook failed.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.closeInterfaceState"),
            CloseInterfaceStateRva,
            CloseInterfaceStateExpected.data(),
            static_cast<std::uint32_t>(CloseInterfaceStateExpected.size()),
            HookCloseInterfaceState,
            &OriginalCloseInterfaceState
        )) {
        context->LogError("RemoteStash: persistent remote UI hook failed.");
        return false;
    }
    if (HotkeySettings.enabled
        && !PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.openInterfaceState"),
            OpenInterfaceStateRva,
            OpenInterfaceStateExpected.data(),
            static_cast<std::uint32_t>(OpenInterfaceStateExpected.size()),
            HookOpenInterfaceState,
            &OriginalOpenInterfaceState
        )) {
        context->LogError("RemoteStash: post-open companion inventory hook failed.");
        return false;
    }
    if (HotkeySettings.enabled
        && !PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.resetMouseInputFinalize"),
            ResetMouseInputStateWithFinalizeRva,
            ResetMouseInputStateWithFinalizeExpected.data(),
            static_cast<std::uint32_t>(
                ResetMouseInputStateWithFinalizeExpected.size()
            ),
            HookResetMouseInputStateWithFinalize,
            &OriginalResetMouseInputStateWithFinalize
        )) {
        context->LogError(
            "RemoteStash: scoped held-click state restoration hook failed."
        );
        return false;
    }
    if (HotkeySettings.enabled
        && !PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.resetMouseInput"),
            ResetMouseInputStateRva,
            ResetMouseInputStateExpected.data(),
            static_cast<std::uint32_t>(ResetMouseInputStateExpected.size()),
            HookResetMouseInputState,
            &OriginalResetMouseInputState
        )) {
        context->LogError("RemoteStash: scoped held-click preservation hook failed.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.goldButtonHandler"),
            GoldButtonHandlerRva,
            GoldButtonHandlerExpected.data(),
            static_cast<std::uint32_t>(GoldButtonHandlerExpected.size()),
            HookGoldButtonHandler,
            &OriginalGoldButtonHandler
        )) {
        context->LogError("RemoteStash: stash gold-button hook failed.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.removeServerUnit"),
            RemoveServerUnitRva,
            RemoveServerUnitExpected.data(),
            static_cast<std::uint32_t>(RemoveServerUnitExpected.size()),
            HookRemoveServerUnit,
            &OriginalRemoveServerUnit
        )) {
        context->LogError("RemoteStash: player-session lifecycle hook failed.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.removeItemHandler"),
            RemoveItemHandlerRva,
            RemoveItemHandlerExpected.data(),
            static_cast<std::uint32_t>(RemoveItemHandlerExpected.size()),
            HookRemoveItemHandler,
            &OriginalRemoveItemHandler
        )) {
        context->LogError("RemoteStash: stash item-removal hook failed.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.validateItemPacketState"),
            ValidateItemPacketStateRva,
            ValidateItemPacketStateExpected.data(),
            static_cast<std::uint32_t>(ValidateItemPacketStateExpected.size()),
            HookValidateItemPacketState,
            &OriginalValidateItemPacketState
        )) {
        context->LogError("RemoteStash: scoped stash-proximity validator hook failed.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.sharedDepositHandler"),
            SharedDepositHandlerRva,
            SharedDepositHandlerExpected.data(),
            static_cast<std::uint32_t>(SharedDepositHandlerExpected.size()),
            HookSharedDepositHandler,
            &OriginalSharedDepositHandler
        )) {
        context->LogError("RemoteStash: shared-deposit handler hook failed.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.sharedWithdrawalHandler"),
            SharedWithdrawalHandlerRva,
            SharedWithdrawalHandlerExpected.data(),
            static_cast<std::uint32_t>(SharedWithdrawalHandlerExpected.size()),
            HookSharedWithdrawalHandler,
            &OriginalSharedWithdrawalHandler
        )) {
        context->LogError("RemoteStash: shared-withdrawal handler hook failed.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(
            context,
            PSH_MANIFEST_SITE("misc.remoteStash.insertItemHandler"),
            InsertItemHandlerRva,
            InsertItemHandlerExpected.data(),
            static_cast<std::uint32_t>(InsertItemHandlerExpected.size()),
            HookInsertItemHandler,
            &OriginalInsertItemHandler
        )) {
        context->LogError("RemoteStash: authoritative open-request hook failed.");
        return false;
    }

    if (HotkeySettings.enabled && !StartInput()) {
        (void)StopInput();
        context->LogError("RemoteStash: bounded hotkey input worker failed.");
        return false;
    }

    if (!PSh_RegisterConsoleCommand(
            context,
            "remote-stash",
            Status,
            "Show Remote Stash status and counters."
        )) {
        context->LogWarn("RemoteStash: status command could not be registered.");
    }

    char message[620]{};
    std::snprintf(
        message,
        sizeof(message),
        "RemoteStash 1.2.1 active for D2R 3.2.92777; button dispatch=PluginPack; "
        "hotkey=%s; binding=%s; input=%s; config=%s.",
        HotkeySettings.enabled ? "enabled" : "disabled",
        HotkeySettings.hotkeyText.c_str(),
        IsMouseHotkey(HotkeySettings.hotkey) ? "mouse" : "keyboard",
        LoadedConfigPath.c_str()
    );
    context->LogInfo(message);
    return true;
}

void Unload() noexcept {
    if (!StopInput()) return;
    BrokerReady.store(false, std::memory_order_release);
    RemoteStashMessage.store(nullptr, std::memory_order_release);
    ExternalUiMessageInterceptor.store(nullptr, std::memory_order_release);
    DeactivateRemoteClientSession(false);
    RemoteItemScope = false;
    RemoteGoldScope = false;
    RemoteHotkeyOpenTransitionScope = false;
    RemoteMovementUiCloseScope = false;
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        RemoteSessions.clear();
    } catch (...) {
    }
    HotkeyRequestPending.store(false, std::memory_order_release);
    FindTopLevelPanel = nullptr;
    OriginalOpenInterfaceState = nullptr;
    OriginalCloseInterfaceState = nullptr;
    OriginalMovementUiClose = nullptr;
    OriginalStashInterfaceTransition = nullptr;
    OriginalResetMouseInputState = nullptr;
    OriginalResetMouseInputStateWithFinalize = nullptr;
    OriginalSharedGoldDeposit = nullptr;
    MarkUiDirty = nullptr;
    if (GoldRangeStub) {
        VirtualFree(GoldRangeStub, 0, MEM_RELEASE);
        GoldRangeStub = nullptr;
    }
    GoldRangeTrampoline = nullptr;
    if (CallSiteRelayPage) {
        VirtualFree(CallSiteRelayPage, 0, MEM_RELEASE);
        CallSiteRelayPage = nullptr;
    }
    HotkeySettings = {};
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::RemoteStash

extern "C" __declspec(dllexport) bool __fastcall
RuffneckkRemoteStashInterceptUiMessage(void* message) noexcept {
    if (!Context || !FindWidget || !BrokerReady.load(std::memory_order_acquire)) {
        return false;
    }
    return InterceptUiMessageChain(message);
}
