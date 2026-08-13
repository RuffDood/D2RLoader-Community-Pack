#include <D2RLPlugin/api.h>
#include "bulk-skill-point-allocation.h"
#include "plugin-shared.h"
#include "skills-private.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// ── D2R function type aliases ─────────────────────────────────────────────────

using CompileSkillsTxt_t = void(__fastcall*)(uint8_t context);
using Consume_t       = int64_t(__fastcall*)(int64_t unit, int* playerUnit,
                                             int skillId, int skillLevel);
using DrainStat_t     = void(__fastcall*)(void* unit, int statId, int delta);
using GetManaCost_t   = int(__fastcall*)(uint8_t unitType, int skillId, int skillLevel);
using CheckStat_t     = bool(__fastcall*)(int* playerUnit, int64_t* skillStruct,
                                          int param3, int currentMana);
using ClientPredict_t = void(__fastcall*)(int* playerUnit, int skillId, int skillLevel);
using GetSkillLevel_t = int(__fastcall*)(int* playerUnit, int64_t* skillStruct, int param3);
using GetMaxSkillLevelForContext_t = int(__fastcall*)(uint8_t context, uint32_t index);
using MonDoSelfHeal_t        = int64_t(__fastcall*)(D2GameStrc* pGame, D2UnitStrc* pUnit,
                                                     int skillId, int param4);
using EvaluateSkillFormula_t = uint32_t(__fastcall*)(uint8_t bExpansion, D2UnitStrc* unit,
                                                      uint32_t calcSlot, int skillId, int param5);
using SetUnitStat_t          = void(__fastcall*)(D2UnitStrc* unit, int statId, int value, int layer);

// ── Addresses (offsets from exe base 0x140000000) ────────────────────────────

static constexpr uint64_t OFF_CompileSkillsTxt = 0x302380; // DATATBLS_CompileSkillsTxt
// D2GAME_sgptDataTables: array of per-context data-table struct pointers.
// Confirmed via decompile of DATATBLS_CompileSkillsTxt: lVar9 = (&DAT_142a9a580)
// [(longlong)context * 2] -- i.e. an 8-byte-element array indexed by context*2,
// so byte offset = context*16. The compiled skills.txt records live inside that
// per-context struct at +0x11b0 (record array pointer) / +0x11b8 (record count),
// with confirmed stride 0x2ec: pppuVar20 = *(lVar9+0x11b8)*0x2ec + *(lVar9+0x11b0).
static constexpr uint64_t OFF_DataTables                   = 0x2a9a580;
static constexpr uint64_t DATATABLES_SKILLS_RECORDS_OFFSET = 0x11b0;
static constexpr uint64_t DATATABLES_SKILLS_COUNT_OFFSET   = 0x11b8;
// OFF_Consume was a transcription bug for a long time (missing digit: 0x36830
// instead of 0x436830), which meant InstallInlineHook was hooking into the
// middle of an unrelated static-initializer function. Verified against
// debug.exe via decompile: FUN_140436830 embeds the literal source path
// "...\Skills\Skills.cpp" and calls D2Common_SKILLMANA_GetManaCost then
// D2GAME_SKILLS_BloodMana_6FD025E0, matching profile's
// D2GAME_SKILLMANA_Consume_6FD10A50 exactly.
//
// AuraConsume and ConsumeWeaponCharge are both **fully inlined** into this
// debug function with no standalone call boundary at all (confirmed via full
// decompile: the charge-drain logic and the BloodMana-or-mana-drain logic are
// both directly inline in FUN_140436830's body). The OFF_AuraConsume/
// OFF_ConsumeWeaponCharge constants that used to exist here were stale
// **profile.exe** addresses (exact matches to profile's named
// D2GAME_SKILLMANA_AuraConsume_6FD10C90/ConsumeWeaponCharge functions) that
// would have InstallInlineHook'd garbage in debug.exe — removed. See
// Hook_Consume for how the ManaCostsLife/Stamina redirect (AuraConsume's old
// job) is now reimplemented directly against the merged function via a
// before/after mana-stat delta, which needs no knowledge of Consume's
// internal charge-vs-mana branching. ChargedPctDrainStat (ConsumeWeaponCharge's
// old job) could not be safely reimplemented the same way — see the comment
// on bEnableChargedPctDrainStat's handling below.
static constexpr uint64_t OFF_Consume          = 0x436830;  // D2GAME_SKILLMANA_Consume (AuraConsume/ConsumeWeaponCharge both inlined here)
static constexpr uint64_t OFF_DrainStat        = 0x2f34f0; // FUN_140227470
static constexpr uint64_t OFF_GetManaCost      = 0x33aa00; // D2Common_SKILLMANA_GetManaCost (real standalone function, not inlined)
static constexpr uint64_t OFF_CheckStat        = 0x340900; // D2Common_SKILLMANA_CheckStat (real standalone function; debug build only reads 2 args, recomputes stat 6/8 internally instead of taking them as params 3/4 like profile does -- verify CheckStat_t/Hook_CheckStat before relying on param3/param4)
static constexpr uint64_t OFF_ClientPredict    = 0x2188b0; // FUN_140197080 (client-side mana prediction)
static constexpr uint64_t OFF_GetUseState      = 0x33f360; // SKILLS_GetUseState_6FDB0B70
static constexpr uint64_t OFF_GetUseState_Call = 0x21b180; // call site inside D2CLIENT_GetUnusableUseState
static constexpr uint64_t OFF_ClassicWW        = 0x5691e2;
// OFF_EnableWWCtC's *real* identity was misleading in the old naming: the
// patch site is NOT inside Whirlwind's own attack loop. It's a JNZ bail-out
// guard inside the generic "chance to cast on hit/attack" event caster.
// Confirmed via the "OnHitOrAttack" string literal (unique in both binaries):
// that string anchors profile's SUNIT_EvFunc_ItemApplyHitOrAttack, which --
// when the CtC roll succeeds -- calls SKILLS_SrvDo114_NecDoBoneSpear (a
// misleadingly-named shared "cast the proc'd skill" routine, not actually
// Bone Spear-specific) to perform the cast. Debug's counterpart, found via
// the same string anchor -> FUN_140583b30 -> FUN_1405896e0, refactored the
// old inline `CMP [table+0x298],0x36 / TEST [state+0xaf4],0x400000` guard
// into a real call: `MOV EDX,0x36; CALL FUN_1403351b0; TEST EAX,EAX; JNZ`
// (FUN_1403351b0 -> FUN_1402f6220 turns out to be a generic
// "does unit have bit-flag N set" state test, called here with N=0x36/54 --
// matches the profile bit position 0xaf4*8+22=0x436, i.e. bit 54, exactly).
// NOPing this JNZ (same 6-byte length as profile's near JNZ) forces the
// cast to always proceed regardless of that state, exactly mirroring
// profile's patch semantics one-for-one.
static constexpr uint64_t OFF_EnableWWCtC      = 0x589736;
static constexpr uint64_t OFF_Telekinesis      = 0x554936;
static constexpr uint64_t OFF_GetSkillLevel    = 0x3400a0; // SKILLS_GetSkillLevel (profile 0x264b20)
static constexpr uint64_t OFF_GetMaxSkillLevelForContext = 0x300c70; // debug-only helper; profile inlines this as a raw D2GAME_sgptDataTables[ctx*2]+0x14e0 double-deref, no separate profile function exists
static constexpr uint64_t OFF_MonDoSelfHeal          = 0x57d030; // SKILLS_SrvDo169_MonDoSelfHeal
static constexpr uint64_t OFF_EvaluateSkillFormula   = 0x3b5160; // SKILLS_EvaluateSkillFormula
static constexpr uint64_t OFF_SetUnitStat            = 0x2f7d10; // STATLIST_SetUnitStat

// ── Expected original bytes (verified against the D2R.exe 3.2.92777 reference image) ────
// D2RLoader requires non-null expected bytes for PatchBytes/PatchRel32/
// InstallInlineHook calls so it can verify the patch site before writing.
static constexpr uint8_t EXP_CompileSkillsTxt[24] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89,
	0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
};
static constexpr uint8_t EXP_Consume[16] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
};
static constexpr uint8_t EXP_CheckStat[14] = {
	0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x45, 0x33, 0xC0,
};
static constexpr uint8_t EXP_ClientPredict[20] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
	0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20,
};
static constexpr uint8_t EXP_GetUseState_Call[5] = { 0xE8, 0xDB, 0x41, 0x12, 0x00 };
static constexpr uint8_t EXP_ClassicWW[5]        = { 0xE8, 0x19, 0x1A, 0x00, 0x00 };
static constexpr uint8_t EXP_EnableWWCtC[6]      = { 0x0F, 0x85, 0xC1, 0x00, 0x00, 0x00 };
static constexpr uint8_t EXP_Telekinesis[5]      = { 0xE8, 0x35, 0xA8, 0xDE, 0xFF };
static constexpr uint8_t EXP_MonDoSelfHeal[20]   = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
	0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x4C, 0x8B,
};

static constexpr uint32_t SKILLSRECORD_TYPE_BOOL = 29;
static constexpr int      MANA_COSTS_LIFE_BIT    = 47;   // first free bit
static constexpr int      MANA_COSTS_STAMINA_BIT = 48;   // second free bit

// ── Plugin state ──────────────────────────────────────────────────────────────

static SkillPluginOptions            g_skillPluginOptions {};
static uintptr_t                     g_ExeBase  = 0;
static const D2RL::PluginContext*    g_Context  = nullptr;

// Compiled skills.txt records (game-owned memory — do not free).
static D2SkillsTxt* g_SkillsRecords = nullptr;
static uint64_t      g_SkillsCount  = 0;

using GetUseState_t    = int(__fastcall*)(int* playerUnit, int64_t* pSkill);
using NeedManaSound_t  = int(__fastcall*)(int* skillEntity, uint32_t* outPriority);
using DiagB2300_t      = void(__fastcall*)(void* param_1, void* param_2);
using DiagDaf0_t       = void(__fastcall*)(int* param_1, void* param_2, uint8_t param_3, char param_4, uint32_t* param_5);
using PlaySoundEffect_t = int64_t(__fastcall*)(int soundId, int* entity, int param3, int param4, int param5);

static Consume_t             Original_Consume            = nullptr;
static CheckStat_t           Original_CheckStat          = nullptr;
static ClientPredict_t       Original_ClientPredict      = nullptr;
static CompileSkillsTxt_t    Original_CompileSkillsTxt   = nullptr;
static void* Original_CanBePickedUpWithTelekinesis = nullptr;
static MonDoSelfHeal_t       Original_MonDoSelfHeal      = nullptr;

// ── ManaCostsLife/ManaCostsStamina column application ───────────────────────
//
// The original design tried to inject ManaCostsLife/ManaCostsStamina as new
// field descriptors into DATATBLS_CompileTxt's parse of skills.txt (redirecting
// the CALL instructions inside DATATBLS_CompileSkillsTxt that invoke it). That
// assumed CompileTxt took a (name, binPath, fields[], recordSize, output)-style
// signature. Decompiling both DATATBLS_CompileTxt and its actual call sites
// disproved this: the call sites don't pass a per-table name/descriptor array at
// all (one call site's "name" slot resolves to the unrelated static string
// "monstats"; the "recordSize" slot holds a bare literal 2; the "fields"/"output"
// slots point to small internal scratch structures, not a real descriptor array
// or a D2TxtContainer). Extending that call was never viable.
//
// The mod's own skills.txt (e.g. Reborne) *already* has manacostslife/
// manacostsstamina columns — they're just not read by the vanilla engine, which
// is exactly why this plugin exists. Rather than fight the engine's internal
// compiler ABI, we let DATATBLS_CompileSkillsTxt run completely untouched (entry
// hook, call Original first) and then read the exact same loose skills.txt file
// ourselves afterward, matching rows to the already-compiled records 1:1 (cross-
// checked against each row's own *Id column), and set the corresponding flags
// bit directly. This needs no knowledge of the compiler's internal ABI at all.

static std::wstring BuildSkillsTxtPath()
{
	if (!g_Context || !g_Context->modDirectory)
		return {};
	std::wstring path = g_Context->modDirectory;
	path += L"\\data\\global\\excel\\skills.txt";
	return path;
}

static std::vector<std::string> SplitTabs(const std::string& line)
{
	std::vector<std::string> fields;
	size_t s = 0;
	for (size_t i = 0; i <= line.size(); ++i) {
		if (i == line.size() || line[i] == '\t') {
			fields.push_back(line.substr(s, i - s));
			s = i + 1;
		}
	}
	return fields;
}

static void ApplyManaCostsColumnsFromTxt()
{
	if (!g_SkillsRecords || g_SkillsCount == 0)
		return;

	std::wstring path = BuildSkillsTxtPath();
	if (path.empty())
		return;

	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		if (g_Context) g_Context->LogWarn("plugin-skills: could not open skills.txt for ManaCosts column read");
		return;
	}
	std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	std::vector<std::string> lines;
	size_t start = 0;
	for (size_t i = 0; i <= content.size(); ++i) {
		if (i == content.size() || content[i] == '\n') {
			std::string line = content.substr(start, i - start);
			if (!line.empty() && line.back() == '\r') line.pop_back();
			lines.push_back(std::move(line));
			start = i + 1;
		}
	}
	if (lines.empty())
		return;

	std::vector<std::string> header = SplitTabs(lines[0]);
	int idCol = -1, lifeCol = -1, staminaCol = -1;
	for (size_t i = 0; i < header.size(); ++i) {
		if      (header[i] == "*Id")              idCol      = static_cast<int>(i);
		else if (header[i] == "manacostslife")    lifeCol    = static_cast<int>(i);
		else if (header[i] == "manacostsstamina") staminaCol = static_cast<int>(i);
	}
	if (lifeCol < 0 && staminaCol < 0)
		return; // this mod's skills.txt doesn't define these columns; nothing to apply.

	uint64_t recordIndex = 0;
	for (size_t li = 1; li < lines.size() && recordIndex < g_SkillsCount; ++li) {
		if (lines[li].empty())
			continue;
		std::vector<std::string> fields = SplitTabs(lines[li]);
		if (fields.empty() || fields[0].empty())
			continue; // spacer/expansion rows (empty name column) aren't compiled as records.

		if (idCol >= 0 && idCol < static_cast<int>(fields.size()) && !fields[idCol].empty()) {
			char* endp = nullptr;
			long id = std::strtol(fields[idCol].c_str(), &endp, 10);
			if (endp != fields[idCol].c_str() && static_cast<uint64_t>(id) != recordIndex) {
				if (g_Context) g_Context->LogWarn("plugin-skills: skills.txt row/*Id mismatch, aborting ManaCosts column apply");
				return;
			}
		}

		D2SkillsTxt* rec = g_SkillsRecords + recordIndex;

		if (lifeCol >= 0 && lifeCol < static_cast<int>(fields.size()) && !fields[lifeCol].empty() && fields[lifeCol] != "0")
			rec->dwFlags |= (uint64_t{1} << MANA_COSTS_LIFE_BIT);
		if (staminaCol >= 0 && staminaCol < static_cast<int>(fields.size()) && !fields[staminaCol].empty() && fields[staminaCol] != "0")
			rec->dwFlags |= (uint64_t{1} << MANA_COSTS_STAMINA_BIT);

		++recordIndex;
	}
}

// ── Hook: DATATBLS_CompileSkillsTxt (function-entry hook, not a call-site
// redirect) ───────────────────────────────────────────────────────────────
// Confirmed via decompile to take a single uint8_t context parameter (the only
// register read in its prologue: MOVZX R14D, CL). Runs the real compile
// unmodified, then locates the resulting records via the game's own
// D2GAME_sgptDataTables[context] entry (see OFF_DataTables above) and applies
// the ManaCostsLife/ManaCostsStamina columns from skills.txt directly.
void __fastcall Hook_CompileSkillsTxt(uint8_t context)
{
	Original_CompileSkillsTxt(context);

	uintptr_t tableBase = *reinterpret_cast<uintptr_t*>(g_ExeBase + OFF_DataTables + static_cast<uint64_t>(context) * 16);
	g_SkillsRecords = *reinterpret_cast<D2SkillsTxt**>(tableBase + DATATABLES_SKILLS_RECORDS_OFFSET);
	g_SkillsCount   = *reinterpret_cast<uint64_t*>(tableBase + DATATABLES_SKILLS_COUNT_OFFSET);

	ApplyManaCostsColumnsFromTxt();
}

// Callers like Hook_GetUseState pass a record pointer read directly out of a
// skill entity (*pSkill) with no guarantee it's non-null or in-range -- e.g.
// empty/unassigned hotbar slots carry a null skills-record pointer. Validate
// against g_SkillsRecords' actual bounds before ever dereferencing it;
// SkillManaCostsLife/Stamina's own skillId-computed rec is already guaranteed
// in-range but this check is cheap enough to apply unconditionally anyway.
static bool IsValidSkillsRecord(const D2SkillsTxt* rec)
{
	if (!rec || !g_SkillsRecords || g_SkillsCount == 0)
		return false;
	if (rec < g_SkillsRecords)
		return false;
	uint64_t index = static_cast<uint64_t>(rec - g_SkillsRecords);
	return index < g_SkillsCount;
}

static bool SkillRecManaCostsLife(const D2SkillsTxt* rec)
{
	if (!IsValidSkillsRecord(rec))
		return false;
	return (rec->dwFlags >> MANA_COSTS_LIFE_BIT) & 1;
}

static bool SkillManaCostsLife(int skillId) noexcept {
	if (!g_SkillsRecords || skillId < 0 || static_cast<uint64_t>(skillId) >= g_SkillsCount)
		return false;
	return SkillRecManaCostsLife(g_SkillsRecords + skillId);
}

static bool SkillRecManaCostsStamina(const D2SkillsTxt* rec)
{
	if (!IsValidSkillsRecord(rec))
		return false;
	return (rec->dwFlags >> MANA_COSTS_STAMINA_BIT) & 1;
}

static bool SkillManaCostsStamina(int skillId) noexcept {
	if (!g_SkillsRecords || skillId < 0 || static_cast<uint64_t>(skillId) >= g_SkillsCount)
		return false;
	return SkillRecManaCostsStamina(g_SkillsRecords + skillId);
}

// ── Hook: D2Common_SKILLMANA_CheckStat ───────────────────────────────────────
// Called by GetUseState to decide if the skill can be cast (returns false → red orb).
//
// The profile build took the current stat value as an explicit param4, so the
// original approach here was to just substitute life/stamina for mana and let
// Original_CheckStat do the comparison. That trick does NOT work against the
// debug build: its CheckStat ignores param3/param4 entirely and always
// re-derives both mana (stat 8) and life (stat 6) itself via direct GetStat
// calls, picking between them based on its own internal "Blood Mana" state
// flag -- passing a substituted currentAlt through would silently be ignored.
//
// Fix: when a skill's cost is redirected to life/stamina, bypass
// Original_CheckStat entirely and replicate its level/cost computation
// ourselves (decompile-verified against debug FUN_140340900), evaluated
// against the correct alternate stat instead of mana.
bool __fastcall Hook_CheckStat(int* playerUnit, int64_t* skillStruct,
                                int param3, int currentMana)
{
    if (playerUnit && skillStruct && g_SkillsRecords) {
        auto recPtr  = *reinterpret_cast<const uintptr_t*>(skillStruct);
        auto base    = reinterpret_cast<uintptr_t>(g_SkillsRecords);
        auto byteOff = recPtr - base;
        if (recPtr >= base && byteOff < g_SkillsCount * sizeof(D2SkillsTxt)
                           && byteOff % sizeof(D2SkillsTxt) == 0) {
            int skillId = static_cast<int>(byteOff / sizeof(D2SkillsTxt));
            int altStatId = 0;
            if      (SkillManaCostsLife(skillId))    altStatId = 6;  // life
            else if (SkillManaCostsStamina(skillId)) altStatId = 10; // stamina
            if (altStatId) {
                auto* unitStrc = reinterpret_cast<D2UnitStrc*>(playerUnit);
                if (unitStrc->statList) {
                    // Replicate CheckStat's own level derivation: base level
                    // (skillStruct+0x40) + bonus levels, clamped to [0, maxLevel].
                    auto GetSkillLevel = reinterpret_cast<GetSkillLevel_t>(g_ExeBase + OFF_GetSkillLevel);
                    int64_t baseLevel  = skillStruct[8]; // offset 0x40
                    int bonusLevel     = GetSkillLevel(playerUnit, skillStruct, 0);
                    int level = static_cast<int>(baseLevel) + bonusLevel;
                    if (level < 0) level = 0;

                    auto GetMaxSkillLevel = reinterpret_cast<GetMaxSkillLevelForContext_t>(g_ExeBase + OFF_GetMaxSkillLevelForContext);
                    int maxLevel = GetMaxSkillLevel(unitStrc->itemTableEntry, 0);
                    if (level > maxLevel) level = maxLevel;

                    auto GetManaCost = reinterpret_cast<GetManaCost_t>(g_ExeBase + OFF_GetManaCost);
                    int manaCost = GetManaCost(unitStrc->itemTableEntry, skillId, level);

                    int currentAlt = PSh_GetStat(g_ExeBase, unitStrc, altStatId);
                    return currentAlt >= manaCost;
                }
            }
        }
    }
    return Original_CheckStat(playerUnit, skillStruct, param3, currentMana);
}

// ── Hook: FUN_140197080 (client-side mana prediction) ────────────────────────
// Called client-side (local player only) to optimistically drain mana before the
// server confirms the cast. Without this hook, the client pre-drains mana, the
// server drains life (our hook), and the server's authoritative stat packet snaps
// the display back — visible rubber-banding for ~1 server tick.
// Fix: drain life immediately on the client to match what the server will do.
// We skip the ring-buffer that the original writes (it's for mana reconciliation
// only); accepting a possible sub-tick snap is far less noticeable than a full
// mana rubber-band.

void __fastcall Hook_ClientPredict(int* playerUnit, int skillId, int skillLevel)
{
    if (*playerUnit == 0 && g_SkillsRecords) {
        int altStatId = 0;
        if      (SkillManaCostsLife(skillId))    altStatId = 6;
        else if (SkillManaCostsStamina(skillId)) altStatId = 10;

        if (altStatId) {
            auto* unitStrc   = reinterpret_cast<D2UnitStrc*>(playerUnit);
            auto GetManaCost = reinterpret_cast<GetManaCost_t>(g_ExeBase + OFF_GetManaCost);
            int manaCost = GetManaCost(unitStrc->itemTableEntry, skillId, skillLevel);
            if (manaCost >= 1 && unitStrc->statList) {
                auto DrainStat = reinterpret_cast<DrainStat_t>(g_ExeBase + OFF_DrainStat);
                int currentAlt = PSh_GetStat(g_ExeBase, unitStrc, altStatId);
                if (currentAlt >= manaCost)
                    DrainStat(playerUnit, altStatId, -manaCost);
            }
            return;
        }
    }
    Original_ClientPredict(playerUnit, skillId, skillLevel);
}

// ── Charge-item cast detection (ConsumeWeaponCharge's gating check) ──────────
// Debug's Consume (FUN_140436830) decides charge-vs-mana by testing, in order:
//   lVar5 = FUN_14034ba40(playerUnit)      == *(uint64_t*)(playerUnit->_unk0x100 + 0x18)
//   FUN_14033e080(lVar5)  == skillId       == *(short*)(*lVar5)
//   FUN_14033cc10(lVar5)  != -1            == *(int*)(lVar5 + 0x4c)
// All three sub-calls were decompiled directly (not inferred) and match the
// profile build's independently-named D2GAME_SKILLMANA_Consume byte-for-byte
// (chargeItem = *(playerUnit->field182_0x100 + 0x18); *(short*)*chargeItem ==
// skillId; *(int*)(chargeItem+0x4c) != -1). playerUnit->_unk0x100 is already
// a static_assert-verified field (plugin-shared.h), so this whole chain is
// now confirmed rather than guessed — see docs/offset-migration-status.md.
static bool IsChargeItemCast(D2UnitStrc* unitStrc, int skillId)
{
	if (!unitStrc) return false;
	auto slotBase = static_cast<uintptr_t>(unitStrc->_unk0x100);
	if (!slotBase) return false;
	auto slot = *reinterpret_cast<uintptr_t*>(slotBase + 0x18);
	if (!slot) return false;
	auto itemPtr = *reinterpret_cast<uintptr_t*>(slot);
	if (!itemPtr) return false;
	int16_t itemSkillCode = *reinterpret_cast<int16_t*>(itemPtr);
	int32_t chargeGuard    = *reinterpret_cast<int32_t*>(slot + 0x4c);
	return itemSkillCode == static_cast<int16_t>(skillId) && chargeGuard != -1;
}

// ── Hook: D2GAME_SKILLMANA_Consume ───────────────────────────────────────────
// AuraConsume and ConsumeWeaponCharge are both fully inlined into this
// function in the debug build (no standalone call boundary survives — see the
// comment by OFF_Consume's declaration), so both of their old jobs are
// reimplemented here directly instead of via separate inline hooks.
//
// ManaCostsLife/ManaCostsStamina redirect (AuraConsume's old job): read mana
// before and after calling Original_Consume, and if it actually decreased,
// refund it and drain the alternate stat by the same amount instead. This is
// correct in every case the original design cared about: a charge-item cast
// never touches mana, so this is a no-op for it; a BloodMana-diverted cast
// already drained life via the engine's own BloodMana path, so mana didn't
// move and this is a no-op for it too; only a genuine mana-cost cast
// triggers the redirect.
//
// ChargedPctDrainStat (ConsumeWeaponCharge's old job, "give a % chance to
// skip draining a charge"): charges are stored as a packed (current |
// max<<8) value written via a dedicated setter (FUN_1402f5df0/FUN_1402f7c20
// in debug), keyed off an item/stat-code pair Original_Consume derives via
// its own internal helper (FUN_1403d4950) — there's no cheap after-the-fact
// "refund one charge" trick the way there is for a plain additive stat like
// mana. Instead, when IsChargeItemCast() confirms this is a charge-based
// cast and the percent roll succeeds, skip calling Original_Consume entirely
// and report success. Caveat: this bypasses vanilla's own "already at 0
// charges" refusal check inside ConsumeWeaponCharge -- in practice a client
// shouldn't be able to reach Consume for a 0-charge item at all (the same
// upstream GetUseState-family gating that blocks insufficient-mana casts
// should already block this), but that specific path hasn't been
// independently verified the way the cast-detection chain above has.
int64_t __fastcall Hook_Consume(int64_t unit, int* playerUnit, int skillId, int skillLevel) {
	int altStatId = 0;
	if      (SkillManaCostsLife(skillId))    altStatId = 6;
	else if (SkillManaCostsStamina(skillId)) altStatId = 10;

	if (altStatId && playerUnit) {
		auto* unitStrc = reinterpret_cast<D2UnitStrc*>(playerUnit);
		if (unitStrc->statList) {
			int manaBefore = PSh_GetStat(g_ExeBase, unitStrc, 8 /* mana */);
			int64_t result = Original_Consume(unit, playerUnit, skillId, skillLevel);
			if (result != 0) {
				int manaAfter = PSh_GetStat(g_ExeBase, unitStrc, 8);
				int drained = manaBefore - manaAfter;
				if (drained > 0) {
					auto DrainStat = reinterpret_cast<DrainStat_t>(g_ExeBase + OFF_DrainStat);
					DrainStat(playerUnit, 8, drained);          // refund mana
					DrainStat(playerUnit, altStatId, -drained); // drain the alternate stat instead
				}
			}
			return result;
		}
	}

	if (g_skillPluginOptions.bEnableChargedPctDrainStat && playerUnit) {
		auto* unitStrc = reinterpret_cast<D2UnitStrc*>(playerUnit);
		if (unitStrc->statList && IsChargeItemCast(unitStrc, skillId)) {
			int pct = PSh_GetStat(g_ExeBase, unitStrc, g_skillPluginOptions.ChargedPctDrainStat);
			if (pct > 0) {
				uint64_t roll = PSh_RollUnit(unitStrc);
				if (static_cast<uint32_t>(roll) % 100 < static_cast<uint32_t>(pct)) {
					return 1; // skip the charge drain; the skill cast itself is handled elsewhere
				}
			}
		}
	}

	return Original_Consume(unit, playerUnit, skillId, skillLevel);
}

// ── Hook: SKILLS_GetUseState call site inside D2CLIENT_GetUnusableUseState ───
// Intercepts the single CALL at 0x1401a2580; all other callers of GetUseState
// are unaffected.

int __fastcall Hook_GetUseState(int* playerUnit, int64_t* pSkill)
{
    auto Original = reinterpret_cast<GetUseState_t>(g_ExeBase + OFF_GetUseState);
	int value = Original(playerUnit, pSkill);
	if (value == 1 &&
		(SkillRecManaCostsLife((const D2SkillsTxt*)*pSkill) || SkillRecManaCostsStamina((const D2SkillsTxt*)*pSkill)))
	{
		return 2;
	}
	return value;
}

// Hook: SKILLS_CanBePickedUpWithTelekinesis. Original function located at 140268e30
int __fastcall Hook_CanBePickedUpWithTelekinesis(D2UnitStrc* ItemUnit)
{
	// ITEMS_CheckItemTypeId(ItemUnit, ITEM_TYPE_XXX) --> 140245230 if you want to check this yourself
	return ItemUnit != nullptr && PSh_UnitType(*ItemUnit) == D2UnitType::Item;
}

// ── Hook: SKILLS_SrvDo169_MonDoSelfHeal ──────────────────────────────────────
// Vanilla computes a target *absolute* stat value from a monster-level min/max
// table keyed off pUnit->itemTableEntry, then clamps to max(target, current) --
// meaningless for a player unit (the table is monster-only), so the "heal"
// silently collapses to a few raw points. skills.txt's native Param1/Param2
// columns are never read by vanilla MonDoSelfHeal, so they're repurposed here:
//   Param1: 0 = heal TO a percentage of the max stat, 1 = heal BY a percentage
//           of the max stat (added to current).
//   Param2: 0 = life stat, 1 = mana stat.
// The percentage itself still comes from the skill's own Calc1 slot, evaluated
// via the same SKILLS_EvaluateSkillFormula vanilla used.
int64_t __fastcall Hook_MonDoSelfHeal(D2GameStrc* pGame, D2UnitStrc* pUnit, int skillId, int param4)
{
	const D2SkillsTxt* rec = (skillId >= 0 && g_SkillsRecords &&
	                           static_cast<uint64_t>(skillId) < g_SkillsCount)
		? g_SkillsRecords + skillId
		: nullptr;

	if (rec && pUnit && pUnit->statList) {
		auto EvaluateSkillFormula = reinterpret_cast<EvaluateSkillFormula_t>(g_ExeBase + OFF_EvaluateSkillFormula);
		uint32_t percent = EvaluateSkillFormula(pGame->expansion, pUnit, rec->nCalc1, skillId, param4);

		int statId    = (rec->nParam2 == 0) ? 6 : 8;
		int maxStatId = (rec->nParam2 == 0) ? 7 : 9;
		int current   = PSh_GetStat(g_ExeBase, pUnit, statId);
		int maxVal    = PSh_GetStat(g_ExeBase, pUnit, maxStatId);

		int64_t target;
		if (rec->nParam1 == 0) {
			// heal TO a percentage of max -- never reduces current value
			target = std::max<int64_t>(current, static_cast<int64_t>(maxVal) * percent / 100);
		} else {
			// heal BY a percentage of max -- additive, capped at max
			target = std::min<int64_t>(maxVal, current + static_cast<int64_t>(maxVal) * percent / 100);
		}

		auto SetUnitStat = reinterpret_cast<SetUnitStat_t>(g_ExeBase + OFF_SetUnitStat);
		SetUnitStat(pUnit, statId, static_cast<int>(target), 0);
		return 1;
	}

	return Original_MonDoSelfHeal(pGame, pUnit, skillId, param4);
}

// ── INI loading ───────────────────────────────────────────────────────────────

void SkillPluginOptions::Load(const D2RL::PluginContext* /*context*/, const nlohmann::json& cfg) {
	bEnableManaCostsLife          = cfg.value("manaCostsLife", false);
	bEnableManaCostsStamina       = cfg.value("manaCostsStamina", false);
	bEnableClassicWW              = cfg.value("classicWhirlwind", false);
	bEnableWWCtc                  = cfg.value("whirlwindCtC", false);
	bTelekinesisPicksUpEverything = cfg.value("telekinesisPicksUpEverything", false);

	auto drain = cfg.value("chargedPctDrainStat", nlohmann::json::object());
	bEnableChargedPctDrainStat = drain.value("enabled", false);
	const auto statEntry = drain.find("statId");
	if (statEntry == drain.end()) {
		ChargedPctDrainStat = 0;
	}
	else {
		if (!statEntry->is_number_integer()) {
			throw std::runtime_error(
				"plugin-skills: skills.chargedPctDrainStat.statId must be an integer.");
		}
		const auto statId = statEntry->get<int64_t>();
		if (statId < 0 || statId > 511) {
			throw std::runtime_error(
				"plugin-skills: skills.chargedPctDrainStat.statId must be between 0 and 511.");
		}
		ChargedPctDrainStat = static_cast<int>(statId);
	}

	bEnableSelfHealParams = cfg.value("selfHealParams", false);
}

// ── Plugin exports ────────────────────────────────────────────────────────────

static constexpr D2RL::PluginInfo PluginInfo {
	.infoSize   = D2RL::PluginInfoSize,
	.apiVersion = D2RL_PLUGIN_API_VERSION,
	.id         = "eezstreet-plugin-skills",
	.name       = "eezstreet Skills Plugin",
	.version    = PSh_PluginPackVersion,
	.author     = "eezstreet",
	.description = "Various skill-related changes.",
	.flags      = D2RL::PluginFlags::NativeHooks,
};

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &PluginInfo;
}

static void CleanupPluginSkillsState() noexcept
{
	RuffnecKk::BulkSkillPointAllocation::Unload();
	g_SkillsRecords = nullptr;
	g_SkillsCount = 0;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!PSh_ValidatePluginTarget(context))
		return false;

	nlohmann::json skillsConfig;
	try {
		auto cfg = PSh_Json_LoadConfig(context);
		skillsConfig = PSh_Json_GetSection(cfg, "skills");
		g_skillPluginOptions.Load(context, skillsConfig);
	}
	catch (const std::exception& error) {
		PSh_Json_LogConfigError(context, error);
		return false;
	}
	const auto expected = [context](uint64_t rva, const auto& bytes) noexcept {
		return context->CheckExpectedBytes(rva, bytes, sizeof(bytes));
	};
	bool originalSignaturesValid = true;
	if (g_skillPluginOptions.bEnableManaCostsLife || g_skillPluginOptions.bEnableManaCostsStamina)
		originalSignaturesValid = originalSignaturesValid
			&& expected(OFF_CompileSkillsTxt, EXP_CompileSkillsTxt)
			&& expected(OFF_CheckStat, EXP_CheckStat)
			&& expected(OFF_ClientPredict, EXP_ClientPredict)
			&& expected(OFF_GetUseState_Call, EXP_GetUseState_Call);
	if (g_skillPluginOptions.bEnableManaCostsLife || g_skillPluginOptions.bEnableManaCostsStamina
		|| g_skillPluginOptions.bEnableChargedPctDrainStat)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_Consume, EXP_Consume);
	if (g_skillPluginOptions.bEnableClassicWW)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_ClassicWW, EXP_ClassicWW);
	if (g_skillPluginOptions.bEnableWWCtc)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_EnableWWCtC, EXP_EnableWWCtC);
	if (g_skillPluginOptions.bTelekinesisPicksUpEverything)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_Telekinesis, EXP_Telekinesis);
	if (g_skillPluginOptions.bEnableSelfHealParams)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_MonDoSelfHeal, EXP_MonDoSelfHeal);
	if (!originalSignaturesValid) {
		context->LogError("plugin-skills: configured eezstreet patch-set signature mismatch; no skill patch was applied.");
		return false;
	}
	g_ExeBase = context->exeBase;
	g_Context = context;
	PSh_HookTransactionScope hookTransaction(&CleanupPluginSkillsState);
	if (!hookTransaction.IsActive()) {
		context->LogError("plugin-skills: could not initialize the deferred hook transaction.");
		return false;
	}

	if (g_skillPluginOptions.bEnableManaCostsLife || g_skillPluginOptions.bEnableManaCostsStamina) {
		// Entry hook on DATATBLS_CompileSkillsTxt itself -- see the comment above
		// ApplyManaCostsColumnsFromTxt for why this replaced the old call-site
		// redirect into DATATBLS_CompileTxt.
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("skills.manaCosts.compileSkillsTxt"),
			OFF_CompileSkillsTxt, EXP_CompileSkillsTxt, sizeof(EXP_CompileSkillsTxt), Hook_CompileSkillsTxt, &Original_CompileSkillsTxt)) {
			D2RL::LogErrorF(context, "plugin-skills: failed to hook CompileSkillsTxt");
			return false;
		}

		// Hook CheckStat so the skill orb turns red when life (not mana) is too low.
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("skills.manaCosts.checkStat"),
			OFF_CheckStat, EXP_CheckStat, sizeof(EXP_CheckStat), Hook_CheckStat, &Original_CheckStat)) {
			D2RL::LogErrorF(context, "plugin-skills: failed to hook CheckStat");
			return false;
		}

		// Hook client-side mana prediction to drain life instead (prevents rubber-banding).
		// First 6 bytes: push rbx (2) + sub rsp,0x20 (4) — old API needed hookSize=6 here;
		// the new InstallInlineHook has no hookSize param, trusting the loader to size it.
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("skills.manaCosts.clientPredict"),
			OFF_ClientPredict, EXP_ClientPredict, sizeof(EXP_ClientPredict), Hook_ClientPredict, &Original_ClientPredict)) {
			D2RL::LogErrorF(context, "plugin-skills: failed to hook ClientPredict");
			return false;
		}

		// Redirect the single CALL at 0x1401a2580 inside D2CLIENT_GetUnusableUseState.
		// Uses PSh_PatchCallSite (not context->PatchRel32) since Hook_GetUseState
		// lives in this plugin DLL, more than 2GB from the exe — see the comment
		// above PSh_PatchCallSite in plugin-shared.h.
		if (!PSh_ManifestPatchCallSite(context, PSH_MANIFEST_SITE("skills.manaCosts.getUseStateCall"),
			OFF_GetUseState_Call, EXP_GetUseState_Call, sizeof(EXP_GetUseState_Call),
				reinterpret_cast<void*>(&Hook_GetUseState))) {
			D2RL::LogErrorF(context, "plugin-skills: failed to patch GetUseState call site");
			return false;
		}
	}

	if (g_skillPluginOptions.bEnableManaCostsLife || g_skillPluginOptions.bEnableManaCostsStamina
		|| g_skillPluginOptions.bEnableChargedPctDrainStat) {
		// Hook Consume; it handles the ManaCostsLife/Stamina redirect and the
		// ChargedPctDrainStat skip itself now (see the comment above Hook_Consume).
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("skills.consume"),
			OFF_Consume, EXP_Consume, sizeof(EXP_Consume), Hook_Consume, &Original_Consume)) {
			D2RL::LogErrorF(context, "plugin-skills: failed to hook Consume");
			return false;
		}
	}

	if (g_skillPluginOptions.bEnableClassicWW)
	{
		uint8_t classicWWBytes[] = { 0xB8, 0x01, 0x00, 0x00, 0x00 };
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("skills.classicWhirlwind"),
			OFF_ClassicWW, EXP_ClassicWW, sizeof(EXP_ClassicWW), classicWWBytes, sizeof(classicWWBytes))) {
			D2RL::LogErrorF(context, "plugin-skills: Classic Whirlwind patch failed");
			return false;
		}
	}

	if (g_skillPluginOptions.bEnableWWCtc)
	{
		uint8_t ctcWWBytes[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("skills.whirlwindCtC"),
			OFF_EnableWWCtC, EXP_EnableWWCtC, sizeof(EXP_EnableWWCtC), ctcWWBytes, sizeof(ctcWWBytes))) {
			D2RL::LogErrorF(context, "plugin-skills: Whirlwind CtC patch failed");
			return false;
		}
	}

	if (g_skillPluginOptions.bTelekinesisPicksUpEverything)
	{
		// Uses PSh_PatchCallSite (not context->PatchRel32) since
		// Hook_CanBePickedUpWithTelekinesis lives in this plugin DLL, more than
		// 2GB from the exe — see the comment above PSh_PatchCallSite in
		// plugin-shared.h.
		if (!PSh_ManifestPatchCallSite(context, PSH_MANIFEST_SITE("skills.telekinesisPicksUpEverything"),
			OFF_Telekinesis, EXP_Telekinesis, sizeof(EXP_Telekinesis),
				reinterpret_cast<void*>(&Hook_CanBePickedUpWithTelekinesis))) {
			D2RL::LogErrorF(context, "plugin-skills: Telekinesis call-site patch failed");
			return false;
		}
	}

	if (g_skillPluginOptions.bEnableSelfHealParams)
	{
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("skills.selfHealParams"),
			OFF_MonDoSelfHeal, EXP_MonDoSelfHeal, sizeof(EXP_MonDoSelfHeal), Hook_MonDoSelfHeal, &Original_MonDoSelfHeal)) {
			D2RL::LogErrorF(context, "plugin-skills: failed to hook MonDoSelfHeal");
			return false;
		}
	}

	if (!RuffnecKk::BulkSkillPointAllocation::Load(context, skillsConfig)) {
		return false;
	}

	const auto commit = hookTransaction.Commit(context);
	if (!commit.success) {
		if (commit.rollbackFailures != 0) {
			D2RL::LogErrorF(context,
				"plugin-skills: deferred hook commit failed with %zu rollback errors; the inactive DLL remains loaded as a safety barrier.",
				commit.rollbackFailures);
			return true;
		}
		context->LogError(
			"plugin-skills: deferred hook commit failed; direct writes were restored and guarded detours were deactivated.");
		return false;
	}

	return true;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderUnloadPlugin() noexcept {
	PSh_HookTransactionDeactivate();
	CleanupPluginSkillsState();
}
