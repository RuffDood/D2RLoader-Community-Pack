#include "items-ethereal.h"
#include <plugin-shared.h>
#include "items-ethereal-policy.h"

#include <intrin.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

// Ported and maintained by RuffnecKk. The final owner is plugin-items.dll;
// this source does not create or depend on a separate runtime plugin.
namespace {
using ruffneckk::plugin_items::ethereal::Config;
using ruffneckk::plugin_items::ethereal::FindItemTypeId;
using ruffneckk::plugin_items::ethereal::HasDirectRulePatches;
using ruffneckk::plugin_items::ethereal::HasExcludedItemTypes;
using ruffneckk::plugin_items::ethereal::ItemTypeRecordStride;
using ruffneckk::plugin_items::ethereal::MaxExcludedItemTypes;
using ruffneckk::plugin_items::ethereal::ParseConfig;
using ruffneckk::plugin_items::ethereal::PatchChance;
using ruffneckk::plugin_items::ethereal::PatchIndestructibleItems;
using ruffneckk::plugin_items::ethereal::PatchSetItems;

constexpr std::uintptr_t CheckItemTypeRva = 0x373890;
constexpr std::uintptr_t GetItemContextRva = 0x34A0E0;
constexpr std::uintptr_t GetDataTablesRva = 0x300A90;
constexpr std::uintptr_t EtherealWeaponCheckReturnRva = 0x4432DA;
constexpr std::uintptr_t EtherealArmorCheckReturnRva = 0x4432E9;
constexpr std::uintptr_t ItemTypesRecordsOffset = 0x1348;
constexpr std::uintptr_t ItemTypesCountOffset = 0x1350;

constexpr std::uintptr_t EtherealChanceRva = 0x4434DF;
constexpr std::uintptr_t SetQualityBranchRva = 0x443315;
constexpr std::uintptr_t DurabilityEligibilityCallRva = 0x4432F4;
constexpr std::uintptr_t IndestructibleHelperCaveRva = 0x46D840;

constexpr std::array<std::uint8_t, 15> ExpectedCheckItemType{
	0x48, 0x89, 0x5C, 0x24, 0x10,
	0x48, 0x89, 0x6C, 0x24, 0x18,
	0x48, 0x89, 0x74, 0x24, 0x20
};
constexpr std::array<std::uint8_t, 16> ExpectedGetItemContext{
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
	0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C
};
constexpr std::array<std::uint8_t, 16> ExpectedGetDataTables{
	0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48,
	0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04
};
constexpr std::array<std::uint8_t, 1> ExpectedEtherealChance{0x05};
constexpr std::array<std::uint8_t, 6> ExpectedSetQualityBranch{
	0x0F, 0x84, 0x3D, 0x02, 0x00, 0x00
};
constexpr std::array<std::uint8_t, 5> ExpectedDurabilityEligibilityCall{
	0xE8, 0x47, 0x02, 0xF3, 0xFF
};
constexpr auto ExpectedIndestructibleHelperCave = [] {
	std::array<std::uint8_t, 67> bytes{};
	bytes.fill(0xCC);
	return bytes;
}();
constexpr std::array<std::uint8_t, 67> IndestructibleHelper{
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0x4C, 0x24, 0x20,
	0xE8, 0xF2, 0x5C, 0xF0, 0xFF, 0x85, 0xC0, 0x75, 0x2C,
	0x48, 0x8B, 0x4C, 0x24, 0x20, 0x45, 0x33, 0xC0, 0xBA,
	0x98, 0x00, 0x00, 0x00, 0xE8, 0xBC, 0x77, 0xE8, 0xFF,
	0x85, 0xC0, 0x7E, 0x14, 0x48, 0x8B, 0x4C, 0x24, 0x20,
	0xE8, 0xEE, 0x72, 0xE8, 0xFF, 0x85, 0xC0, 0x0F, 0x9F,
	0xC0, 0x0F, 0xB6, 0xC0, 0xEB, 0x02, 0x33, 0xC0, 0x48,
	0x83, 0xC4, 0x28, 0xC3
};

struct ResolvedTypeCache {
	const void* dataTables{};
	const void* records{};
	std::uint64_t recordCount{};
	std::array<std::int32_t, MaxExcludedItemTypes> ids{};
	std::size_t idCount{};
	std::size_t unresolvedCount{};
};

using CheckItemTypeFn = std::int32_t(__fastcall*)(const void*, std::int32_t) noexcept;
using GetItemContextFn = std::uint8_t(__fastcall*)(const void*) noexcept;
using GetDataTablesFn = std::uint8_t*(__fastcall*)(std::uint8_t) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
CheckItemTypeFn OriginalCheckItemType{};
GetItemContextFn GetItemContext{};
GetDataTablesFn GetDataTables{};
std::atomic<std::uint64_t> ExcludedEligibleItems{};
std::atomic<std::uint32_t> ResolvedTypeCount{};
std::atomic<std::uint32_t> UnresolvedTypeCount{};
std::atomic_flag UnresolvedWarningLogged = ATOMIC_FLAG_INIT;
thread_local ResolvedTypeCache TypeCache{};
thread_local const void* PendingGateItem{};
thread_local bool PendingGateExcluded{};
thread_local bool PendingGateWasWeapon{};

template<class T>
T At(std::uintptr_t rva) noexcept {
	return reinterpret_cast<T>(Base + rva);
}

bool RefreshTypeCache(const void* item) noexcept {
	if (!item || !GetItemContext || !GetDataTables) return false;
	const auto context = GetItemContext(item);
	auto* dataTables = GetDataTables(context);
	if (!dataTables) return false;

	const auto* records =
		*reinterpret_cast<const std::uint8_t* const*>(dataTables + ItemTypesRecordsOffset);
	const auto recordCount =
		*reinterpret_cast<const std::uint64_t*>(dataTables + ItemTypesCountOffset);
	if (!records || recordCount == 0 || recordCount > 4096) return false;
	if (TypeCache.dataTables == dataTables
		&& TypeCache.records == records
		&& TypeCache.recordCount == recordCount) {
		return true;
	}

	TypeCache = {};
	TypeCache.dataTables = dataTables;
	TypeCache.records = records;
	TypeCache.recordCount = recordCount;
	for (std::size_t index = 0; index < Settings.excludedItemTypeCount; ++index) {
		const auto id = FindItemTypeId(
			records,
			recordCount,
			ItemTypeRecordStride,
			Settings.excludedItemTypes[index]
		);
		if (id < 0) {
			++TypeCache.unresolvedCount;
			continue;
		}
		TypeCache.ids[TypeCache.idCount++] = id;
	}

	ResolvedTypeCount.store(
		static_cast<std::uint32_t>(TypeCache.idCount),
		std::memory_order_relaxed
	);
	UnresolvedTypeCount.store(
		static_cast<std::uint32_t>(TypeCache.unresolvedCount),
		std::memory_order_relaxed
	);
	if (TypeCache.unresolvedCount != 0
		&& !UnresolvedWarningLogged.test_and_set(std::memory_order_relaxed)) {
		Context->LogWarn(
			"plugin-items: one or more RuffnecKk ethereal exclusion codes do not exist "
			"in the active itemtypes table."
		);
	}
	return true;
}

bool IsExcluded(const void* item) noexcept {
	if (!HasExcludedItemTypes(Settings) || !RefreshTypeCache(item)) {
		return false;
	}
	for (std::size_t index = 0; index < TypeCache.idCount; ++index) {
		if (OriginalCheckItemType(item, TypeCache.ids[index]) != 0) return true;
	}
	return false;
}

std::int32_t __fastcall HookCheckItemType(const void* item, std::int32_t itemType) noexcept {
	const auto result = OriginalCheckItemType(item, itemType);
	const auto returnRva = reinterpret_cast<std::uintptr_t>(_ReturnAddress())
		- reinterpret_cast<std::uintptr_t>(Base);

	if (returnRva == EtherealWeaponCheckReturnRva) {
		PendingGateItem = item;
		PendingGateExcluded = IsExcluded(item);
		PendingGateWasWeapon = result != 0;
		return PendingGateExcluded ? 0 : result;
	}
	if (returnRva == EtherealArmorCheckReturnRva) {
		const bool excluded = PendingGateItem == item ? PendingGateExcluded : IsExcluded(item);
		const bool wasEligible = (PendingGateItem == item && PendingGateWasWeapon) || result != 0;
		PendingGateItem = nullptr;
		PendingGateExcluded = false;
		PendingGateWasWeapon = false;
		if (excluded && wasEligible) {
			ExcludedEligibleItems.fetch_add(1, std::memory_order_relaxed);
			return 0;
		}
	}
	return result;
}

template<std::size_t Size>
bool Preflight(
	std::uintptr_t rva,
	const std::array<std::uint8_t, Size>& expected,
	const char* label
) noexcept {
	if (Context->CheckExpectedBytes(
			rva,
			expected.data(),
			static_cast<std::uint32_t>(expected.size())
		)) {
		return true;
	}
	const auto message = std::string("plugin-items: RuffnecKk ethereal ") + label
		+ " signature mismatch; plugin refused before mutation.";
	Context->LogError(message.c_str());
	return false;
}

bool PreflightEnabledSites() noexcept {
	bool valid = true;
	if (HasExcludedItemTypes(Settings)) {
		valid = Preflight(CheckItemTypeRva, ExpectedCheckItemType, "item-type hook") && valid;
		valid = Preflight(GetItemContextRva, ExpectedGetItemContext, "item context helper")
			&& valid;
		valid = Preflight(GetDataTablesRva, ExpectedGetDataTables, "data tables helper")
			&& valid;
	}
	if (PatchChance(Settings)) {
		valid = Preflight(EtherealChanceRva, ExpectedEtherealChance, "chance byte") && valid;
	}
	if (PatchSetItems(Settings)) {
		valid = Preflight(SetQualityBranchRva, ExpectedSetQualityBranch, "set-quality branch")
			&& valid;
	}
	if (PatchIndestructibleItems(Settings)) {
		valid = Preflight(
			DurabilityEligibilityCallRva,
			ExpectedDurabilityEligibilityCall,
			"durability eligibility call"
		) && valid;
		valid = Preflight(
			IndestructibleHelperCaveRva,
			ExpectedIndestructibleHelperCave,
			"indestructible helper cave"
		) && valid;
	}
	return valid;
}

bool InstallExclusionHook() noexcept {
	if (!HasExcludedItemTypes(Settings)) return true;
	GetItemContext = At<GetItemContextFn>(GetItemContextRva);
	GetDataTables = At<GetDataTablesFn>(GetDataTablesRva);
	if (!PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.etherealItemRules.itemTypeCheck"),
			CheckItemTypeRva,
			ExpectedCheckItemType.data(),
			static_cast<std::uint32_t>(ExpectedCheckItemType.size()),
			HookCheckItemType,
			&OriginalCheckItemType
		)) {
		Context->LogError("plugin-items: RuffnecKk ethereal item-type hook failed.");
		return false;
	}
	return true;
}

bool InstallRulePatches() noexcept {
	if (PatchIndestructibleItems(Settings)) {
		if (!PSh_ManifestPatchBytes(Context, PSH_MANIFEST_SITE("items.etherealItemRules.indestructibleHelper"),
				IndestructibleHelperCaveRva,
				ExpectedIndestructibleHelperCave.data(),
				static_cast<std::uint32_t>(ExpectedIndestructibleHelperCave.size()),
				IndestructibleHelper.data(),
				static_cast<std::uint32_t>(IndestructibleHelper.size())
			)) {
			Context->LogError("plugin-items: RuffnecKk ethereal helper patch failed.");
			return false;
		}
		if (!PSh_ManifestPatchCallRel32(Context, PSH_MANIFEST_SITE("items.etherealItemRules.durabilityEligibilityCall"),
				DurabilityEligibilityCallRva,
				ExpectedDurabilityEligibilityCall.data(),
				static_cast<std::uint32_t>(ExpectedDurabilityEligibilityCall.size()),
				IndestructibleHelperCaveRva
			)) {
			Context->LogError("plugin-items: RuffnecKk ethereal durability call patch failed.");
			return false;
		}
	}
	if (PatchSetItems(Settings)
		&& !PSh_ManifestPatchNop(Context, PSH_MANIFEST_SITE("items.etherealItemRules.setEligibility"),
			SetQualityBranchRva,
			ExpectedSetQualityBranch.data(),
			static_cast<std::uint32_t>(ExpectedSetQualityBranch.size()),
			static_cast<std::uint32_t>(ExpectedSetQualityBranch.size())
		)) {
		Context->LogError("plugin-items: RuffnecKk ethereal set-quality patch failed.");
		return false;
	}
	if (PatchChance(Settings)
		&& !PSh_ManifestPatchWriteU8(Context, PSH_MANIFEST_SITE("items.etherealItemRules.chance"),
			EtherealChanceRva,
			ExpectedEtherealChance.data(),
			static_cast<std::uint32_t>(ExpectedEtherealChance.size()),
			Settings.chancePercent
		)) {
		Context->LogError("plugin-items: RuffnecKk ethereal chance patch failed.");
		return false;
	}
	return true;
}
} // namespace

bool ItemsEthereal_Install(
	const D2RL::PluginContext* context,
	const nlohmann::json& itemsConfig
) noexcept {
	if (!context || context->exeBase == 0) return false;
	Context = context;
	Base = reinterpret_cast<std::uint8_t*>(context->exeBase);

	try {
		Settings = ParseConfig(itemsConfig);
	} catch (const std::exception& exception) {
		const auto message = std::string("plugin-items: invalid RuffnecKk ethereal configuration (")
			+ exception.what() + ").";
		context->LogError(message.c_str());
		return false;
	}

	if (!PreflightEnabledSites()) return false;
	if (!InstallExclusionHook() || !InstallRulePatches()) return false;

	if (Settings.enabled) {
		context->LogInfo(
			"plugin-items: RuffnecKk EthItemRules is configured from one block."
		);
	}
	return true;
}

void ItemsEthereal_Reset() noexcept {
	Context = nullptr;
	Base = nullptr;
	Settings = {};
	OriginalCheckItemType = nullptr;
	GetItemContext = nullptr;
	GetDataTables = nullptr;
	ExcludedEligibleItems.store(0, std::memory_order_relaxed);
	ResolvedTypeCount.store(0, std::memory_order_relaxed);
	UnresolvedTypeCount.store(0, std::memory_order_relaxed);
	UnresolvedWarningLogged.clear(std::memory_order_relaxed);
	TypeCache = {};
	PendingGateItem = nullptr;
	PendingGateExcluded = false;
	PendingGateWasWeapon = false;
}
