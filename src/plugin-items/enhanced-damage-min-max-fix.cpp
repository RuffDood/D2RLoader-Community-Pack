#include "enhanced-damage-min-max-fix.h"
#include <plugin-shared.h>
#include "enhanced-damage-min-max-fix-policy.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

namespace RuffnecKk::EnhancedDamageMinMaxFix {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t EvaluateAndUpdateStatRva = 0x2FA430;
constexpr std::uintptr_t GetTotalStatRva = 0x2F9B10;
constexpr std::uintptr_t UpdateUnitStatRva = 0x2F9DB0;
constexpr std::uintptr_t GetUnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t CheckItemTypeRva = 0x373890;

constexpr std::size_t StatListUnitOffset = 0x00;
constexpr std::size_t StatListOwnerTypeOffset = 0x08;
constexpr std::size_t StatListOwnerOffset = 0xA0;
constexpr std::size_t ItemStatCostOperationOffset = 0x50;

constexpr std::array<std::uint8_t, 15> ExpectedEvaluateAndUpdateStat{
	0x4C, 0x89, 0x4C, 0x24, 0x20,
	0x4C, 0x89, 0x44, 0x24, 0x18,
	0x89, 0x54, 0x24, 0x10,
	0x53
};

using EvaluateAndUpdateStatFn = std::int32_t(__fastcall*)(
	void*, std::int32_t, void*, void*) noexcept;
using GetTotalStatFn = std::int32_t(__fastcall*)(
	void*, std::int32_t, void*) noexcept;
using UpdateUnitStatFn = void(__fastcall*)(
	void*, std::int32_t, std::int32_t, void*, void*) noexcept;
using GetUnitTypeFn = std::int32_t(__fastcall*)(const void*) noexcept;
using CheckItemTypeFn = std::int32_t(__fastcall*)(const void*, std::int32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
Policy Settings{};
EvaluateAndUpdateStatFn OriginalEvaluateAndUpdateStat{};
GetTotalStatFn GetTotalStat{};
UpdateUnitStatFn UpdateUnitStat{};
GetUnitTypeFn GetUnitType{};
CheckItemTypeFn CheckItemType{};
thread_local bool CorrectionWriteActive{};

std::atomic<std::uint64_t> RestoredUpdates{};
std::atomic<std::uint64_t> RestoredMaximumComponents{};
std::atomic<std::uint64_t> RestoredMinimumComponents{};
std::atomic<std::uint64_t> WeaponUpdatesLeftVanilla{};
std::atomic<std::uint64_t> PostWriteVerificationFailures{};
std::atomic<bool> FirstCorrectionLogged{};

template<class T>
T At(std::uintptr_t rva) noexcept {
	return reinterpret_cast<T>(Base + rva);
}

template<class T>
T ReadAt(const void* address, std::size_t offset) noexcept {
	return *reinterpret_cast<const T*>(
		static_cast<const std::uint8_t*>(address) + offset);
}

struct CorrectionWriteScope {
	CorrectionWriteScope() noexcept { CorrectionWriteActive = true; }
	~CorrectionWriteScope() { CorrectionWriteActive = false; }
};

void ResetTelemetry() noexcept {
	RestoredUpdates.store(0, std::memory_order_relaxed);
	RestoredMaximumComponents.store(0, std::memory_order_relaxed);
	RestoredMinimumComponents.store(0, std::memory_order_relaxed);
	WeaponUpdatesLeftVanilla.store(0, std::memory_order_relaxed);
	PostWriteVerificationFailures.store(0, std::memory_order_relaxed);
	FirstCorrectionLogged.store(false, std::memory_order_relaxed);
}

void* ResolveEffectiveItem(const void* statList) noexcept {
	auto* activeUnit = ReadAt<void*>(statList, StatListUnitOffset);
	if (activeUnit && GetUnitType(activeUnit) == ItemUnitType) return activeUnit;
	auto* originalOwner = ReadAt<void*>(statList, StatListOwnerOffset);
	if (originalOwner && GetUnitType(originalOwner) == ItemUnitType) return originalOwner;
	return nullptr;
}

void LogFirstCorrection(
	std::int32_t packedStat,
	std::int32_t retainedValue,
	std::int32_t evaluatedValue
) noexcept {
	if (!Context || FirstCorrectionLogged.exchange(true, std::memory_order_relaxed)) return;
	char message[320]{};
	std::snprintf(
		message,
		sizeof(message),
		"plugin-items: Enhanced Damage Min/Max Fix 1.2.0 by RuffnecKk applied its "
		"first off-weapon repair (stat=%u, retained=%d, evaluated=%d).",
		static_cast<unsigned>(static_cast<std::uint32_t>(packedStat) >> 16U),
		retainedValue,
		evaluatedValue);
	Context->LogInfo(message);
}

std::int32_t __fastcall HookEvaluateAndUpdateStat(
	void* statList,
	std::int32_t packedStat,
	void* itemStatCost,
	void* callbackUnit
) noexcept {
	const auto evaluatedValue = OriginalEvaluateAndUpdateStat(
		statList, packedStat, itemStatCost, callbackUnit);
	if (CorrectionWriteActive || !statList || !itemStatCost
		|| !IsEnhancedDamagePackedStat(packedStat)) {
		return evaluatedValue;
	}

	const auto ownerType = ReadAt<std::int32_t>(statList, StatListOwnerTypeOffset);
	const auto operation = ReadAt<std::uint8_t>(
		itemStatCost, ItemStatCostOperationOffset);
	if (ownerType != ItemUnitType) return evaluatedValue;

	auto* effectiveItem = ResolveEffectiveItem(statList);
	if (!effectiveItem) return evaluatedValue;
	const bool effectiveItemIsWeapon =
		CheckItemType(effectiveItem, WeaponItemTypeId) != 0;
	if (effectiveItemIsWeapon) {
		WeaponUpdatesLeftVanilla.fetch_add(1, std::memory_order_relaxed);
		return evaluatedValue;
	}

	const auto retainedValue = GetTotalStat(statList, packedStat, itemStatCost);
	if (!ShouldRestoreSuppressedUpdate(
			ownerType,
			operation,
			packedStat,
			effectiveItemIsWeapon,
			evaluatedValue,
			retainedValue)) {
		return evaluatedValue;
	}

	{
		const CorrectionWriteScope scope;
		UpdateUnitStat(
			statList, packedStat, evaluatedValue, itemStatCost, callbackUnit);
	}
	const auto repairedValue = GetTotalStat(statList, packedStat, itemStatCost);
	if (repairedValue != evaluatedValue) {
		PostWriteVerificationFailures.fetch_add(1, std::memory_order_relaxed);
		return evaluatedValue;
	}

	RestoredUpdates.fetch_add(1, std::memory_order_relaxed);
	const auto stat = static_cast<std::uint32_t>(packedStat) >> 16U;
	if (stat == static_cast<std::uint32_t>(ItemMaxDamagePercentStat)) {
		RestoredMaximumComponents.fetch_add(1, std::memory_order_relaxed);
	} else {
		RestoredMinimumComponents.fetch_add(1, std::memory_order_relaxed);
	}
	LogFirstCorrection(packedStat, retainedValue, evaluatedValue);
	return evaluatedValue;
}

D2RL::ConsoleCommandResult Status(
	D2R::Game::Client*,
	const D2RL::ConsoleCommandContext* command,
	void*
) noexcept {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	char message[512]{};
	std::snprintf(
		message,
		sizeof(message),
		"Enhanced Damage Min/Max Fix 1.2.0 (plugin-items): enabled=%s; "
		"restored=%llu; maximum=%llu; minimum=%llu; weapons left vanilla=%llu; "
		"post-write failures=%llu.",
		Settings.enabled ? "true" : "false",
		static_cast<unsigned long long>(RestoredUpdates.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(RestoredMaximumComponents.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(RestoredMinimumComponents.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(WeaponUpdatesLeftVanilla.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(PostWriteVerificationFailures.load(std::memory_order_relaxed)));
	command->plugin->WriteConsoleMessage(message);
	return D2RL::ConsoleCommandResult::Handled;
}

bool InstallHook() noexcept {
	if (!PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.enhancedDamageMinMaxFix.evaluateAndUpdateStat"),
			EvaluateAndUpdateStatRva,
			ExpectedEvaluateAndUpdateStat.data(),
			static_cast<std::uint32_t>(ExpectedEvaluateAndUpdateStat.size()),
			HookEvaluateAndUpdateStat,
			&OriginalEvaluateAndUpdateStat)) {
		Context->LogError(
			"plugin-items: Enhanced Damage Min/Max Fix signature mismatch or hook failed.");
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
			"plugin-items: invalid items.enhancedDamageMinMaxFix (")
			+ exception.what() + ").";
		context->LogError(message.c_str());
		return false;
	}

	if (context->modDataVersionBuild != 0
		&& context->modDataVersionBuild != SupportedBuild) {
		context->LogError(
			"plugin-items: Enhanced Damage Min/Max Fix supports only D2R build 92777.");
		return false;
	}
	if (Settings.enabled) {
		GetTotalStat = At<GetTotalStatFn>(GetTotalStatRva);
		UpdateUnitStat = At<UpdateUnitStatFn>(UpdateUnitStatRva);
		GetUnitType = At<GetUnitTypeFn>(GetUnitTypeRva);
		CheckItemType = At<CheckItemTypeFn>(CheckItemTypeRva);
		if (!InstallHook()) return false;
	}

	if (!PSh_RegisterConsoleCommand(context,
			"enhanced-damage-min-max-fix",
			Status,
			"Show off-weapon Enhanced Damage repair counters.")) {
		context->LogWarn(
			"plugin-items: Enhanced Damage Min/Max Fix status command was not registered.");
	}
	char message[192]{};
	std::snprintf(
		message,
		sizeof(message),
		"plugin-items: Enhanced Damage Min/Max Fix 1.2.0 by RuffnecKk loaded: "
		"enabled=%s; config=items.enhancedDamageMinMaxFix.",
		Settings.enabled ? "true" : "false");
	context->LogInfo(message);
	return true;
}

void Unload() noexcept {
	OriginalEvaluateAndUpdateStat = nullptr;
	CheckItemType = nullptr;
	GetUnitType = nullptr;
	UpdateUnitStat = nullptr;
	GetTotalStat = nullptr;
	Settings = {};
	ResetTelemetry();
	Base = 0;
	Context = nullptr;
}

} // namespace RuffnecKk::EnhancedDamageMinMaxFix
