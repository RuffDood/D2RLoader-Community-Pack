#include <D2RLPlugin/api.h>
#include "levels-private.h"

// ── Addresses (offsets from exe base 0x140000000) ────────────────────────────

// Found via a caller-chain + byte-identical data-table walk: profile's containing
// function FUN_1402d86a0 was located from FUN_1402c8a80's level-type dispatch
// (case 0), itself anchored on a byte-identical 25-entry data table
// (DAT_1416e6820, the case-0x6c preset-id list) matched via search_byte_patterns.
// Debug's dispatch (FUN_1403ee320) case 0 calls FUN_1403fac10 (matches
// FUN_1402d86a0), but debug's compiler inlined/merged
// DRLGOUTDOORS_SpawnAct1DirtPaths + FUN_1402da600 (our target) + the
// post-processing loop that used to live in FUN_1402d86a0 itself into a single
// new function, FUN_1403ff5c0 — confirmed by decompile shape (vertex-setup call,
// placement-search call, then the identical `if (dwFlags[0x330] > 0)` loop).
// The two patch sites are otherwise structurally identical to profile, just
// relocated into this merged function, with one encoding difference: the guard
// branch compiles to a short JLE (2 bytes: 7E <disp8>) here instead of profile's
// near JLE (6 bytes: 0F 8E <disp32>), so flipping it to unconditional is now a
// single opcode-byte patch (0x7E -> 0xEB, same disp8) rather than a 6-byte
// JMP+NOP replacement.
static constexpr uint64_t OFF_DisableAct1DirtPath1 = 0x3ff5de;
static constexpr uint64_t OFF_DisableAct1DirtPath2 = 0x3ff5eb;

// Expected original bytes (verified against the D2R.exe 3.2.92777 reference image). D2RLoader
// requires non-null expected bytes for PatchBytes calls so it can verify the
// patch site before writing.
static constexpr uint8_t EXP_DisableAct1DirtPath1[5] = { 0xE8, 0x3D, 0x02, 0x00, 0x00 };
static constexpr uint8_t EXP_DisableAct1DirtPath2[1] = { 0x7E };

// ── Plugin state ──────────────────────────────────────────────────────────────

static LevelPluginOptions g_pluginOptions;

// ── JSON loading ──────────────────────────────────────────────────────────────

void LevelPluginOptions::Load(const D2RL::PluginContext* /*context*/, const nlohmann::json& cfg)
{
	bDisableAct1Path = cfg.value("disableAct1Path", false);
}

// ── Plugin exports ────────────────────────────────────────────────────────────

static constexpr D2RL::PluginInfo PluginInfo {
	.infoSize   = D2RL::PluginInfoSize,
	.apiVersion = D2RL_PLUGIN_API_VERSION,
	.id         = "eezstreet-plugin-levels",
	.name       = "eezstreet Levels Plugin",
	.version    = PSh_PluginPackVersion,
	.author     = "eezstreet",
	.description = "Various level-related changes.",
	.flags      = D2RL::PluginFlags::None,
};

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &PluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!PSh_ValidatePluginTarget(context)) {
		return false;
	}

	try {
		auto cfg = PSh_Json_LoadConfig(context);
		g_pluginOptions.Load(context, PSh_Json_GetSection(cfg, "levels"));
	}
	catch (const std::exception& error) {
		PSh_Json_LogConfigError(context, error);
		return false;
	}
	if (g_pluginOptions.bDisableAct1Path
		&& (!context->CheckExpectedBytes(OFF_DisableAct1DirtPath1, EXP_DisableAct1DirtPath1, sizeof(EXP_DisableAct1DirtPath1))
			|| !context->CheckExpectedBytes(OFF_DisableAct1DirtPath2, EXP_DisableAct1DirtPath2, sizeof(EXP_DisableAct1DirtPath2)))) {
		context->LogError("plugin-levels: Disable Act 1 Path signature mismatch; no patch was applied.");
		return false;
	}
	PSh_HookTransactionScope hookTransaction(nullptr);
	if (!hookTransaction.IsActive()) {
		context->LogError("plugin-levels: could not initialize the deferred hook transaction.");
		return false;
	}

	if (g_pluginOptions.bDisableAct1Path)
	{
		unsigned char patch1[] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("levels.disableAct1Path.spawnCall"),
			OFF_DisableAct1DirtPath1, EXP_DisableAct1DirtPath1, sizeof(EXP_DisableAct1DirtPath1), patch1, sizeof(patch1))) {
			context->LogError("plugin-levels: Disable Act 1 Path spawn-call patch failed.");
			return false;
		}

		// Short JLE (0x7E) -> short JMP (0xEB), same disp8 — see the comment by
		// OFF_DisableAct1DirtPath2's declaration for why this is 1 byte now
		// instead of the old 6-byte JMP+NOP replacement.
		unsigned char patch2[] = { 0xEB };
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("levels.disableAct1Path.guardBranch"),
			OFF_DisableAct1DirtPath2, EXP_DisableAct1DirtPath2, sizeof(EXP_DisableAct1DirtPath2), patch2, sizeof(patch2))) {
			context->LogError("plugin-levels: Disable Act 1 Path branch patch failed.");
			return false;
		}
	}
	const auto commit = hookTransaction.Commit(context);
	if (!commit.success) {
		if (commit.rollbackFailures != 0) {
			D2RL::LogErrorF(context,
				"plugin-levels: deferred hook commit failed with %zu rollback errors; the inactive DLL remains loaded as a safety barrier.",
				commit.rollbackFailures);
			return true;
		}
		context->LogError(
			"plugin-levels: deferred hook commit failed; direct writes were restored and guarded detours were deactivated.");
		return false;
	}

	return true;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderUnloadPlugin() noexcept {
	PSh_HookTransactionDeactivate();
}
