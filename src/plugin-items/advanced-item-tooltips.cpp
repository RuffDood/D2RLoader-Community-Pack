#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <D2RLPlugin/api.h>
#include <plugin-shared.h>
#include "advanced-item-tooltips.h"
#include "advanced-item-tooltips-policy.h"
#include "embedded_vanilla_resource_ids.h"
#include "socket_tooltip.hpp"
#include "tooltip_ranges.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

extern "C" __declspec(dllexport) std::size_t __cdecl AdvancedItemTooltipsEnhanceTooltip(
    void* item,
    const char* tooltip,
    std::size_t tooltipLength,
    char* output,
    std::size_t outputCapacity
) noexcept;
extern "C" __declspec(dllexport) std::size_t __cdecl AdvancedItemTooltipsBuildSocketLine(
    void* item,
    char* output,
    std::size_t outputCapacity
) noexcept;
extern "C" __declspec(dllexport) std::size_t __cdecl AdvancedItemTooltipsFindSocketLineInsertion(
    const char* tooltip,
    std::size_t tooltipLength
) noexcept;
std::size_t EnhanceTooltipForVisibility(
    void* item,
    const char* tooltip,
    std::size_t tooltipLength,
    char* output,
    std::size_t outputCapacity,
    bool rangesVisible
) noexcept;

namespace {
constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t BuildItemTooltipRva = 0x2BD480;
constexpr std::uintptr_t EnsureStringCapacityRva = 0x076210;
constexpr std::uintptr_t GetMaxSocketsRva = 0x36EAD0;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t GetItemDataContextRva = 0x34A0E0;
constexpr std::uintptr_t GetItemDataRva = 0x34A500;
constexpr std::uintptr_t GetInventoryRva = 0x34A360;
constexpr std::uintptr_t GetFirstInventoryItemRva = 0x388C10;
constexpr std::uintptr_t GetNextInventoryItemRva = 0x38ABA0;
constexpr std::uintptr_t GetItemsTxtRecordRva = 0x314110;
constexpr std::uintptr_t GetRunesTxtRecordFromItemRva = 0x372260;
constexpr std::uintptr_t GetLocalizedStringRva = 0x5F4A50;
constexpr std::uintptr_t GetLocalizedStringByKeyRva = 0x5F4B90;
constexpr std::int32_t ArmorClassStat = 31;
constexpr std::int32_t SocketCountStat = 0xC2;

constexpr std::size_t UnitClassIdOffset = 0x04;
constexpr std::size_t ItemDataQualityOffset = 0x00;
constexpr std::size_t ItemDataFlagsOffset = 0x18;
constexpr std::size_t ItemDataFileIndexOffset = 0x34;
constexpr std::size_t ItemDataRarePrefixOffset = 0x42;
constexpr std::size_t ItemDataRareSuffixOffset = 0x44;
constexpr std::size_t ItemDataAutoPrefixOffset = 0x46;
constexpr std::size_t ItemDataMagicPrefixOffset = 0x48;
constexpr std::size_t ItemDataMagicSuffixOffset = 0x4E;
constexpr std::size_t ItemsTxtCodeOffset = 0x80;
constexpr std::size_t RunesTxtStringIdOffset = 0x46;
constexpr std::uint32_t ItemFlagIdentified = 0x00000010;
constexpr std::uint32_t ItemFlagEthereal = 0x00400000;
constexpr std::uint32_t ItemFlagRuneword = 0x04000000;

struct GameStringView {
    const char* data{};
    std::size_t size{};
};

using GetMaxSocketsFn = std::uint8_t(__fastcall*)(void*) noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(void*, std::int32_t, std::uint16_t) noexcept;
using GetItemDataContextFn = std::uint8_t(__fastcall*)(void*) noexcept;
using GetItemDataFn = std::uint8_t*(__fastcall*)(void*) noexcept;
using GetInventoryFn = void*(__fastcall*)(void*) noexcept;
using GetInventoryItemFn = void*(__fastcall*)(void*) noexcept;
using GetItemsTxtRecordFn = std::uint8_t*(__fastcall*)(std::uint8_t, std::int32_t) noexcept;
using GetRunesTxtRecordFromItemFn = std::uint8_t*(__fastcall*)(void*) noexcept;
using GetLocalizedStringFn = const char*(__fastcall*)(std::uint16_t) noexcept;
using GetLocalizedStringByKeyFn = const char*(__fastcall*)(const GameStringView*) noexcept;
using BuildItemTooltipFn = void*(__fastcall*)(
    void*, void*, void*, void*, std::uint64_t, std::uint64_t, std::uint64_t,
    std::uint64_t, std::uint64_t) noexcept;
using EnsureStringCapacityFn = void(__fastcall*)(void*, std::size_t) noexcept;

struct TooltipCallSite {
    const char* manifestId{};
    std::uintptr_t rva{};
    std::array<std::uint8_t, 5> expected{};
};

constexpr std::array TooltipCallSites{
    TooltipCallSite{PSH_MANIFEST_SITE("items.advancedTooltips.buildItemTooltipCall2291DC"), 0x2291DC, {0xE8,0x9F,0x42,0x09,0x00}},
    TooltipCallSite{PSH_MANIFEST_SITE("items.advancedTooltips.buildItemTooltipCall2BCEE9"), 0x2BCEE9, {0xE8,0x92,0x05,0x00,0x00}},
    TooltipCallSite{PSH_MANIFEST_SITE("items.advancedTooltips.buildItemTooltipCall2C8C02"), 0x2C8C02, {0xE8,0x79,0x48,0xFF,0xFF}},
    TooltipCallSite{PSH_MANIFEST_SITE("items.advancedTooltips.buildItemTooltipCall2CB32E"), 0x2CB32E, {0xE8,0x4D,0x21,0xFF,0xFF}},
    TooltipCallSite{PSH_MANIFEST_SITE("items.advancedTooltips.buildItemTooltipCall2CE716"), 0x2CE716, {0xE8,0x65,0xED,0xFE,0xFF}},
    TooltipCallSite{PSH_MANIFEST_SITE("items.advancedTooltips.buildItemTooltipCall87E882"), 0x87E882, {0xE8,0xF9,0xEB,0xA3,0xFF}},
    TooltipCallSite{PSH_MANIFEST_SITE("items.advancedTooltips.buildItemTooltipCall150C377"), 0x150C377, {0xE8,0x04,0x11,0xDB,0xFE}},
};

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
GetMaxSocketsFn GetMaxSockets{};
GetUnitStatFn GetUnitStat{};
GetItemDataContextFn GetItemDataContext{};
GetItemDataFn GetItemData{};
GetInventoryFn GetInventory{};
GetInventoryItemFn GetFirstInventoryItem{};
GetInventoryItemFn GetNextInventoryItem{};
GetItemsTxtRecordFn GetItemsTxtRecord{};
GetRunesTxtRecordFromItemFn GetRunesTxtRecordFromItem{};
GetLocalizedStringFn GetLocalizedString{};
GetLocalizedStringByKeyFn GetLocalizedStringByKey{};
BuildItemTooltipFn BuildItemTooltip{};
EnsureStringCapacityFn EnsureStringCapacity{};
void* TooltipRelay{};
tcp::tooltips::RangeCatalog Catalog;
bool CatalogLoaded{};
std::string CatalogSource{"unavailable"};
std::mutex RunewordNamesMutex;

bool RunewordNamesBuilt{};
std::unordered_map<std::string, std::string> RunewordKeyByLocalizedName;
std::mutex LocalizationMutex;
bool LocalizationBuilt{};
tcp::tooltips::TooltipLocalization Localization;
RuffnecKk::AdvancedTooltips::Config Settings{};
std::string LoadedConfigPath{"items.advancedTooltips"};

struct CachedAffixState {
    std::uintptr_t identity{};
    std::int32_t classId{};
    std::uint32_t quality{};
    std::uint32_t flags{};
    std::uint32_t fileIndex{};
    std::uint16_t rarePrefix{};
    std::uint16_t rareSuffix{};
    std::uint16_t autoPrefix{};
    std::array<std::uint16_t, 3> magicPrefix{};
    std::array<std::uint16_t, 3> magicSuffix{};

    bool operator==(const CachedAffixState&) const = default;
};

struct TooltipCacheState {
    CachedAffixState item;
    bool rangesVisible{};
    std::int32_t socketCount{};
    std::int32_t defense{};
    std::uint8_t maximumSockets{};
    std::array<CachedAffixState, 6> socketFillers{};
    std::size_t socketFillerCount{};

    bool operator==(const TooltipCacheState&) const = default;
};

struct TooltipCacheEntry {
    bool occupied{};
    TooltipCacheState state;
    std::string original;
    std::string enhanced;
};

// Large stashes and comparison panels routinely cycle through more than eight
// items. Keep enough exact transformations to avoid rebuilding table-backed
// histories whenever the cursor returns to a recently inspected item.
constexpr std::size_t TooltipCacheCapacity = 64;
constexpr std::size_t TooltipScratchCapacity = 64 * 1024;
thread_local std::array<TooltipCacheEntry, TooltipCacheCapacity> TooltipCache;
thread_local std::size_t TooltipCacheNext{};
thread_local std::array<char, TooltipScratchCapacity> TooltipScratch;

bool LoadEmbeddedVanillaTable(
    std::string_view tableName,
    std::string& text,
    std::string& error
) {
    constexpr std::array resources{
        std::pair{"itemstatcost.txt", IDR_VANILLA_ITEMSTATCOST},
        std::pair{"properties.txt", IDR_VANILLA_PROPERTIES},

        std::pair{"magicsuffix.txt", IDR_VANILLA_MAGICSUFFIX},
        std::pair{"magicprefix.txt", IDR_VANILLA_MAGICPREFIX},
        std::pair{"automagic.txt", IDR_VANILLA_AUTOMAGIC},
        std::pair{"qualityitems.txt", IDR_VANILLA_QUALITYITEMS},
        std::pair{"uniqueitems.txt", IDR_VANILLA_UNIQUEITEMS},
        std::pair{"setitems.txt", IDR_VANILLA_SETITEMS},
        std::pair{"armor.txt", IDR_VANILLA_ARMOR},
        std::pair{"itemtypes.txt", IDR_VANILLA_ITEMTYPES},
        std::pair{"weapons.txt", IDR_VANILLA_WEAPONS},
        std::pair{"misc.txt", IDR_VANILLA_MISC},
        std::pair{"gems.txt", IDR_VANILLA_GEMS},
        std::pair{"runes.txt", IDR_VANILLA_RUNES},
        std::pair{"cubemain.txt", IDR_VANILLA_CUBEMAIN},
    };
    int resourceId{};
    for (const auto& [name, id] : resources) {
        if (tableName == name) {
            resourceId = id;
            break;
        }
    }
    if (resourceId == 0) {
        error = "Embedded vanilla table is not registered: " + std::string(tableName);
        return false;
    }

    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&LoadEmbeddedVanillaTable), &module)) {
        error = "Cannot resolve the AdvancedItemTooltips module handle";
        return false;
    }
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));
    if (!resource) {
        error = "Embedded vanilla table resource is missing: " + std::string(tableName);
        return false;
    }
    const auto size = SizeofResource(module, resource);
    const auto loaded = LoadResource(module, resource);
    const auto* bytes = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    if (!bytes || size == 0) {
        error = "Embedded vanilla table resource is empty: " + std::string(tableName);
        return false;
    }
    text.assign(bytes, bytes + size);
    return true;
}

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

template<class T>
T Read(const std::uint8_t* address, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, address + offset, sizeof(value));
    return value;
}

bool IsReadable(const void* address, std::size_t size) noexcept {
    if (!address || size == 0) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory)
        || memory.State != MEM_COMMIT
        || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return begin <= regionEnd && size <= regionEnd - begin;
}

bool IsExecutableAddress(const void* address) noexcept {
    if (!address) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory)
        || memory.State != MEM_COMMIT
        || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    const auto protection = memory.Protect & 0xFF;
    return protection == PAGE_EXECUTE
        || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
}

tcp::tooltips::ItemAffixIds ReadAffixIds(const std::uint8_t* data) noexcept {
    tcp::tooltips::ItemAffixIds ids{};
    ids.quality = Read<std::uint32_t>(data, ItemDataQualityOffset);
    ids.fileIndex = Read<std::uint32_t>(data, ItemDataFileIndexOffset);
    const auto flags = Read<std::uint32_t>(data, ItemDataFlagsOffset);
    ids.runeword = (flags & ItemFlagRuneword) != 0;
    ids.ethereal = (flags & ItemFlagEthereal) != 0;
    ids.rarePrefix = Read<std::uint16_t>(data, ItemDataRarePrefixOffset);
    ids.rareSuffix = Read<std::uint16_t>(data, ItemDataRareSuffixOffset);
    ids.autoPrefix = Read<std::uint16_t>(data, ItemDataAutoPrefixOffset);
    for (std::size_t index = 0; index < 3; ++index) {
        ids.magicPrefix[index] = Read<std::uint16_t>(data, ItemDataMagicPrefixOffset + index * 2);
        ids.magicSuffix[index] = Read<std::uint16_t>(data, ItemDataMagicSuffixOffset + index * 2);
    }
    return ids;
}

std::string ItemCode(void* item) noexcept {
    if (!GetItemsTxtRecord || !GetItemDataContext) return {};
    const auto classId = Read<std::int32_t>(static_cast<const std::uint8_t*>(item), UnitClassIdOffset);

    const auto* record = GetItemsTxtRecord(GetItemDataContext(item), classId);
    if (!record) return {};
    char code[5]{};
    std::memcpy(code, record + ItemsTxtCodeOffset, 4);
    std::string result(code);
    while (!result.empty() && (result.back() == ' ' || result.back() == '\0')) result.pop_back();
    return result;
}

bool CaptureAffixState(void* item, CachedAffixState& state) noexcept {
    if (!item || !GetItemData) return false;
    const auto* data = GetItemData(item);
    if (!data) return false;
    state.identity = reinterpret_cast<std::uintptr_t>(item);
    state.classId = Read<std::int32_t>(static_cast<const std::uint8_t*>(item), UnitClassIdOffset);
    state.quality = Read<std::uint32_t>(data, ItemDataQualityOffset);
    state.flags = Read<std::uint32_t>(data, ItemDataFlagsOffset);
    state.fileIndex = Read<std::uint32_t>(data, ItemDataFileIndexOffset);
    state.rarePrefix = Read<std::uint16_t>(data, ItemDataRarePrefixOffset);
    state.rareSuffix = Read<std::uint16_t>(data, ItemDataRareSuffixOffset);
    state.autoPrefix = Read<std::uint16_t>(data, ItemDataAutoPrefixOffset);
    for (std::size_t index = 0; index < 3; ++index) {
        state.magicPrefix[index] = Read<std::uint16_t>(
            data, ItemDataMagicPrefixOffset + index * 2);
        state.magicSuffix[index] = Read<std::uint16_t>(
            data, ItemDataMagicSuffixOffset + index * 2);
    }
    return true;
}

bool RangesVisibleNow() noexcept {
    if (Settings.rangeDisplayMode
        == RuffnecKk::AdvancedTooltips::RangeDisplayMode::Always) {
        return true;
    }
    const auto hotkeyDown = (GetAsyncKeyState(
        static_cast<int>(Settings.holdToDisplayHotkey.virtualKey)) & 0x8000) != 0;
    return RuffnecKk::AdvancedTooltips::ShouldDisplayRanges(
        Settings.rangeDisplayMode, hotkeyDown);
}

bool CaptureTooltipCacheState(
    void* item, TooltipCacheState& state, bool rangesVisible) noexcept {
    if (!CaptureAffixState(item, state.item)) return false;
    state.rangesVisible = rangesVisible;
    if (GetUnitStat) {
        state.socketCount = GetUnitStat(item, SocketCountStat, 0);
        state.defense = GetUnitStat(item, ArmorClassStat, 0);
    }
    if (GetMaxSockets) state.maximumSockets = GetMaxSockets(item);

    if (!rangesVisible
        || !Settings.includeSocketedContributionsInRanges
        || (state.item.flags & ItemFlagRuneword) != 0
        || !GetInventory || !GetFirstInventoryItem || !GetNextInventoryItem) return true;
    const auto inventory = GetInventory(item);
    if (!inventory) return true;
    auto* filler = GetFirstInventoryItem(inventory);
    while (filler && state.socketFillerCount < state.socketFillers.size()) {
        auto& fillerState = state.socketFillers[state.socketFillerCount];
        if (!CaptureAffixState(filler, fillerState)) return false;
        ++state.socketFillerCount;
        filler = GetNextInventoryItem(filler);
    }
    return true;
}

const std::string* FindCachedTooltip(
    const TooltipCacheState& state, std::string_view original) noexcept {
    for (const auto& entry : TooltipCache) {

        if (entry.occupied && entry.state == state && entry.original == original)
            return &entry.enhanced;
    }
    return nullptr;
}

const std::string& StoreCachedTooltip(
    TooltipCacheState state, std::string original, std::string enhanced) {
    auto& entry = TooltipCache[TooltipCacheNext];
    TooltipCacheNext = (TooltipCacheNext + 1) % TooltipCache.size();
    entry.occupied = true;
    entry.state = std::move(state);
    entry.original = std::move(original);
    entry.enhanced = std::move(enhanced);
    return entry.enhanced;
}

const tcp::tooltips::TooltipLocalization& CurrentLocalization() {
    std::scoped_lock lock(LocalizationMutex);
    if (!LocalizationBuilt) {
        Localization = Catalog.BuildLocalization([](std::string_view key) {
            if (!GetLocalizedStringByKey || key.empty()) return std::string{};
            const GameStringView view{key.data(), key.size()};
            const auto* text = GetLocalizedStringByKey(&view);
            return text ? std::string(text) : std::string{};
        });
        // The native resolver is initialized before the first item tooltip.
        // Cache the immutable profile so concurrent comparison tooltips cannot
        // observe a partially rebuilt language map.
        LocalizationBuilt = true;
    }
    return Localization;
}

bool HasRunewordTitle(std::string_view tooltip, std::string_view name) {
    std::size_t start{};
    while (start <= tooltip.size()) {
        const auto end = tooltip.find('\n', start);
        const auto line = tooltip.substr(start, end - start);
        std::string visible;
        visible.reserve(line.size());
        for (std::size_t index = 0; index < line.size();) {
            if (index + 4 <= line.size()
                && static_cast<unsigned char>(line[index]) == 0xEE
                && static_cast<unsigned char>(line[index + 1]) == 0x81
                && static_cast<unsigned char>(line[index + 2]) == 0xBE) {
                index += 4;
                continue;
            }
            visible.push_back(line[index++]);
        }
        while (!visible.empty() && std::isspace(static_cast<unsigned char>(visible.front())))
            visible.erase(visible.begin());
        while (!visible.empty() && std::isspace(static_cast<unsigned char>(visible.back())))
            visible.pop_back();
        if (visible == name
            || (visible.size() > name.size() + 2
                && visible.compare(0, name.size(), name) == 0
                && visible[name.size()] == ' '
                && visible[name.size() + 1] == '(')) return true;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return false;
}

std::string ResolveRunewordKey(
    void* item, const std::uint8_t* itemData, std::string_view tooltip) {
    if (!item || !itemData || !GetLocalizedStringByKey
        || (Read<std::uint32_t>(itemData, ItemDataFlagsOffset) & ItemFlagRuneword) == 0) return {};


    std::scoped_lock lock(RunewordNamesMutex);
    if (!RunewordNamesBuilt) {
        std::unordered_map<std::string, std::string> resolved;
        std::unordered_set<std::string> ambiguous;
        for (const auto& key : Catalog.RunewordKeys()) {
            const GameStringView view{key.data(), key.size()};
            const auto* text = GetLocalizedStringByKey(&view);
            if (!text || text[0] == '\0') continue;
            const std::string name(text);
            if (const auto existing = resolved.find(name); existing != resolved.end()) {
                resolved.erase(existing);
                ambiguous.insert(name);
            } else if (!ambiguous.contains(name)) {
                resolved.emplace(name, key);
            }
        }
        // Localization is initialized by the time the first tooltip is built.
        // Still retry on a later tooltip if the resolver unexpectedly yielded
        // no names instead of caching an unusable empty map.
        if (!resolved.empty()) {
            RunewordKeyByLocalizedName = std::move(resolved);
            RunewordNamesBuilt = true;
        }
    }
    // Prefer the native runes.txt record. Some torso runewords can however
    // arrive without a usable localized record during final-tooltip assembly;
    // fail over to the already-rendered title instead of dropping every range.
    if (GetRunesTxtRecordFromItem && GetLocalizedString) {
        if (const auto* record = GetRunesTxtRecordFromItem(item)) {
            const auto stringId = Read<std::uint16_t>(record, RunesTxtStringIdOffset);
            if (const auto* localizedName = GetLocalizedString(stringId);
                localizedName && localizedName[0] != '\0') {
                if (const auto found = RunewordKeyByLocalizedName.find(localizedName);
                    found != RunewordKeyByLocalizedName.end()) return found->second;
            }
        }
    }
    for (const auto& [name, key] : RunewordKeyByLocalizedName)
        if (HasRunewordTitle(tooltip, name)) return key;
    return {};
}

std::string InsertBaseDefense(std::string tooltip, void* item, const std::uint8_t* itemData,
    const tcp::tooltips::TooltipLocalization& localization, bool rangesVisible) {
    if (!rangesVisible || !Settings.showBaseDefenseRange || !GetUnitStat) return tooltip;
    const auto armor = Catalog.FindArmor(ItemCode(item));
    if (!armor) return tooltip;
    std::size_t duplicateStart{};
    while (duplicateStart <= tooltip.size()) {
        const auto end = tooltip.find('\n', duplicateStart);
        if (tcp::tooltips::MatchesLocalizedTemplate(
                tooltip.substr(duplicateStart, end - duplicateStart),
                localization.baseDefenseFormat)) return tooltip;
        if (end == std::string::npos) break;
        duplicateStart = end + 1;
    }
    const auto ethereal = (Read<std::uint32_t>(itemData, ItemDataFlagsOffset)
        & ItemFlagEthereal) != 0;
    const auto flat = tcp::tooltips::ExactFlatDefenseTotal(tooltip, &localization);
    const auto enhancedDefense = tcp::tooltips::ExactEnhancedDefensePercent(
        tooltip, &localization);
    if (!flat || !enhancedDefense) return tooltip;
    const auto rolled = tcp::tooltips::ReconstructBaseDefense(
        GetUnitStat(item, ArmorClassStat, 0),
        *enhancedDefense,
        *flat,
        armor->minimum,
        armor->maximum,
        ethereal);

    if (!rolled) return tooltip;
    auto minimum = armor->minimum;
    auto maximum = armor->maximum;
    if (ethereal) {
        minimum = minimum * 3 / 2;
        maximum = maximum * 3 / 2;
    }
    std::size_t start{};
    while (start < tooltip.size()) {
        const auto end = tooltip.find('\n', start);
        const auto line = tooltip.substr(start, end - start);
        if (std::any_of(localization.defenseTemplates.begin(),
                localization.defenseTemplates.end(), [&](const auto& format) {
                    return tcp::tooltips::MatchesLocalizedTemplate(line, format);
                })) {
            const auto added = std::string("\xEE\x81\xBE" "0")
                + tcp::tooltips::FormatLocalizedInteger(
                    localization.baseDefenseFormat, *rolled) + " "
                + tcp::tooltips::FormatPositiveRange(minimum, maximum, '0',
                    RuffnecKk::AdvancedTooltips::PropertyRangeColorCode(
                        Settings.propertyRangeColor)) + "\n";
            tooltip.insert(start, added);
            break;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return tooltip;
}

std::string InsertMaxSocketsBelowPrimaryStat(std::string tooltip, void* item) {
    std::array<char, 64> line{};
    const auto length = AdvancedItemTooltipsBuildSocketLine(item, line.data(), line.size());
    if (length == 0 || length >= line.size()) return tooltip;
    const std::string_view text(line.data(), length);
    if (tooltip.find(text) != std::string::npos) return tooltip;
    const auto insertion = AdvancedItemTooltipsFindSocketLineInsertion(
        tooltip.data(), tooltip.size());
    if (insertion > tooltip.size()) return tooltip;
    tooltip.insert(insertion, std::string(text) + "\n");
    return tooltip;
}

void* TransformOwnedTooltip(void* result, void* item) noexcept {
    if (!result || !item || !EnsureStringCapacity || !IsReadable(result, 24)) return result;
    try {
        const auto* object = static_cast<const std::uint8_t*>(result);
        const auto* data = *reinterpret_cast<char* const*>(object);
        const auto length = *reinterpret_cast<const std::size_t*>(object + 8);
        if (length == 0 || length > 16 * 1024 || !IsReadable(data, length + 1)) return result;

        const std::string_view originalView(data, length);
        const auto rangesVisible = RangesVisibleNow();
        TooltipCacheState state;
        const auto cacheable = CaptureTooltipCacheState(item, state, rangesVisible);
        const std::string* enhanced = cacheable
            ? FindCachedTooltip(state, originalView)
            : nullptr;
        std::string uncached;
        if (!enhanced) {
            std::string original(originalView);
            uncached = original;
            const auto enhancedLength = EnhanceTooltipForVisibility(
                item, original.data(), original.size(),
                TooltipScratch.data(), TooltipScratch.size(), rangesVisible);
            if (enhancedLength > 0 && enhancedLength < TooltipScratch.size()) {
                uncached.assign(TooltipScratch.data(), enhancedLength);
            }
            uncached = InsertMaxSocketsBelowPrimaryStat(std::move(uncached), item);
            if (cacheable) {

                enhanced = &StoreCachedTooltip(
                    std::move(state), std::move(original), std::move(uncached));
            } else {
                enhanced = &uncached;
            }
        }
        if (*enhanced == originalView) return result;

        EnsureStringCapacity(result, enhanced->size());
        auto* destination = *reinterpret_cast<char**>(result);
        if (!IsReadable(destination, enhanced->size() + 1)) return result;
        std::memcpy(destination, enhanced->c_str(), enhanced->size() + 1);
        const auto size = enhanced->size();
        std::memcpy(static_cast<std::uint8_t*>(result) + 8, &size, sizeof(size));
    } catch (...) {
        if (Context) Context->LogError(
            "AdvancedItemTooltips: autonomous tooltip transform failed safely.");
    }
    return result;
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
    auto* result = BuildItemTooltip(output, a2, a3, item, a5, a6, a7, a8, a9);
    return TransformOwnedTooltip(result, item);
}

void* AllocateNear(void* hint, std::size_t size) noexcept {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto base = reinterpret_cast<std::uintptr_t>(hint) & ~(granularity - 1);
    for (std::uintptr_t delta = granularity; delta < 0x70000000ULL; delta += granularity) {
        if (base > std::numeric_limits<std::uintptr_t>::max() - delta) break;
        if (auto* memory = VirtualAlloc(
                reinterpret_cast<void*>(base + delta), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)) return memory;
    }
    return nullptr;
}

bool InstallTooltipCallSites() noexcept {
    constexpr std::size_t RelaySize = 14;
    TooltipRelay = AllocateNear(Base + TooltipCallSites.front().rva, RelaySize);
    if (!TooltipRelay) return false;
    auto* relay = static_cast<std::uint8_t*>(TooltipRelay);
    relay[0] = 0xFF;
    relay[1] = 0x25;
    relay[2] = relay[3] = relay[4] = relay[5] = 0x00;
    const auto target = reinterpret_cast<std::uint64_t>(&HookBuildItemTooltip);
    std::memcpy(relay + 6, &target, sizeof(target));
    FlushInstructionCache(GetCurrentProcess(), relay, RelaySize);
    DWORD previousProtection{};
    if (!VirtualProtect(relay, RelaySize, PAGE_EXECUTE_READ, &previousProtection)) return false;

    const auto relayAddress = reinterpret_cast<std::uintptr_t>(TooltipRelay);
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(Base);
    if (relayAddress < baseAddress) return false;
    const auto relayRva = static_cast<std::uint64_t>(relayAddress - baseAddress);
    for (const auto& site : TooltipCallSites) {
        const auto nextInstruction = reinterpret_cast<std::uintptr_t>(Base + site.rva + 5);
        const auto displacement = static_cast<std::int64_t>(relayAddress)

            - static_cast<std::int64_t>(nextInstruction);
        if (displacement < std::numeric_limits<std::int32_t>::min()
            || displacement > std::numeric_limits<std::int32_t>::max()
            || !PSh_ManifestPatchCallRel32(
                Context,
                site.manifestId,
                site.rva,
                site.expected.data(),
                static_cast<std::uint32_t>(site.expected.size()),
                relayRva,
                static_cast<std::uint32_t>(site.expected.size()))) return false;
    }
    return true;
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "AdvancedItemTooltips 3.4.0: enabled=%s; maxSockets=%s; maxSocketsOnSocketed=%s; baseDefense=%s; propertyRanges=%s; rangeDisplayMode=%s; holdToDisplayHotkey=%s; rangeColor=%s; socketContributions=%s; cubeMutations=ignored; catalog=%s; config=%s.",
        Settings.enabled ? "yes" : "no",
        Settings.showMaxSockets ? "yes" : "no",
        Settings.showMaxSocketsOnSocketedItems ? "yes" : "no",
        Settings.showBaseDefenseRange ? "yes" : "no",
        Settings.showPropertyRanges ? "yes" : "no",
        RuffnecKk::AdvancedTooltips::RangeDisplayModeName(
            Settings.rangeDisplayMode).data(),
        Settings.holdToDisplayHotkey.name.c_str(),
        RuffnecKk::AdvancedTooltips::PropertyRangeColorName(
            Settings.propertyRangeColor).data(),
        Settings.includeSocketedContributionsInRanges ? "included" : "intrinsic only",
        CatalogSource.c_str(),
        LoadedConfigPath.c_str());
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}
} // namespace

std::size_t EnhanceTooltipForVisibility(
    void* item,
    const char* tooltip,
    std::size_t tooltipLength,
    char* output,
    std::size_t outputCapacity,
    bool rangesVisible
) noexcept {
    if (!rangesVisible) return 0;
    if (!Settings.enabled || !item || !tooltip || !output || outputCapacity == 0 || !CatalogLoaded
        || tooltipLength == 0 || tooltipLength > 16 * 1024 || !GetItemData) return 0;
    try {
        const auto* itemData = GetItemData(item);
        if (!itemData || (Read<std::uint32_t>(itemData, ItemDataFlagsOffset) & ItemFlagIdentified) == 0) return 0;
        auto ids = ReadAffixIds(itemData);
        if (GetUnitStat) {
            ids.socketCount = static_cast<std::uint32_t>(
                std::max<std::int32_t>(0, GetUnitStat(item, SocketCountStat, 0)));
        }
        const auto code = ItemCode(item);
        const auto& localization = CurrentLocalization();
        const auto runewordKey = ResolveRunewordKey(
            item, itemData, std::string_view(tooltip, tooltipLength));
        auto resolution = Catalog.ResolveCandidateSet(
            ids, code, runewordKey, Settings.includeSocketedContributionsInRanges,
            [&](std::int32_t statId, std::uint16_t layer) {
                return GetUnitStat ? GetUnitStat(item, statId, layer) : 0;
            }, std::string_view(tooltip, tooltipLength), &localization);
        bool hasSocketFillers{};
        // Runeword rune bonuses are already reconstructed from runes.txt plus
        // gems.txt in ResolveCandidates. Enumerating their socket inventory as

        // ordinary fillers would count every rune twice.
        if (GetInventory && GetFirstInventoryItem) {
            if (auto* inventory = GetInventory(item)) {
                auto* socketFiller = GetFirstInventoryItem(inventory);
                hasSocketFillers = socketFiller != nullptr;
                if (Settings.includeSocketedContributionsInRanges && !ids.runeword
                    && GetNextInventoryItem) {
                    for (std::size_t index = 0; socketFiller && index < 6; ++index) {
                        if (const auto* fillerData = GetItemData(socketFiller)) {
                            const auto fillerIds = ReadAffixIds(fillerData);
                            const auto fillerCode = ItemCode(socketFiller);
                            const auto fillerCandidates = Catalog.ResolveSocketFillerCandidates(
                                fillerIds, fillerCode, code);
                            resolution.candidates = tcp::tooltips::MergeCandidateSources(
                                resolution.candidates, fillerCandidates);
                            resolution.intrinsicCandidates =
                                tcp::tooltips::MergeCandidateSources(
                                    resolution.intrinsicCandidates, fillerCandidates);
                        }
                        socketFiller = GetNextInventoryItem(socketFiller);
                    }
                }
            }
        }
        const std::string original(tooltip, tooltipLength);
        auto enhanced = rangesVisible && Settings.showPropertyRanges
            ? tcp::tooltips::AppendConsensusRanges(original, resolution.candidates,
                !Settings.includeSocketedContributionsInRanges && hasSocketFillers,
                &localization, &resolution.intrinsicCandidates,
                RuffnecKk::AdvancedTooltips::PropertyRangeColorCode(
                    Settings.propertyRangeColor))
            : original;
        enhanced = InsertBaseDefense(
            std::move(enhanced), item, itemData, localization, rangesVisible);
        if (enhanced == original || enhanced.size() + 1 > outputCapacity) return 0;
        std::memcpy(output, enhanced.data(), enhanced.size());
        output[enhanced.size()] = '\0';
        return enhanced.size();
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) std::size_t __cdecl AdvancedItemTooltipsEnhanceTooltip(
    void* item,
    const char* tooltip,
    std::size_t tooltipLength,
    char* output,
    std::size_t outputCapacity
) noexcept {
    return EnhanceTooltipForVisibility(
        item, tooltip, tooltipLength, output, outputCapacity, RangesVisibleNow());
}

extern "C" __declspec(dllexport) std::size_t __cdecl AdvancedItemTooltipsBuildSocketLine(
    void* item,
    char* output,
    std::size_t outputCapacity
) noexcept {
    if (!Settings.enabled || !Settings.showMaxSockets || !item || !output
        || outputCapacity == 0 || !GetMaxSockets) return 0;
    try {
        if (!Settings.showMaxSocketsOnSocketedItems && GetUnitStat
            && GetUnitStat(item, SocketCountStat, 0) > 0) return 0;
        const auto maximumSockets = static_cast<unsigned>(GetMaxSockets(item));
        const auto line = tcp::tooltips::FormatMaxSocketsLine(
            maximumSockets, 0, CurrentLocalization());
        if (line.empty() || line.size() + 1 > outputCapacity) return 0;
        std::memcpy(output, line.data(), line.size());
        output[line.size()] = '\0';

        return line.size();
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) std::size_t __cdecl AdvancedItemTooltipsFindSocketLineInsertion(
    const char* tooltip,
    std::size_t tooltipLength
) noexcept {
    if (!tooltip || tooltipLength == 0 || tooltipLength > 16 * 1024) {
        return tcp::tooltips::NoSocketLineInsertion;
    }
    try {
        return tcp::tooltips::FindMaxSocketsInsertion(
            std::string_view(tooltip, tooltipLength), CurrentLocalization()
        );
    } catch (...) {
        return tcp::tooltips::NoSocketLineInsertion;
    }
}

bool RuffnecKk::AdvancedTooltips::Load(
    const D2RL::PluginContext* context,
    const nlohmann::json& itemsConfig) noexcept {
    if (!context || context->exeBase == 0) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    try {
        Settings = ParseConfig(itemsConfig);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "AdvancedItemTooltips: invalid items.advancedTooltips configuration (")
            + exception.what() + "); feature refused.";
        context->LogError(message.c_str());
        return false;
    }
    if (context->modDataVersionBuild != 0 && context->modDataVersionBuild != SupportedBuild) {
        context->LogError("AdvancedItemTooltips: only D2R build 92777 is supported.");
        return false;
    }

    if (!PSh_RegisterConsoleCommand(context,
            "advanced-item-tooltips",
            Status,
            "Show Advanced Item Tooltips configuration status."
        )) {
        context->LogWarn("AdvancedItemTooltips: status command could not be registered.");
    }
    if (!Settings.enabled) {
        context->LogInfo("AdvancedItemTooltips 3.4.0 disabled by JSON config; no hooks installed.");
        return true;
    }

    constexpr std::array<std::uint8_t, 16> getMaxSocketsExpected{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,
        0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48};
    constexpr std::array<std::uint8_t, 16> getUnitStatExpected{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,
        0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
    constexpr std::array<std::uint8_t, 16> getItemDataContextExpected{
        0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,
        0x1A,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C};
    constexpr std::array<std::uint8_t, 16> getItemDataExpected{
        0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
        0xD9,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C};
    constexpr std::array<std::uint8_t, 16> getItemsTxtRecordExpected{
        0x40,0x57,0x48,0x83,0xEC,0x30,0x8B,0xFA,
        0xE8,0x73,0xC9,0xFE,0xFF,0x3B,0xB8,0xA8};
    if (!context->CheckExpectedBytes(GetMaxSocketsRva,
            getMaxSocketsExpected.data(), getMaxSocketsExpected.size())
        || !context->CheckExpectedBytes(GetUnitStatRva,

            getUnitStatExpected.data(), getUnitStatExpected.size())
        || !context->CheckExpectedBytes(GetItemDataContextRva,
            getItemDataContextExpected.data(), getItemDataContextExpected.size())
        || !context->CheckExpectedBytes(GetItemDataRva,
            getItemDataExpected.data(), getItemDataExpected.size())
        || !context->CheckExpectedBytes(GetItemsTxtRecordRva,
            getItemsTxtRecordExpected.data(), getItemsTxtRecordExpected.size())) {
        context->LogError("AdvancedItemTooltips: core item ABI signature mismatch for build 92777.");
        return false;
    }

    GetMaxSockets = At<GetMaxSocketsFn>(GetMaxSocketsRva);
    GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
    GetItemDataContext = At<GetItemDataContextFn>(GetItemDataContextRva);
    GetItemData = At<GetItemDataFn>(GetItemDataRva);
    GetInventory = At<GetInventoryFn>(GetInventoryRva);
    GetFirstInventoryItem = At<GetInventoryItemFn>(GetFirstInventoryItemRva);
    GetNextInventoryItem = At<GetInventoryItemFn>(GetNextInventoryItemRva);
    GetItemsTxtRecord = At<GetItemsTxtRecordFn>(GetItemsTxtRecordRva);
    constexpr std::array<std::uint8_t, 29> runewordResolverExpected{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,
        0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x55,
        0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8B,0xEC,0x48,0x83};
    constexpr std::array<std::uint8_t, 14> localizedStringExpected{
        0x80,0x3D,0x05,0xAE,0xCF,0x02,0x00,
        0x48,0x8D,0x05,0xDA,0x96,0xDB,0x01};
    constexpr std::array<std::uint8_t, 16> getInventoryExpected{
        0x48,0x89,0x5C,0x24,0x18,0x56,0x48,0x83,
        0xEC,0x20,0x48,0x8B,0xF1,0x48,0x85,0xC9};
    constexpr std::array<std::uint8_t, 14> getFirstInventoryItemExpected{
        0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
        0xD9,0x48,0x85,0xC9,0x74,0x2E};
    constexpr std::array<std::uint8_t, 14> getNextInventoryItemExpected{
        0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
        0xD9,0x48,0x85,0xC9,0x75,0x10};
    constexpr std::array<std::uint8_t, 24> ensureStringCapacityExpected{
        0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x08,0x49,
        0x89,0x6B,0x18,0x49,0x89,0x73,0x20,0x49,
        0x89,0x53,0x10,0x57,0x48,0x83,0xEC,0x30};
    if (!context->CheckExpectedBytes(GetRunesTxtRecordFromItemRva,
            runewordResolverExpected.data(), runewordResolverExpected.size())
        || !context->CheckExpectedBytes(GetLocalizedStringRva,
            localizedStringExpected.data(), localizedStringExpected.size())) {
        context->LogError("AdvancedItemTooltips: runeword ABI signature mismatch for build 92777.");
        return false;
    }
    // plugin-skills can legitimately own an inline hook on this shared
    // localization entry before global plugins load. AdvancedItemTooltips only
    // calls the entry and never owns it, so require a live executable target
    // without demanding the original vanilla prologue.
    if (!IsExecutableAddress(Base + GetLocalizedStringByKeyRva)) {
        context->LogError("AdvancedItemTooltips: localization entry is not executable.");
        return false;
    }
    if (!context->CheckExpectedBytes(GetInventoryRva,
            getInventoryExpected.data(), getInventoryExpected.size())
        || !context->CheckExpectedBytes(GetFirstInventoryItemRva,
            getFirstInventoryItemExpected.data(), getFirstInventoryItemExpected.size())
        || !context->CheckExpectedBytes(GetNextInventoryItemRva,
            getNextInventoryItemExpected.data(), getNextInventoryItemExpected.size())) {
        context->LogError("AdvancedItemTooltips: socket inventory ABI signature mismatch for build 92777.");
        return false;
    }
    if (!context->CheckExpectedBytes(EnsureStringCapacityRva,
            ensureStringCapacityExpected.data(), ensureStringCapacityExpected.size())) {
        context->LogError("AdvancedItemTooltips: native string ABI signature mismatch for build 92777.");
        return false;
    }
    BuildItemTooltip = At<BuildItemTooltipFn>(BuildItemTooltipRva);

    EnsureStringCapacity = At<EnsureStringCapacityFn>(EnsureStringCapacityRva);
    GetRunesTxtRecordFromItem = At<GetRunesTxtRecordFromItemFn>(GetRunesTxtRecordFromItemRva);
    GetLocalizedString = At<GetLocalizedStringFn>(GetLocalizedStringRva);
    GetLocalizedStringByKey = At<GetLocalizedStringByKeyFn>(GetLocalizedStringByKeyRva);
    std::string catalogError;
    const bool hasActiveMod = context->activeMod && context->activeMod[0] != '\0';
    if (hasActiveMod && context->modDirectory) {
        const auto modDirectory = std::filesystem::path(context->modDirectory);
        std::error_code directoryError;
        const auto directoryExists = std::filesystem::exists(modDirectory, directoryError);
        if (directoryError) {
            catalogError = "Cannot inspect active package directory " + modDirectory.string()
                + ": " + directoryError.message();
        } else if (!directoryExists) {
            catalogError = "Active package directory is unavailable: " + modDirectory.string();
        } else {
            const std::vector<std::filesystem::path> excelCandidates{
                modDirectory / L"data/global/excel",
                modDirectory / (std::string(context->activeMod) + ".mpq")
                    / L"data/global/excel"
            };
            std::size_t physicalLoads{};
            std::size_t fallbackLoads{};
            const auto provider = [&](std::string_view tableName, std::string& text,
                                      std::string& error) {
                return tcp::tooltips::RangeCatalog::LoadLayeredTable(
                    excelCandidates, LoadEmbeddedVanillaTable, tableName, text, error,
                    physicalLoads, fallbackLoads);
            };
            if (Catalog.Load(provider, catalogError)) {
                CatalogLoaded = true;
                if (physicalLoads == 0) {
                    CatalogSource = "embedded vanilla 3.2.92777 (cosmetic package)";
                } else if (fallbackLoads == 0) {
                    const auto source = std::find_if(excelCandidates.begin(), excelCandidates.end(),
                        [](const auto& excel) {
                            std::error_code error;
                            return std::filesystem::exists(excel / L"properties.txt", error)
                                && !error;
                        });
                    CatalogSource = source == excelCandidates.end()
                        ? "active package TXT tables"
                        : source->string();
                } else {
                    CatalogSource = "active package TXT tables + embedded vanilla fallback";
                }
            }
        }
    } else if (hasActiveMod) {
        catalogError = "D2RLoader did not expose the active package directory";
    }
    if (!CatalogLoaded && !hasActiveMod) {
        catalogError.clear();
        if (Catalog.Load(LoadEmbeddedVanillaTable, catalogError)) {
            CatalogLoaded = true;
            CatalogSource = "embedded vanilla 3.2.92777";
        }
    }
    if (!CatalogLoaded) {
        const auto message = "AdvancedItemTooltips: roll ranges unavailable; sockets remain active. " + catalogError;
        context->LogWarn(message.c_str());
    } else if (!Catalog.UnsupportedPropertyFunctions().empty()) {
        std::string message = "AdvancedItemTooltips: unsupported property functions omitted:";
        for (const auto& [function, count] : Catalog.UnsupportedPropertyFunctions()) {
            message += " func" + std::to_string(function) + "=" + std::to_string(count);
        }
        message += ".";
        context->LogInfo(message.c_str());
    }
    if (!InstallTooltipCallSites()) {

        context->LogError(
            "AdvancedItemTooltips: autonomous tooltip call-sites are unavailable; plugin refused.");
        return false;
    }
    const auto activation = std::string(
        "AdvancedItemTooltips 3.4.0 active for D2R 3.2.92777 (7/7 call-sites); catalog=")
        + CatalogSource
        + "; config="
        + LoadedConfigPath
        + "; maxSocketsOnSocketed="
        + (Settings.showMaxSocketsOnSocketedItems ? "yes" : "no")
        + "; rangeColor="
        + std::string(RuffnecKk::AdvancedTooltips::PropertyRangeColorName(
            Settings.propertyRangeColor))
        + "; rangeDisplayMode="
        + std::string(RuffnecKk::AdvancedTooltips::RangeDisplayModeName(
            Settings.rangeDisplayMode))
        + "; holdToDisplayHotkey="
        + Settings.holdToDisplayHotkey.name
        + "; socketContributions="
        + (Settings.includeSocketedContributionsInRanges ? "included" : "intrinsic only")
        + "; cubeMutations=ignored"
        + ".";
    context->LogInfo(activation.c_str());
    return true;
}

void RuffnecKk::AdvancedTooltips::Unload() noexcept {
    GetMaxSockets = nullptr;
    GetUnitStat = nullptr;
    GetItemDataContext = nullptr;
    GetItemData = nullptr;
    GetItemsTxtRecord = nullptr;
    GetRunesTxtRecordFromItem = nullptr;
    GetLocalizedString = nullptr;
    GetLocalizedStringByKey = nullptr;
    BuildItemTooltip = nullptr;
    EnsureStringCapacity = nullptr;
    {
        std::scoped_lock lock(RunewordNamesMutex);
        RunewordKeyByLocalizedName.clear();
        RunewordNamesBuilt = false;
    }
    {
        std::scoped_lock lock(LocalizationMutex);
        Localization = {};
        LocalizationBuilt = false;
    }
    CatalogLoaded = false;
    CatalogSource = "unavailable";
    Context = nullptr;
}
