#include <D2RLPlugin/api.h>
#include <D2RLPlugin/logging.h>
#include <plugin-shared.h>
#include "larzuk-sockets.h"
#include "quests-private.h"
#include <plugin-shared-json.h>
#include <limits>
#include <stdexcept>

// ── Addresses (offsets from exe base 0x140000000) ────────────────────────────

// Den of Evil reward: found via ACT1Q1_Callback11_ScrollMessage (decompile embeds
// the "a1q1.cpp" source path), located from ACT1Q1_InitQuestData's callback table,
// itself found by a byte-identical match of the gpAct1Q1NpcMessages data table.
// Unlike profile.exe (amount is a plain MOV imm32, stat-id is a dependent
// LEA R8D,[R9+4] that drifts if the amount register changes, hence the old
// PUSH 5/POP R8 decouple patch), debug.exe computes both values independently as
// LEA reg,[R9+<const>] off a register that's always zeroed by an immediately
// preceding XOR R9D,R9D. There is no drift to decouple, so the old Patch2 site/
// patch is unnecessary here — the amount is just the disp8 byte of its own LEA,
// patched exactly like the old single-byte MOV case.
static constexpr uint64_t OFF_DenOfEvilPatch = 0x5de5a0;
// Izual/Fallen Angel reward: same shape as Den of Evil above. Debug computes
// the stat-id via LEA EDX,[R9+0x5] off a register that's always zeroed by an
// immediately preceding XOR R9D,R9D, completely independent of the reward
// register — so the old Patch2 (PUSH 5/POP R8 decouple) is unnecessary here
// too, and was in fact still being applied at its stale *profile.exe*
// address against the debug binary (a live bug: it was corrupting 4
// unrelated bytes of debug.exe). Removed; see OFF_DenOfEvilPatch's comment
// for the full explanation of why no decouple site exists in this build.
static constexpr uint64_t OFF_FallenAngelPatch = 0x5e6eee;
// Note: the debug build's codegen dropped the duplicate copy of these
// immediates that profile.exe wrote into a stack struct for an
// EVENTS_InvokeHandlerList notification call (that call site doesn't exist
// in this build) — only one copy of each stat/amount immediate remains, so
// the old Patch1/Patch2 aliases resolved to the same instruction. Each reward
// now owns and writes that site exactly once.
static constexpr uint64_t OFF_BlackBookPatch = 0x5ed713;
static constexpr uint64_t OFF_GoldenBirdStatPatch = 0x5806c1;
static constexpr uint64_t OFF_GoldenBirdAmountPatch = 0x5806b7;
static constexpr uint64_t OFF_SkillBookStatPatch = 0x58078b;
static constexpr uint64_t OFF_SkillBookAmountPatch = 0x58078f;
static constexpr uint64_t OFF_AkaraRingItem = 0x5d9a52;
static constexpr uint64_t OFF_AkaraRingItemQuality = 0x5d9a4d;
static constexpr uint64_t OFF_AkaraRingCallSite = 0x5d9a6d;
static constexpr uint64_t OFF_OrmusRingItem = 0x5e9bc3;
static constexpr uint64_t OFF_OrmusRingItemQuality = 0x5e9bd6;
static constexpr uint64_t OFF_OrmusRingCallSite = 0x5e9bde;
static constexpr uint64_t OFF_GiveQuestItem = 0x517530;
static constexpr uint64_t OFF_QualKehkItem1       = 0x23a02c0;
static constexpr uint64_t OFF_QualKehkItem2       = 0x23a02c4;
static constexpr uint64_t OFF_QualKehkItem3       = 0x23a02c8;
static constexpr uint64_t OFF_QualKehkCallSite    = 0x54c9ac;
static constexpr uint64_t OFF_ImbueSocket1 = 0x36b123;
static constexpr uint64_t OFF_ImbueSocket2 = 0x36b61b;

// ── Expected original bytes (verified against the D2R.exe 3.2.92777 reference image) ────
// D2RLoader requires non-null expected bytes for PatchBytes/PatchRel32 calls
// so it can verify the patch site before writing.
static constexpr uint8_t EXP_DenOfEvilPatch[1]        = { 0x01 };
static constexpr uint8_t EXP_FallenAngelPatch[1]      = { 0x02 };
static constexpr uint8_t EXP_BlackBookPatch[1]        = { 0x04 };
static constexpr uint8_t EXP_GoldenBirdStatPatch[1]   = { 0x07 };
static constexpr uint8_t EXP_GoldenBirdAmountPatch[4] = { 0x00, 0x14, 0x00, 0x00 };
static constexpr uint8_t EXP_SkillBookAmountPatch[1]  = { 0x01 };
static constexpr uint8_t EXP_SkillBookStatPatch[1]    = { 0x05 };
static constexpr uint8_t EXP_AkaraRingItem[4]         = { 0x72, 0x69, 0x6E, 0x20 };
static constexpr uint8_t EXP_AkaraRingItemQuality[4]  = { 0x8B, 0x53, 0x1C, 0x41 };
static constexpr uint8_t EXP_AkaraRingCallSite[5]     = { 0xE8, 0xBE, 0xDA, 0xF3, 0xFF };
static constexpr uint8_t EXP_OrmusRingItem[4]         = { 0x72, 0x69, 0x6E, 0x20 };
static constexpr uint8_t EXP_OrmusRingItemQuality[1]  = { 0xC7 };
static constexpr uint8_t EXP_OrmusRingCallSite[5]     = { 0xE8, 0x4D, 0xD9, 0xF2, 0xFF };
static constexpr uint8_t EXP_QualKehkItem1[3]         = { 0x72, 0x30, 0x37 };
static constexpr uint8_t EXP_QualKehkItem2[3]         = { 0x72, 0x30, 0x38 };
static constexpr uint8_t EXP_QualKehkItem3[3]         = { 0x72, 0x30, 0x39 };
static constexpr uint8_t EXP_QualKehkCallSite[5]      = { 0xE8, 0x7F, 0xAB, 0xFC, 0xFF };
static constexpr uint8_t EXP_ImbueSocket1[2]          = { 0x0F, 0x85 };
static constexpr uint8_t EXP_ImbueSocket2[2]          = { 0x75, 0x6A };

// ── Plugin state ──────────────────────────────────────────────────────────────

static QuestPluginOptions g_questPluginOptions;

using GiveQuestItemFn_t = int* (__fastcall*)(const D2GameStrc* pGame, void* pPlayer, uint32_t itemCode, int param4, uint32_t quality, int param6);
static GiveQuestItemFn_t g_GiveQuestItemFn = nullptr;
static uintptr_t g_exeBase = 0;
static const D2RL::PluginContext* g_context = nullptr;

// ── DifferentPerDifficulty call-site hooks ────────────────────────────────────
static int* __fastcall Hook_AkaraCainRingDiffReward(const D2GameStrc* pGame, void* pPlayer, uint32_t, int param4, uint32_t, int param6)
{
	const uint8_t di = static_cast<uint8_t>(pGame->difficultyLevel) <= 2 ? static_cast<uint8_t>(pGame->difficultyLevel) : 0;
	return g_GiveQuestItemFn(pGame, pPlayer,
		g_questPluginOptions.AkaraCainRingItem.RewardPerDifficulty[di],
		param4,
		g_questPluginOptions.AkaraCainRingQuality.RewardPerDifficulty[di],
		param6);
}

static int* __fastcall Hook_OrmusGidbinnRingDiffReward(const D2GameStrc* pGame, void* pPlayer, uint32_t, int param4, uint32_t, int param6)
{
	const uint8_t di = static_cast<uint8_t>(pGame->difficultyLevel) <= 2 ? static_cast<uint8_t>(pGame->difficultyLevel) : 0;
	return g_GiveQuestItemFn(pGame, pPlayer,
		g_questPluginOptions.OrmusGidbinnRingItem.RewardPerDifficulty[di],
		param4,
		g_questPluginOptions.OrmusGidbinnRingQuality.RewardPerDifficulty[di],
		param6);
}

static int* __fastcall Hook_QualKehkRuneDiffReward(const D2GameStrc* pGame, void* pPlayer, uint32_t itemCode, int param4, uint32_t quality, int param6)
{
	const uint8_t di = static_cast<uint8_t>(pGame->difficultyLevel) <= 2 ? static_cast<uint8_t>(pGame->difficultyLevel) : 0;
	const auto* tbl = reinterpret_cast<const uint32_t*>(g_exeBase + OFF_QualKehkItem1);
	int slot = (itemCode == tbl[0]) ? 0 : (itemCode == tbl[1]) ? 1 : 2;
	uint32_t newItem = g_questPluginOptions.QualKehkRuneItems[slot].RewardPerDifficulty[di];
	D2RL::LogInfoF(g_context,
		"QualKehkRuneDiffReward: rawDifficulty=%u di=%u slot=%d itemCode=0x%08X newItem=0x%08X",
		static_cast<unsigned>(pGame->difficultyLevel), di, slot, itemCode, newItem);
	return g_GiveQuestItemFn(pGame, pPlayer, newItem, param4, quality, param6);
}

// ── JSON loading ──────────────────────────────────────────────────────────────

static int64_t ReadBoundedInteger(
	const nlohmann::json& object,
	const char* key,
	int64_t fallback,
	int64_t minimum,
	int64_t maximum,
	const char* setting)
{
	const auto entry = object.find(key);
	if (entry == object.end()) {
		return fallback;
	}
	if (!entry->is_number_integer()) {
		throw std::runtime_error(std::string("plugin-quests: ") + setting + " must be an integer.");
	}

	int64_t value{};
	if (entry->is_number_unsigned()) {
		const auto unsignedValue = entry->get<uint64_t>();
		if (unsignedValue > static_cast<uint64_t>(maximum)) {
			throw std::runtime_error(std::string("plugin-quests: ") + setting + " is out of range.");
		}
		value = static_cast<int64_t>(unsignedValue);
	}
	else {
		value = entry->get<int64_t>();
	}
	if (value < minimum || value > maximum) {
		throw std::runtime_error(std::string("plugin-quests: ") + setting + " is out of range.");
	}
	return value;
}

static RewardType ParseRewardMode(const nlohmann::json& obj, bool allowDifferent = true)
{
	std::string mode = obj.value("mode", "disabled");
	if (mode == "same")      return RewardType::SamePerDifficulty;
	if (mode == "different" && allowDifferent) return RewardType::DifferentPerDifficulty;
	if (mode == "disabled") return RewardType::Disabled;
	throw std::runtime_error(
		allowDifferent
			? "plugin-quests: quest reward mode must be disabled, same, or different."
			: "plugin-quests: this quest reward mode must be disabled or same.");
}

// Encode a JSON string item code (e.g. "rin", "r07") to a little-endian uint32_t.
// Defaults to the provided fallback string if the key is absent.
static uint32_t ParseItemCode(const nlohmann::json& obj, const char* key, const char* fallback)
{
	std::string code = obj.value(key, fallback);
	if (code.size() != 3) {
		throw std::runtime_error(
			std::string("plugin-quests: item code '") + code + "' must contain exactly 3 characters.");
	}
	return PSh_EncodeItemCode(code.c_str());
}

static ItemQuestReward<uint32_t> ReadItemCodeReward(const nlohmann::json& obj,
                                                     const char* itemKey, const char* fallback)
{
	ItemQuestReward<uint32_t> r;
	r.Reward = ParseItemCode(obj, itemKey, fallback);

	auto pd = obj.value("perDifficulty", nlohmann::json::array());
	if (!pd.is_array()) {
		throw std::runtime_error("plugin-quests: perDifficulty must be an array.");
	}
	for (int i = 0; i < 3; i++) {
		if (i < static_cast<int>(pd.size()) && pd[i].is_object())
			r.RewardPerDifficulty[i] = ParseItemCode(pd[i], "item", fallback);
		else
			r.RewardPerDifficulty[i] = r.Reward;
	}
	return r;
}

static ItemQuestReward<uint8_t> ReadQualityReward(const nlohmann::json& obj,
                                                   const char* qualKey, uint8_t fallback)
{
	ItemQuestReward<uint8_t> r;
	r.Reward = static_cast<uint8_t>(ReadBoundedInteger(
		obj, qualKey, fallback, 2, 9, "quest reward quality"));

	auto pd = obj.value("perDifficulty", nlohmann::json::array());
	if (!pd.is_array()) {
		throw std::runtime_error("plugin-quests: perDifficulty must be an array.");
	}
	for (int i = 0; i < 3; i++) {
		if (i < static_cast<int>(pd.size()) && pd[i].is_object())
			r.RewardPerDifficulty[i] = static_cast<uint8_t>(ReadBoundedInteger(
				pd[i], "quality", fallback, 2, 9, "quest reward perDifficulty quality"));
		else
			r.RewardPerDifficulty[i] = r.Reward;
	}
	return r;
}

void QuestPluginOptions::Load(const D2RL::PluginContext* /*context*/, const nlohmann::json& cfg)
{
	auto doe = cfg.value("denOfEvil", nlohmann::json::object());
	DenOfEvilRewardEnabled = ParseRewardMode(doe, false);
	DenOfEvilSkillPointReward = static_cast<uint8_t>(ReadBoundedInteger(
		doe, "skillPoints", 1, 0, 255, "quests.denOfEvil.skillPoints"));

	auto iz = cfg.value("izual", nlohmann::json::object());
	IzualRewardEnabled = ParseRewardMode(iz, false);
	IzualSkillPointReward = static_cast<uint8_t>(ReadBoundedInteger(
		iz, "skillPoints", 2, 0, 255, "quests.izual.skillPoints"));

	auto bb = cfg.value("blackBook", nlohmann::json::object());
	BlackBookRewardEnabled = ParseRewardMode(bb, false);
	BlackBookStatPointReward = static_cast<uint8_t>(ReadBoundedInteger(
		bb, "statPoints", 5, 0, 255, "quests.blackBook.statPoints"));

	auto gb = cfg.value("goldenBird", nlohmann::json::object());
	GoldenBirdRewardEnabled = ParseRewardMode(gb, false);
	GoldenBirdRewardStat = static_cast<uint8_t>(ReadBoundedInteger(
		gb, "stat", 7, 0, 255, "quests.goldenBird.stat"));
	GoldenBirdRewardAmount = static_cast<uint32_t>(ReadBoundedInteger(
		gb, "amount", 0x1400, 0, std::numeric_limits<uint32_t>::max(),
		"quests.goldenBird.amount")); // in 256ths for HP/MP

	auto sb = cfg.value("radamentSkillBook", nlohmann::json::object());
	SkillBookRewardEnabled = ParseRewardMode(sb, false);
	SkillBookRewardStat = static_cast<uint8_t>(ReadBoundedInteger(
		sb, "stat", 5, 0, 255, "quests.radamentSkillBook.stat"));
	SkillBookRewardAmount = static_cast<uint8_t>(ReadBoundedInteger(
		sb, "amount", 1, 0, 255, "quests.radamentSkillBook.amount"));

	auto akara = cfg.value("akaraCainRing", nlohmann::json::object());
	AkaraCainRingRewardEnabled = ParseRewardMode(akara);
	AkaraCainRingItem          = ReadItemCodeReward(akara, "item", "rin");
	AkaraCainRingQuality       = ReadQualityReward(akara, "quality", 6);

	auto ormus = cfg.value("ormusGidbinnRing", nlohmann::json::object());
	OrmusGidbinnRingRewardEnabled = ParseRewardMode(ormus);
	OrmusGidbinnRingItem          = ReadItemCodeReward(ormus, "item", "rin");
	OrmusGidbinnRingQuality       = ReadQualityReward(ormus, "quality", 6);

	auto qk = cfg.value("qualKehkRunes", nlohmann::json::object());
	QualKehkRuneRewardEnabled = ParseRewardMode(qk);
	static const char* runeDefaults[3] = { "r07", "r08", "r09" };
	auto items = qk.value("items", nlohmann::json::array());
	auto perDiff = qk.value("perDifficulty", nlohmann::json::array());
	if (!items.is_array() || !perDiff.is_array()) {
		throw std::runtime_error(
			"plugin-quests: quests.qualKehkRunes items and perDifficulty must be arrays.");
	}
	for (int slot = 0; slot < 3; slot++) {
		const char* def = runeDefaults[slot];
		if (slot < static_cast<int>(items.size()) && !items[slot].is_string()) {
			throw std::runtime_error("plugin-quests: quests.qualKehkRunes.items entries must be strings.");
		}
		std::string sameCode = slot < static_cast<int>(items.size())
			? items[slot].get<std::string>() : def;
		if (sameCode.size() != 3) {
			throw std::runtime_error(
				"plugin-quests: quests.qualKehkRunes item codes must contain exactly 3 characters.");
		}
		QualKehkRuneItems[slot].Reward = PSh_EncodeItemCode(sameCode.c_str());
		for (int di = 0; di < 3; di++) {
			uint32_t code = QualKehkRuneItems[slot].Reward;
			if (di < static_cast<int>(perDiff.size())) {
				if (!perDiff[di].is_array()) {
					throw std::runtime_error(
						"plugin-quests: quests.qualKehkRunes.perDifficulty rows must be arrays.");
				}
				auto& row = perDiff[di];
				if (slot < static_cast<int>(row.size())) {
					if (!row[slot].is_string()) {
						throw std::runtime_error(
							"plugin-quests: quests.qualKehkRunes.perDifficulty entries must be strings.");
					}
					const auto difficultyCode = row[slot].get<std::string>();
					if (difficultyCode.size() != 3) {
						throw std::runtime_error(
							"plugin-quests: quests.qualKehkRunes item codes must contain exactly 3 characters.");
					}
					code = PSh_EncodeItemCode(difficultyCode.c_str());
				}
			}
			QualKehkRuneItems[slot].RewardPerDifficulty[di] = code;
		}
	}

	ImbueAllowSockets = cfg.value("imbueAllowSockets", false) ? 1 : 0;
}

// ── Plugin exports ────────────────────────────────────────────────────────────

static constexpr D2RL::PluginInfo PluginInfo {
	.infoSize   = D2RL::PluginInfoSize,
	.apiVersion = D2RL_PLUGIN_API_VERSION,
	.id         = "eezstreet-plugin-quests",
	.name       = "eezstreet Quests Plugin",
	.version    = PSh_PluginPackVersion,
	.author     = "eezstreet",
	.description = "Various quest-related changes.",
	.flags      = D2RL::PluginFlags::NativeHooks,
};

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &PluginInfo;
}

static void CleanupPluginQuestsState() noexcept
{
	RuffnecKk::ForceLarzukSockets::Unload();
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!PSh_ValidatePluginTarget(context)) {
		return false;
	}

	g_context = context;

	nlohmann::json questsConfig;
	try {
		auto cfg = PSh_Json_LoadConfig(context);
		questsConfig = PSh_Json_GetSection(cfg, "quests");
		g_questPluginOptions.Load(context, questsConfig);
	}
	catch (const std::exception& error) {
		PSh_Json_LogConfigError(context, error);
		return false;
	}
	const auto expected = [context](uint64_t rva, const auto& bytes) noexcept {
		return context->CheckExpectedBytes(rva, bytes, sizeof(bytes));
	};
	bool signaturesValid = true;
	if (g_questPluginOptions.DenOfEvilRewardEnabled == RewardType::SamePerDifficulty)
		signaturesValid = signaturesValid && expected(OFF_DenOfEvilPatch, EXP_DenOfEvilPatch);
	if (g_questPluginOptions.IzualRewardEnabled == RewardType::SamePerDifficulty)
		signaturesValid = signaturesValid && expected(OFF_FallenAngelPatch, EXP_FallenAngelPatch);
	if (g_questPluginOptions.BlackBookRewardEnabled == RewardType::SamePerDifficulty)
		signaturesValid = signaturesValid && expected(OFF_BlackBookPatch, EXP_BlackBookPatch);
	if (g_questPluginOptions.GoldenBirdRewardEnabled == RewardType::SamePerDifficulty)
		signaturesValid = signaturesValid
			&& expected(OFF_GoldenBirdStatPatch, EXP_GoldenBirdStatPatch)
			&& expected(OFF_GoldenBirdAmountPatch, EXP_GoldenBirdAmountPatch);
	if (g_questPluginOptions.SkillBookRewardEnabled == RewardType::SamePerDifficulty)
		signaturesValid = signaturesValid
			&& expected(OFF_SkillBookAmountPatch, EXP_SkillBookAmountPatch)
			&& expected(OFF_SkillBookStatPatch, EXP_SkillBookStatPatch);
	if (g_questPluginOptions.AkaraCainRingRewardEnabled == RewardType::SamePerDifficulty)
		signaturesValid = signaturesValid
			&& expected(OFF_AkaraRingItem, EXP_AkaraRingItem)
			&& expected(OFF_AkaraRingItemQuality, EXP_AkaraRingItemQuality);
	else if (g_questPluginOptions.AkaraCainRingRewardEnabled == RewardType::DifferentPerDifficulty)
		signaturesValid = signaturesValid && expected(OFF_AkaraRingCallSite, EXP_AkaraRingCallSite);
	if (g_questPluginOptions.OrmusGidbinnRingRewardEnabled == RewardType::SamePerDifficulty)
		signaturesValid = signaturesValid
			&& expected(OFF_OrmusRingItem, EXP_OrmusRingItem)
			&& expected(OFF_OrmusRingItemQuality, EXP_OrmusRingItemQuality);
	else if (g_questPluginOptions.OrmusGidbinnRingRewardEnabled == RewardType::DifferentPerDifficulty)
		signaturesValid = signaturesValid && expected(OFF_OrmusRingCallSite, EXP_OrmusRingCallSite);
	if (g_questPluginOptions.QualKehkRuneRewardEnabled == RewardType::SamePerDifficulty)
		signaturesValid = signaturesValid
			&& expected(OFF_QualKehkItem1, EXP_QualKehkItem1)
			&& expected(OFF_QualKehkItem2, EXP_QualKehkItem2)
			&& expected(OFF_QualKehkItem3, EXP_QualKehkItem3);
	else if (g_questPluginOptions.QualKehkRuneRewardEnabled == RewardType::DifferentPerDifficulty)
		signaturesValid = signaturesValid && expected(OFF_QualKehkCallSite, EXP_QualKehkCallSite);
	if (g_questPluginOptions.ImbueAllowSockets)
		signaturesValid = signaturesValid
			&& expected(OFF_ImbueSocket1, EXP_ImbueSocket1)
			&& expected(OFF_ImbueSocket2, EXP_ImbueSocket2);
	if (!signaturesValid) {
		context->LogError("plugin-quests: configured eezstreet patch-set signature mismatch; no quest patch was applied.");
		return false;
	}
	PSh_HookTransactionScope hookTransaction(&CleanupPluginQuestsState);
	if (!hookTransaction.IsActive()) {
		context->LogError("plugin-quests: could not initialize the deferred hook transaction.");
		return false;
	}
	if (!RuffnecKk::ForceLarzukSockets::Load(context, questsConfig)) {
		return false;
	}

	if (g_questPluginOptions.DenOfEvilRewardEnabled == RewardType::SamePerDifficulty)
	{	// Fixed reward on each difficulty. No decouple patch needed here anymore —
		// see the comment by OFF_DenOfEvilPatch's declaration.
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.denOfEvil.skillPoints"),
			OFF_DenOfEvilPatch, EXP_DenOfEvilPatch, sizeof(EXP_DenOfEvilPatch), &g_questPluginOptions.DenOfEvilSkillPointReward, 1)) {
			context->LogError("plugin-quests: Den of Evil reward patch failed.");
			return false;
		}
	}
	if (g_questPluginOptions.IzualRewardEnabled == RewardType::SamePerDifficulty)
	{	// Fixed reward on each difficulty. No decouple patch needed here anymore —
		// see the comment by OFF_FallenAngelPatch's declaration.
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.izual.skillPoints"),
			OFF_FallenAngelPatch, EXP_FallenAngelPatch, sizeof(EXP_FallenAngelPatch), &g_questPluginOptions.IzualSkillPointReward, 1)) {
			context->LogError("plugin-quests: Izual reward patch failed.");
			return false;
		}
	}
	if (g_questPluginOptions.BlackBookRewardEnabled == RewardType::SamePerDifficulty)
	{	// Fixed reward on each difficulty
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.blackBook.statPoints"),
			OFF_BlackBookPatch, EXP_BlackBookPatch, sizeof(EXP_BlackBookPatch), &g_questPluginOptions.BlackBookStatPointReward, 1)) {
			context->LogError("plugin-quests: Black Book reward patch failed.");
			return false;
		}
	}
	if (g_questPluginOptions.GoldenBirdRewardEnabled == RewardType::SamePerDifficulty)
	{
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.goldenBird.stat"),
				OFF_GoldenBirdStatPatch, EXP_GoldenBirdStatPatch, sizeof(EXP_GoldenBirdStatPatch), &g_questPluginOptions.GoldenBirdRewardStat, 1)
			|| !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.goldenBird.amount"),
				OFF_GoldenBirdAmountPatch, EXP_GoldenBirdAmountPatch, sizeof(EXP_GoldenBirdAmountPatch), &g_questPluginOptions.GoldenBirdRewardAmount, 4)) {
			context->LogError("plugin-quests: Golden Bird reward patch failed.");
			return false;
		}
	}
	if (g_questPluginOptions.SkillBookRewardEnabled == RewardType::SamePerDifficulty)
	{
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.radamentSkillBook.amount"),
				OFF_SkillBookAmountPatch, EXP_SkillBookAmountPatch, sizeof(EXP_SkillBookAmountPatch), &g_questPluginOptions.SkillBookRewardAmount, 1)
			|| !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.radamentSkillBook.stat"),
				OFF_SkillBookStatPatch, EXP_SkillBookStatPatch, sizeof(EXP_SkillBookStatPatch), &g_questPluginOptions.SkillBookRewardStat, 1)) {
			context->LogError("plugin-quests: Radament reward patch failed.");
			return false;
		}
	}

	if (g_questPluginOptions.AkaraCainRingRewardEnabled == RewardType::SamePerDifficulty)
	{	// Fixed item on each difficulty
		// Turn this into a XOR EAX,EAX; MOV AL, (reward)
		unsigned char rewardBytes[4] = { 0x31, 0xC0, 0xB0, g_questPluginOptions.AkaraCainRingQuality.Reward };

		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.akaraCainRing.item"),
				OFF_AkaraRingItem, EXP_AkaraRingItem, sizeof(EXP_AkaraRingItem), &g_questPluginOptions.AkaraCainRingItem.Reward, 4)
			|| !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.akaraCainRing.quality"),
				OFF_AkaraRingItemQuality, EXP_AkaraRingItemQuality, sizeof(EXP_AkaraRingItemQuality), rewardBytes, sizeof(rewardBytes))) {
			context->LogError("plugin-quests: Akara reward patch failed.");
			return false;
		}
	}
	else if (g_questPluginOptions.AkaraCainRingRewardEnabled == RewardType::DifferentPerDifficulty)
	{	// Different item/quality depending on difficulty. Uses PSh_PatchCallSite
		// (not context->PatchRel32) since Hook_AkaraCainRingDiffReward lives in
		// this plugin DLL, more than 2GB from the exe — see the comment above
		// PSh_PatchCallSite in plugin-shared.h.
		g_GiveQuestItemFn = reinterpret_cast<GiveQuestItemFn_t>(context->exeBase + OFF_GiveQuestItem);
		bool ok = PSh_ManifestPatchCallSite(context, PSH_MANIFEST_SITE("quests.akaraCainRing.difficultyCall"),
			OFF_AkaraRingCallSite, EXP_AkaraRingCallSite, sizeof(EXP_AkaraRingCallSite),
			reinterpret_cast<void*>(&Hook_AkaraCainRingDiffReward));
		D2RL::LogInfoF(context, "AkaraCainRing: DifferentPerDifficulty callsite patch %s", ok ? "installed" : "FAILED");
		if (!ok) return false;
	}

	if (g_questPluginOptions.OrmusGidbinnRingRewardEnabled == RewardType::SamePerDifficulty)
	{	// Fixed item on each difficulty
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.ormusGidbinnRing.item"),
				OFF_OrmusRingItem, EXP_OrmusRingItem, sizeof(EXP_OrmusRingItem), &g_questPluginOptions.OrmusGidbinnRingItem.Reward, 4)
			|| !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.ormusGidbinnRing.quality"),
				OFF_OrmusRingItemQuality, EXP_OrmusRingItemQuality, sizeof(EXP_OrmusRingItemQuality), &g_questPluginOptions.OrmusGidbinnRingQuality.Reward, 1)) {
			context->LogError("plugin-quests: Ormus reward patch failed.");
			return false;
		}
	}
	else if (g_questPluginOptions.OrmusGidbinnRingRewardEnabled == RewardType::DifferentPerDifficulty)
	{	// Different item/quality depending on difficulty — see the comment on the
		// Akara branch above for why this uses PSh_PatchCallSite.
		g_GiveQuestItemFn = reinterpret_cast<GiveQuestItemFn_t>(context->exeBase + OFF_GiveQuestItem);
		bool ok = PSh_ManifestPatchCallSite(context, PSH_MANIFEST_SITE("quests.ormusGidbinnRing.difficultyCall"),
			OFF_OrmusRingCallSite, EXP_OrmusRingCallSite, sizeof(EXP_OrmusRingCallSite),
			reinterpret_cast<void*>(&Hook_OrmusGidbinnRingDiffReward));
		D2RL::LogInfoF(context, "OrmusGidbinnRing: DifferentPerDifficulty callsite patch %s", ok ? "installed" : "FAILED");
		if (!ok) return false;
	}

	if (g_questPluginOptions.QualKehkRuneRewardEnabled == RewardType::SamePerDifficulty)
	{
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.qualKehkRunes.item1"),
				OFF_QualKehkItem1, EXP_QualKehkItem1, sizeof(EXP_QualKehkItem1), &g_questPluginOptions.QualKehkRuneItems[0].Reward, 3)
			|| !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.qualKehkRunes.item2"),
				OFF_QualKehkItem2, EXP_QualKehkItem2, sizeof(EXP_QualKehkItem2), &g_questPluginOptions.QualKehkRuneItems[1].Reward, 3)
			|| !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("quests.qualKehkRunes.item3"),
				OFF_QualKehkItem3, EXP_QualKehkItem3, sizeof(EXP_QualKehkItem3), &g_questPluginOptions.QualKehkRuneItems[2].Reward, 3)) {
			context->LogError("plugin-quests: Qual-Kehk reward patch failed.");
			return false;
		}
	}
	else if (g_questPluginOptions.QualKehkRuneRewardEnabled == RewardType::DifferentPerDifficulty)
	{	// see the comment on the Akara branch above for why this uses PSh_PatchCallSite
		g_exeBase = context->exeBase;
		g_GiveQuestItemFn = reinterpret_cast<GiveQuestItemFn_t>(context->exeBase + OFF_GiveQuestItem);
		bool ok = PSh_ManifestPatchCallSite(context, PSH_MANIFEST_SITE("quests.qualKehkRunes.difficultyCall"),
			OFF_QualKehkCallSite, EXP_QualKehkCallSite, sizeof(EXP_QualKehkCallSite),
			reinterpret_cast<void*>(&Hook_QualKehkRuneDiffReward));
		D2RL::LogInfoF(context, "QualKehkRunes: DifferentPerDifficulty callsite patch %s", ok ? "installed" : "FAILED");
		if (!ok) return false;
	}

	if (g_questPluginOptions.ImbueAllowSockets)
	{
		if (!PSh_ManifestPatchNop(context, PSH_MANIFEST_SITE("quests.imbueAllowSockets.guard1"),
				OFF_ImbueSocket1, EXP_ImbueSocket1, sizeof(EXP_ImbueSocket1), 2)
			|| !PSh_ManifestPatchNop(context, PSH_MANIFEST_SITE("quests.imbueAllowSockets.guard2"),
				OFF_ImbueSocket2, EXP_ImbueSocket2, sizeof(EXP_ImbueSocket2), 2)) {
			context->LogError("plugin-quests: Imbue Allow Sockets patch failed.");
			return false;
		}
	}

	const auto commit = hookTransaction.Commit(context);
	if (!commit.success) {
		if (commit.rollbackFailures != 0) {
			D2RL::LogErrorF(context,
				"plugin-quests: deferred hook commit failed with %zu rollback errors; the inactive DLL remains loaded as a safety barrier.",
				commit.rollbackFailures);
			return true;
		}
		context->LogError(
			"plugin-quests: deferred hook commit failed; direct writes were restored and guarded detours were deactivated.");
		return false;
	}

	return true;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderUnloadPlugin() noexcept {
	PSh_HookTransactionDeactivate();
	CleanupPluginQuestsState();
}
