#include "repair-costs-cap.h"
#include <plugin-shared.h>
#include "repair-costs-cap-policy.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RuffnecKk::RepairCostsCap {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t TransactionCostBodyRva = 0x36F0C0;
constexpr std::uintptr_t RepairAllCostRva = 0x375330;
constexpr std::uintptr_t RepairAllZeroCostBranchSignatureRva = 0x53FF65;
constexpr std::uintptr_t RepairItemRva = 0x53BB50;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t GetUnitBaseStatRva = 0x2F48C0;
constexpr std::uintptr_t GetUnitBaseStatSignatureRva = GetUnitBaseStatRva + 5;
constexpr std::uintptr_t GetMaxDurabilityRva = 0x2F4B60;
constexpr std::uintptr_t GetUnitSeedRva = 0x34A1E0;
constexpr std::uintptr_t RollRandomRva = 0x153B00;
constexpr std::uintptr_t GetClientFromPlayerRva = 0x48FDE0;
constexpr std::uintptr_t SetStatAndNotifyRva = 0x43EB30;
constexpr std::size_t RepairAllBranchDisplacementOffset = 11;
constexpr std::int32_t DurabilityStat = 72;
constexpr std::int32_t MaximumDurabilityStat = 73;

constexpr std::array<std::uint8_t, 29> TransactionCostBodyExpected{
	0x4C, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44,
	0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48,
	0x89, 0x4C, 0x24, 0x08, 0x55, 0x57, 0x41, 0x55,
	0x48, 0x8D, 0x6C, 0x24, 0xC9
};
constexpr std::array<std::uint8_t, 33> RepairAllCostExpected{
	0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
	0x48, 0x81, 0xEC, 0x88, 0x02, 0x00, 0x00, 0x48,
	0x8B, 0x05, 0x82, 0x5F, 0x65, 0x02, 0x48, 0x33,
	0xC4, 0x48, 0x89, 0x84, 0x24, 0x50, 0x02, 0x00,
	0x00
};
constexpr std::array<std::uint8_t, 16> RepairAllZeroCostBranchExpected{
	0x3B, 0xC7, 0x0F, 0x82, 0xAD, 0x00, 0x00, 0x00,
	0x85, 0xFF, 0x74, 0x6F, 0x48, 0x8B, 0x55, 0x48
};
constexpr std::array<std::uint8_t, 32> RepairItemExpected{
	0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74,
	0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
	0x8B, 0xE9, 0x49, 0x8B, 0xF0, 0x48, 0x8B, 0xCA,
	0x48, 0x8B, 0xFA, 0xE8, 0xD0, 0xEF, 0xE2, 0xFF
};
constexpr std::array<std::uint8_t, 16> GetUnitStatExpected{
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
	0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57
};
// DurabilityResistance may own a five-byte jump at the entry point. Sign the
// unique untouched remainder so both features compose without weakening the gate.
constexpr std::array<std::uint8_t, 50> GetUnitBaseStatExpected{
	0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74,
	0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
	0x0F, 0xB7, 0xE8, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
	0x48, 0x85, 0xC9, 0x75, 0x2A, 0x88, 0x4C, 0x24,
	0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x00,
	0xD2, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC,
	0x33, 0xC0
};
constexpr std::array<std::uint8_t, 16> GetMaxDurabilityExpected{
	0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74,
	0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48
};
constexpr std::array<std::uint8_t, 32> GetUnitSeedExpected{
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
	0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C,
	0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
	0x94, 0xBB, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01
};
constexpr std::array<std::uint8_t, 14> RollRandomExpected{
	0x44, 0x8B, 0xCA, 0x48, 0x8B, 0xD1, 0x45,
	0x85, 0xC9, 0x7F, 0x03, 0x33, 0xC0, 0xC3
};
constexpr std::array<std::uint8_t, 16> GetClientFromPlayerExpected{
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
	0xD9, 0x48, 0x85, 0xC9, 0x74, 0x1E, 0xE8, 0xDD
};
constexpr std::array<std::uint8_t, 32> SetStatAndNotifyExpected{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
	0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
	0xEC, 0x30, 0x41, 0x8B, 0xF1, 0x45, 0x0F, 0xB6,
	0xF0, 0x48, 0x8B, 0xEA, 0x48, 0x8B, 0xD9, 0xE8
};

using TransactionCostFn = std::int32_t(*)(
	void*, void*, std::uint32_t, void*, std::int32_t, std::int32_t) noexcept;
using RepairAllCostFn = std::int32_t(*)(
	void*, void*, std::int32_t, std::uint32_t, void*, void*) noexcept;
using RepairItemFn = void(__fastcall*)(void*, void*, void*) noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(void*, std::int32_t, std::uint16_t) noexcept;
using GetMaxDurabilityFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitSeedFn = void*(__fastcall*)(void*) noexcept;
using RollRandomFn = std::uint32_t(__fastcall*)(void*, std::int32_t) noexcept;
using GetClientFromPlayerFn = void*(__fastcall*)(void*) noexcept;
using SetStatAndNotifyFn = void(__fastcall*)(
	void*, void*, bool, std::int32_t, std::int32_t, std::uint16_t) noexcept;

struct TelemetryCounters {
	std::atomic<std::uint64_t> itemEvaluations{};
	std::atomic<std::uint64_t> itemAdjustments{};
	std::atomic<std::uint64_t> itemQuotedGoldReduced{};
	std::atomic<std::uint64_t> repairAllEvaluations{};
	std::atomic<std::uint64_t> repairAllAdjustments{};
	std::atomic<std::uint64_t> repairAllExtraGoldReduced{};
	std::atomic<std::uint64_t> physicalRepairsEvaluated{};
	std::atomic<std::uint64_t> maximumDurabilityLost{};

	void Reset() noexcept {
		itemEvaluations.store(0, std::memory_order_relaxed);
		itemAdjustments.store(0, std::memory_order_relaxed);
		itemQuotedGoldReduced.store(0, std::memory_order_relaxed);
		repairAllEvaluations.store(0, std::memory_order_relaxed);
		repairAllAdjustments.store(0, std::memory_order_relaxed);
		repairAllExtraGoldReduced.store(0, std::memory_order_relaxed);
		physicalRepairsEvaluated.store(0, std::memory_order_relaxed);
		maximumDurabilityLost.store(0, std::memory_order_relaxed);
	}
};

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
RepairPolicy Settings{};
TransactionCostFn OriginalTransactionCost{};
RepairAllCostFn OriginalRepairAllCost{};
RepairItemFn OriginalRepairItem{};
GetUnitStatFn GetUnitStat{};
GetUnitStatFn GetUnitBaseStat{};
GetMaxDurabilityFn GetMaxDurability{};
GetUnitSeedFn GetUnitSeed{};
RollRandomFn RollRandom{};
GetClientFromPlayerFn GetClientFromPlayer{};
SetStatAndNotifyFn SetStatAndNotify{};
TelemetryCounters Telemetry{};

template<class T>
T At(std::uintptr_t rva) noexcept {
	return reinterpret_cast<T>(Base + rva);
}

bool IsAllowedKey(
	std::string_view key,
	std::initializer_list<std::string_view> allowed
) noexcept {
	for (const auto candidate : allowed) {
		if (key == candidate) return true;
	}
	return false;
}

void ValidateObject(
	const nlohmann::json& value,
	std::string_view label,
	std::initializer_list<std::string_view> allowed
) {
	if (!value.is_object()) {
		throw std::invalid_argument(std::string(label) + " must be an object");
	}
	for (const auto& [key, child] : value.items()) {
		(void)child;
		if (!IsAllowedKey(key, allowed)) {
			throw std::invalid_argument(std::string(label) + " has unknown setting: " + key);
		}
	}
}

void ReadConfig(const nlohmann::json& itemsConfig) {
	Settings = {};
	if (!itemsConfig.is_object()) throw std::invalid_argument("items must be an object");
	const auto entry = itemsConfig.find("repairCostsCap");
	if (entry == itemsConfig.end()) return;

	const auto& config = *entry;
	ValidateObject(config, "items.repairCostsCap", {"enabled", "maximumGold", "durabilityWear"});
	if (!config.contains("enabled") || !config.at("enabled").is_boolean()) {
		throw std::invalid_argument("items.repairCostsCap.enabled must be a boolean");
	}
	if (!config.contains("maximumGold") || !config.at("maximumGold").is_number_integer()) {
		throw std::invalid_argument("items.repairCostsCap.maximumGold must be an integer");
	}
	const auto maximumGold = config.at("maximumGold").get<std::int64_t>();
	if (!IsValidMaximumGold(maximumGold)) {
		throw std::out_of_range(
			"items.repairCostsCap.maximumGold must be between 0 and 2147483647");
	}
	if (!config.contains("durabilityWear")) {
		throw std::invalid_argument("items.repairCostsCap.durabilityWear is required");
	}

	const auto& durabilityWear = config.at("durabilityWear");
	ValidateObject(
		durabilityWear,
		"items.repairCostsCap.durabilityWear",
		{"enabled", "chance"}
	);
	if (!durabilityWear.contains("enabled")
		|| !durabilityWear.at("enabled").is_boolean()) {
		throw std::invalid_argument(
			"items.repairCostsCap.durabilityWear.enabled must be a boolean");
	}
	if (!durabilityWear.contains("chance") || !durabilityWear.at("chance").is_number()) {
		throw std::invalid_argument(
			"items.repairCostsCap.durabilityWear.chance must be a number");
	}
	const auto chance = durabilityWear.at("chance").get<double>();
	if (!IsValidChance(chance)) {
		throw std::out_of_range(
			"items.repairCostsCap.durabilityWear.chance must be between 0.0 and 1.0");
	}

	Settings.enabled = config.at("enabled").get<bool>();
	Settings.maximumGold = static_cast<std::int32_t>(maximumGold);
	Settings.durabilityWearEnabled = durabilityWear.at("enabled").get<bool>();
	Settings.durabilityWearChance = chance;
}

void HookRepairItem(void* game, void* item, void* player) noexcept {
	const auto durabilityBefore = item && player ? GetUnitStat(item, DurabilityStat, 0) : 0;
	const auto maximumBefore = item && player ? GetMaxDurability(item) : 0;
	const auto baseMaximumBefore = item && player
		? GetUnitBaseStat(item, MaximumDurabilityStat, 0)
		: 0;

	OriginalRepairItem(game, item, player);

	if (!item || !player || baseMaximumBefore <= 1
		|| !Settings.enabled || !Settings.durabilityWearEnabled) {
		return;
	}
	const auto durabilityAfter = GetUnitStat(item, DurabilityStat, 0);
	if (!DidPhysicalRepairSucceed(durabilityBefore, maximumBefore, durabilityAfter)) return;

	Telemetry.physicalRepairsEvaluated.fetch_add(1, std::memory_order_relaxed);
	auto* seed = GetUnitSeed(item);
	if (!seed) return;
	const auto roll = RollRandom(seed, ChanceBasisPointScale);
	if (!ShouldLoseMaximumDurability(
			Settings.durabilityWearEnabled, Settings.durabilityWearChance, roll)) {
		return;
	}

	const auto reducedBaseMaximum = ReducedMaximumDurability(baseMaximumBefore);
	auto* client = GetClientFromPlayer(player);
	SetStatAndNotify(item, client, true, MaximumDurabilityStat, reducedBaseMaximum, 0);
	const auto reducedEffectiveMaximum = GetMaxDurability(item);
	if (reducedEffectiveMaximum > 0) {
		SetStatAndNotify(item, client, true, DurabilityStat, reducedEffectiveMaximum, 0);
	}
	Telemetry.maximumDurabilityLost.fetch_add(1, std::memory_order_relaxed);
}

std::int32_t HookTransactionCost(
	void* player,
	void* item,
	std::uint32_t difficulty,
	void* questFlags,
	std::int32_t vendorId,
	std::int32_t transactionType
) noexcept {
	const auto vanillaCost = OriginalTransactionCost(
		player, item, difficulty, questFlags, vendorId, transactionType);
	if (!player || !item || transactionType != RepairTransactionType || vanillaCost <= 0) {
		return vanillaCost;
	}

	Telemetry.itemEvaluations.fetch_add(1, std::memory_order_relaxed);
	const auto adjustedCost = ApplyRepairCostCap(vanillaCost, transactionType, Settings);
	if (adjustedCost != vanillaCost) {
		Telemetry.itemAdjustments.fetch_add(1, std::memory_order_relaxed);
		Telemetry.itemQuotedGoldReduced.fetch_add(
			GoldReduction(vanillaCost, adjustedCost), std::memory_order_relaxed);
	}
	return adjustedCost;
}

std::int32_t HookRepairAllCost(
	void* game,
	void* player,
	std::int32_t vendorId,
	std::uint32_t difficulty,
	void* questFlags,
	void* repairCallback
) noexcept {
	const auto adjustedItemTotal = OriginalRepairAllCost(
		game, player, vendorId, difficulty, questFlags, repairCallback);
	if (!game || !player || adjustedItemTotal <= 0) return adjustedItemTotal;

	Telemetry.repairAllEvaluations.fetch_add(1, std::memory_order_relaxed);
	const auto cappedTotal = ApplyRepairAllCap(adjustedItemTotal, Settings);
	if (cappedTotal != adjustedItemTotal) {
		Telemetry.repairAllAdjustments.fetch_add(1, std::memory_order_relaxed);
		Telemetry.repairAllExtraGoldReduced.fetch_add(
			GoldReduction(adjustedItemTotal, cappedTotal), std::memory_order_relaxed);
	}
	return cappedTotal;
}

D2RL::ConsoleCommandResult Status(
	D2R::Game::Client*,
	const D2RL::ConsoleCommandContext* command,
	void*
) noexcept {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;

	char pricing[320]{};
	std::snprintf(
		pricing,
		sizeof(pricing),
		"Repair Costs Cap 1.4.0 (plugin-items): config=items.repairCostsCap; enabled=%s; "
		"maximumGold=%d (per item and Repair All).",
		Settings.enabled ? "true" : "false",
		Settings.maximumGold
	);
	command->plugin->WriteConsoleMessage(pricing);

	char wear[192]{};
	std::snprintf(
		wear,
		sizeof(wear),
		"Permanent durability wear: enabled=%s; chance=%.2f%% per physically repaired item.",
		Settings.durabilityWearEnabled ? "true" : "false",
		Settings.durabilityWearChance * 100.0
	);
	command->plugin->WriteConsoleMessage(wear);

	char telemetry[448]{};
	std::snprintf(
		telemetry,
		sizeof(telemetry),
		"Session pricing evaluations: items=%llu adjusted=%llu quotedGoldReduced=%llu; "
		"RepairAll=%llu capped=%llu extraGoldReduced=%llu; physicalRepairs=%llu durabilityLost=%llu.",
		static_cast<unsigned long long>(Telemetry.itemEvaluations.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(Telemetry.itemAdjustments.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(Telemetry.itemQuotedGoldReduced.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(Telemetry.repairAllEvaluations.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(Telemetry.repairAllAdjustments.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(Telemetry.repairAllExtraGoldReduced.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(Telemetry.physicalRepairsEvaluated.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(Telemetry.maximumDurabilityLost.load(std::memory_order_relaxed))
	);
	command->plugin->WriteConsoleMessage(telemetry);
	return D2RL::ConsoleCommandResult::Handled;
}

bool ValidateSignatures() noexcept {
	if (!Context->CheckExpectedBytes(
			TransactionCostBodyRva,
			TransactionCostBodyExpected.data(),
			TransactionCostBodyExpected.size())) {
		Context->LogError("plugin-items: Repair Costs Cap transaction-cost signature mismatch.");
		return false;
	}
	if (!Context->CheckExpectedBytes(
			RepairAllCostRva, RepairAllCostExpected.data(), RepairAllCostExpected.size())) {
		Context->LogError("plugin-items: Repair Costs Cap Repair All cost signature mismatch.");
		return false;
	}
	if (!Context->CheckExpectedBytes(
			RepairAllZeroCostBranchSignatureRva,
			RepairAllZeroCostBranchExpected.data(),
			RepairAllZeroCostBranchExpected.size())) {
		Context->LogError("plugin-items: Repair Costs Cap zero-cost signature mismatch.");
		return false;
	}
	if (Settings.durabilityWearEnabled
		&& (!Context->CheckExpectedBytes(
				RepairItemRva, RepairItemExpected.data(), RepairItemExpected.size())
			|| !Context->CheckExpectedBytes(
				GetUnitStatRva, GetUnitStatExpected.data(), GetUnitStatExpected.size())
			|| !Context->CheckExpectedBytes(
				GetUnitBaseStatSignatureRva,
				GetUnitBaseStatExpected.data(),
				GetUnitBaseStatExpected.size())
			|| !Context->CheckExpectedBytes(
				GetMaxDurabilityRva,
				GetMaxDurabilityExpected.data(),
				GetMaxDurabilityExpected.size())
			|| !Context->CheckExpectedBytes(
				GetUnitSeedRva, GetUnitSeedExpected.data(), GetUnitSeedExpected.size())
			|| !Context->CheckExpectedBytes(
				RollRandomRva, RollRandomExpected.data(), RollRandomExpected.size())
			|| !Context->CheckExpectedBytes(
				GetClientFromPlayerRva,
				GetClientFromPlayerExpected.data(),
				GetClientFromPlayerExpected.size())
			|| !Context->CheckExpectedBytes(
				SetStatAndNotifyRva,
				SetStatAndNotifyExpected.data(),
				SetStatAndNotifyExpected.size()))) {
		Context->LogError("plugin-items: Repair Costs Cap durability-wear signature mismatch.");
		return false;
	}
	return true;
}

bool InstallChanges() noexcept {
	if (!PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.repairCostsCap.transactionCost"),
			TransactionCostBodyRva,
			TransactionCostBodyExpected.data(),
			static_cast<std::uint32_t>(TransactionCostBodyExpected.size()),
			HookTransactionCost,
			&OriginalTransactionCost)) {
		Context->LogError("plugin-items: Repair Costs Cap transaction-cost hook failed.");
		return false;
	}
	if (!PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.repairCostsCap.repairAllCost"),
			RepairAllCostRva,
			RepairAllCostExpected.data(),
			static_cast<std::uint32_t>(RepairAllCostExpected.size()),
			HookRepairAllCost,
			&OriginalRepairAllCost)) {
		Context->LogError("plugin-items: Repair Costs Cap Repair All hook failed.");
		return false;
	}

	auto replacement = RepairAllZeroCostBranchExpected;
	replacement[RepairAllBranchDisplacementOffset] = 0x21;
	if (!PSh_ManifestPatchBytes(Context, PSH_MANIFEST_SITE("items.repairCostsCap.zeroCostBranch"),
			RepairAllZeroCostBranchSignatureRva,
			RepairAllZeroCostBranchExpected.data(),
			static_cast<std::uint32_t>(RepairAllZeroCostBranchExpected.size()),
			replacement.data(),
			static_cast<std::uint32_t>(replacement.size()))) {
		Context->LogError("plugin-items: Repair Costs Cap zero-cost patch failed.");
		return false;
	}
	if (Settings.durabilityWearEnabled
		&& !PSh_ManifestInstallInlineHook(Context, PSH_MANIFEST_SITE("items.repairCostsCap.repairItem"),
			RepairItemRva,
			RepairItemExpected.data(),
			static_cast<std::uint32_t>(RepairItemExpected.size()),
			HookRepairItem,
			&OriginalRepairItem)) {
		Context->LogError("plugin-items: Repair Costs Cap durability-wear hook failed.");
		return false;
	}
	return true;
}

} // namespace

bool Load(const D2RL::PluginContext* context, const nlohmann::json& itemsConfig) noexcept {
	if (!context) return false;
	Context = context;
	Base = context->exeBase;
	Telemetry.Reset();

	try {
		ReadConfig(itemsConfig);
	} catch (const std::exception& exception) {
		const auto message = std::string("plugin-items: invalid items.repairCostsCap (")
			+ exception.what() + ").";
		context->LogError(message.c_str());
		return false;
	}

	if (context->modDataVersionBuild != 0 && context->modDataVersionBuild != SupportedBuild) {
		context->LogError("plugin-items: Repair Costs Cap supports only D2R build 92777.");
		return false;
	}

	GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
	GetUnitBaseStat = At<GetUnitStatFn>(GetUnitBaseStatRva);
	GetMaxDurability = At<GetMaxDurabilityFn>(GetMaxDurabilityRva);
	GetUnitSeed = At<GetUnitSeedFn>(GetUnitSeedRva);
	RollRandom = At<RollRandomFn>(RollRandomRva);
	GetClientFromPlayer = At<GetClientFromPlayerFn>(GetClientFromPlayerRva);
	SetStatAndNotify = At<SetStatAndNotifyFn>(SetStatAndNotifyRva);

	if (Settings.enabled && (!ValidateSignatures() || !InstallChanges())) return false;

	if (!PSh_RegisterConsoleCommand(context,
			"repair-costs-cap",
			Status,
			"Show the configured NPC repair policy and session statistics.")) {
		context->LogWarn("plugin-items: Repair Costs Cap status command was not registered.");
	}

	char message[384]{};
	std::snprintf(
		message,
		sizeof(message),
		"plugin-items: Repair Costs Cap 1.4.0 by RuffnecKk loaded: enabled=%s; "
		"maximumGold=%d (per item and Repair All); durability wear=%s at %.2f%%; "
		"config=items.repairCostsCap.",
		Settings.enabled ? "true" : "false",
		Settings.maximumGold,
		Settings.durabilityWearEnabled ? "enabled" : "disabled",
		Settings.durabilityWearChance * 100.0
	);
	context->LogInfo(message);
	return true;
}

void Unload() noexcept {
	OriginalRepairItem = nullptr;
	OriginalRepairAllCost = nullptr;
	OriginalTransactionCost = nullptr;
	SetStatAndNotify = nullptr;
	GetClientFromPlayer = nullptr;
	RollRandom = nullptr;
	GetUnitSeed = nullptr;
	GetMaxDurability = nullptr;
	GetUnitBaseStat = nullptr;
	GetUnitStat = nullptr;
	Base = 0;
	Context = nullptr;
}

} // namespace RuffnecKk::RepairCostsCap
