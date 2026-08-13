#include <D2RLPlugin/api.h>
#include <plugin-shared.h>
#include <plugin-shared-json.h>
#include "cube-quick-move.h"
#include "equipped-item-to-cube.h"
#include "transmute-hotkey.h"
#include "prevent-merc-death-in-town.h"
#include "remote-stash.h"
#include "floating-damage-feature.h"
#include <limits>

// ── Addresses (offsets from exe base 0x140000000) ────────────────────────────

// Inside FUN_140188700 (/players command handler)
static constexpr uint64_t OFF_Players_AtoiCall    = 0x18885b; // E8 CALL to atoi (parses the numeric arg)
static constexpr uint64_t OFF_Players_ApplyCall   = 0x18887f; // E8 CALL to FUN_140d2f020 (applies player count)

// Callees saved so hooks can forward through
static constexpr uint64_t OFF_Players_Atoi        = 0x12da3a4; // atoi used at the /players call site
static constexpr uint64_t OFF_SetPlayerCount      = 0xd2f020;  // FUN_140d2f020(session, count)

// GAME_GetPlayerCountBonus (debug FUN_140542f40; profile 0x1403e34b0) computes the
// per-monster /players bonus struct { nHp%, nExperience%, nMonsterSkillBonus,
// nDifficulty, nPlayerCount } used by MONSTER_InitializeStatsAndSkills (see D2MOO's
// D2PlayerCountBonusStrc in Units/Player.h and Monster.cpp for the field layout —
// confirmed by decompile-vs-decompile structural match against the profile build,
// including the identical (n+26)*10 / (n-2)*50 extrapolation formulas for n>8).
// Only nHp and nExperience are driven by player count; nMonsterSkillBonus is
// difficulty-based (not player-count-based) and there is no player-count-driven
// monster damage bonus anywhere in this engine, so those are the only two fields
// worth capping independently.
static constexpr uint64_t OFF_GetPlayerCountBonus = 0x542f40;

// Expected original bytes (verified against the D2R.exe 3.2.92777 reference image). D2RLoader
// requires non-null expected bytes for PatchRel32/InstallInlineHook calls so it
// can verify the patch site before writing.
static constexpr uint8_t EXP_Players_AtoiCall[5]  = { 0xE8, 0x44, 0x1B, 0x15, 0x01 };
static constexpr uint8_t EXP_Players_ApplyCall[5] = { 0xE8, 0x9C, 0x67, 0xBA, 0x00 };
static constexpr uint8_t EXP_GetPlayerCountBonus[16] = {
	0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x49, 0x8B, 0xD9,
};

// ── D2R function types ────────────────────────────────────────────────────────

using PlayersAtoi_t          = int  (__fastcall*)(const char* str);
using SetPlayerCount_t       = void (__fastcall*)(void* session, int count);
using GetPlayerCountBonus_t  = void (__fastcall*)(void* game, int32_t* outBonus, void* room, void* monster);

// ── Plugin state ──────────────────────────────────────────────────────────────

static int                   g_PlayersCommandLimit             = 8;
static int                   g_LastPlayersArg                  = 1;
static int                   g_MonsterHpPlayerCountCap          = 0;
static int                   g_MonsterExperiencePlayerCountCap  = 0;
static PlayersAtoi_t         Real_PlayersAtoi                  = nullptr;
static SetPlayerCount_t      Real_SetPlayerCount               = nullptr;
static GetPlayerCountBonus_t Real_GetPlayerCountBonus          = nullptr;

// Player-count bonus-percent table for indices 1-8, mirrored from the game's own
// DAT_1416ec070/DAT_141d3d4a8 table (verified via read_memory: 0,0,50,100,150,200,
// 250,300,350 -- index 0 unused). Hardcoded here rather than re-read at runtime
// since it's small, static content the plugin doesn't otherwise need a pointer to.
static constexpr int kPlayerCountBonusTable[9] = { 0, 0, 50, 100, 150, 200, 250, 300, 350 };

static int PlayerCountBonusPercent_Hp(int count) {
	if (count < 9) return kPlayerCountBonusTable[count];
	return (count - 2) * 50;
}

static int PlayerCountBonusPercent_Experience(int count) {
	if (count < 9) return kPlayerCountBonusTable[count];
	return (count + 26) * 10;
}

// ── Hook: GAME_GetPlayerCountBonus ─────────────────────────────────────────────
// Runs the real computation first (so nMonsterSkillBonus/nDifficulty/nPlayerCount
// stay untouched), then independently re-derives nHp/nExperience using their own
// capped player counts instead of the real /players value.

static void __fastcall Hook_GetPlayerCountBonus(void* game, int32_t* outBonus, void* room, void* monster) {
	Real_GetPlayerCountBonus(game, outBonus, room, monster);
	if (outBonus == nullptr) {
		return;
	}

	const int effectiveCount = outBonus[4];
	if (effectiveCount <= 0) {
		return;
	}

	if (g_MonsterHpPlayerCountCap > 0) {
		const int cappedCount = effectiveCount < g_MonsterHpPlayerCountCap ? effectiveCount : g_MonsterHpPlayerCountCap;
		outBonus[0] = PlayerCountBonusPercent_Hp(cappedCount);
	}
	if (g_MonsterExperiencePlayerCountCap > 0) {
		const int cappedCount = effectiveCount < g_MonsterExperiencePlayerCountCap ? effectiveCount : g_MonsterExperiencePlayerCountCap;
		outBonus[1] = PlayerCountBonusPercent_Experience(cappedCount);
	}
}

// ── Hook: atoi call site inside /players handler ──────────────────────────────
// Captures the raw numeric argument before the game's [1,8] ceiling clamp runs.

static int __fastcall Hook_PlayersAtoi(const char* str) {
	int val = Real_PlayersAtoi(str);
	g_LastPlayersArg = val;
	return val;
}

// ── Hook: FUN_1408082b0 call site inside /players handler ─────────────────────
// Replaces the already-clamped count (EDX ≤ 8) with the raw value re-clamped
// to [1, PlayersCommandLimit].

static void __fastcall Hook_SetPlayerCount(void* session, int /*count*/) {
	int clamped = g_LastPlayersArg;
	if (clamped < 1) clamped = 1;
	if (clamped > g_PlayersCommandLimit) clamped = g_PlayersCommandLimit;
	Real_SetPlayerCount(session, clamped);
}

// ── Plugin info ───────────────────────────────────────────────────────────────

static constexpr D2RL::PluginInfo PluginInfo {
	.infoSize   = D2RL::PluginInfoSize,
	.apiVersion = D2RL_PLUGIN_API_VERSION,
	.id         = "eezstreet-plugin-misc",
	.name       = "eezstreet Misc Plugin",
	.version    = PSh_PluginPackVersion,
	.author     = "eezstreet",
	.description = "Miscellaneous changes.",
	.flags      = D2RL::PluginFlags::NativeHooks,
};

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &PluginInfo;
}

static void CleanupPluginMiscState() noexcept
{
	RuffnecKk::FloatingDamageFeature::Unload();
	RuffnecKk::RemoteStash::Unload();
	RuffnecKk::PreventMercDeathInTown::Unload();
	RuffnecKk::TransmuteHotkey::Unload();
	RuffnecKk::EquippedItemToCube::Unload();
	RuffnecKk::CubeQuickMove::Unload();
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!PSh_ValidatePluginTarget(context)) {
		return false;
	}

	nlohmann::json misc;
	try {
		auto cfg = PSh_Json_LoadConfig(context);
		misc = PSh_Json_GetSection(cfg, "misc");
		const auto readPlayerCount = [&misc](
			const char* key,
			int fallback,
			int minimum,
			const char* setting) -> int {
			const auto entry = misc.find(key);
			if (entry == misc.end()) {
				return fallback;
			}
			if (!entry->is_number_integer()) {
				throw std::runtime_error(std::string("plugin-misc: ") + setting + " must be an integer.");
			}
			const auto value = entry->get<int64_t>();
			if (value < minimum || value > std::numeric_limits<int>::max()) {
				throw std::runtime_error(std::string("plugin-misc: ") + setting + " is out of range.");
			}
			return static_cast<int>(value);
		};
		g_PlayersCommandLimit = readPlayerCount(
			"playersCommandLimit", 8, 1, "misc.playersCommandLimit");
		g_MonsterHpPlayerCountCap = readPlayerCount(
			"monsterHpPlayerCountCap", 0, 0, "misc.monsterHpPlayerCountCap");
		g_MonsterExperiencePlayerCountCap = readPlayerCount(
			"monsterExperiencePlayerCountCap", 0, 0,
			"misc.monsterExperiencePlayerCountCap");
	}
	catch (const std::exception& error) {
		PSh_Json_LogConfigError(context, error);
		return false;
	}
	if (g_PlayersCommandLimit > 8
		&& (!context->CheckExpectedBytes(OFF_Players_AtoiCall, EXP_Players_AtoiCall, sizeof(EXP_Players_AtoiCall))
			|| !context->CheckExpectedBytes(OFF_Players_ApplyCall, EXP_Players_ApplyCall, sizeof(EXP_Players_ApplyCall)))) {
		context->LogError("plugin-misc: /players command signature mismatch; no /players patch was applied.");
		return false;
	}
	if ((g_MonsterHpPlayerCountCap > 0 || g_MonsterExperiencePlayerCountCap > 0)
		&& !context->CheckExpectedBytes(OFF_GetPlayerCountBonus, EXP_GetPlayerCountBonus, sizeof(EXP_GetPlayerCountBonus))) {
		context->LogError("plugin-misc: monster player-count cap signature mismatch; hook refused.");
		return false;
	}
	PSh_HookTransactionScope hookTransaction(&CleanupPluginMiscState);
	if (!hookTransaction.IsActive()) {
		context->LogError("plugin-misc: could not initialize the deferred hook transaction.");
		return false;
	}
	if (!RuffnecKk::CubeQuickMove::Load(context, misc)) {
		return false;
	}
	if (!RuffnecKk::EquippedItemToCube::Load(context, misc)) {
		return false;
	}
	if (!RuffnecKk::TransmuteHotkey::Load(context, misc)) {
		return false;
	}
	if (!RuffnecKk::PreventMercDeathInTown::Load(context, misc)) {
		return false;
	}
	if (!RuffnecKk::RemoteStash::Load(context, misc)) {
		return false;
	}
	if (!RuffnecKk::FloatingDamageFeature::Load(context, misc)) {
		return false;
	}

	if (g_PlayersCommandLimit > 8) {
		Real_PlayersAtoi    = reinterpret_cast<PlayersAtoi_t>(context->exeBase + OFF_Players_Atoi);
		Real_SetPlayerCount = reinterpret_cast<SetPlayerCount_t>(context->exeBase + OFF_SetPlayerCount);

		if (!PSh_ManifestPatchCallSite(context, PSH_MANIFEST_SITE("misc.playersCommandLimit.parseCall"),
				OFF_Players_AtoiCall, EXP_Players_AtoiCall, sizeof(EXP_Players_AtoiCall),
				reinterpret_cast<void*>(&Hook_PlayersAtoi))
			|| !PSh_ManifestPatchCallSite(context, PSH_MANIFEST_SITE("misc.playersCommandLimit.applyCall"),
				OFF_Players_ApplyCall, EXP_Players_ApplyCall, sizeof(EXP_Players_ApplyCall),
				reinterpret_cast<void*>(&Hook_SetPlayerCount))) {
			context->LogError("plugin-misc: /players command call-site patch failed.");
			return false;
		}
	}

	if (g_MonsterHpPlayerCountCap > 0 || g_MonsterExperiencePlayerCountCap > 0) {
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("misc.playerCountCaps.getBonus"),
				OFF_GetPlayerCountBonus, EXP_GetPlayerCountBonus, sizeof(EXP_GetPlayerCountBonus),
				&Hook_GetPlayerCountBonus, &Real_GetPlayerCountBonus)) {
			context->LogError("plugin-misc: failed to hook GetPlayerCountBonus");
			return false;
		}
	}

	const auto commit = hookTransaction.Commit(context);
	if (!commit.success) {
		if (commit.rollbackFailures != 0) {
			D2RL::LogErrorF(context,
				"plugin-misc: deferred hook commit failed with %zu rollback errors; the inactive DLL remains loaded as a safety barrier.",
				commit.rollbackFailures);
			return true;
		}
		context->LogError(
			"plugin-misc: deferred hook commit failed; direct writes were restored and guarded detours were deactivated.");
		return false;
	}

	return true;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderUnloadPlugin() noexcept {
	PSh_HookTransactionDeactivate();
	CleanupPluginMiscState();
}
