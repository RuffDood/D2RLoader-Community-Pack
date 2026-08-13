#include "charm-aura-trigger-fix.h"
#include <plugin-shared.h>
#include "charm-aura-trigger-fix-policy.h"

#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

namespace RuffnecKk::CharmAuraTriggerFix {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t ActTransitionRva = 0x502D00;
constexpr std::uintptr_t ZoneTransitionCallRva = 0x486AE0;
constexpr std::uintptr_t ZoneTransitionReturnRva = 0x486AE5;
constexpr std::uintptr_t AttachSoundRva = 0x491960;
constexpr std::uintptr_t CorpseRecoveryReturnRva = 0x4B35A6;
constexpr std::uint16_t CorpseRecoverySoundId = 0x64;
constexpr std::uintptr_t PlayerModeFinalizeRva = 0x42D2C0;
constexpr std::uintptr_t TownRespawnReturnRva = 0x4B6650;
constexpr std::uintptr_t GetInventoryRva = 0x34A360;
constexpr std::uintptr_t GetItemDataRva = 0x34A500;
constexpr std::uintptr_t GetStatListRva = 0x34B870;
constexpr std::uintptr_t CheckItemTypeRva = 0x373890;
constexpr std::uintptr_t GetFirstItemRva = 0x388C10;
constexpr std::uintptr_t GetNextItemRva = 0x38ABA0;
constexpr std::uintptr_t MergeStatListsRva = 0x2F81A0;
constexpr std::uintptr_t RefreshPlayerItemsRva = 0x46F220;
constexpr std::uintptr_t GetSkillListRva = 0x34B6E0;
constexpr std::uintptr_t SetLeftActiveSkillRva = 0x33EC70;
constexpr std::uintptr_t SetRightActiveSkillRva = 0x33EF10;
constexpr std::size_t ItemFlagsOffset = 0x18;
constexpr std::size_t InventoryNodePositionOffset = 0xB8;
constexpr std::size_t StatListOwnerOffset = 0;
constexpr std::size_t StatListBaseStatsOffset = 0x30;
constexpr std::size_t SkillListLeftSkillOffset = 0x08;
constexpr std::size_t SkillListRightSkillOffset = 0x10;
constexpr std::size_t SkillRecordOffset = 0;
constexpr std::size_t SkillOwnerGuidOffset = 0x4C;
constexpr std::size_t SkillsTxtSkillIdOffset = 0;
constexpr std::size_t MaximumTraversedItems = 4096;
constexpr std::size_t MaximumStatsPerCharm = 4096;

constexpr std::array<std::uint8_t, 15> ExpectedActTransitionHook{
	0x48, 0x89, 0x5C, 0x24, 0x08,
	0x48, 0x89, 0x6C, 0x24, 0x10,
	0x48, 0x89, 0x74, 0x24, 0x18
};
constexpr std::array<std::uint8_t, 31> ExpectedActTransitionSignature{
	0x48, 0x89, 0x5C, 0x24, 0x08,
	0x48, 0x89, 0x6C, 0x24, 0x10,
	0x48, 0x89, 0x74, 0x24, 0x18,
	0x57, 0x48, 0x83, 0xEC, 0x30,
	0x33, 0xED, 0x44, 0x89, 0x44, 0x24, 0x20,
	0x89, 0x6C, 0x24, 0x24
};
constexpr std::array<std::uint8_t, 5> ExpectedZoneTransitionCall{
	0xE8, 0x1B, 0xC2, 0x07, 0x00
};
constexpr std::array<std::uint8_t, 15> ExpectedAttachSoundHook{
	0x48, 0x89, 0x5C, 0x24, 0x08,
	0x48, 0x89, 0x74, 0x24, 0x18,
	0x57, 0x48, 0x83, 0xEC, 0x20
};
constexpr std::array<std::uint8_t, 18> ExpectedCorpseRecoveryCall{
	0xBA, 0x64, 0x00, 0x00, 0x00,
	0x48, 0x8B, 0x4C, 0x24, 0x68,
	0x4C, 0x8B, 0xC1,
	0xE8, 0xBA, 0xE3, 0xFD, 0xFF
};
constexpr std::array<std::uint8_t, 16> ExpectedPlayerModeFinalizeHook{
	0x48, 0x89, 0x5C, 0x24, 0x20,
	0x4C, 0x89, 0x44, 0x24, 0x18,
	0x55, 0x56, 0x57, 0x48, 0x83, 0xEC
};
constexpr std::array<std::uint8_t, 43> ExpectedTownRespawnCall{
	0x44, 0x89, 0x7C, 0x24, 0x38,
	0x41, 0xB9, 0x01, 0x00, 0x00, 0x00,
	0xC7, 0x44, 0x24, 0x30, 0x01, 0x00, 0x00, 0x00,
	0x45, 0x33, 0xC0,
	0x44, 0x89, 0x7C, 0x24, 0x28,
	0x49, 0x8B, 0xD5,
	0x49, 0x8B, 0xCE,
	0x44, 0x89, 0x7C, 0x24, 0x20,
	0xE8, 0x70, 0x6C, 0xF7, 0xFF
};

struct RuntimeStatArray {
	const PackedStatRecord* records{};
	std::uint64_t count{};
};

struct ActiveSkillIdentity {
	std::int32_t skillId{};
	std::int32_t ownerGuid{-1};
	bool valid{};
};

struct ActiveSkillSnapshot {
	ActiveSkillIdentity left{};
	ActiveSkillIdentity right{};
};

using ActTransitionFn = void(__fastcall*)(
	void*, void*, std::int32_t, std::int32_t) noexcept;
using AttachSoundFn = void(__fastcall*)(void*, std::uint16_t, void*) noexcept;
using PlayerModeFinalizeFn = void(__fastcall*)(
	void*, void*, void*, std::int32_t, std::int32_t, std::int32_t,
	std::int32_t) noexcept;
using GetInventoryFn = void*(__fastcall*)(
	void*, const char*, std::int32_t) noexcept;
using GetItemDataFn = std::uint8_t*(__fastcall*)(void*) noexcept;
using GetStatListFn = std::uint8_t*(__fastcall*)(void*) noexcept;
using CheckItemTypeFn = std::int32_t(__fastcall*)(void*, std::int32_t) noexcept;
using GetFirstItemFn = void*(__fastcall*)(void*) noexcept;
using GetNextItemFn = void*(__fastcall*)(void*) noexcept;
using MergeStatListsFn = void(__fastcall*)(void*, void*, std::int32_t) noexcept;
using RefreshPlayerItemsFn = void(__fastcall*)(void*, void*) noexcept;
using GetSkillListFn = std::uint8_t*(__fastcall*)(void*) noexcept;
using SetActiveSkillFn = void(__fastcall*)(
	void*, std::int32_t, std::int32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
Config Settings{};
ActTransitionFn OriginalActTransition{};
AttachSoundFn OriginalAttachSound{};
PlayerModeFinalizeFn OriginalPlayerModeFinalize{};
GetInventoryFn GetInventory{};
GetItemDataFn GetItemData{};
GetStatListFn GetStatList{};
CheckItemTypeFn CheckItemType{};
GetFirstItemFn GetFirstItem{};
GetNextItemFn GetNextItem{};
MergeStatListsFn MergeStatLists{};
RefreshPlayerItemsFn RefreshPlayerItems{};
GetSkillListFn GetSkillList{};
SetActiveSkillFn SetLeftActiveSkill{};
SetActiveSkillFn SetRightActiveSkill{};
std::atomic<std::uint64_t> ZoneTransitions{};
std::atomic<std::uint64_t> CorpseRecoveries{};
std::atomic<std::uint64_t> NativeCorpseRefreshes{};
std::atomic<std::uint64_t> TownRespawns{};
std::atomic<std::uint64_t> NativeTownRespawnRefreshes{};
std::atomic<std::uint64_t> ScannedItems{};
std::atomic<std::uint64_t> RefreshedCharms{};
std::atomic<std::uint64_t> SkippedWithoutAura{};
std::atomic<std::uint64_t> RestoredActiveSkills{};
std::atomic<std::uint64_t> FailedActiveSkillRestores{};
std::atomic<std::uint64_t> InvalidStatArrays{};
std::atomic<std::uint64_t> RefreshCapHits{};
std::atomic<std::uint64_t> TraversalGuardHits{};

template<class T>
T At(std::uintptr_t rva) noexcept {
	return reinterpret_cast<T>(Base + rva);
}

void ResetTelemetry() noexcept {
	ZoneTransitions.store(0, std::memory_order_relaxed);
	CorpseRecoveries.store(0, std::memory_order_relaxed);
	NativeCorpseRefreshes.store(0, std::memory_order_relaxed);
	TownRespawns.store(0, std::memory_order_relaxed);
	NativeTownRespawnRefreshes.store(0, std::memory_order_relaxed);
	ScannedItems.store(0, std::memory_order_relaxed);
	RefreshedCharms.store(0, std::memory_order_relaxed);
	SkippedWithoutAura.store(0, std::memory_order_relaxed);
	RestoredActiveSkills.store(0, std::memory_order_relaxed);
	FailedActiveSkillRestores.store(0, std::memory_order_relaxed);
	InvalidStatArrays.store(0, std::memory_order_relaxed);
	RefreshCapHits.store(0, std::memory_order_relaxed);
	TraversalGuardHits.store(0, std::memory_order_relaxed);
}

ActiveSkillIdentity CaptureActiveSkill(const std::uint8_t* skill) noexcept {
	if (!skill) return {};
	const auto* record = *reinterpret_cast<const std::uint8_t* const*>(
		skill + SkillRecordOffset);
	if (!record) return {};
	return {
		static_cast<std::int32_t>(*reinterpret_cast<const std::int16_t*>(
			record + SkillsTxtSkillIdOffset)),
		*reinterpret_cast<const std::int32_t*>(skill + SkillOwnerGuidOffset),
		true
	};
}

ActiveSkillSnapshot CaptureActiveSkills(void* player) noexcept {
	const auto* skillList = GetSkillList(player);
	if (!skillList) return {};
	const auto* left = *reinterpret_cast<const std::uint8_t* const*>(
		skillList + SkillListLeftSkillOffset);
	const auto* right = *reinterpret_cast<const std::uint8_t* const*>(
		skillList + SkillListRightSkillOffset);
	return {CaptureActiveSkill(left), CaptureActiveSkill(right)};
}

bool MatchesActiveSkill(
	const ActiveSkillIdentity& expected,
	const ActiveSkillIdentity& actual
) noexcept {
	return expected.valid == actual.valid
		&& (!expected.valid
			|| (expected.skillId == actual.skillId
				&& expected.ownerGuid == actual.ownerGuid));
}

void RestoreActiveSkill(
	void* player,
	const ActiveSkillIdentity& identity,
	SetActiveSkillFn setter,
	std::size_t slotOffset
) noexcept {
	if (!identity.valid) return;
	setter(player, identity.skillId, identity.ownerGuid);

	const auto* skillList = GetSkillList(player);
	const auto* skill = skillList
		? *reinterpret_cast<const std::uint8_t* const*>(skillList + slotOffset)
		: nullptr;
	if (MatchesActiveSkill(identity, CaptureActiveSkill(skill))) {
		RestoredActiveSkills.fetch_add(1, std::memory_order_relaxed);
	} else {
		FailedActiveSkillRestores.fetch_add(1, std::memory_order_relaxed);
	}
}

void RefreshCharmAuras(void* player) noexcept {
	if (!player) return;

	auto* inventory = GetInventory(player, "CharmAuraTriggerFix", __LINE__);
	if (!inventory) return;

	std::array<void*, MaximumRefreshedCharms> charms{};
	std::size_t charmCount{};
	std::size_t traversed{};
	for (auto* item = GetFirstItem(inventory);
		item && traversed < MaximumTraversedItems;
		item = GetNextItem(item)) {
		++traversed;
		auto* itemData = GetItemData(item);
		if (!itemData) continue;

		const auto itemFlags = *reinterpret_cast<const std::uint32_t*>(
			itemData + ItemFlagsOffset);
		const auto nodePosition = itemData[InventoryNodePositionOffset];
		const bool matchesCharm = CheckItemType(item, CharmItemTypeId) != 0;
		if (!IsEligible(matchesCharm, nodePosition, itemFlags)) continue;

		const auto* statList = GetStatList(item);
		if (!statList) continue;
		const auto* stats = reinterpret_cast<const RuntimeStatArray*>(
			statList + StatListBaseStatsOffset);
		if (!stats->records || stats->count > MaximumStatsPerCharm) {
			InvalidStatArrays.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		if (!HasNonzeroStat(
			stats->records,
			static_cast<std::size_t>(stats->count),
			ItemAuraStatId)) {
			SkippedWithoutAura.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		if (charmCount < charms.size()) {
			charms[charmCount++] = item;
		} else {
			RefreshCapHits.fetch_add(1, std::memory_order_relaxed);
		}
	}

	ScannedItems.fetch_add(traversed, std::memory_order_relaxed);
	if (traversed == MaximumTraversedItems) {
		TraversalGuardHits.fetch_add(1, std::memory_order_relaxed);
	}
	if (charmCount == 0) return;

	const auto activeSkills = CaptureActiveSkills(player);
	for (std::size_t index = 0; index < charmCount; ++index) {
		auto* item = charms[index];
		if (auto* statList = GetStatList(item)) {
			*reinterpret_cast<void**>(statList + StatListOwnerOffset) = nullptr;
		}
		MergeStatLists(player, item, 1);
	}
	RestoreActiveSkill(
		player, activeSkills.left, SetLeftActiveSkill, SkillListLeftSkillOffset);
	RestoreActiveSkill(
		player, activeSkills.right, SetRightActiveSkill, SkillListRightSkillOffset);
	RefreshedCharms.fetch_add(charmCount, std::memory_order_relaxed);
}

__declspec(noinline) void __fastcall HookAttachSound(
	void* unit,
	std::uint16_t soundId,
	void* source
) noexcept {
	const auto returnRva = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - Base;
	OriginalAttachSound(unit, soundId, source);
	if (returnRva != CorpseRecoveryReturnRva
		|| soundId != CorpseRecoverySoundId
		|| unit != source) {
		return;
	}

	CorpseRecoveries.fetch_add(1, std::memory_order_relaxed);
	RefreshPlayerItems(nullptr, unit);
	NativeCorpseRefreshes.fetch_add(1, std::memory_order_relaxed);
}

__declspec(noinline) void __fastcall HookActTransition(
	void* game,
	void* player,
	std::int32_t previousAct,
	std::int32_t nextAct
) noexcept {
	const auto returnRva = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - Base;
	OriginalActTransition(game, player, previousAct, nextAct);
	if (returnRva != ZoneTransitionReturnRva) return;

	ZoneTransitions.fetch_add(1, std::memory_order_relaxed);
	RefreshCharmAuras(player);
}

__declspec(noinline) void __fastcall HookPlayerModeFinalize(
	void* game,
	void* player,
	void* usedSkill,
	std::int32_t mode,
	std::int32_t x,
	std::int32_t y,
	std::int32_t finalFlag
) noexcept {
	const auto returnRva = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - Base;
	OriginalPlayerModeFinalize(
		game, player, usedSkill, mode, x, y, finalFlag);
	if (returnRva != TownRespawnReturnRva || !game || !player) return;

	TownRespawns.fetch_add(1, std::memory_order_relaxed);
	RefreshPlayerItems(game, player);
	NativeTownRespawnRefreshes.fetch_add(1, std::memory_order_relaxed);
}

D2RL::ConsoleCommandResult Status(
	D2R::Game::Client*,
	const D2RL::ConsoleCommandContext* command,
	void*
) noexcept {
	if (!command || !command->plugin) {
		return D2RL::ConsoleCommandResult::Failed;
	}

	char message[768]{};
	std::snprintf(
		message,
		sizeof(message),
		"Charm Aura Trigger Fix 1.6.0 (plugin-items): enabled=%s; transitions=%llu; "
		"corpse recoveries=%llu; native corpse refreshes=%llu; town respawns=%llu; "
		"native town refreshes=%llu; items scanned=%llu; aura charms refreshed=%llu; "
		"non-aura charms skipped=%llu; active skills restored=%llu; active skill "
		"restore failures=%llu; invalid stat arrays=%llu; over-cap charms=%llu; "
		"traversal guards=%llu.",
		Settings.enabled ? "true" : "false",
		static_cast<unsigned long long>(
			ZoneTransitions.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			CorpseRecoveries.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			NativeCorpseRefreshes.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			TownRespawns.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			NativeTownRespawnRefreshes.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			ScannedItems.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			RefreshedCharms.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			SkippedWithoutAura.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			RestoredActiveSkills.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			FailedActiveSkillRestores.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			InvalidStatArrays.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			RefreshCapHits.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			TraversalGuardHits.load(std::memory_order_relaxed)));
	command->plugin->WriteConsoleMessage(message);
	return D2RL::ConsoleCommandResult::Handled;
}

bool Check(
	std::uintptr_t rva,
	const std::uint8_t* expected,
	std::size_t size,
	const char* label
) noexcept {
	if (Context->CheckExpectedBytes(
		rva, expected, static_cast<std::uint32_t>(size))) {
		return true;
	}
	const auto message = std::string(
		"plugin-items: Charm Aura Trigger Fix ") + label
		+ " signature mismatch.";
	Context->LogError(message.c_str());
	return false;
}

bool ValidateSkillRuntime() noexcept {
	constexpr std::array<std::uint8_t, 14> getSkillListExpected{
		0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48,
		0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x20
	};
	constexpr std::array<std::uint8_t, 20> setActiveSkillExpected{
		0x48, 0x89, 0x5C, 0x24, 0x08,
		0x48, 0x89, 0x6C, 0x24, 0x10,
		0x48, 0x89, 0x74, 0x24, 0x18,
		0x57, 0x48, 0x83, 0xEC, 0x20
	};
	constexpr std::array<std::uint8_t, 4> leftSlotStoreExpected{
		0x48, 0x89, 0x58, 0x08
	};
	constexpr std::array<std::uint8_t, 4> rightSlotStoreExpected{
		0x48, 0x89, 0x58, 0x10
	};
	return Check(
			GetSkillListRva,
			getSkillListExpected.data(),
			getSkillListExpected.size(),
			"skill-list")
		&& Check(
			SetLeftActiveSkillRva,
			setActiveSkillExpected.data(),
			setActiveSkillExpected.size(),
			"left active-skill")
		&& Check(
			SetLeftActiveSkillRva + 0x82,
			leftSlotStoreExpected.data(),
			leftSlotStoreExpected.size(),
			"left skill-slot")
		&& Check(
			SetRightActiveSkillRva,
			setActiveSkillExpected.data(),
			setActiveSkillExpected.size(),
			"right active-skill")
		&& Check(
			SetRightActiveSkillRva + 0x82,
			rightSlotStoreExpected.data(),
			rightSlotStoreExpected.size(),
			"right skill-slot");
}

bool ValidateNativeRefresh() noexcept {
	constexpr std::array<std::uint8_t, 14> refreshPlayerItemsExpected{
		0x48, 0x85, 0xD2,
		0x0F, 0x84, 0xD9, 0x00, 0x00, 0x00,
		0x56, 0x48, 0x83, 0xEC, 0x50
	};
	constexpr std::array<std::uint8_t, 45> expireAndMergeExpected{
		0x48, 0x8D, 0x54, 0x24, 0x68,
		0x48, 0x8B, 0xCB,
		0xE8, 0x67, 0x8E, 0xE8, 0xFF,
		0x48, 0x85, 0xC0,
		0x74, 0x1B,
		0x48, 0x8B, 0xD3,
		0x48, 0x8B, 0xCE,
		0xE8, 0xC7, 0x8F, 0xE8, 0xFF,
		0x44, 0x8B, 0x44, 0x24, 0x68,
		0x48, 0x8B, 0xD3,
		0x48, 0x8B, 0xCE,
		0xE8, 0xC7, 0x8E, 0xE8, 0xFF
	};
	return Check(
			RefreshPlayerItemsRva,
			refreshPlayerItemsExpected.data(),
			refreshPlayerItemsExpected.size(),
			"native item-refresh")
		&& Check(
			RefreshPlayerItemsRva + 0x8C,
			expireAndMergeExpected.data(),
			expireAndMergeExpected.size(),
			"native expire/merge");
}

bool InstallHooks() noexcept {
	if (!Check(
			ActTransitionRva,
			ExpectedActTransitionSignature.data(),
			ExpectedActTransitionSignature.size(),
			"act-transition")
		|| !Check(
			ZoneTransitionCallRva,
			ExpectedZoneTransitionCall.data(),
			ExpectedZoneTransitionCall.size(),
			"zone-transition call site")
		|| !Check(
			AttachSoundRva,
			ExpectedAttachSoundHook.data(),
			ExpectedAttachSoundHook.size(),
			"attach-sound")
		|| !Check(
			CorpseRecoveryReturnRva - ExpectedCorpseRecoveryCall.size(),
			ExpectedCorpseRecoveryCall.data(),
			ExpectedCorpseRecoveryCall.size(),
			"corpse-recovery call site")
		|| !Check(
			PlayerModeFinalizeRva,
			ExpectedPlayerModeFinalizeHook.data(),
			ExpectedPlayerModeFinalizeHook.size(),
			"player-mode")
		|| !Check(
			TownRespawnReturnRva - ExpectedTownRespawnCall.size(),
			ExpectedTownRespawnCall.data(),
			ExpectedTownRespawnCall.size(),
			"town-respawn call site")) {
		return false;
	}

	if (!PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.charmAuraTriggerFix.actTransition"),
		ActTransitionRva,
		ExpectedActTransitionHook.data(),
		static_cast<std::uint32_t>(ExpectedActTransitionHook.size()),
		HookActTransition,
		&OriginalActTransition)) {
		Context->LogError(
			"plugin-items: Charm Aura Trigger Fix act-transition hook failed.");
		return false;
	}
	if (!PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.charmAuraTriggerFix.attachSound"),
		AttachSoundRva,
		ExpectedAttachSoundHook.data(),
		static_cast<std::uint32_t>(ExpectedAttachSoundHook.size()),
		HookAttachSound,
		&OriginalAttachSound)) {
		Context->LogError(
			"plugin-items: Charm Aura Trigger Fix corpse-recovery hook failed.");
		return false;
	}
	if (!PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.charmAuraTriggerFix.playerModeFinalize"),
		PlayerModeFinalizeRva,
		ExpectedPlayerModeFinalizeHook.data(),
		static_cast<std::uint32_t>(ExpectedPlayerModeFinalizeHook.size()),
		HookPlayerModeFinalize,
		&OriginalPlayerModeFinalize)) {
		Context->LogError(
			"plugin-items: Charm Aura Trigger Fix town-respawn hook failed.");
		return false;
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
	try {
		Settings = ParseConfig(itemsConfig);
	} catch (const std::exception& exception) {
		const auto message = std::string(
			"plugin-items: invalid items.charmAuraTriggerFix (")
			+ exception.what() + ").";
		context->LogError(message.c_str());
		return false;
	}

	if (context->modDataVersionBuild != 0
		&& context->modDataVersionBuild != SupportedBuild) {
		context->LogError(
			"plugin-items: Charm Aura Trigger Fix supports only D2R build 92777.");
		return false;
	}

	if (Settings.enabled) {
		GetInventory = At<GetInventoryFn>(GetInventoryRva);
		GetItemData = At<GetItemDataFn>(GetItemDataRva);
		GetStatList = At<GetStatListFn>(GetStatListRva);
		CheckItemType = At<CheckItemTypeFn>(CheckItemTypeRva);
		GetFirstItem = At<GetFirstItemFn>(GetFirstItemRva);
		GetNextItem = At<GetNextItemFn>(GetNextItemRva);
		MergeStatLists = At<MergeStatListsFn>(MergeStatListsRva);
		RefreshPlayerItems = At<RefreshPlayerItemsFn>(RefreshPlayerItemsRva);
		GetSkillList = At<GetSkillListFn>(GetSkillListRva);
		SetLeftActiveSkill = At<SetActiveSkillFn>(SetLeftActiveSkillRva);
		SetRightActiveSkill = At<SetActiveSkillFn>(SetRightActiveSkillRva);
		if (!ValidateSkillRuntime() || !ValidateNativeRefresh() || !InstallHooks()) {
			return false;
		}
	}

	if (!PSh_RegisterConsoleCommand(context,
		"charm-aura-trigger-fix",
		Status,
		"Show charm-aura trigger-fix status and refresh counters.")) {
		context->LogWarn(
			"plugin-items: Charm Aura Trigger Fix status command was not registered.");
	}

	char message[256]{};
	std::snprintf(
		message,
		sizeof(message),
		"plugin-items: Charm Aura Trigger Fix 1.6.0 by RuffnecKk loaded: "
		"enabled=%s; config=items.charmAuraTriggerFix.",
		Settings.enabled ? "true" : "false");
	context->LogInfo(message);
	return true;
}

void Unload() noexcept {
	OriginalPlayerModeFinalize = nullptr;
	OriginalAttachSound = nullptr;
	OriginalActTransition = nullptr;
	SetRightActiveSkill = nullptr;
	SetLeftActiveSkill = nullptr;
	GetSkillList = nullptr;
	RefreshPlayerItems = nullptr;
	MergeStatLists = nullptr;
	GetNextItem = nullptr;
	GetFirstItem = nullptr;
	CheckItemType = nullptr;
	GetStatList = nullptr;
	GetItemData = nullptr;
	GetInventory = nullptr;
	Settings = {};
	ResetTelemetry();
	Base = 0;
	Context = nullptr;
}

} // namespace RuffnecKk::CharmAuraTriggerFix
