#include <D2RLPlugin/api.h>
#include <plugin-shared.h>
#include <Windows.h>
#include <json.hpp>

#include "mass-identify.h"
#include "mass-identify-policy.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace RuffnecKk::MassIdentify;

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t QueueOutgoingPacketRva = 0x0EE2A0;
constexpr std::uintptr_t GetLocalDataContextRva = 0x08B2D0;
constexpr std::uintptr_t GetLocalPlayerRva = 0x09A480;
constexpr std::uintptr_t TargetingPacketWorkerRva = 0x1C7A30;
constexpr std::uintptr_t IsVirtualKeyDownRva = 0x120A100;
constexpr std::uintptr_t LegacyDropAppenderCallRva = 0x2279BD;
constexpr std::uintptr_t ModernDropAppenderCallRva = 0x2C552D;
constexpr std::array<std::uintptr_t, 2> LegacyMoveAppenderCallRvas{
    0x2278DC,
    0x227936,
};
constexpr std::array<const char*, 2> LegacyMoveAppenderManifestIds{
    PSH_MANIFEST_SITE("items.massIdentify.legacyMoveAppenderA"),
    PSH_MANIFEST_SITE("items.massIdentify.legacyMoveAppenderB"),
};
constexpr std::array<std::uintptr_t, 3> ModernMoveAppenderCallRvas{
    0x2C5241,
    0x2C528D,
    0x2C53AB,
};
constexpr std::array<const char*, 3> ModernMoveAppenderManifestIds{
    PSH_MANIFEST_SITE("items.massIdentify.modernMoveAppenderA"),
    PSH_MANIFEST_SITE("items.massIdentify.modernMoveAppenderB"),
    PSH_MANIFEST_SITE("items.massIdentify.modernMoveAppenderC"),
};
constexpr std::uintptr_t AlternateMoveAppenderCallRva = 0x2CA2E0;
constexpr std::uintptr_t ModernSellAppenderCallRva = 0x2C51A9;
constexpr std::uintptr_t ModernGiveAppenderCallRva = 0x2C5455;
constexpr std::uintptr_t ModernUiStateProbeCallRva = 0x2C55F2;
constexpr std::uintptr_t IsUiStateOpenRva = 0x0CE500;
constexpr std::uintptr_t GetLocalizedStringByKeyRva = 0x5F4B90;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t CheckStateRva = 0x3351B0;
constexpr std::uintptr_t GetUnitIdRva = 0x34A330;
constexpr std::uintptr_t GetUnitInventoryRva = 0x34A360;
constexpr std::uintptr_t GetItemDataRva = 0x34A500;
constexpr std::uintptr_t GetUnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t CheckItemFlagRva = 0x36E2D0;
constexpr std::uintptr_t SetItemFlagRva = 0x36D8F0;
constexpr std::uintptr_t GetItemSuffixIdRva = 0x36EDD0;
constexpr std::uintptr_t GetItemCodeRva = 0x36EF50;
constexpr std::uintptr_t GetCursorItemRva = 0x388A70;
constexpr std::uintptr_t GetFirstItemRva = 0x388C10;
constexpr std::uintptr_t GetNextItemRva = 0x38ABA0;
constexpr std::uintptr_t GetParentInventoryRva = 0x38AC50;
constexpr std::uintptr_t GetInventoryOwnerIdRva = 0x388BA0;
constexpr std::uintptr_t GetFirstCorpseRva = 0x388E00;
constexpr std::uintptr_t GetNextCorpseRva = 0x38CD70;
constexpr std::uintptr_t GetCorpseUnitIdRva = 0x2EF880;
constexpr std::uintptr_t IdentifyItemRva = 0x46E8C0;
constexpr std::uintptr_t SynchronizeQuantityRva = 0x46F090;
constexpr std::uintptr_t ServerUnitRva = 0x48FE80;
constexpr std::uintptr_t CainIdentifyCallbackRva = 0x4C6C90;
constexpr std::int32_t SharedStashProxyState = 0xBA;

constexpr std::array<std::uint8_t, 32> QueueOutgoingPacketExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
    0x48, 0x81, 0xEC, 0x30, 0x02, 0x00, 0x00, 0x48,
    0x8B, 0x05, 0x12, 0xD0, 0x8D, 0x02, 0x48, 0x33,
    0xC4, 0x48, 0x89, 0x84, 0x24, 0x20, 0x02, 0x00,
};
constexpr std::array<std::uint8_t, 35> TargetingPacketWorkerExpected{
    0x40, 0x53, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00,
    0x00, 0x48, 0x8B, 0x05, 0x88, 0x38, 0x80, 0x02,
    0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x90,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0xE8, 0x8D,
    0x77, 0xF8, 0xFF,
};
constexpr std::array<std::uint8_t, 21> IsVirtualKeyDownExpected{
    0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15, 0x86, 0x6E,
    0xAA, 0x00, 0xC1, 0xE8, 0x0F, 0x83, 0xE0, 0x01,
    0x48, 0x83, 0xC4, 0x28, 0xC3,
};
constexpr std::array<std::uint8_t, 5> LegacyDropAppenderCallExpected{
    0xE8, 0xCE, 0xD1, 0x3C, 0x00,
};
constexpr std::array<std::uint8_t, 5> ModernDropAppenderCallExpected{
    0xE8, 0x5E, 0xF6, 0x32, 0x00,
};
constexpr std::array<std::array<std::uint8_t, 5>, 2>
        LegacyMoveAppenderCallsExpected{{
    {0xE8, 0xAF, 0xD2, 0x3C, 0x00},
    {0xE8, 0x55, 0xD2, 0x3C, 0x00},
}};
constexpr std::array<std::array<std::uint8_t, 5>, 3>
        ModernMoveAppenderCallsExpected{{
    {0xE8, 0x4A, 0xF9, 0x32, 0x00},
    {0xE8, 0xFE, 0xF8, 0x32, 0x00},
    {0xE8, 0xE0, 0xF7, 0x32, 0x00},
}};
constexpr std::array<std::uint8_t, 5> AlternateMoveAppenderCallExpected{
    0xE8, 0xAB, 0xA8, 0x32, 0x00,
};
constexpr std::array<std::uint8_t, 5> ModernSellAppenderCallExpected{
    0xE8, 0xE2, 0xF9, 0x32, 0x00,
};
constexpr std::array<std::uint8_t, 5> ModernGiveAppenderCallExpected{
    0xE8, 0x36, 0xF7, 0x32, 0x00,
};
constexpr std::array<std::uint8_t, 5> ModernUiStateProbeCallExpected{
    0xE8, 0x09, 0x8F, 0xE0, 0xFF,
};
constexpr std::array<std::uint8_t, 32> CainIdentifyCallbackExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0xAC,
    0x24, 0x18, 0xB0, 0xFF, 0xFF, 0xB8, 0xE8, 0x50,
    0x00, 0x00, 0xE8, 0x39, 0xA4, 0xE0, 0x00, 0x48,
    0x2B, 0xE0, 0x48, 0x8B, 0x05, 0x17, 0x46, 0x50,
};
constexpr std::array<std::uint8_t, 32> IdentifyItemExpected{
    0x48, 0x89, 0x6C, 0x24, 0x20, 0x41, 0x54, 0x41,
    0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x49,
    0x8B, 0xE8, 0x45, 0x0F, 0xB6, 0xE1, 0x4C, 0x8B,
    0xF2, 0x4C, 0x8D, 0x0D, 0x30, 0xD7, 0x8A, 0x01,
};
constexpr std::array<std::uint8_t, 32> SynchronizeQuantityExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x49, 0x8B,
    0xF8, 0x48, 0x8B, 0xDA, 0x45, 0x33, 0xC0, 0x48,
};
constexpr std::array<std::uint8_t, 32> GetLocalDataContextExpected{
    0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0x8B, 0x05, 0x76, 0x84, 0x99, 0x02, 0xC3, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 32> GetLocalPlayerExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85,
    0x00, 0x00, 0x00, 0x8B, 0xD9, 0x48, 0x89, 0x5C,
    0x24, 0x38, 0x48, 0x83, 0xFB, 0x08, 0x72, 0x19,
};
constexpr std::array<std::uint8_t, 16> GetUnitStatExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
};
constexpr std::array<std::uint8_t, 32> CheckStateExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B,
    0xDA, 0x48, 0x8B, 0xF1, 0xE8, 0x07, 0x68, 0x01,
    0x00, 0x85, 0xC0, 0x74, 0x0E, 0x83, 0xE8, 0x01,
};
constexpr std::array<std::uint8_t, 32> GetUnitIdExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x39, 0xCA, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0xFF, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetUnitInventoryExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x70, 0xCC, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetUnitTypeExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x39, 0x9E, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0x06, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 32> GetItemDataExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x74, 0xC4, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
};
constexpr std::array<std::uint8_t, 16> CheckItemFlagExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48,
};
constexpr std::array<std::uint8_t, 16> SetItemFlagExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
};
constexpr std::array<std::uint8_t, 32> GetItemSuffixIdExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0xBA, 0x12, 0x00, 0x00, 0x00, 0x48,
    0x8B, 0xD9, 0xE8, 0xA9, 0x4A, 0x00, 0x00, 0x85,
    0xC0, 0x0F, 0x84, 0x9C, 0x00, 0x00, 0x00, 0x48,
};
constexpr std::array<std::uint8_t, 16> GetCursorItemExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1B, 0x88, 0x4C,
};
constexpr std::array<std::uint8_t, 32> GetItemCodeExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x80, 0x83, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetFirstItemExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x74, 0x2E, 0x81, 0x39,
    0x04, 0x03, 0x02, 0x01, 0x74, 0x1C, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00,
};
constexpr std::array<std::uint8_t, 32> GetNextItemExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x10, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x84, 0x98, 0xFF, 0xFF, 0xEB, 0x67, 0xE8, 0x0D,
};
constexpr std::array<std::uint8_t, 32> GetParentInventoryExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0x6D,
    0x0D, 0xFC, 0xFF, 0x83, 0xF8, 0x04, 0x74, 0x19,
    0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24,
};
constexpr std::array<std::uint8_t, 32> GetInventoryOwnerIdExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1E, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0xB4, 0xC2, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x30,
};
constexpr std::array<std::uint8_t, 16> GetFirstCorpseExpected{
    0x48, 0x8B, 0x41, 0x68, 0xC3, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 16> GetNextCorpseExpected{
    0x48, 0x8B, 0x41, 0x10, 0xC3, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 16> GetCorpseUnitIdExpected{
    0x8B, 0x01, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 32> ServerUnitExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x48,
    0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x38,
};

struct GameStringView {
    const char* data{};
    std::size_t size{};
};

struct TooltipLocale {
    std::string_view defenseFingerprint;
    std::string_view massIdText;
};

// D2R's thirteen shipped locales, selected from the same native defense
// fingerprint used by AdvancedItemTooltips. Only MassID's invented label lives
// in this table; the game remains the authority for the active language.
constexpr std::array TooltipLocales{
    TooltipLocale{"Defense: %d", "Shift + Right Click to Mass ID"},
    TooltipLocale{"防禦：%d", "Shift + 右鍵點擊以批量鑑定"},
    TooltipLocale{"Verteidigung: %d", "Umschalt + Rechtsklick für Massenidentifizierung"},
    TooltipLocale{"Defensa: %d", "Mayús + clic derecho para identificar todo"},
    TooltipLocale{"Défense : %d", "Maj + clic droit pour tout identifier"},
    TooltipLocale{"Difesa: %d", "Maiusc + clic destro per identificare tutto"},
    TooltipLocale{"방어력: %d", "Shift + 오른쪽 클릭으로 모두 감정"},
    TooltipLocale{"Obrona: %d", "Shift + prawy przycisk, aby zidentyfikować wszystko"},
    TooltipLocale{"Defensa: %d", "Mayús + clic derecho para identificar todo"},
    TooltipLocale{"防御力: %d", "Shift + 右クリックですべて鑑定"},
    TooltipLocale{"Defesa: %d", "Shift + clique direito para identificar tudo"},
    TooltipLocale{"Защита: %d", "Shift + ПКМ, чтобы опознать всё"},
    TooltipLocale{"防御: %d", "Shift + 右键点击以批量辨识"},
};

using QueueOutgoingPacketFn = void(__fastcall*)(
    const std::uint8_t*, std::int32_t) noexcept;
using TargetingPacketWorkerFn = void(__fastcall*)(
    const std::uint8_t*) noexcept;
using CainIdentifyCallbackFn = std::int32_t(__fastcall*)(
    void*, void*, const std::uint8_t*, std::int32_t) noexcept;
using GetLocalDataContextFn = std::int32_t(__fastcall*)() noexcept;
using GetLocalPlayerFn = void*(__fastcall*)(std::int32_t) noexcept;
using IsVirtualKeyDownFn = std::int32_t(__fastcall*)(std::int32_t) noexcept;
using IsUiStateOpenFn = std::int32_t(__fastcall*)(std::int32_t) noexcept;
using GetLocalizedStringByKeyFn = const char*(__fastcall*)(
    const GameStringView*) noexcept;
using GetUnitInventoryFn = void*(__fastcall*)(void*) noexcept;
using GetCursorItemFn = void*(__fastcall*)(void*) noexcept;
using GetParentInventoryFn = void*(__fastcall*)(void*) noexcept;
using GetUnitTypeFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetItemDataFn = void*(__fastcall*)(void*) noexcept;
using GetItemCodeFn = std::uint32_t(__fastcall*)(void*) noexcept;
using GetUnitIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetServerUnitFn = void*(__fastcall*)(void*, std::int32_t, std::int32_t) noexcept;
using GetFirstItemFn = void*(__fastcall*)(void*) noexcept;
using GetNextItemFn = void*(__fastcall*)(void*) noexcept;
using GetInventoryOwnerIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetFirstCorpseFn = void*(__fastcall*)(void*) noexcept;
using GetNextCorpseFn = void*(__fastcall*)(void*) noexcept;
using GetCorpseUnitIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using CheckItemFlagFn = std::int32_t(__fastcall*)(
    void*, std::uint32_t) noexcept;
using SetItemFlagFn = void(__fastcall*)(
    void*, std::uint32_t, std::int32_t) noexcept;
using GetItemSuffixIdFn = std::uint16_t(__fastcall*)(void*) noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::int32_t) noexcept;
using CheckStateFn = std::int32_t(__fastcall*)(
    void*, std::int32_t) noexcept;
using IdentifyItemFn = void(__fastcall*)(
    void*, void*, void*, std::uint8_t) noexcept;
using SynchronizeQuantityFn = void(__fastcall*)(
    void*, void*, void*, std::int32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
constexpr const char* ConfigPath = "items.massIdentify";

QueueOutgoingPacketFn QueueOutgoingPacket{};
TargetingPacketWorkerFn OriginalTargetingPacketWorker{};
CainIdentifyCallbackFn OriginalCainIdentifyCallback{};
GetLocalDataContextFn GetLocalDataContext{};
GetLocalPlayerFn GetLocalPlayer{};
IsVirtualKeyDownFn IsVirtualKeyDown{};
IsUiStateOpenFn IsUiStateOpen{};
GetLocalizedStringByKeyFn GetLocalizedStringByKey{};
GetUnitInventoryFn GetUnitInventory{};
GetCursorItemFn GetCursorItem{};
GetParentInventoryFn GetParentInventory{};
GetUnitTypeFn GetUnitType{};
GetItemDataFn GetItemData{};
GetItemCodeFn GetItemCode{};
GetUnitIdFn GetUnitId{};
GetServerUnitFn GetServerUnit{};
GetFirstItemFn GetFirstItem{};
GetNextItemFn GetNextItem{};
GetInventoryOwnerIdFn GetInventoryOwnerId{};
GetFirstCorpseFn GetFirstCorpse{};
GetNextCorpseFn GetNextCorpse{};
GetCorpseUnitIdFn GetCorpseUnitId{};
CheckItemFlagFn CheckItemFlag{};
SetItemFlagFn SetItemFlag{};
GetItemSuffixIdFn GetItemSuffixId{};
GetUnitStatFn GetUnitStat{};
CheckStateFn CheckState{};
IdentifyItemFn IdentifyItem{};
SynchronizeQuantityFn SynchronizeQuantity{};

std::atomic<std::uint64_t> RequestsSent{};
std::atomic<std::uint64_t> GesturesObserved{};
std::atomic<std::uint64_t> TargetingWorkersObserved{};
std::atomic<std::uint64_t> UiContextProbesObserved{};
std::atomic<std::uint64_t> RequestsAccepted{};
std::atomic<std::uint64_t> RequestsRejected{};
std::atomic<std::uint64_t> ItemsIdentified{};
std::atomic<std::uint64_t> ChargesConsumed{};
std::atomic<std::uint32_t> HoveredIdentifyTomeGuid{};
std::atomic<std::uint64_t> HoveredIdentifyTomeTick{};
std::atomic<std::uint32_t> PendingMassIdGuid{};
std::atomic<std::uint64_t> PendingMassIdTick{};
std::atomic_bool SuppressRightButtonUp{};
std::atomic_bool PluginActive{};
void* TooltipRelayPage{};
bool TooltipCallSitesInstalled{};
std::atomic<HHOOK> GameMessageHook{};
std::mutex GameMessageHookMutex;
HMODULE GameMessageHookModule{};
std::atomic<std::uint32_t> ActiveMessageHookCallbacks{};

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

template<std::size_t Size>
bool Matches(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected) noexcept {
    return Base && std::memcmp(Base + rva, expected.data(), Size) == 0;
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

std::uint8_t GetInventoryPage(void* item) noexcept {
    return ReadInventoryPageFromItemData(GetItemData(item));
}

bool LoadConfig(const nlohmann::json& itemsConfig) noexcept {
    try {
        Settings = ParseConfig(itemsConfig);
        return true;
    } catch (const std::exception& exception) {
        if (Context) {
            const auto message = std::string("plugin-items: invalid ")
                + ConfigPath + " (" + exception.what() + ").";
            Context->LogError(message.c_str());
        }
        return false;
    }
}

bool ValidateRuntime() noexcept {
    bool valid = Base != nullptr;
    const auto check = [&valid](
            std::uintptr_t rva,
            const auto& expected,
            const char* label) noexcept {
        if (Matches(rva, expected)) return;
        valid = false;
        if (Context) {
            char message[320]{};
            std::snprintf(
                message,
                sizeof(message),
                "MassID: signature mismatch for %s at RVA 0x%llX.",
                label,
                static_cast<unsigned long long>(rva));
            Context->LogError(message);
        }
    };

    // This is a composable live entry that MassID calls but does not hook.
    // A plugin loaded earlier may legitimately own its prologue.
    if (!IsExecutableAddress(Base + QueueOutgoingPacketRva)) {
        valid = false;
        if (Context) {
            Context->LogError(
                "MassID: CLIENT_QueueOutgoingPacket is not executable.");
        }
    }
    check(TargetingPacketWorkerRva, TargetingPacketWorkerExpected,
        "CLIENT_ProcessTargetingPacket");
    check(IsVirtualKeyDownRva, IsVirtualKeyDownExpected,
        "INPUT_IsVirtualKeyDownAsync");
    check(LegacyDropAppenderCallRva, LegacyDropAppenderCallExpected,
        "UI_LegacyInventoryTooltipAppenderDrop call");
    check(ModernDropAppenderCallRva, ModernDropAppenderCallExpected,
        "UI_ModernInventoryTooltipAppenderDrop call");
    check(LegacyMoveAppenderCallRvas[0], LegacyMoveAppenderCallsExpected[0],
        "UI_LegacyInventoryTooltipAppenderMove call A");
    check(LegacyMoveAppenderCallRvas[1], LegacyMoveAppenderCallsExpected[1],
        "UI_LegacyInventoryTooltipAppenderMove call B");
    check(ModernMoveAppenderCallRvas[0], ModernMoveAppenderCallsExpected[0],
        "UI_ModernInventoryTooltipAppenderMove call A");
    check(ModernMoveAppenderCallRvas[1], ModernMoveAppenderCallsExpected[1],
        "UI_ModernInventoryTooltipAppenderMove call B");
    check(ModernMoveAppenderCallRvas[2], ModernMoveAppenderCallsExpected[2],
        "UI_ModernInventoryTooltipAppenderMove call C");
    check(AlternateMoveAppenderCallRva, AlternateMoveAppenderCallExpected,
        "UI_AlternateInventoryTooltipAppenderMove call");
    check(ModernSellAppenderCallRva, ModernSellAppenderCallExpected,
        "UI_ModernInventoryTooltipAppenderSell call");
    check(ModernGiveAppenderCallRva, ModernGiveAppenderCallExpected,
        "UI_ModernInventoryTooltipAppenderGive call");
    check(ModernUiStateProbeCallRva, ModernUiStateProbeCallExpected,
        "UI_ModernInventoryTooltip UI-state probe call");
    // RemoteStash legitimately owns this live entry. MassID calls the current
    // chain but patches only the independent tooltip callsite above.
    if (!IsExecutableAddress(Base + IsUiStateOpenRva)) {
        valid = false;
        if (Context) {
            Context->LogError("MassID: UI_IsStateOpen is not executable.");
        }
    }
    check(CainIdentifyCallbackRva, CainIdentifyCallbackExpected,
        "D2GAME_PACKETCALLBACK_Rcv0x34_IdentifyItemsWithNpc");
    check(IdentifyItemRva, IdentifyItemExpected,
        "D2GAME_ITEMS_Identify");
    check(SynchronizeQuantityRva, SynchronizeQuantityExpected,
        "SynchronizeItemAndBoundSkillQuantity");
    check(GetLocalDataContextRva, GetLocalDataContextExpected,
        "CLIENT_GetLocalDataContext");
    check(GetLocalPlayerRva, GetLocalPlayerExpected,
        "CLIENT_GetLocalPlayer");
    // plugin-skills legitimately owns this live entry when Bulk Skill Point
    // Allocation is enabled. MassID calls the current chain but never hooks it;
    // requiring the vanilla prologue would make coexistence load-order dependent.
    if (!IsExecutableAddress(Base + GetLocalizedStringByKeyRva)) {
        valid = false;
        if (Context) {
            Context->LogError(
                "MassID: LOCALIZATION_GetStringByKey is not executable.");
        }
    }
    check(GetUnitStatRva, GetUnitStatExpected, "STATLIST_GetUnitStat");
    check(CheckStateRva, CheckStateExpected, "STATES_CheckState");
    check(GetUnitIdRva, GetUnitIdExpected, "UNITS_GetUnitId");
    check(GetUnitInventoryRva, GetUnitInventoryExpected, "UNITS_GetInventory");
    check(GetItemDataRva, GetItemDataExpected, "UNITS_GetItemData");
    check(GetUnitTypeRva, GetUnitTypeExpected, "UNITS_GetUnitType");
    check(CheckItemFlagRva, CheckItemFlagExpected, "ITEMS_CheckItemFlag");
    check(SetItemFlagRva, SetItemFlagExpected, "ITEMS_SetItemFlag");
    check(GetItemSuffixIdRva, GetItemSuffixIdExpected,
        "ITEMS_GetSuffixId");
    check(GetItemCodeRva, GetItemCodeExpected, "ITEMS_GetItemCode");
    check(GetCursorItemRva, GetCursorItemExpected, "INVENTORY_GetCursorItem");
    check(GetFirstItemRva, GetFirstItemExpected, "INVENTORY_GetFirstItem");
    check(GetNextItemRva, GetNextItemExpected, "INVENTORY_GetNextItem");
    check(GetParentInventoryRva, GetParentInventoryExpected,
        "INVENTORY_GetParentInventory");
    check(GetInventoryOwnerIdRva, GetInventoryOwnerIdExpected,
        "INVENTORY_GetOwnerId");
    check(GetFirstCorpseRva, GetFirstCorpseExpected,
        "INVENTORY_GetFirstCorpse");
    check(GetNextCorpseRva, GetNextCorpseExpected,
        "INVENTORY_GetNextCorpse");
    check(GetCorpseUnitIdRva, GetCorpseUnitIdExpected,
        "INVENTORY_GetUnitGUIDFromCorpse");
    check(ServerUnitRva, ServerUnitExpected, "SUNIT_GetServerUnit");
    return valid;
}

bool CaptureMassIdRequest(void* item, const char* path) noexcept {
    GesturesObserved.fetch_add(1, std::memory_order_relaxed);
    void* localPlayer = GetLocalPlayer(GetLocalDataContext());
    void* inventory = localPlayer ? GetUnitInventory(localPlayer) : nullptr;
    const bool cursorEmpty = inventory && GetCursorItem(inventory) == nullptr;
    const auto unitType = item ? GetUnitType(item) : -1;
    const auto itemCode = item ? GetItemCode(item) : 0;
    if (item && ShouldCaptureGesture(
            Settings.enabled, true, true, cursorEmpty, unitType, itemCode)) {
        const auto tomeGuid = static_cast<std::uint32_t>(GetUnitId(item));
        const auto packet = MakeRequest(tomeGuid);
        QueueOutgoingPacket(
            packet.data(), static_cast<std::int32_t>(packet.size()));
        RequestsSent.fetch_add(1, std::memory_order_relaxed);
        if (Context) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "MassID: %s Shift-right-click captured; queued bytes=34 %08X %08X %08X 00000000 00000000; Tome GUID %u.",
                path,
                tomeGuid,
                RequestMarker,
                RequestGuard,
                tomeGuid);
            Context->LogInfo(message);
        }
        return true;
    }
    if (Context) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: %s Shift-right-click ignored; item=%p; cursorEmpty=%s; unitType=%d; itemCode=0x%08X.",
            path,
            item,
            cursorEmpty ? "true" : "false",
            unitType,
            itemCode);
        Context->LogInfo(message);
    }
    return false;
}

void* FindIdentifyTomeForPacket(const std::uint8_t* packet) noexcept {
    if (!packet) return nullptr;
    void* localPlayer = GetLocalPlayer(GetLocalDataContext());
    void* inventory = localPlayer ? GetUnitInventory(localPlayer) : nullptr;
    if (!inventory) return nullptr;
    const auto suffixId = static_cast<std::uint16_t>(
        packet[1] | (static_cast<std::uint16_t>(packet[2]) << 8));
    for (void* item = GetFirstItem(inventory); item; item = GetNextItem(item)) {
        if (GetUnitType(item) == 4
                && GetParentInventory(item) == inventory
                && GetItemCode(item) == IdentifyTomeCode
                && GetItemSuffixId(item) == suffixId
                && IsSupportedInventoryPage(GetInventoryPage(item))) {
            return item;
        }
    }
    return nullptr;
}

void* FindIdentifyTomeByGuid(std::uint32_t tomeGuid) noexcept {
    if (tomeGuid == 0) return nullptr;
    void* localPlayer = GetLocalPlayer(GetLocalDataContext());
    void* inventory = localPlayer ? GetUnitInventory(localPlayer) : nullptr;
    if (!inventory) return nullptr;
    for (void* item = GetFirstItem(inventory); item; item = GetNextItem(item)) {
        if (GetUnitType(item) == 4
                && GetParentInventory(item) == inventory
                && GetItemCode(item) == IdentifyTomeCode
                && static_cast<std::uint32_t>(GetUnitId(item)) == tomeGuid
                && IsSupportedInventoryPage(GetInventoryPage(item))) {
            return item;
        }
    }
    return nullptr;
}

void __fastcall HookTargetingPacketWorker(
        const std::uint8_t* packet) noexcept {
    const bool nativeShift = IsVirtualKeyDown(VK_SHIFT) != 0;
    const bool win32Shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    void* tome = FindIdentifyTomeForPacket(packet);
    TargetingWorkersObserved.fetch_add(1, std::memory_order_relaxed);
    if (Context) {
        char message[224]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: targeting worker observed; tome=%p; nativeShift=%s; win32Shift=%s.",
            tome,
            nativeShift ? "true" : "false",
            win32Shift ? "true" : "false");
        Context->LogInfo(message);
    }
    if (Settings.enabled && (nativeShift || win32Shift)
            && CaptureMassIdRequest(tome, "targeting worker")) {
        return;
    }
    OriginalTargetingPacketWorker(packet);
}

void SuppressWindowMessage(MSG* message) noexcept {
    if (!message) return;
    message->message = WM_NULL;
    message->wParam = 0;
    message->lParam = 0;
}

LRESULT CALLBACK MassIDMessageHook(
        int code, WPARAM removeMode, LPARAM parameter) noexcept {
    struct CallbackGuard {
        CallbackGuard() noexcept {
            ActiveMessageHookCallbacks.fetch_add(1, std::memory_order_acq_rel);
        }
        ~CallbackGuard() noexcept {
            ActiveMessageHookCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    } callbackGuard;
    auto* message = code >= 0 && removeMode == PM_REMOVE && parameter
        ? reinterpret_cast<MSG*>(parameter)
        : nullptr;
    if (message && PluginActive.load(std::memory_order_acquire)
            && Settings.enabled && message->message == WM_RBUTTONDOWN) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0
            || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        if (shift) {
            const auto now = GetTickCount64();
            const auto hoveredAt = HoveredIdentifyTomeTick.load(
                std::memory_order_acquire);
            const auto tomeGuid = HoveredIdentifyTomeGuid.load(
                std::memory_order_acquire);
            const auto age = now >= hoveredAt ? now - hoveredAt
                                               : UINT64_MAX;
            if (Context) {
                char logMessage[192]{};
                std::snprintf(
                    logMessage,
                    sizeof(logMessage),
                    "MassID: window Shift-right-button observed; hoveredGuid=%u; ageMs=%llu.",
                    tomeGuid,
                    static_cast<unsigned long long>(age));
                Context->LogInfo(logMessage);
            }
            if (tomeGuid != 0 && age <= 1500) {
                PendingMassIdGuid.store(tomeGuid, std::memory_order_release);
                PendingMassIdTick.store(now, std::memory_order_release);
                SuppressRightButtonUp.store(true, std::memory_order_release);
                SuppressWindowMessage(message);
            }
        }
    }
    if (message && message->message == WM_RBUTTONUP
            && SuppressRightButtonUp.exchange(
                false, std::memory_order_acq_rel)) {
        SuppressWindowMessage(message);
    }
    return CallNextHookEx(
        GameMessageHook.load(std::memory_order_acquire),
        code,
        removeMode,
        parameter);
}

BOOL CALLBACK FindGameWindowCallback(HWND window, LPARAM state) noexcept {
    DWORD processId{};
    GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId()
            || GetWindow(window, GW_OWNER) != nullptr
            || !IsWindowVisible(window)) {
        return TRUE;
    }
    *reinterpret_cast<HWND*>(state) = window;
    return FALSE;
}

bool TryInstallGameMessageHook() noexcept {
    if (GameMessageHook.load(std::memory_order_acquire)) return true;
    const std::lock_guard lock(GameMessageHookMutex);
    if (GameMessageHook.load(std::memory_order_relaxed)) return true;
    HWND window{};
    EnumWindows(FindGameWindowCallback, reinterpret_cast<LPARAM>(&window));
    if (!window) return false;
    const auto threadId = GetWindowThreadProcessId(window, nullptr);
    if (threadId == 0) return false;
    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&MassIDMessageHook),
            &module)) {
        return false;
    }
    const auto hook = SetWindowsHookExW(
        WH_GETMESSAGE,
        MassIDMessageHook,
        module,
        threadId);
    if (!hook) {
        FreeLibrary(module);
        return false;
    }
    GameMessageHookModule = module;
    GameMessageHook.store(hook, std::memory_order_release);
    if (Context) {
        char message[160]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: game UI message hook installed; thread=%lu.",
            static_cast<unsigned long>(threadId));
        Context->LogInfo(message);
    }
    return true;
}

bool IsOwnedIdentifyTome(
        void* player, void* inventory, void* tome) noexcept {
    return player
        && inventory
        && tome
        && GetUnitType(tome) == 4
        && GetParentInventory(tome) == inventory
        && GetItemCode(tome) == IdentifyTomeCode
        && IsSupportedInventoryPage(GetInventoryPage(tome));
}

std::int32_t IdentifyPage(
        void* game,
        void* inventoryActor,
        void* inventory,
        std::uint8_t page,
        std::int32_t budget) noexcept {
    std::int32_t identified{};
    for (void* item = GetFirstItem(inventory);
            item && identified < budget;) {
        void* next = GetNextItem(item);
        if (GetUnitType(item) == 4
                && GetParentInventory(item) == inventory
                && GetInventoryPage(item) == page
                && CheckItemFlag(item, IdentifiedItemFlag) == 0) {
            IdentifyItem(game, inventoryActor, item, 1);
            if (CheckItemFlag(item, IdentifiedItemFlag) != 0) {
                ++identified;
            }
        }
        item = next;
    }
    return identified;
}

struct SharedIdentifyResult {
    std::int32_t identified{};
    std::int32_t containers{};
};

SharedIdentifyResult IdentifySharedStashes(
        void* game,
        void* player,
        void* playerInventory,
        std::int32_t budget) noexcept {
    SharedIdentifyResult result{};
    if (!game || !player || !playerInventory || budget <= 0) return result;

    const auto playerGuid = GetUnitId(player);
    for (void* record = GetFirstCorpse(playerInventory);
            record && result.identified < budget;
            record = GetNextCorpse(record)) {
        const auto proxyGuid = GetCorpseUnitId(record);
        void* proxy = GetServerUnit(game, 0, proxyGuid);
        if (!proxy || proxy == player
                || CheckState(proxy, SharedStashProxyState) == 0) {
            continue;
        }

        void* proxyInventory = GetUnitInventory(proxy);
        if (!proxyInventory
                || GetInventoryOwnerId(proxyInventory) != playerGuid) {
            continue;
        }

        ++result.containers;
        // ITEMS_SendItemUpdate has a dedicated state-0xBA route for shared
        // stash proxy players.  The proxy must remain the identification actor;
        // using the main player makes the client deserialize the update into
        // personal inventory and leaves a frozen ghost until the next reload.
        result.identified += IdentifyPage(
            game,
            proxy,
            proxyInventory,
            StashPage,
            budget - result.identified);
    }
    return result;
}

std::int32_t __fastcall HookCainIdentifyCallback(
        void* game,
        void* player,
        const std::uint8_t* packet,
        std::int32_t packetSize) noexcept {
    if (Context) {
        const auto field = [packet, packetSize](std::size_t offset) noexcept {
            return packet && packetSize >= static_cast<std::int32_t>(offset + 4)
                ? ReadU32(packet, offset)
                : UINT32_MAX;
        };
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: server opcode-0x34 callback observed; size=%d; opcode=0x%02X; fields=%08X %08X %08X %08X %08X.",
            packetSize,
            packet && packetSize > 0 ? packet[0] : 0,
            field(1), field(5), field(9), field(13), field(17));
        Context->LogInfo(message);
    }
    if (!IsPrivateRequest(packet, packetSize)) {
        return OriginalCainIdentifyCallback(
            game, player, packet, packetSize);
    }

    void* inventory = player
        ? GetUnitInventory(player)
        : nullptr;
    const auto tomeGuid = static_cast<std::int32_t>(ReadU32(packet, 1));
    void* tome = game ? GetServerUnit(game, 4, tomeGuid) : nullptr;
    if (!IsOwnedIdentifyTome(player, inventory, tome)) {
        RequestsRejected.fetch_add(1, std::memory_order_relaxed);
        if (Context) {
            char message[160]{};
            std::snprintf(
                message,
                sizeof(message),
                "MassID: rejected request for invalid Tome GUID %u.",
                static_cast<std::uint32_t>(tomeGuid));
            Context->LogWarn(message);
        }
        return 0;
    }
    SetItemFlag(tome, 0x00000004u, 0);

    const auto quantity = GetUnitStat(
        tome, static_cast<std::int32_t>(QuantityStat), 0);
    const auto budget = IdentificationBudget(
        Settings.freeIdentification, quantity);

    std::int32_t inventoryIdentified{};
    std::int32_t cubeIdentified{};
    std::int32_t personalStashIdentified{};
    SharedIdentifyResult sharedStash{};
    if (budget > 0) {
        inventoryIdentified = IdentifyPage(
            game, player, inventory, InventoryPage, budget);
        if (IncludesTarget(Settings.targets, TargetContainer::Cube)
                && inventoryIdentified < budget) {
            cubeIdentified = IdentifyPage(
                game,
                player,
                inventory,
                CubePage,
                budget - inventoryIdentified);
        }
        if (IncludesTarget(Settings.targets, TargetContainer::PersonalStash)
                && inventoryIdentified + cubeIdentified < budget) {
            personalStashIdentified = IdentifyPage(
                game,
                player,
                inventory,
                StashPage,
                budget - inventoryIdentified - cubeIdentified);
        }
        const auto mainInventoryIdentified = inventoryIdentified
            + cubeIdentified
            + personalStashIdentified;
        if (IncludesTarget(Settings.targets, TargetContainer::SharedStash)
                && mainInventoryIdentified < budget) {
            sharedStash = IdentifySharedStashes(
                game,
                player,
                inventory,
                budget - mainInventoryIdentified);
        }
    }
    const auto identified = inventoryIdentified
        + cubeIdentified
        + personalStashIdentified
        + sharedStash.identified;

    if (!Settings.freeIdentification && identified > 0) {
        SynchronizeQuantity(game, player, tome, -identified);
        ChargesConsumed.fetch_add(
            static_cast<std::uint64_t>(identified),
            std::memory_order_relaxed);
    }
    RequestsAccepted.fetch_add(1, std::memory_order_relaxed);
    ItemsIdentified.fetch_add(
        static_cast<std::uint64_t>(identified),
        std::memory_order_relaxed);
    if (Context) {
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: accepted Tome GUID %u; quantity=%d; identified=%d (inventory=%d; cube=%d; personalStash=%d; sharedStash=%d; sharedContainers=%d); consumed=%d; freeIdentification=%s.",
            static_cast<std::uint32_t>(tomeGuid),
            quantity,
            identified,
            inventoryIdentified,
            cubeIdentified,
            personalStashIdentified,
            sharedStash.identified,
            sharedStash.containers,
            Settings.freeIdentification ? 0 : identified,
            Settings.freeIdentification ? "true" : "false");
        Context->LogInfo(message);
    }
    return 0;
}

std::string_view CurrentMassIdTooltipText() noexcept {
    constexpr std::string_view key = "ItemStats1h";
    if (!GetLocalizedStringByKey) return TooltipLocales.front().massIdText;
    const GameStringView view{key.data(), key.size()};
    const auto* defense = GetLocalizedStringByKey(&view);
    if (!defense || defense[0] == '\0') return TooltipLocales.front().massIdText;
    for (const auto& locale : TooltipLocales) {
        if (locale.defenseFingerprint == defense) return locale.massIdText;
    }
    return TooltipLocales.front().massIdText;
}

void ObserveHoveredItem(void* item, const char* path) noexcept {
    if (PluginActive.load(std::memory_order_acquire)) {
        TryInstallGameMessageHook();
    }
    if (!Settings.enabled || !item
            || GetUnitType(item) != 4
            || GetItemCode(item) != IdentifyTomeCode) {
        HoveredIdentifyTomeGuid.store(0, std::memory_order_release);
        HoveredIdentifyTomeTick.store(0, std::memory_order_release);
        PendingMassIdGuid.store(0, std::memory_order_release);
        PendingMassIdTick.store(0, std::memory_order_release);
        SuppressRightButtonUp.store(false, std::memory_order_release);
        return;
    }

    const auto itemGuid = static_cast<std::uint32_t>(GetUnitId(item));
    HoveredIdentifyTomeGuid.store(itemGuid, std::memory_order_release);
    HoveredIdentifyTomeTick.store(GetTickCount64(), std::memory_order_release);
    const auto pendingGuid = PendingMassIdGuid.exchange(
        0, std::memory_order_acq_rel);
    const auto pendingAt = PendingMassIdTick.exchange(
        0, std::memory_order_acq_rel);
    if (pendingGuid == 0) return;
    const auto now = GetTickCount64();
    const auto pendingAge = now >= pendingAt ? now - pendingAt : UINT64_MAX;
    if (pendingGuid == itemGuid && pendingAt != 0 && pendingAge <= 1500) {
        CaptureMassIdRequest(item, path);
    } else if (Context) {
        char message[192]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: deferred request discarded; pendingGuid=%u; renderedGuid=%u; ageMs=%llu; path=%s.",
            pendingGuid,
            itemGuid,
            static_cast<unsigned long long>(pendingAge),
            path);
        Context->LogWarn(message);
    }
}

const char* __fastcall HookTooltipAppender(
        const GameStringView* key, void* item) noexcept {
    const auto* original = GetLocalizedStringByKey(key);
    ObserveHoveredItem(item, "deferred tooltip-appender input");
    if (!Settings.enabled || !original || !item
            || GetUnitType(item) != 4
            || GetItemCode(item) != IdentifyTomeCode) {
        return original;
    }
    try {
        thread_local std::string enhanced;
        enhanced = AddMassIdTooltipLine(
            original, CurrentMassIdTooltipText());
        return enhanced.c_str();
    } catch (...) {
        if (Context) {
            Context->LogError(
                "MassID: Identify Tome tooltip-appender transform failed safely.");
        }
        return original;
    }
}

std::int32_t __fastcall HookUiContextProbe(
        std::int32_t state, void* item) noexcept {
    const auto result = IsUiStateOpen(state);
    UiContextProbesObserved.fetch_add(1, std::memory_order_relaxed);
    ObserveHoveredItem(item, "deferred UI-state input");
    return result;
}

void* AllocateNear(void* hint, std::size_t size) noexcept {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity);
    const auto base = reinterpret_cast<std::uintptr_t>(hint)
        & ~(granularity - 1);
    for (std::uintptr_t delta = granularity;
            delta < 0x70000000ULL; delta += granularity) {
        if (base > std::numeric_limits<std::uintptr_t>::max() - delta) break;
        if (auto* memory = VirtualAlloc(
                reinterpret_cast<void*>(base + delta), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

bool WriteTooltipRelay(
        std::uint8_t* destination,
        std::span<const std::uint8_t> itemMove) noexcept {
    if (!destination || itemMove.empty()) return false;
    std::memcpy(destination, itemMove.data(), itemMove.size());
    auto* jump = destination + itemMove.size();
    jump[0] = 0xFF;
    jump[1] = 0x25;
    jump[2] = jump[3] = jump[4] = jump[5] = 0x00;
    const auto target = reinterpret_cast<std::uint64_t>(
        &HookTooltipAppender);
    std::memcpy(jump + 6, &target, sizeof(target));
    return true;
}

bool WriteUiContextProbeRelay(std::uint8_t* destination) noexcept {
    if (!destination) return false;
    constexpr std::array<std::uint8_t, 3> ItemMove{
        0x4C, 0x89, 0xE2, // mov rdx, r12
    };
    std::memcpy(destination, ItemMove.data(), ItemMove.size());
    auto* jump = destination + ItemMove.size();
    jump[0] = 0xFF;
    jump[1] = 0x25;
    jump[2] = jump[3] = jump[4] = jump[5] = 0x00;
    const auto target = reinterpret_cast<std::uint64_t>(&HookUiContextProbe);
    std::memcpy(jump + 6, &target, sizeof(target));
    return true;
}

bool InstallTooltipCallSites() noexcept {
    constexpr std::size_t RelayStride = 32;
    constexpr std::size_t RelayBytes = RelayStride * 4;
    constexpr std::array<std::uint8_t, 3> LegacyItemMove{
        0x4C, 0x89, 0xEA, // mov rdx, r13
    };
    constexpr std::array<std::uint8_t, 4> ModernItemMove{
        0x48, 0x8B, 0x55, 0x88, // mov rdx, [rbp-0x78]
    };
    constexpr std::array<std::uint8_t, 3> ModernMoveItemMove{
        0x4C, 0x89, 0xE2, // mov rdx, r12
    };

    TooltipRelayPage = AllocateNear(
        Base + LegacyDropAppenderCallRva, RelayBytes);
    if (!TooltipRelayPage) return false;
    auto* relays = static_cast<std::uint8_t*>(TooltipRelayPage);
    if (!WriteTooltipRelay(relays, LegacyItemMove)
            || !WriteTooltipRelay(relays + RelayStride, ModernItemMove)
            || !WriteTooltipRelay(
                relays + RelayStride * 2, ModernMoveItemMove)
            || !WriteUiContextProbeRelay(relays + RelayStride * 3)) {
        return false;
    }
    DWORD previousProtection{};
    if (!VirtualProtect(
            relays, RelayBytes, PAGE_EXECUTE_READ, &previousProtection)) {
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relays, RelayBytes);

    // The shared guarded call-site relay delegates to the original target while
    // the transaction is inactive. This keeps every residual call safe even if
    // Windows refuses an exceptional rollback write during shutdown.
    bool installed = PSh_ManifestPatchCallSite(
            Context,
            PSH_MANIFEST_SITE("items.massIdentify.legacyDropAppender"),
            LegacyDropAppenderCallRva,
            LegacyDropAppenderCallExpected.data(),
            static_cast<std::uint32_t>(LegacyDropAppenderCallExpected.size()),
            relays)
        && PSh_ManifestPatchCallSite(
            Context,
            PSH_MANIFEST_SITE("items.massIdentify.modernDropAppender"),
            ModernDropAppenderCallRva,
            ModernDropAppenderCallExpected.data(),
            static_cast<std::uint32_t>(ModernDropAppenderCallExpected.size()),
            relays + RelayStride);
    for (std::size_t index = 0;
            installed && index < LegacyMoveAppenderCallRvas.size(); ++index) {
        installed = PSh_ManifestPatchCallSite(
            Context,
            LegacyMoveAppenderManifestIds[index],
            LegacyMoveAppenderCallRvas[index],
            LegacyMoveAppenderCallsExpected[index].data(),
            static_cast<std::uint32_t>(
                LegacyMoveAppenderCallsExpected[index].size()),
            relays);
    }
    for (std::size_t index = 0;
            installed && index < ModernMoveAppenderCallRvas.size(); ++index) {
        installed = PSh_ManifestPatchCallSite(
            Context,
            ModernMoveAppenderManifestIds[index],
            ModernMoveAppenderCallRvas[index],
            ModernMoveAppenderCallsExpected[index].data(),
            static_cast<std::uint32_t>(
                ModernMoveAppenderCallsExpected[index].size()),
            relays + RelayStride * 2);
    }
    installed = installed && PSh_ManifestPatchCallSite(
        Context,
        PSH_MANIFEST_SITE("items.massIdentify.modernSellAppender"),
        ModernSellAppenderCallRva,
        ModernSellAppenderCallExpected.data(),
        static_cast<std::uint32_t>(ModernSellAppenderCallExpected.size()),
        relays + RelayStride * 2);
    installed = installed && PSh_ManifestPatchCallSite(
        Context,
        PSH_MANIFEST_SITE("items.massIdentify.modernGiveAppender"),
        ModernGiveAppenderCallRva,
        ModernGiveAppenderCallExpected.data(),
        static_cast<std::uint32_t>(ModernGiveAppenderCallExpected.size()),
        relays + RelayStride * 2);
    installed = installed && PSh_ManifestPatchCallSite(
        Context,
        PSH_MANIFEST_SITE("items.massIdentify.alternateMoveAppender"),
        AlternateMoveAppenderCallRva,
        AlternateMoveAppenderCallExpected.data(),
        static_cast<std::uint32_t>(
            AlternateMoveAppenderCallExpected.size()),
        relays);
    return installed && PSh_ManifestPatchCallSite(
        Context,
        PSH_MANIFEST_SITE("items.massIdentify.modernUiStateProbe"),
        ModernUiStateProbeCallRva,
        ModernUiStateProbeCallExpected.data(),
        static_cast<std::uint32_t>(
            ModernUiStateProbeCallExpected.size()),
        relays + RelayStride * 3);
}

bool InstallHooks() noexcept {
    if (!PSh_ManifestInstallInlineHook(
            Context,
            PSH_MANIFEST_SITE("items.massIdentify.targetingPacketWorker"),
            TargetingPacketWorkerRva,
            TargetingPacketWorkerExpected.data(),
            static_cast<std::uint32_t>(
                TargetingPacketWorkerExpected.size()),
            HookTargetingPacketWorker,
            &OriginalTargetingPacketWorker)) {
        Context->LogError("MassID: targeting packet worker hook refused.");
        return false;
    }
    if (!PSh_ManifestInstallInlineHook(
            Context,
            PSH_MANIFEST_SITE("items.massIdentify.cainIdentifyCallback"),
            CainIdentifyCallbackRva,
            CainIdentifyCallbackExpected.data(),
            static_cast<std::uint32_t>(CainIdentifyCallbackExpected.size()),
            HookCainIdentifyCallback,
            &OriginalCainIdentifyCallback)) {
        Context->LogError("MassID: Cain identify callback hook refused.");
        return false;
    }
    TooltipCallSitesInstalled = InstallTooltipCallSites();
    if (!TooltipCallSitesInstalled) {
        Context->LogError(
            "MassID: tooltip appender call-sites are unavailable.");
        return false;
    }
    return true;
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char message[640]{};
    std::snprintf(
        message,
        sizeof(message),
        "MassID 1.1.1 (plugin-items): enabled=%s; freeIdentification=%s; includeCube=%s; includePersonalStash=%s; includeSharedStash=%s; windowInput=%s; pendingGuid=%u; uiContextProbes=%llu; targetingWorkers=%llu; gestures=%llu; sent=%llu; accepted=%llu; rejected=%llu; identified=%llu; consumed=%llu; tooltip=%s; config=%s.",
        Settings.enabled ? "true" : "false",
        Settings.freeIdentification ? "true" : "false",
        Settings.targets.includeCube ? "true" : "false",
        Settings.targets.includePersonalStash ? "true" : "false",
        Settings.targets.includeSharedStash ? "true" : "false",
        GameMessageHook.load(std::memory_order_acquire)
            ? "installed" : "pending",
        PendingMassIdGuid.load(std::memory_order_acquire),
        static_cast<unsigned long long>(UiContextProbesObserved.load()),
        static_cast<unsigned long long>(TargetingWorkersObserved.load()),
        static_cast<unsigned long long>(GesturesObserved.load()),
        static_cast<unsigned long long>(RequestsSent.load()),
        static_cast<unsigned long long>(RequestsAccepted.load()),
        static_cast<unsigned long long>(RequestsRejected.load()),
        static_cast<unsigned long long>(ItemsIdentified.load()),
        static_cast<unsigned long long>(ChargesConsumed.load()),
        TooltipCallSitesInstalled
            ? "drop-move-sell-give-and-ui-state-probe"
            : "unavailable",
        ConfigPath);
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}
} // namespace

bool RuffnecKk::MassIdentify::Load(
        const D2RL::PluginContext* context,
        const nlohmann::json& itemsConfig) noexcept {
    if (!context) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);

    if (!LoadConfig(itemsConfig)) {
        return false;
    }
    if (!PSh_RegisterConsoleCommand(
            context,
            "mass-id",
            Status,
            "Show MassID configuration and counters.")) {
        context->LogWarn(
            "plugin-items: MassID status command was not registered.");
    }
    if (!Settings.enabled) {
        context->LogInfo(
            "plugin-items: MassID 1.1.1 by RuffnecKk disabled; no hooks or input capture installed; config=items.massIdentify.");
        return true;
    }
    if (context->modDataVersionBuild != 0
            && context->modDataVersionBuild != SupportedBuild) {
        context->LogError(
            "plugin-items: MassID supports only D2R build 92777.");
        return false;
    }
    if (!ValidateRuntime()) {
        context->LogError(
            "plugin-items: MassID runtime signature mismatch; feature refused.");
        return false;
    }

    QueueOutgoingPacket = At<QueueOutgoingPacketFn>(QueueOutgoingPacketRva);
    GetLocalDataContext = At<GetLocalDataContextFn>(GetLocalDataContextRva);
    GetLocalPlayer = At<GetLocalPlayerFn>(GetLocalPlayerRva);
    IsVirtualKeyDown = At<IsVirtualKeyDownFn>(IsVirtualKeyDownRva);
    IsUiStateOpen = At<IsUiStateOpenFn>(IsUiStateOpenRva);
    GetLocalizedStringByKey = At<GetLocalizedStringByKeyFn>(
        GetLocalizedStringByKeyRva);
    GetUnitInventory = At<GetUnitInventoryFn>(GetUnitInventoryRva);
    GetItemData = At<GetItemDataFn>(GetItemDataRva);
    GetCursorItem = At<GetCursorItemFn>(GetCursorItemRva);
    GetParentInventory = At<GetParentInventoryFn>(GetParentInventoryRva);
    GetUnitType = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetItemCode = At<GetItemCodeFn>(GetItemCodeRva);
    GetUnitId = At<GetUnitIdFn>(GetUnitIdRva);
    GetServerUnit = At<GetServerUnitFn>(ServerUnitRva);
    GetFirstItem = At<GetFirstItemFn>(GetFirstItemRva);
    GetNextItem = At<GetNextItemFn>(GetNextItemRva);
    GetInventoryOwnerId = At<GetInventoryOwnerIdFn>(GetInventoryOwnerIdRva);
    GetFirstCorpse = At<GetFirstCorpseFn>(GetFirstCorpseRva);
    GetNextCorpse = At<GetNextCorpseFn>(GetNextCorpseRva);
    GetCorpseUnitId = At<GetCorpseUnitIdFn>(GetCorpseUnitIdRva);
    CheckItemFlag = At<CheckItemFlagFn>(CheckItemFlagRva);
    SetItemFlag = At<SetItemFlagFn>(SetItemFlagRva);
    GetItemSuffixId = At<GetItemSuffixIdFn>(GetItemSuffixIdRva);
    GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
    CheckState = At<CheckStateFn>(CheckStateRva);
    IdentifyItem = At<IdentifyItemFn>(IdentifyItemRva);
    SynchronizeQuantity = At<SynchronizeQuantityFn>(SynchronizeQuantityRva);

    if (!InstallHooks()) return false;

    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "plugin-items: MassID 1.1.1 by RuffnecKk prepared; target containers=inventory(always),cube=%s,personal-stash=%s,shared-stash=%s; freeIdentification=%s; tooltip=%s; config=%s.",
        Settings.targets.includeCube ? "enabled" : "disabled",
        Settings.targets.includePersonalStash ? "enabled" : "disabled",
        Settings.targets.includeSharedStash ? "enabled" : "disabled",
        Settings.freeIdentification ? "true" : "false",
        TooltipCallSitesInstalled
            ? "drop-move-sell-give-and-ui-state-probe"
            : "unavailable",
        ConfigPath);
    context->LogInfo(message);
    return true;
}

void RuffnecKk::MassIdentify::Activate() noexcept {
    if (!Context || !Settings.enabled) return;
    PluginActive.store(true, std::memory_order_release);
    const bool windowInputInstalled = TryInstallGameMessageHook();
    if (!windowInputInstalled) {
        Context->LogWarn(
            "plugin-items: MassID game UI input capture is pending until an Identify Tome tooltip is rendered.");
    }
    Context->LogInfo(
        windowInputInstalled
            ? "plugin-items: MassID 1.1.1 active; Shift-right-click input capture installed; config=items.massIdentify."
            : "plugin-items: MassID 1.1.1 active; Shift-right-click input capture pending; config=items.massIdentify.");
}

void RuffnecKk::MassIdentify::Unload() noexcept {
    PluginActive.store(false, std::memory_order_release);
    {
        const std::lock_guard lock(GameMessageHookMutex);
        if (const auto hook = GameMessageHook.load(std::memory_order_acquire)) {
            if (UnhookWindowsHookEx(hook)) {
                GameMessageHook.store(nullptr, std::memory_order_release);
                while (ActiveMessageHookCallbacks.load(
                        std::memory_order_acquire) != 0) {
                    SwitchToThread();
                }
                if (GameMessageHookModule) {
                    const auto module = GameMessageHookModule;
                    GameMessageHookModule = nullptr;
                    FreeLibrary(module);
                }
            } else if (Context) {
                Context->LogError(
                    "plugin-items: MassID could not remove its UI message hook; the DLL module remains retained as an inactive safety barrier.");
            }
        }
    }
    HoveredIdentifyTomeGuid.store(0, std::memory_order_release);
    HoveredIdentifyTomeTick.store(0, std::memory_order_release);
    PendingMassIdGuid.store(0, std::memory_order_release);
    PendingMassIdTick.store(0, std::memory_order_release);
    SuppressRightButtonUp.store(false, std::memory_order_release);
    TooltipCallSitesInstalled = false;
    QueueOutgoingPacket = nullptr;
    OriginalTargetingPacketWorker = nullptr;
    OriginalCainIdentifyCallback = nullptr;
    GetLocalDataContext = nullptr;
    GetLocalPlayer = nullptr;
    IsVirtualKeyDown = nullptr;
    IsUiStateOpen = nullptr;
    GetLocalizedStringByKey = nullptr;
    GetUnitInventory = nullptr;
    GetCursorItem = nullptr;
    GetParentInventory = nullptr;
    GetUnitType = nullptr;
    GetItemData = nullptr;
    GetItemCode = nullptr;
    GetUnitId = nullptr;
    GetServerUnit = nullptr;
    GetFirstItem = nullptr;
    GetNextItem = nullptr;
    GetInventoryOwnerId = nullptr;
    GetFirstCorpse = nullptr;
    GetNextCorpse = nullptr;
    GetCorpseUnitId = nullptr;
    CheckItemFlag = nullptr;
    SetItemFlag = nullptr;
    GetItemSuffixId = nullptr;
    GetUnitStat = nullptr;
    CheckState = nullptr;
    IdentifyItem = nullptr;
    SynchronizeQuantity = nullptr;
    Context = nullptr;
    Base = nullptr;
}
