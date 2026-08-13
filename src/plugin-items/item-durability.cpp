#include "item-durability.h"
#include <plugin-shared.h>
#include "item-durability-policy.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace RuffnecKk::ItemDurability {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t UpdateDurabilityRva = 0x441B10;
constexpr std::uintptr_t GetBaseStatRva = 0x2F48C0;
constexpr std::uintptr_t GetDataTablesRva = 0x300A90;
constexpr std::uintptr_t GetItemsTxtRecordRva = 0x314110;
constexpr std::uintptr_t UnitSeedRva = 0x34A1E0;
constexpr std::uintptr_t RandomRva = 0x153B00;
constexpr std::uintptr_t CheckItemFlagRva = 0x36E2D0;
constexpr std::uintptr_t EtherealMaximumReturnRva = 0x44351F;
constexpr std::uint32_t EtherealItemFlag = 0x00400000;
constexpr std::int32_t MaximumDurabilityStat = 73;

constexpr std::uintptr_t ItemsNoDurabilityOffset = 0x122;
constexpr std::uintptr_t ItemsPrimaryTypeOffset = 0x12E;
constexpr std::uintptr_t ItemTypesRecordsOffset = 0x1348;
constexpr std::uintptr_t ItemTypesCountOffset = 0x1350;
constexpr std::uintptr_t ItemTypesRepairOffset = 0x08;
constexpr std::size_t ItemTypesRecordStride = 0xE8;
constexpr std::size_t ItemTypesCodeOffset = 0x00;
constexpr std::size_t ItemTypesEquivalentOneOffset = 0x04;
constexpr std::size_t ItemTypesEquivalentTwoOffset = 0x06;

// Full, instruction-aligned signatures. The governed 92777 image contains one
// exact match for UpdateDurability and GetItemsTxtRecord. The base-stat getter
// belongs to a three-member generated family until the call displacement is
// included; this 55-byte signature is unique at 0x2F48C0.
constexpr std::array<std::uint8_t, 28> ExpectedUpdateDurability{
	0x48, 0x89, 0x6C, 0x24, 0x10, 0x56, 0x57, 0x41,
	0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC,
	0x30, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0xBA,
	0x32, 0x00, 0x00, 0x00
};
constexpr std::array<std::uint8_t, 55> ExpectedGetBaseStat{
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
	0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
	0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8,
	0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9,
	0x75, 0x2A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
	0x4C, 0x24, 0x30, 0xE8, 0x00, 0xD2, 0xFF, 0xFF,
	0x84, 0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0
};
constexpr std::array<std::uint8_t, 19> ExpectedGetItemsTxtRecord{
	0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0xFA,
	0xE8, 0x73, 0xC9, 0xFE, 0xFF, 0x3B, 0xB8, 0xA8,
	0x15, 0x00, 0x00
};

using UpdateDurabilityFn = void(__fastcall*)(void*, void*, void*) noexcept;
using GetBaseStatFn = std::int32_t(__fastcall*)(
	void*, std::int32_t, std::uint16_t) noexcept;
using GetDataTablesFn = std::uint8_t*(__fastcall*)(std::uint8_t) noexcept;
using GetItemsTxtRecordFn = std::uint8_t*(__fastcall*)(
	std::uint8_t, std::int32_t) noexcept;
using UnitSeedFn = void*(__fastcall*)(void*) noexcept;
using RandomFn = std::uint32_t(__fastcall*)(void*, std::int32_t) noexcept;
using CheckItemFlagFn = std::int32_t(__fastcall*)(
	void*, std::uint32_t, std::int32_t, const char*) noexcept;
using ItemRecordTransformFn = std::uint8_t*(__cdecl*)(
	std::uint8_t*, std::uint8_t, std::int32_t) noexcept;
const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
Policy Settings{};
UpdateDurabilityFn OriginalUpdateDurability{};
GetBaseStatFn OriginalGetBaseStat{};
GetDataTablesFn GetDataTables{};
GetItemsTxtRecordFn OriginalGetItemsTxtRecord{};
UnitSeedFn GetUnitSeed{};
RandomFn RollRandom{};
CheckItemFlagFn CheckItemFlag{};
std::atomic<ItemRecordTransformFn> ExternalItemRecordTransform{};
bool ItemRecordHookInstalled{};

std::atomic<std::uint64_t> PreventedNormal{};
std::atomic<std::uint64_t> PreventedEthereal{};
std::atomic<std::uint64_t> RangedWeaponRecordsEnabled{};
std::atomic<std::uint64_t> RepairTypeRecordsEnabled{};

template<class T>
T At(std::uintptr_t rva) noexcept {
	return reinterpret_cast<T>(Base + rva);
}

void ResetTelemetry() noexcept {
	PreventedNormal.store(0, std::memory_order_relaxed);
	PreventedEthereal.store(0, std::memory_order_relaxed);
	RangedWeaponRecordsEnabled.store(0, std::memory_order_relaxed);
	RepairTypeRecordsEnabled.store(0, std::memory_order_relaxed);
}

bool IsEtherealItem(void* item) noexcept {
	return item && CheckItemFlag
		&& CheckItemFlag(item, EtherealItemFlag, __LINE__, "ItemDurability") != 0;
}

bool IsBowOrCrossbowItemType(
	const std::uint8_t* records,
	std::uint64_t count,
	std::uint16_t itemType
) noexcept {
	if (!records || count == 0 || count > 4096 || itemType >= count) return false;

	std::array<std::uint16_t, 16> pending{};
	std::size_t pendingCount{1};
	pending[0] = itemType;
	for (std::size_t visited = 0;
		pendingCount != 0 && visited < pending.size(); ++visited) {
		const auto current = pending[--pendingCount];
		if (current >= count) continue;
		const auto* record = records
			+ static_cast<std::size_t>(current) * ItemTypesRecordStride;
		std::uint32_t code{};
		std::memcpy(&code, record + ItemTypesCodeOffset, sizeof(code));
		if (IsBowOrCrossbowItemTypeCode(code)) return true;

		const auto equivalentOne = *reinterpret_cast<const std::uint16_t*>(
			record + ItemTypesEquivalentOneOffset);
		const auto equivalentTwo = *reinterpret_cast<const std::uint16_t*>(
			record + ItemTypesEquivalentTwoOffset);
		if (equivalentOne < count && equivalentOne != current
			&& pendingCount < pending.size()) {
			pending[pendingCount++] = equivalentOne;
		}
		if (equivalentTwo < count && equivalentTwo != current
			&& pendingCount < pending.size()) {
			pending[pendingCount++] = equivalentTwo;
		}
	}
	return false;
}

void EnableRepairForItemType(
	std::uint8_t* records,
	std::uint64_t count,
	std::uint16_t itemType
) noexcept {
	if (!records || itemType >= count) return;
	auto& repair = records[
		static_cast<std::size_t>(itemType) * ItemTypesRecordStride
		+ ItemTypesRepairOffset];
	if (repair == 0) {
		repair = 1;
		RepairTypeRecordsEnabled.fetch_add(1, std::memory_order_relaxed);
	}
}

std::uint8_t* TransformRangedItemRecord(
	std::uint8_t* record,
	std::uint8_t context,
	std::int32_t
) noexcept {
	if (!record) return record;

	const auto itemType = *reinterpret_cast<const std::uint16_t*>(
		record + ItemsPrimaryTypeOffset);
	auto* dataTables = GetDataTables ? GetDataTables(context) : nullptr;
	if (!dataTables) return record;
	auto* itemTypeRecords = *reinterpret_cast<std::uint8_t**>(
		dataTables + ItemTypesRecordsOffset);
	const auto itemTypeCount = *reinterpret_cast<const std::uint64_t*>(
		dataTables + ItemTypesCountOffset);
	if (!IsBowOrCrossbowItemType(itemTypeRecords, itemTypeCount, itemType)) {
		return record;
	}

	auto& noDurability = record[ItemsNoDurabilityOffset];
	if (noDurability != 0) {
		noDurability = 0;
		RangedWeaponRecordsEnabled.fetch_add(1, std::memory_order_relaxed);
	}
	EnableRepairForItemType(itemTypeRecords, itemTypeCount, itemType);
	return record;
}

std::uint8_t* __fastcall HookGetItemsTxtRecord(
	std::uint8_t context,
	std::int32_t classId
) noexcept {
	auto* record = OriginalGetItemsTxtRecord(context, classId);
	record = TransformRangedItemRecord(record, context, classId);
	if (const auto transform = ExternalItemRecordTransform.load(
			std::memory_order_acquire)) {
		record = transform(record, context, classId);
	}
	return record;
}

void __fastcall HookUpdateDurability(void* game, void* unit, void* item) noexcept {
	if (!unit || !item) {
		OriginalUpdateDurability(game, unit, item);
		return;
	}

	const bool ethereal = IsEtherealItem(item);
	const auto resistance = ethereal
		? Settings.etherealResistancePercent
		: Settings.normalResistancePercent;
	if (resistance == 0) {
		OriginalUpdateDurability(game, unit, item);
		return;
	}

	auto* seed = GetUnitSeed(unit);
	if (!seed || !PreventsLoss(resistance, RollRandom(seed, 100))) {
		OriginalUpdateDurability(game, unit, item);
		return;
	}

	if (ethereal) {
		PreventedEthereal.fetch_add(1, std::memory_order_relaxed);
	} else {
		PreventedNormal.fetch_add(1, std::memory_order_relaxed);
	}
}

__declspec(noinline) std::int32_t __fastcall HookGetBaseStat(
	void* unit,
	std::int32_t stat,
	std::uint16_t layer
) noexcept {
	const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
	const auto value = OriginalGetBaseStat(unit, stat, layer);
	if (returnAddress != Base + EtherealMaximumReturnRva
		|| stat != MaximumDurabilityStat) {
		return value;
	}
	if (Settings.forceMaximumDurability && value > 0) {
		return EncodeEtherealMaximumTarget(255);
	}
	return EncodeForVanillaEtherealHalving(
		value, Settings.etherealMaximumPercent);
}

void FormatChance(
	char* output,
	std::size_t size,
	std::uint32_t basisPoints
) noexcept {
	std::snprintf(output, size, "%u.%02u%%", basisPoints / 100, basisPoints % 100);
}

D2RL::ConsoleCommandResult Status(
	D2R::Game::Client*,
	const D2RL::ConsoleCommandContext* command,
	void*
) noexcept {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;

	char normalWeapon[16]{}, normalArmor[16]{};
	char etherealWeapon[16]{}, etherealArmor[16]{};
	FormatChance(normalWeapon, sizeof(normalWeapon),
		EffectiveChanceBasisPoints(4, Settings.normalResistancePercent));
	FormatChance(normalArmor, sizeof(normalArmor),
		EffectiveChanceBasisPoints(10, Settings.normalResistancePercent));
	FormatChance(etherealWeapon, sizeof(etherealWeapon),
		EffectiveChanceBasisPoints(4, Settings.etherealResistancePercent));
	FormatChance(etherealArmor, sizeof(etherealArmor),
		EffectiveChanceBasisPoints(10, Settings.etherealResistancePercent));

	char message[640]{};
	std::snprintf(
		message,
		sizeof(message),
		"Item Durability 1.2.0 (plugin-items): enabled=%s; normal resistance=%u%% "
		"(weapon %s, armor %s); ethereal resistance=%u%% (weapon %s, armor %s); "
		"ethereal maximum=%u%s; bows/crossbows=%s; item records=%llu; repair "
		"types=%llu; prevented normal=%llu ethereal=%llu.",
		Settings.enabled ? "true" : "false",
		Settings.normalResistancePercent,
		normalWeapon,
		normalArmor,
		Settings.etherealResistancePercent,
		etherealWeapon,
		etherealArmor,
		Settings.forceMaximumDurability ? 255u : Settings.etherealMaximumPercent,
		Settings.forceMaximumDurability ? " points (forced maximum)" : "%",
		Settings.bowsAndCrossbowsHaveDurability ? "enabled" : "disabled",
		static_cast<unsigned long long>(
			RangedWeaponRecordsEnabled.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			RepairTypeRecordsEnabled.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			PreventedNormal.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			PreventedEthereal.load(std::memory_order_relaxed)));
	command->plugin->WriteConsoleMessage(message);
	return D2RL::ConsoleCommandResult::Handled;
}

bool Preflight(
	std::uintptr_t rva,
	const std::uint8_t* expected,
	std::size_t size,
	const char* label
) noexcept {
	if (Context->CheckExpectedBytes(rva, expected, size)) return true;
	const auto message = std::string("plugin-items: Item Durability ")
		+ label + " signature mismatch.";
	Context->LogError(message.c_str());
	return false;
}

bool InstallChanges() noexcept {
	const bool needsLossHook = Settings.normalResistancePercent != 0
		|| Settings.etherealResistancePercent != 0;
	const bool needsMaximumHook = Settings.forceMaximumDurability
		|| Settings.etherealMaximumPercent != 50;

	if (needsLossHook) {
		if (!Preflight(
				UpdateDurabilityRva,
				ExpectedUpdateDurability.data(),
				ExpectedUpdateDurability.size(),
				"durability-loss")
			|| !PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.itemDurability.loss"),
				UpdateDurabilityRva,
				ExpectedUpdateDurability.data(),
				static_cast<std::uint32_t>(ExpectedUpdateDurability.size()),
				HookUpdateDurability,
				&OriginalUpdateDurability)) {
			Context->LogError(
				"plugin-items: Item Durability durability-loss hook failed.");
			return false;
		}
	}

	if (needsMaximumHook) {
		if (!Preflight(
				GetBaseStatRva,
				ExpectedGetBaseStat.data(),
				ExpectedGetBaseStat.size(),
				"ethereal maximum")
			|| !PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.itemDurability.etherealMaximum"),
				GetBaseStatRva,
				ExpectedGetBaseStat.data(),
				static_cast<std::uint32_t>(ExpectedGetBaseStat.size()),
				HookGetBaseStat,
				&OriginalGetBaseStat)) {
			Context->LogError(
				"plugin-items: Item Durability ethereal-maximum hook failed.");
			return false;
		}
	}

	if (Settings.bowsAndCrossbowsHaveDurability) {
		if (!Preflight(
				GetItemsTxtRecordRva,
				ExpectedGetItemsTxtRecord.data(),
				ExpectedGetItemsTxtRecord.size(),
				"ranged item-record")
			|| !PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.itemDurability.rangedRecords"),
				GetItemsTxtRecordRva,
				ExpectedGetItemsTxtRecord.data(),
				static_cast<std::uint32_t>(ExpectedGetItemsTxtRecord.size()),
				HookGetItemsTxtRecord,
				&OriginalGetItemsTxtRecord)) {
			Context->LogError(
				"plugin-items: Item Durability ranged item-record broker failed.");
			return false;
		}
		ItemRecordHookInstalled = true;
	}

	return true;
}

} // namespace

bool Load(
	const D2RL::PluginContext* context,
	const nlohmann::json& itemsConfig
) noexcept {
	if (!context || context->exeBase == 0) return false;
	Context = context;
	Base = context->exeBase;
	ResetTelemetry();
	ExternalItemRecordTransform.store(nullptr, std::memory_order_release);
	ItemRecordHookInstalled = false;
	try {
		Settings = ParseConfig(itemsConfig);
	} catch (const std::exception& exception) {
		const auto message = std::string(
			"plugin-items: invalid items.itemDurability (")
			+ exception.what() + ").";
		context->LogError(message.c_str());
		return false;
	}

	if (context->modDataVersionBuild != 0
		&& context->modDataVersionBuild != SupportedBuild) {
		context->LogError(
			"plugin-items: Item Durability supports only D2R build 92777.");
		return false;
	}

	if (Settings.enabled) {
		GetUnitSeed = At<UnitSeedFn>(UnitSeedRva);
		RollRandom = At<RandomFn>(RandomRva);
		CheckItemFlag = At<CheckItemFlagFn>(CheckItemFlagRva);
		GetDataTables = At<GetDataTablesFn>(GetDataTablesRva);
		if (!InstallChanges()) return false;
	}

	if (!PSh_RegisterConsoleCommand(context,
			"item-durability",
			Status,
			"Show item durability policy and session counters.")) {
		context->LogWarn(
			"plugin-items: Item Durability status command was not registered.");
	}

	char message[448]{};
	std::snprintf(
		message,
		sizeof(message),
		"plugin-items: Item Durability 1.2.0 by RuffnecKk loaded: enabled=%s; "
		"normal resistance=%u%%; ethereal resistance=%u%%; ethereal maximum=%u%s; "
		"bows/crossbows=%s; itemRecords=%s; config=items.itemDurability.",
		Settings.enabled ? "true" : "false",
		Settings.normalResistancePercent,
		Settings.etherealResistancePercent,
		Settings.forceMaximumDurability ? 255u : Settings.etherealMaximumPercent,
		Settings.forceMaximumDurability ? " points" : "%",
		Settings.bowsAndCrossbowsHaveDurability ? "enabled" : "disabled",
		ItemRecordHookInstalled ? "owner" : "inactive");
	context->LogInfo(message);
	return true;
}

extern "C" __declspec(dllexport) bool __cdecl
PluginItemsRegisterItemRecordTransform(ItemRecordTransformFn transform) noexcept {
	if (!transform || !ItemRecordHookInstalled) return false;
	auto expected = static_cast<ItemRecordTransformFn>(nullptr);
	return ExternalItemRecordTransform.compare_exchange_strong(
		expected, transform, std::memory_order_acq_rel)
		|| expected == transform;
}

extern "C" __declspec(dllexport) void __cdecl
PluginItemsUnregisterItemRecordTransform(ItemRecordTransformFn transform) noexcept {
	if (!transform) return;
	auto expected = transform;
	ExternalItemRecordTransform.compare_exchange_strong(
		expected,
		nullptr,
		std::memory_order_acq_rel,
		std::memory_order_acquire);
}

void Unload() noexcept {
	OriginalGetItemsTxtRecord = nullptr;
	OriginalGetBaseStat = nullptr;
	OriginalUpdateDurability = nullptr;
	CheckItemFlag = nullptr;
	RollRandom = nullptr;
	GetUnitSeed = nullptr;
	GetDataTables = nullptr;
	Settings = {};
	ResetTelemetry();
	Base = 0;
	Context = nullptr;
}

} // namespace RuffnecKk::ItemDurability
