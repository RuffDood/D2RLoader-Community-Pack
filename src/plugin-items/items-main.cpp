#include <D2RLPlugin/api.h>
#include "advanced-item-tooltips.h"
#include "charm-aura-trigger-fix.h"
#include "enhanced-damage-min-max-fix.h"
#include "extended-item-stats.h"
#include "gamble-screen-limit.h"
#include "ground-item-label-limit.h"
#include "item-durability.h"
#include "items-ethereal.h"
#include "items-private.h"
#include "mass-identify.h"
#include "magic-find-formula.h"
#include "potion-auto-pickup.h"
#include "qty-display-issue.h"
#include "repair-costs-cap.h"
#include "vendor-stock-refresh.h"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
#include <windows.h>

// ── Addresses (offsets from exe base 0x140000000) ────────────────────────────

// Both found via D2MOO source cross-reference: profile's containing functions
// are D2MOO's sub_6FC4D5E0 (magic, single prefix+suffix roll) and the
// table-based new-format rare-item roll near D2GAME_RollRareItem_6FC53360
// (both end with `ITEMS_SetItemFlag(pItem, IFLAG_IDENTIFIED, 0)`). Profile
// inlines that clear as `AND dword[reg+0x18],0xFFFFFFEF`; debug keeps it as a
// real call to a shared flag-set/clear utility,
// `FUN_14036d8f0(item, 0x10, /*bSet=*/0)` — confirmed to have no side effects
// beyond the bit clear (full decompile checked), so NOP'ing the 5-byte CALL
// (rather than trying to flip its bSet argument, which doesn't fit in the
// available instruction encoding without corrupting the adjacent flag-mask
// LEA) achieves the same "stays identified" outcome as profile's forced OR,
// since items are always created identified and only explicitly
// un-identified by this one call in each function.
static constexpr uint64_t OFF_MagicItemsSpawnIdentified  = 0x442d2a;
static constexpr uint64_t OFF_RareItemsSpawnIdentified   = 0x58be1a;
static constexpr uint64_t OFF_GetInventoryGoldLimit      = 0x34b320; // D2GAME_GetInventoryGoldLimit
// Jump table for the quality switch inside ITEMS_ValidateRuneword (0x372260-0x372634).
// Dispatch at 0x372483: MOV ECX,[RDX + RAX*4 + 0x372638]; ADD RCX,RDX; JMP RCX
// where RDX=imageBase, RAX=(quality-4). Six 4-byte RVA entries covering quality
// 4 (Magic) through 9 (Tempered); each currently holds 0x00372440, the address
// of the function's early "XOR EAX,EAX" reject path (no runeword allowed).
// The out-of-range case (quality outside 4..9, i.e. Normal/Superior/etc.) is
// handled by the JA at 0x372474, which jumps straight to 0x3722e4 -- the real
// "keep validating normally" continuation. Writing 0x003722e4 to an entry
// redirects that quality to that same continuation, i.e. treats it exactly
// like Normal quality. (Read directly from the D2R.exe 3.2.92777 reference image and confirmed
// against the live jump-table bytes; see docs/offset-migration-status.md.)
static constexpr uint64_t OFF_GoldPenaltyCall            = 0x424ad1; // lone CALL site that applies the gold penalty (D2GAME_ApplyDeathGoldPenalty)
static constexpr uint64_t OFF_RunewordQualityJumpTable   = 0x372638; // inside ITEMS_ValidateRuneword
static constexpr unsigned char RUNEWORD_QUALITY_PASS[]   = { 0xE4, 0x22, 0x37, 0x00 }; // RVA 0x3722e4 LE
static constexpr const char* RUNEWORD_QUALITY_MANIFEST_IDS[6] = {
	PSH_MANIFEST_SITE("items.runewordQualities.magic"),
	PSH_MANIFEST_SITE("items.runewordQualities.set"),
	PSH_MANIFEST_SITE("items.runewordQualities.rare"),
	PSH_MANIFEST_SITE("items.runewordQualities.unique"),
	PSH_MANIFEST_SITE("items.runewordQualities.crafted"),
	PSH_MANIFEST_SITE("items.runewordQualities.tempered"),
};

static constexpr uint64_t OFF_FillStoreInventory         = 0x53c9f0; // D2GAME_NPC_FillStoreInventory
static constexpr uint64_t OFF_GenerateStoreItem          = 0x540ea0; // D2GAME_NPC_GenerateStoreItem
static constexpr uint64_t OFF_ComputeItemLevel           = 0x53cfa0; // item level cap helper
static constexpr uint64_t OFF_GetItemIdFromCode          = 0xa12150; // items.txt code → ID
static constexpr uint64_t OFF_GetMaxStack                = 0x3719e0; // get total max stack for quivers
static constexpr uint64_t OFF_SetUnitStat                = 0x2f7d10; // STATLIST_SetUnitStat
static constexpr uint64_t OFF_FillGamble                 = 0x541880; // D2GAME_STORES_FillGamble
static constexpr uint64_t OFF_FillGamble_JgeOpcode       = 0x541a28; // JGE byte that enforces ring/amulet
static constexpr uint64_t OFF_sgptDataTables             = 0x2a9a580; // D2GAME_sgptDataTables (global data-table pointer array, not a function)
static constexpr uint64_t OFF_CompileTxt                 = 0x2ff970;  // DATATBLS_CompileTxt
static constexpr uint64_t OFF_CompileTxtCallInTC         = 0x3a85a2;  // CompileTxt call site inside TC compiler (FUN_1403a7ea0)
static constexpr uint64_t OFF_CreateCompiledTCStruct     = 0x3a6dc0;  // FUN_1403a6dc0 (raw record → compiled TC struct)
static constexpr uint64_t OFF_CreateCompiledTCStructCall = 0x3a8682;  // call site of FUN_1403a6dc0 inside TC compiler loop
static constexpr uint64_t OFF_TCDropFunction             = 0x441300;  // monster death drops entry (debug: 10 real params vs. our 4-arg TCDropFunction_t -- trailing args are caller-supplied literal 0s in both builds, verify hook still receives what it needs)
static constexpr uint64_t OFF_ConditionGate              = 0x444920;  // TC condition gate, receives compiled struct
static constexpr uint64_t OFF_ConditionCalcEval          = 0x3b5380;  // ConditionCalc expression evaluator
static constexpr uint64_t OFF_PhysResist                 = 0x4524d6; // inlined into the resist/absorb wrapper in debug (no standalone SUNITDMG_ApplyResistances call)
static constexpr uint64_t OFF_ElementalResist = 0x4524de; // MOV EAX,0x5f cap, immediately after OFF_PhysResist
static constexpr uint64_t OFF_AbsorbCap = 0x4506a1; // MOV ECX,0x28 cap inside the (still-standalone) absorb function

// ── TC raw record layout (stride 0x304, output of DATATBLS_CompileTxt) ─────
static constexpr uint64_t TC_RECORD_STRIDE               = 0x304;
static constexpr uint32_t TCREC_CONDCALC_OFF             = 0x2fc;  // uint32_t compiled expression index
static constexpr uint32_t TCREC_USEPLAYER_OFF            = 0x301;  // uint8_t UsePlayerForConditionCalc flag

// Vendor difficulty upgrade constants inside D2GAME_NPC_GenerateStoreItem (0x540ea0).
// *ScaleByte = the shift-count operand in a SHL EAX,n instruction (1 byte).
// *BaseImm   = the imm32 operand in an ADD EAX,imm instruction (4 bytes, little-endian).
static constexpr uint64_t OFF_NMUberScaleByte     = 0x540f88; // SHL EAX,0x6  → ×64
static constexpr uint64_t OFF_NMUberBaseImm       = 0x540f8a; // ADD EAX,0xfa0 → +4000
static constexpr uint64_t OFF_HellUltraScaleByte  = 0x54104d; // SHL EAX,0x4  → ×16
static constexpr uint64_t OFF_HellUltraBaseImm    = 0x54104f; // ADD EAX,0x3e8 → +1000
static constexpr uint64_t OFF_HellUberScaleByte   = 0x541084; // SHL EAX,0x7  → ×128
static constexpr uint64_t OFF_HellUberBaseImm     = 0x541086; // ADD EAX,0x1388 → +5000

// ── Expected original bytes (verified against the D2R.exe 3.2.92777 reference image) ────
// D2RLoader requires non-null expected bytes for PatchBytes/PatchRel32/
// InstallInlineHook calls so it can verify the patch site before writing.
static constexpr uint8_t EXP_FillStoreInventory[5]         = { 0x48, 0x89, 0x5C, 0x24, 0x10 };
static constexpr uint8_t EXP_NMUberBaseImm[4]               = { 0xA0, 0x0F, 0x00, 0x00 };
static constexpr uint8_t EXP_HellUberBaseImm[4]             = { 0x88, 0x13, 0x00, 0x00 };
static constexpr uint8_t EXP_HellUltraBaseImm[4]            = { 0xE8, 0x03, 0x00, 0x00 };
static constexpr uint8_t EXP_NMUberScaleByte[1]             = { 0x06 };
static constexpr uint8_t EXP_HellUberScaleByte[1]           = { 0x07 };
static constexpr uint8_t EXP_HellUltraScaleByte[1]          = { 0x04 };
static constexpr uint8_t EXP_FillGamble_JgeOpcode[1]        = { 0x7D };
static constexpr uint8_t EXP_FillGamble[6]                  = { 0x40, 0x56, 0x41, 0x54, 0x41, 0x55 };
static constexpr uint8_t EXP_GoldPenaltyCall[5]             = { 0xE8, 0xDA, 0x11, 0x00, 0x00 };
static constexpr uint8_t EXP_MagicItemsSpawnIdentified[5]   = { 0xE8, 0xC1, 0xAB, 0xF2, 0xFF };
static constexpr uint8_t EXP_RareItemsSpawnIdentified[5]    = { 0xE8, 0xD1, 0x1A, 0xDE, 0xFF };
static constexpr uint8_t EXP_GetInventoryGoldLimit[7]       = { 0x48, 0x83, 0xEC, 0x28, 0x45, 0x33, 0xC0 };
static constexpr uint8_t EXP_RunewordQualityJumpTableEntry[4] = { 0x40, 0x24, 0x37, 0x00 };
static constexpr uint8_t EXP_CompileTxtCallInTC[5]          = { 0xE8, 0xC9, 0x73, 0xF5, 0xFF };
static constexpr uint8_t EXP_CreateCompiledTCStructCall[5]  = { 0xE8, 0x39, 0xE7, 0xFF, 0xFF };
static constexpr uint8_t EXP_TCDropFunction[6]              = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41 };
static constexpr uint8_t EXP_ConditionGate[5]               = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
static constexpr uint8_t EXP_ConditionCalcEval[5]           = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
static constexpr uint8_t EXP_PhysResistCap[1]               = { 0x32 };
static constexpr uint8_t EXP_ElementalResistCap[1]          = { 0x5F };
static constexpr uint8_t EXP_AbsorbCap[1]                   = { 0x28 };

// ── D2ItemsTxt field offsets (confirmed from Ghidra, stride = 0x1c0) ─────────
static constexpr uint32_t ITEMREC_STRIDE      = 0x1c0;
//static constexpr uint32_t ITEMREC_LEVEL       = 0x10d; // bLevel  (uint8_t)
//static constexpr uint32_t ITEMREC_VERSION     = 0x0fe; // wVersion (uint16_t)
//static constexpr uint32_t ITEMREC_BITFIELD1   = 0x0e4; // dwBitField1 (uint32_t)
//static constexpr uint32_t ITEMREC_CODE        = 0x080; // dwCode (uint32_t) — used for quiver check

// ── Data-table field offsets (relative to sgptDataTables[expansion*2]) ───────
static constexpr uint32_t DATATBL_ITEMS_TXT_ARR   = 0x220;  // void* passed to GetItemIdFromCode
static constexpr uint32_t DATATBL_ITEMS_TXT_BASE  = 0x15a0; // D2ItemsTxt* record array base
static constexpr uint32_t DATATBL_ITEMS_TXT_COUNT = 0x15a8; // uint32_t record count
static constexpr uint32_t DATATBL_GAMBLE_POOL_PTR = 0x16b8; // int32_t* gamble selection pool
static constexpr uint32_t DATATBL_GAMBLE_LIMITS   = 0x16d0; // uint32_t[100] pool size per item level

// ── Quiver type-code check ────────────────────────────────────────────────────
// Catches dwCode == 0x20767161 ('aqv ') arrows or 0x20767163 ('cqv ') bolts.
static constexpr uint32_t QUIVER_CODE_MASK = 0xfffffffd;
static constexpr uint32_t QUIVER_CODE_BASE = 0x20767161;

// ── Stat IDs ─────────────────────────────────────────────────────────────────
static constexpr int STAT_LEVEL    = 0x0C;
static constexpr int STAT_QUANTITY = 0x46;

// ── D2R function type aliases ─────────────────────────────────────────────────

using GetInventoryGoldLimit_t  = int(*)(int64_t unitPtr);
using FillStoreInventory_t     = void(*)(D2GameStrc* pGame, D2UnitStrc* pPlayer, D2UnitStrc* pNpc);
using FillGamble_t             = void(*)(D2GameStrc* pGame, int64_t param2, D2UnitStrc* pPlayer);
using CompileTxt_t             = void(__fastcall*)(uint8_t expansion, const char* p2, const char* p3,
                                                    const char* p4, D2TxtFieldDesc* fields,
                                                    uint64_t stride, D2TxtContainer* output);
using CreateCompiledTCStruct_t = void*(__fastcall*)(uint8_t expansion, const void* rawRecord);
using TCDropFunction_t         = void(__fastcall*)(void* p1, void* p2, uint32_t p3, void* damageEvent);
using ConditionGate_t          = int(__fastcall*)(void* gameCtx, void* compiledTCStruct, D2UnitStrc* unit, uint8_t pickFlag);
using ConditionCalcEval_t      = int(__fastcall*)(uint8_t expansion, D2UnitStrc* unit, uint32_t exprIdx);
// RCX=pNpc RDX=dwCode R8=pGame R9=unused [RSP+28]=nQuality [RSP+30]=nItemLevel [RSP+38]=nPlayerLevel
// (quality is 5th arg on the stack, NOT R9 — verified by disassembly at 0x1403e74bc)
using GenerateStoreItem_t      = D2UnitStrc*(*)(D2UnitStrc* pNpc, uint32_t dwCode, D2GameStrc* pGame,
                                                 int nUnused, int nQuality, int nItemLevel, int nPlayerLevel);
using ComputeItemLevel_t       = int(*)(D2GameStrc* pGame, D2UnitStrc* pNpc);
using GetItemIdFromCode_t      = int(*)(void* itemsTxtArr, uint32_t dwCode);
using SetUnitStat_t            = void(*)(D2UnitStrc* pItem, int statId, int value, int layer);
using GetMaxStack_t            = int(*)(D2UnitStrc* pItem);

// ── Plugin state ──────────────────────────────────────────────────────────────

static ItemPluginOptions        g_pluginOptions;
static uintptr_t                g_exeBase = 0;
static GetInventoryGoldLimit_t  Original_GetInventoryGoldLimit = nullptr;
static FillStoreInventory_t     Original_FillStoreInventory    = nullptr;
static FillGamble_t             Original_FillGamble            = nullptr;
static TCDropFunction_t         Original_TCDropFunction        = nullptr;
static ConditionCalcEval_t      Original_ConditionCalcEval     = nullptr;
static ConditionGate_t          Original_ConditionGate         = nullptr;

// Player unit captured at the monster-death-drop entry for the duration of that drop.
thread_local D2UnitStrc* g_currentDropPlayer    = nullptr;
// Set by Hook_ConditionGate for the specific TC record being evaluated.
thread_local bool        g_currentCalcUsePlayer = false;
static GenerateStoreItem_t      Fn_GenerateStoreItem           = nullptr;
static ComputeItemLevel_t       Fn_ComputeItemLevel            = nullptr;
static GetItemIdFromCode_t      Fn_GetItemIdFromCode           = nullptr;
static SetUnitStat_t            Fn_SetUnitStat                 = nullptr;
static GetMaxStack_t            Fn_GetMaxStack                 = nullptr;

// ── Inline helpers ────────────────────────────────────────────────────────────

// Advance the game-level LCG (pGame->rngSeedLow/High) and return the low 32 bits.
static inline uint32_t AdvanceGameRng(D2GameStrc* pGame) {
	uint64_t next = (uint64_t)pGame->rngSeedLow * 0x6AC690C5 + pGame->rngSeedHigh;
	pGame->rngSeedLow  = (uint32_t)next;
	pGame->rngSeedHigh = (uint32_t)(next >> 32);
	return pGame->rngSeedLow;
}

// Roll a value in [lo, hi] using the game RNG (equivalent to D2MOO RollLimitedRandom).
static inline uint32_t RollGameRngRange(D2GameStrc* pGame, uint32_t lo, uint32_t hi) {
	if (lo >= hi) return lo;
	uint32_t range = hi - lo + 1;
	uint32_t roll  = AdvanceGameRng(pGame);
	uint32_t idx   = (range & (range - 1)) == 0 ? (roll & (range - 1)) : (roll % range);
	return lo + idx;
}

// Return pointer to the dataTbl base for the current expansion.
static inline intptr_t GetDataTable(D2GameStrc* pGame) {
	return reinterpret_cast<intptr_t*>(g_exeBase + OFF_sgptDataTables)[pGame->expansion * 2];
}

// Return a const pointer to the raw D2ItemsTxt record bytes for dwCode, or nullptr.
static inline const D2ItemsTxt* GetItemRecord(intptr_t dataTbl, uint32_t dwCode) {
	void*    arr   = *reinterpret_cast<void**>(dataTbl + DATATBL_ITEMS_TXT_ARR);
	int      id    = Fn_GetItemIdFromCode(arr, dwCode);
	if (id < 0) return nullptr;
	uint32_t count = *reinterpret_cast<uint32_t*>(dataTbl + DATATBL_ITEMS_TXT_COUNT);
	if ((uint32_t)id >= count) return nullptr;
	const uint8_t* base = *reinterpret_cast<const uint8_t**>(dataTbl + DATATBL_ITEMS_TXT_BASE);
	if (!base) return nullptr;
	return (D2ItemsTxt*)(base + (size_t)id * sizeof(D2ItemsTxt));
}

// ── Hook: DATATBLS_CompileTxt call site inside TC compiler ───────────────────
//
// Intercepts the single CompileTxt call inside FUN_140284a40 (TreasureClassEx
// compiler). Appends UsePlayerForConditionCalc as a bool field (type=4) at byte
// offset 0x301 — padding within the existing 0x304 stride, so stride is unchanged.

static void __fastcall Hook_CompileTxt_TC(uint8_t expansion, const char* p2, const char* p3,
                                           const char* p4, D2TxtFieldDesc* origFields,
                                           uint64_t stride, D2TxtContainer* output)
{
    int n = 0;
    while (origFields[n].pName && std::strcmp(origFields[n].pName, "end") != 0) ++n;

    static const D2TxtFieldDesc usePlayerDesc = { "UsePlayerForConditionCalc", 4, 0, TCREC_USEPLAYER_OFF, 0 };

    std::vector<D2TxtFieldDesc> extended(origFields, origFields + n);
    extended.push_back(usePlayerDesc);
    extended.push_back(origFields[n]);  // preserve sentinel

    auto RealCompileTxt = reinterpret_cast<CompileTxt_t>(g_exeBase + OFF_CompileTxt);
    RealCompileTxt(expansion, p2, p3, p4, extended.data(), stride, output);
}

// ── Hook: FUN_140283d10 call site inside TC compiler loop ────────────────────
//
// Called once per TC row; RCX=expansion CL, RDX=raw record, RAX=compiled struct.
// Stamps bit 0x08 into compiled[0x25] when UsePlayerForConditionCalc is set in
// the raw record. That bit is confirmed unused by the executable (no reads/writes).

static void* __fastcall Hook_CreateCompiledTCStruct(uint8_t expansion, const uint8_t* rawRecord)
{
    auto Real = reinterpret_cast<CreateCompiledTCStruct_t>(g_exeBase + OFF_CreateCompiledTCStruct);
    uint8_t* compiled = static_cast<uint8_t*>(Real(expansion, rawRecord));
    if (compiled && rawRecord && rawRecord[TCREC_USEPLAYER_OFF])
        compiled[0x25] |= 0x08;
    return compiled;
}

// ── Hook: FUN_140382ad0 (monster death drop entry) ───────────────────────────
//
// Captures the killer player unit (at damageEvent+0x10) for the duration of the
// entire drop chain. hookSize=6: MOV R11,RSP (3) + PUSH RBP (1) + PUSH R13 (2).

static void __fastcall Hook_TCDropFunction(void* p1, void* p2, uint32_t p3, void* damageEvent)
{
    D2UnitStrc* player = nullptr;
    if (damageEvent) {
        player = *reinterpret_cast<D2UnitStrc**>(static_cast<uint8_t*>(damageEvent) + 0x10);
        if (player && PSh_UnitType(*player) != D2UnitType::Player)
            player = nullptr;
    }
    g_currentDropPlayer = player;
    Original_TCDropFunction(p1, p2, p3, damageEvent);
    g_currentDropPlayer = nullptr;
}

// ── Hook: FUN_140321b90 (TC condition gate) ───────────────────────────────────
//
// Called before each TC ConditionCalc evaluation with the compiled TC struct as
// param_2. Reads UsePlayerForConditionCalc directly from bit 0x08 of compiled[0x25],
// stamped at compile time by Hook_CreateCompiledTCStruct.
// hookSize=5: MOV qword ptr [RSP+0x8],RBX (5 bytes).

static int __fastcall Hook_ConditionGate(void* gameCtx, void* compiledTCStruct,
                                          D2UnitStrc* unit, uint8_t pickFlag)
{
    bool prev = g_currentCalcUsePlayer;
    g_currentCalcUsePlayer = g_currentDropPlayer && compiledTCStruct &&
        (static_cast<uint8_t*>(compiledTCStruct)[0x25] & 0x08) != 0;

    int result = Original_ConditionGate(gameCtx, compiledTCStruct, unit, pickFlag);
    g_currentCalcUsePlayer = prev;
    return result;
}

// ── Hook: FUN_1402a7950 (ConditionCalc expression evaluator) ─────────────────
//
// When the condition gate flagged UsePlayerForConditionCalc for this TC record,
// substitutes the player for the monster unit before evaluating the expression.
// hookSize=5: MOV qword ptr [RSP+0x8],RBX (5 bytes).

static int __fastcall Hook_ConditionCalcEval(uint8_t expansion, D2UnitStrc* unit, uint32_t exprIdx)
{
    D2UnitStrc* evalUnit =
        (g_currentCalcUsePlayer && g_currentDropPlayer) ? g_currentDropPlayer : unit;
    return Original_ConditionCalcEval(expansion, evalUnit, exprIdx);
}

// ── Hook: D2GAME_NPC_FillStoreInventory ──────────────────────────────────────
//
// Faithful C++ rewrite of FUN_1403e7e70.  Finds the VendorChainEntry for pNpc,
// updates its tick timestamp, then generates proxy (normal + magic) and perm store
// items by calling the original D2GAME_NPC_GenerateStoreItem (FUN_1403e7200).
//
static void Hook_FillStoreInventory(D2GameStrc* pGame, D2UnitStrc* pPlayer, D2UnitStrc* pNpc)
{
	// ── Find the VendorChainEntry for this NPC ───────────────────────────────
	const uint16_t npcId = pNpc ? static_cast<uint16_t>(PSh_UnitClassId(*pNpc)) : static_cast<uint16_t>(-1);

	VendorChainEntry* entryArr   = pGame->pVendorChain;
	uint64_t          entryCount = pGame->nVendorChain;
	VendorChainEntry* entry = nullptr;
	for (uint64_t i = 0; i < entryCount; ++i) {
		if (entryArr[i].npcId == npcId) {
			entry = &entryArr[i];
			break;
		}
	}
	if (!entry) return; // NPC not registered (assert in original)

	// ── Refresh tick timestamp ────────────────────────────────────────────────
	entry->qwTicks = GetTickCount64();

	// ── Player level + item level cap ─────────────────────────────────────────
	int playerLevel = 0;
	if (pPlayer && pPlayer->statList) {
		playerLevel = PSh_GetStat(g_exeBase, pPlayer, STAT_LEVEL);
	}
	int itemLevel = Fn_ComputeItemLevel(pGame, pNpc);

	intptr_t dataTbl = GetDataTable(pGame);

	// ── Proxy items loop ──────────────────────────────────────────────────────
	int nSpawned = 0;
	auto* cache = static_cast<NpcItemCacheEntry*>(entry->pItemCache);

	for (uint64_t i = 0; i < entry->nItems; ++i) {
		const NpcItemCacheEntry& ci = cache[i];
		const D2ItemsTxt* rec = GetItemRecord(dataTbl, ci.dwCode);
		if (!rec) continue;
		if (rec->nLevel > itemLevel) continue;
		if (rec->wVersion >= 100 && pGame->wItemFormat < 100) continue;

		// Normal items — only when itemLevel < 25
		uint32_t nNormal = 0;
		if (itemLevel < g_pluginOptions.VOHNormalItemLevelMaxThreshold) {
			nNormal = RollGameRngRange(pGame, ci.nMin, ci.nMax);
		}

		for (uint32_t j = 0; j < nNormal; ++j) {
			// Quality roll: low 32 bits of RNG advance % 100
			uint32_t roll    = AdvanceGameRng(pGame) % 100;
			D2ItemQuality      quality = D2ItemQuality::Normal;

			if (itemLevel < g_pluginOptions.VOHLowQualityMaxLevelThreshold &&
				roll >= 100 - g_pluginOptions.VOHLowQualityDowngradeChance)
			{
				quality = D2ItemQuality::LowQuality;
			}
			else if (itemLevel >= g_pluginOptions.VOHSuperiorLevelMinLevelThreshold &&
				roll >= 100 - g_pluginOptions.VOHSuperiorUpgradeChance)
			{
				quality = D2ItemQuality::Superior;
			}

			if (!Fn_GenerateStoreItem(pNpc, ci.dwCode, pGame, 0, (int)quality, itemLevel, playerLevel)) {
				++nSpawned;
			}
			if (nSpawned > 32) return;
		}

		// Magic items — requires bitfield1 & 1 and magicLevel check
		if ((rec->dwBitField1 & 1) && (int)(uint8_t)ci.nMagicLevel <= itemLevel) {
			uint32_t nMagicBase = 1;
			if (itemLevel >= g_pluginOptions	.VOHNormalItemLevelMaxThreshold) {
				nMagicBase = (AdvanceGameRng(pGame) & 1) + 2;
			}
			uint32_t nMagic = RollGameRngRange(pGame,
			                                   ci.nMagicMin,
			                                   ci.nMagicMax + (uint8_t)nMagicBase - 1u);
			for (uint32_t j = 0; j < nMagic; ++j) {
				const uint32_t randomValue = AdvanceGameRng(pGame);
				D2ItemQuality quality = PSh_Items_ShouldGenerateRareVendorItem(
					g_pluginOptions.bEnableVOHRandomRareVendorItems,
					randomValue,
					g_pluginOptions.VOHRareItemChance)
					? D2ItemQuality::Rare : D2ItemQuality::Magic;

				if (!Fn_GenerateStoreItem(pNpc, ci.dwCode, pGame, 0, (int)quality, itemLevel, playerLevel)) {
					++nSpawned;
				}
			}
		}
	}

	// ── Perm items loop ───────────────────────────────────────────────────────
	for (uint64_t i = 0; i < entry->nPerms; ++i) {
		uint32_t code = entry->pPermCache[i];
		D2UnitStrc* pStoreItem = Fn_GenerateStoreItem(pNpc, code, pGame, 0, (int)D2ItemQuality::Normal, itemLevel, playerLevel);
		if (pStoreItem) {
			const D2ItemsTxt* rec = GetItemRecord(dataTbl, code);
			if (rec) {
				if (((rec->dwCode - QUIVER_CODE_BASE) & QUIVER_CODE_MASK) == 0) {
					int maxStack = Fn_GetMaxStack(pStoreItem);
					Fn_SetUnitStat(pStoreItem, STAT_QUANTITY, maxStack, 0);
				}
			}
		} else {
			++nSpawned;
		}
		if (nSpawned > 32) return;
	}
}

// ── Hook: D2GAME_STORES_FillGamble (Bitfield1Flag4Only) ──────────────────────
//
// Temporarily replaces the gamble pool (dataTbl+0x16b8) and per-level limits
// (dataTbl+0x16d0) with a filtered copy that contains only items whose
// dwBitField1 & 4 is set, then calls the original FillGamble.
// The original pool/limits are restored before returning.
//
static constexpr int GAMBLE_POOL_MAX = 4096;

static void Hook_FillGamble_Bitfield(D2GameStrc* pGame, int64_t param2, D2UnitStrc* pPlayer)
{
	intptr_t dataTbl = GetDataTable(pGame);

	int32_t** poolPtrField  = reinterpret_cast<int32_t**>(dataTbl + DATATBL_GAMBLE_POOL_PTR);
	uint32_t* origLimits    = reinterpret_cast<uint32_t*>(dataTbl + DATATBL_GAMBLE_LIMITS);
	int32_t*  origPool      = *poolPtrField;
	int       maxChoose     = (origPool && origLimits) ? (int)origLimits[99] : 0;

	const uint8_t* itemsTxtBase  = *reinterpret_cast<const uint8_t**>(dataTbl + DATATBL_ITEMS_TXT_BASE);
	uint32_t       itemsTxtCount = *reinterpret_cast<uint32_t*>(dataTbl + DATATBL_ITEMS_TXT_COUNT);

	if (maxChoose <= 0 || !origPool || !itemsTxtBase) {
		Original_FillGamble(pGame, param2, pPlayer);
		return;
	}

	// Build filtered pool, preserving original level-ordering.
	// filtLimits[L] = how many filtered items came from origPool[0..origLimits[L]-1].
	static int32_t  filtPool[GAMBLE_POOL_MAX];
	static uint32_t filtLimits[100];

	uint32_t filtCount = 0;
	int      origSoFar = 0;
	for (int level = 0; level < 100; ++level) {
		int limit = (int)origLimits[level];
		while (origSoFar < limit && origSoFar < maxChoose) {
			int32_t itemId = origPool[origSoFar];
			if ((uint32_t)itemId < itemsTxtCount) {
				const D2ItemsTxt* rec = GetItemRecord((intptr_t)itemsTxtBase, itemId);
				if (rec->dwBitField1 & g_pluginOptions.GambleBitfield) {
					if (filtCount < GAMBLE_POOL_MAX) {
						filtPool[filtCount++] = itemId;
					}
				}
			}
			++origSoFar;
		}
		filtLimits[level] = filtCount;
	}

	if (filtCount == 0) {
		// No qualifying items — fall back to original behaviour.
		Original_FillGamble(pGame, param2, pPlayer);
		return;
	}

	// Temporarily swap in the filtered pool and limits.
	// D2 game sessions are single-threaded per game, so this is safe.
	uint32_t savedLimits[100];
	memcpy(savedLimits, origLimits, sizeof(savedLimits));
	*poolPtrField = filtPool;
	memcpy(origLimits, filtLimits, sizeof(filtLimits));

	Original_FillGamble(pGame, param2, pPlayer);

	// Restore original pool and limits.
	*poolPtrField = origPool;
	memcpy(origLimits, savedLimits, sizeof(savedLimits));
}

// ── Hook: D2GAME_GetInventoryGoldLimit ───────────────────────────────────────

static int __fastcall Hook_GetInventoryGoldLimit(int64_t unitPtr)
{
	if (g_pluginOptions.InventoryGoldLimitChange == GoldOption::Flat) {
		return unitPtr ? static_cast<int>(g_pluginOptions.InventoryGoldLimit) : 0;
	}
	// PerLevel: original returns level * 10000; substitute our per-level multiplier.
	int result = Original_GetInventoryGoldLimit(unitPtr);
	if (result == 0) return 0;
	return (result / 10000) * static_cast<int>(g_pluginOptions.InventoryGoldLimit);
}

// ── INI loading ───────────────────────────────────────────────────────────────

static int64_t ReadConfigInteger(
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
		throw std::runtime_error(std::string("plugin-items: ") + setting + " must be an integer.");
	}

	int64_t value{};
	if (entry->is_number_unsigned()) {
		const auto unsignedValue = entry->get<uint64_t>();
		if (unsignedValue > static_cast<uint64_t>(maximum)) {
			throw std::runtime_error(std::string("plugin-items: ") + setting + " is out of range.");
		}
		value = static_cast<int64_t>(unsignedValue);
	}
	else {
		value = entry->get<int64_t>();
	}
	if (value < minimum || value > maximum) {
		throw std::runtime_error(std::string("plugin-items: ") + setting + " is out of range.");
	}
	return value;
}

bool ItemPluginOptions::Load(const D2RL::PluginContext* context, const nlohmann::json& cfg)
{
	bMagicItemsSpawnIdentified = cfg.value("magicItemsSpawnIdentified", false);
	bRareItemsSpawnIdentified  = cfg.value("rareItemsSpawnIdentified", false);
	bDisableGoldPenalty        = cfg.value("disableGoldPenalty", false);

	{
		auto goldLimit = cfg.value("inventoryGoldLimit", nlohmann::json::object());
		std::string mode = goldLimit.value("mode", "disabled");
		if      (mode == "perLevel") InventoryGoldLimitChange = GoldOption::PerLevel;
		else if (mode == "flat")     InventoryGoldLimitChange = GoldOption::Flat;
		else if (mode == "disabled") InventoryGoldLimitChange = GoldOption::Disabled;
		else throw std::runtime_error("plugin-items: items.inventoryGoldLimit.mode is invalid.");
		InventoryGoldLimit = static_cast<uint32_t>(ReadConfigInteger(
			goldLimit, "value", 10000, 0, std::numeric_limits<uint32_t>::max(),
			"items.inventoryGoldLimit.value"));
	}

	// Index 0=Magic 1=Set 2=Rare 3=Unique 4=Crafted 5=Tempered
	{
		auto rq = cfg.value("runewordQualities", nlohmann::json::object());
		static const char* keys[] = { "magic", "set", "rare", "unique", "crafted", "tempered" };
		for (int i = 0; i < 6; i++)
			bRunewordQualities[i] = rq.value(keys[i], false);
	}

	{
		auto gamble = cfg.value("gambleFilter", nlohmann::json::object());
		std::string mode = gamble.value("mode", "disabled");
		if      (mode == "noRingAmuletGuarantee") GambleFilter = GambleOption::NoRingAmuletGuarantee;
		else if (mode == "bitfield")              GambleFilter = GambleOption::Bitfield;
		else if (mode == "disabled")              GambleFilter = GambleOption::Disabled;
		else throw std::runtime_error("plugin-items: items.gambleFilter.mode is invalid.");
		GambleBitfield = gamble.value("bitfield", 4);
	}

	{
		auto voh = cfg.value("vendorOverhaul", nlohmann::json::object());
		bEnableVendorOverhaul             = voh.value("enabled", false);
		VOHNormalItemLevelMaxThreshold = static_cast<uint32_t>(ReadConfigInteger(
			voh, "normalItemLevelMaxThreshold", 25, 0, std::numeric_limits<uint32_t>::max(),
			"items.vendorOverhaul.normalItemLevelMaxThreshold"));
		VOHMagicMinLevelThreshold = static_cast<uint32_t>(ReadConfigInteger(
			voh, "magicMinLevelThreshold", 0, 0, std::numeric_limits<uint32_t>::max(),
			"items.vendorOverhaul.magicMinLevelThreshold"));
		VOHSuperiorLevelMinLevelThreshold = static_cast<uint32_t>(ReadConfigInteger(
			voh, "superiorMinLevelThreshold", 5, 0, std::numeric_limits<uint32_t>::max(),
			"items.vendorOverhaul.superiorMinLevelThreshold"));
		VOHLowQualityMaxLevelThreshold = static_cast<uint32_t>(ReadConfigInteger(
			voh, "lowQualityMaxLevelThreshold", 5, 0, std::numeric_limits<uint32_t>::max(),
			"items.vendorOverhaul.lowQualityMaxLevelThreshold"));
		VOHSuperiorUpgradeChance = static_cast<uint32_t>(ReadConfigInteger(
			voh, "superiorUpgradeChance", 25, 0, 100,
			"items.vendorOverhaul.superiorUpgradeChance"));
		VOHLowQualityDowngradeChance = static_cast<uint32_t>(ReadConfigInteger(
			voh, "lowQualityDowngradeChance", 10, 0, 100,
			"items.vendorOverhaul.lowQualityDowngradeChance"));
		bEnableVOHRandomRareVendorItems   = voh.value("randomRareItems", false);
		VOHRareItemChance = static_cast<uint32_t>(ReadConfigInteger(
			voh, "rareItemChance", 1024, 1, std::numeric_limits<uint32_t>::max(),
			"items.vendorOverhaul.rareItemChance"));
		VendorNightmareUpgradeBaseChance = static_cast<int>(ReadConfigInteger(
			voh, "nightmareBaseChance", 4000, 0, std::numeric_limits<int>::max(),
			"items.vendorOverhaul.nightmareBaseChance"));
		VendorHellUberUpgradeBaseChance = static_cast<int>(ReadConfigInteger(
			voh, "hellUberBaseChance", 5000, 0, std::numeric_limits<int>::max(),
			"items.vendorOverhaul.hellUberBaseChance"));
		VendorHellUpgradeBaseChance = static_cast<int>(ReadConfigInteger(
			voh, "hellBaseChance", 1000, 0, std::numeric_limits<int>::max(),
			"items.vendorOverhaul.hellBaseChance"));
		VendorNightmareUpgradeLevelScale = static_cast<int>(ReadConfigInteger(
			voh, "nightmareLevelScale", 64, 0, 1 << 30,
			"items.vendorOverhaul.nightmareLevelScale"));
		VendorHellUberUpgradeLevelScale = static_cast<int>(ReadConfigInteger(
			voh, "hellUberLevelScale", 128, 0, 1 << 30,
			"items.vendorOverhaul.hellUberLevelScale"));
		VendorHellUpgradeLevelScale = static_cast<int>(ReadConfigInteger(
			voh, "hellLevelScale", 16, 0, 1 << 30,
			"items.vendorOverhaul.hellLevelScale"));
		if (!PSh_Items_IsValidVendorLevelScale(VendorNightmareUpgradeLevelScale)
			|| !PSh_Items_IsValidVendorLevelScale(VendorHellUberUpgradeLevelScale)
			|| !PSh_Items_IsValidVendorLevelScale(VendorHellUpgradeLevelScale)) {
			context->LogError("plugin-items: vendor level scales must be 0 or a power of two.");
			return false;
		}
	}

	bEnablePlayerConditionCalc = cfg.value("playerConditionCalc", false);

	{
		auto physCap = cfg.value("physResistCap", nlohmann::json::object());
		bEnablePhysResistMaxChange = physCap.value("enabled", false);
		const auto maximum = physCap.value("max", 50);
		if (maximum < 0 || maximum > 255) {
			context->LogError("plugin-items: items.physResistCap.max must be between 0 and 255.");
			return false;
		}
		MaxPhysResist = static_cast<uint8_t>(maximum);
	}
	{
		auto elemCap = cfg.value("elementalResistCap", nlohmann::json::object());
		bEnableElementalResistMaxChange = elemCap.value("enabled", false);
		const auto maximum = elemCap.value("max", 95);
		if (maximum < 0 || maximum > 255) {
			context->LogError("plugin-items: items.elementalResistCap.max must be between 0 and 255.");
			return false;
		}
		MaxElementalResist = static_cast<uint8_t>(maximum);
	}
	{
		auto absorbCap = cfg.value("absorbCap", nlohmann::json::object());
		bEnableAbsorbCapChange = absorbCap.value("enabled", false);
		const auto maximum = absorbCap.value("max", 40);
		if (maximum < 0 || maximum > 255) {
			context->LogError("plugin-items: items.absorbCap.max must be between 0 and 255.");
			return false;
		}
		MaxAbsorbPct = static_cast<uint8_t>(maximum);
	}
	return true;
}

// Returns the SHL shift count for a given power-of-2 multiplier (floor log2).
static uint8_t ShiftCount(int n) {
	if (n <= 1) return 0;
	uint8_t s = 0;
	while ((2 << s) <= n && s < 30) s++;
	return s;
}

// ── Plugin exports ────────────────────────────────────────────────────────────

static constexpr D2RL::PluginInfo PluginInfo{
	.infoSize = D2RL::PluginInfoSize,
	.apiVersion = D2RL_PLUGIN_API_VERSION,
	.id = "eezstreet-plugin-items",
	.name = "eezstreet Items Plugin",
	.version = PSh_PluginPackVersion,
	.author = "eezstreet",
	.description = "Various item-related changes.",
	.flags = D2RL::PluginFlags::NativeHooks,
};

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &PluginInfo;
}

static void CleanupPluginItemsState() noexcept
{
	RuffnecKk::MassIdentify::Unload();
	RuffnecKk::PotionAutoPickUp::Unload();
	RuffnecKk::MagicFindFormula::Unload();
	RuffnecKk::VendorStockRefresh::Unload();
	RuffnecKk::QtyDisplayIssue::Unload();
	RuffnecKk::RepairCostsCap::Unload();
	RuffnecKk::EnhancedDamageMinMaxFix::Unload();
	RuffnecKk::CharmAuraTriggerFix::Unload();
	RuffnecKk::ItemDurability::Unload();
	ItemsEthereal_Reset();
	RuffnecKk::GroundItemLabelLimit::Unload();
	RuffnecKk::GambleScreenLimit::Unload();
	RuffnecKk::AdvancedTooltips::Unload();
	RuffnecKk::ExtendedItemStats::Unload();
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!PSh_ValidatePluginTarget(context)) {
		return false;
	}

	nlohmann::json itemsConfig;
	try {
		auto cfg = PSh_Json_LoadConfig(context);
		itemsConfig = PSh_Json_GetSection(cfg, "items");
		if (!g_pluginOptions.Load(context, itemsConfig)) {
			return false;
		}
	}
	catch (const std::exception& error) {
		PSh_Json_LogConfigError(context, error);
		return false;
	}
	const auto expected = [context](uint64_t rva, const auto& bytes) noexcept {
		return context->CheckExpectedBytes(rva, bytes, sizeof(bytes));
	};
	bool originalSignaturesValid = true;
	if (g_pluginOptions.bEnableVendorOverhaul) {
		originalSignaturesValid = originalSignaturesValid
			&& expected(OFF_FillStoreInventory, EXP_FillStoreInventory);
		if (g_pluginOptions.VendorNightmareUpgradeBaseChance > 0)
			originalSignaturesValid = originalSignaturesValid && expected(OFF_NMUberBaseImm, EXP_NMUberBaseImm);
		if (g_pluginOptions.VendorHellUberUpgradeBaseChance > 0)
			originalSignaturesValid = originalSignaturesValid && expected(OFF_HellUberBaseImm, EXP_HellUberBaseImm);
		if (g_pluginOptions.VendorHellUpgradeBaseChance > 0)
			originalSignaturesValid = originalSignaturesValid && expected(OFF_HellUltraBaseImm, EXP_HellUltraBaseImm);
		if (g_pluginOptions.VendorNightmareUpgradeLevelScale > 0)
			originalSignaturesValid = originalSignaturesValid && expected(OFF_NMUberScaleByte, EXP_NMUberScaleByte);
		if (g_pluginOptions.VendorHellUberUpgradeLevelScale > 0)
			originalSignaturesValid = originalSignaturesValid && expected(OFF_HellUberScaleByte, EXP_HellUberScaleByte);
		if (g_pluginOptions.VendorHellUpgradeLevelScale > 0)
			originalSignaturesValid = originalSignaturesValid && expected(OFF_HellUltraScaleByte, EXP_HellUltraScaleByte);
	}
	if (g_pluginOptions.GambleFilter == GambleOption::NoRingAmuletGuarantee)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_FillGamble_JgeOpcode, EXP_FillGamble_JgeOpcode);
	else if (g_pluginOptions.GambleFilter == GambleOption::Bitfield)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_FillGamble, EXP_FillGamble);
	if (g_pluginOptions.bDisableGoldPenalty)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_GoldPenaltyCall, EXP_GoldPenaltyCall);
	if (g_pluginOptions.bMagicItemsSpawnIdentified)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_MagicItemsSpawnIdentified, EXP_MagicItemsSpawnIdentified);
	if (g_pluginOptions.bRareItemsSpawnIdentified)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_RareItemsSpawnIdentified, EXP_RareItemsSpawnIdentified);
	if (g_pluginOptions.InventoryGoldLimitChange != GoldOption::Disabled)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_GetInventoryGoldLimit, EXP_GetInventoryGoldLimit);
	for (int i = 0; i < 6; ++i) {
		if (g_pluginOptions.bRunewordQualities[i])
			originalSignaturesValid = originalSignaturesValid
				&& expected(OFF_RunewordQualityJumpTable + static_cast<uint64_t>(i) * 4, EXP_RunewordQualityJumpTableEntry);
	}
	if (g_pluginOptions.bEnablePlayerConditionCalc)
		originalSignaturesValid = originalSignaturesValid
			&& expected(OFF_CompileTxtCallInTC, EXP_CompileTxtCallInTC)
			&& expected(OFF_CreateCompiledTCStructCall, EXP_CreateCompiledTCStructCall)
			&& expected(OFF_TCDropFunction, EXP_TCDropFunction)
			&& expected(OFF_ConditionGate, EXP_ConditionGate)
			&& expected(OFF_ConditionCalcEval, EXP_ConditionCalcEval);
	if (g_pluginOptions.bEnablePhysResistMaxChange)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_PhysResist, EXP_PhysResistCap);
	if (g_pluginOptions.bEnableElementalResistMaxChange)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_ElementalResist, EXP_ElementalResistCap);
	if (g_pluginOptions.bEnableAbsorbCapChange)
		originalSignaturesValid = originalSignaturesValid && expected(OFF_AbsorbCap, EXP_AbsorbCap);
	if (!originalSignaturesValid) {
		context->LogError("plugin-items: configured eezstreet patch-set signature mismatch; no item patch was applied.");
		return false;
	}
	g_exeBase = context->exeBase;
	PSh_HookTransactionScope hookTransaction(&CleanupPluginItemsState);
	if (!hookTransaction.IsActive()) {
		context->LogError("plugin-items: could not initialize the deferred hook transaction.");
		return false;
	}
	if (!RuffnecKk::ExtendedItemStats::Load(context)) {
		return false;
	}
	if (!RuffnecKk::AdvancedTooltips::Load(context, itemsConfig)) {
		return false;
	}
	if (!RuffnecKk::MassIdentify::Load(context, itemsConfig)) {
		return false;
	}
	if (!RuffnecKk::GambleScreenLimit::Load(context, itemsConfig)) {
		return false;
	}
	if (!RuffnecKk::GroundItemLabelLimit::Load(context, itemsConfig)) {
		return false;
	}
	if (!ItemsEthereal_Install(context, itemsConfig)) {
		return false;
	}
	if (!RuffnecKk::CharmAuraTriggerFix::Load(context, itemsConfig)) {
		return false;
	}
	if (!RuffnecKk::EnhancedDamageMinMaxFix::Load(context, itemsConfig)) {
		return false;
	}
	if (!RuffnecKk::RepairCostsCap::Load(context, itemsConfig)) {
		return false;
	}
	if (!RuffnecKk::QtyDisplayIssue::Load(context, itemsConfig)) {
		return false;
	}
	if (!RuffnecKk::VendorStockRefresh::Load(context, itemsConfig)) {
		return false;
	}
	if (!RuffnecKk::MagicFindFormula::Load(context, itemsConfig)) {
		return false;
	}
	if (!RuffnecKk::PotionAutoPickUp::Load(context, itemsConfig)) {
		return false;
	}

	// Resolve internal function pointers used by both hooks.
	Fn_GenerateStoreItem = reinterpret_cast<GenerateStoreItem_t>(g_exeBase + OFF_GenerateStoreItem);
	Fn_ComputeItemLevel = reinterpret_cast<ComputeItemLevel_t>(g_exeBase + OFF_ComputeItemLevel);
	Fn_GetItemIdFromCode = reinterpret_cast<GetItemIdFromCode_t>(g_exeBase + OFF_GetItemIdFromCode);
	Fn_SetUnitStat = reinterpret_cast<SetUnitStat_t>(g_exeBase + OFF_SetUnitStat);
	Fn_GetMaxStack = reinterpret_cast<GetMaxStack_t>(g_exeBase + OFF_GetMaxStack);

	if (g_pluginOptions.bEnableVendorOverhaul)
	{
		// First instruction is MOV qword ptr [RSP+0x18],R8 = 5 bytes (4C 89 44 24 18).
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("items.vendorOverhaul.fillStoreInventory"),
			OFF_FillStoreInventory, EXP_FillStoreInventory, sizeof(EXP_FillStoreInventory),
			Hook_FillStoreInventory, &Original_FillStoreInventory))
		{
			D2RL::LogErrorF(context, "plugin-items: failed to hook FillStoreInventory");
			return false;
		}

		// Difficulty upgrade threshold patches inside D2GAME_NPC_GenerateStoreItem.
		{
			uint32_t nmBase = (uint32_t)g_pluginOptions.VendorNightmareUpgradeBaseChance;
			uint32_t hellBase = (uint32_t)g_pluginOptions.VendorHellUberUpgradeBaseChance;
			uint32_t ultBase = (uint32_t)g_pluginOptions.VendorHellUpgradeBaseChance;
			if ((g_pluginOptions.VendorNightmareUpgradeBaseChance > 0
					&& !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("items.vendorOverhaul.nightmareBase"),
						OFF_NMUberBaseImm, EXP_NMUberBaseImm, sizeof(EXP_NMUberBaseImm), &nmBase, sizeof(nmBase)))
				|| (g_pluginOptions.VendorHellUberUpgradeBaseChance > 0
					&& !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("items.vendorOverhaul.hellUberBase"),
						OFF_HellUberBaseImm, EXP_HellUberBaseImm, sizeof(EXP_HellUberBaseImm), &hellBase, sizeof(hellBase)))
				|| (g_pluginOptions.VendorHellUpgradeBaseChance > 0
					&& !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("items.vendorOverhaul.hellBase"),
						OFF_HellUltraBaseImm, EXP_HellUltraBaseImm, sizeof(EXP_HellUltraBaseImm), &ultBase, sizeof(ultBase)))) {
				D2RL::LogErrorF(context, "plugin-items: Vendor Overhaul base-chance patch failed");
				return false;
			}

			unsigned char nmScale = ShiftCount(g_pluginOptions.VendorNightmareUpgradeLevelScale);
			unsigned char hellScale = ShiftCount(g_pluginOptions.VendorHellUberUpgradeLevelScale);
			unsigned char ultScale = ShiftCount(g_pluginOptions.VendorHellUpgradeLevelScale);
			if ((g_pluginOptions.VendorNightmareUpgradeLevelScale > 0
					&& !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("items.vendorOverhaul.nightmareScale"),
						OFF_NMUberScaleByte, EXP_NMUberScaleByte, sizeof(EXP_NMUberScaleByte), &nmScale, sizeof(nmScale)))
				|| (g_pluginOptions.VendorHellUberUpgradeLevelScale > 0
					&& !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("items.vendorOverhaul.hellUberScale"),
						OFF_HellUberScaleByte, EXP_HellUberScaleByte, sizeof(EXP_HellUberScaleByte), &hellScale, sizeof(hellScale)))
				|| (g_pluginOptions.VendorHellUpgradeLevelScale > 0
					&& !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("items.vendorOverhaul.hellScale"),
						OFF_HellUltraScaleByte, EXP_HellUltraScaleByte, sizeof(EXP_HellUltraScaleByte), &ultScale, sizeof(ultScale)))) {
				D2RL::LogErrorF(context, "plugin-items: Vendor Overhaul level-scale patch failed");
				return false;
			}
		}
	}

	if (g_pluginOptions.GambleFilter == GambleOption::NoRingAmuletGuarantee)
	{
		// Change JGE (0x7D) → JMP (0xEB) at the ring/amulet override check in FillGamble.
		// This makes the jump unconditional, permanently skipping the forced ring/amulet logic.
		unsigned char patch[] = { 0xEB };
		if (!PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("items.gambleFilter.noRingAmuletGuarantee"),
			OFF_FillGamble_JgeOpcode, EXP_FillGamble_JgeOpcode, sizeof(EXP_FillGamble_JgeOpcode), patch, sizeof(patch))) {
			D2RL::LogErrorF(context, "plugin-items: Gamble Filter branch patch failed");
			return false;
		}
	}
	else if (g_pluginOptions.GambleFilter == GambleOption::Bitfield)
	{
		// First 7 bytes: PUSH RBP (1) + PUSH RSI (1) + PUSH RDI (1) + PUSH R14 (2) + PUSH R15 (2).
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("items.gambleFilter.bitfield"),
			OFF_FillGamble, EXP_FillGamble, sizeof(EXP_FillGamble),
			Hook_FillGamble_Bitfield, &Original_FillGamble))
		{
			D2RL::LogErrorF(context, "plugin-items: failed to hook FillGamble");
			return false;
		}
	}

	if (g_pluginOptions.bDisableGoldPenalty)
	{
		// CALL is 5 bytes (E8 + 4-byte rel32); replace with NOPs to skip the penalty entirely.
		if (!PSh_ManifestPatchNop(context, PSH_MANIFEST_SITE("items.disableGoldPenalty"),
			OFF_GoldPenaltyCall, EXP_GoldPenaltyCall, sizeof(EXP_GoldPenaltyCall), 5)) {
			D2RL::LogErrorF(context, "plugin-items: Disable Gold Penalty patch failed");
			return false;
		}
	}

	if (g_pluginOptions.bMagicItemsSpawnIdentified)
	{
		// CALL is 5 bytes (E8 + 4-byte rel32); NOP out the IFLAG_IDENTIFIED-clear
		// call so the item keeps its default identified state. See the comment by
		// OFF_MagicItemsSpawnIdentified's declaration.
		if (!PSh_ManifestPatchNop(context, PSH_MANIFEST_SITE("items.magicItemsSpawnIdentified"),
			OFF_MagicItemsSpawnIdentified, EXP_MagicItemsSpawnIdentified, sizeof(EXP_MagicItemsSpawnIdentified), 5)) {
			D2RL::LogErrorF(context, "plugin-items: Magic Items Spawn Identified patch failed");
			return false;
		}
	}

	if (g_pluginOptions.bRareItemsSpawnIdentified)
	{
		if (!PSh_ManifestPatchNop(context, PSH_MANIFEST_SITE("items.rareItemsSpawnIdentified"),
			OFF_RareItemsSpawnIdentified, EXP_RareItemsSpawnIdentified, sizeof(EXP_RareItemsSpawnIdentified), 5)) {
			D2RL::LogErrorF(context, "plugin-items: Rare Items Spawn Identified patch failed");
			return false;
		}
	}

	if (g_pluginOptions.InventoryGoldLimitChange != GoldOption::Disabled)
	{
		// SUB RSP,0x28 (4 bytes) + TEST RCX,RCX (3 bytes) = 7 bytes.
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("items.inventoryGoldLimit"),
			OFF_GetInventoryGoldLimit, EXP_GetInventoryGoldLimit, sizeof(EXP_GetInventoryGoldLimit),
			Hook_GetInventoryGoldLimit, &Original_GetInventoryGoldLimit))
		{
			D2RL::LogErrorF(context, "plugin-items: failed to hook GetInventoryGoldLimit");
			return false;
		}
	}

	for (int i = 0; i < 6; i++)
	{
		if (g_pluginOptions.bRunewordQualities[i])
		{
			if (!PSh_ManifestPatchBytes(context, RUNEWORD_QUALITY_MANIFEST_IDS[i],
				OFF_RunewordQualityJumpTable + static_cast<uint64_t>(i) * 4,
				EXP_RunewordQualityJumpTableEntry, sizeof(EXP_RunewordQualityJumpTableEntry), RUNEWORD_QUALITY_PASS, sizeof(RUNEWORD_QUALITY_PASS))) {
				D2RL::LogErrorF(context, "plugin-items: Runeword Qualities patch failed");
				return false;
			}
		}
	}

	if (g_pluginOptions.bEnablePlayerConditionCalc)
	{
		if (!PSh_ManifestPatchCallSite(context, PSH_MANIFEST_SITE("items.playerConditionCalc.compileTxtCall"),
				OFF_CompileTxtCallInTC, EXP_CompileTxtCallInTC, sizeof(EXP_CompileTxtCallInTC),
				reinterpret_cast<void*>(&Hook_CompileTxt_TC))
			|| !PSh_ManifestPatchCallSite(context, PSH_MANIFEST_SITE("items.playerConditionCalc.createCompiledStructCall"),
				OFF_CreateCompiledTCStructCall, EXP_CreateCompiledTCStructCall, sizeof(EXP_CreateCompiledTCStructCall),
				reinterpret_cast<void*>(&Hook_CreateCompiledTCStruct))) {
			D2RL::LogErrorF(context, "plugin-items: Player Condition Calc call-site patch failed");
			return false;
		}
		// hookSize=6: MOV R11,RSP (3) + PUSH RBP (1) + PUSH R13 (2)
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("items.playerConditionCalc.tcDropFunction"),
			OFF_TCDropFunction, EXP_TCDropFunction, sizeof(EXP_TCDropFunction),
			Hook_TCDropFunction, &Original_TCDropFunction))
		{
			D2RL::LogErrorF(context, "plugin-items: failed to hook TCDropFunction");
			return false;
		}
		// hookSize=5: MOV qword ptr [RSP+0x8],RBX (5 bytes)
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("items.playerConditionCalc.conditionGate"),
			OFF_ConditionGate, EXP_ConditionGate, sizeof(EXP_ConditionGate),
			Hook_ConditionGate, &Original_ConditionGate))
		{
			D2RL::LogErrorF(context, "plugin-items: failed to hook ConditionGate");
			return false;
		}
		// hookSize=5: MOV qword ptr [RSP+0x8],RBX (5 bytes)
		if (!PSh_ManifestInstallInlineHook(context, PSH_MANIFEST_SITE("items.playerConditionCalc.conditionCalcEval"),
			OFF_ConditionCalcEval, EXP_ConditionCalcEval, sizeof(EXP_ConditionCalcEval),
			Hook_ConditionCalcEval, &Original_ConditionCalcEval))
		{
			D2RL::LogErrorF(context, "plugin-items: failed to hook ConditionCalcEval");
			return false;
		}
	}

	if (g_pluginOptions.bEnablePhysResistMaxChange
		&& !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("items.physResistCap.maximum"),
			OFF_PhysResist, EXP_PhysResistCap, sizeof(EXP_PhysResistCap),
			&g_pluginOptions.MaxPhysResist, sizeof(g_pluginOptions.MaxPhysResist))) {
		D2RL::LogErrorF(context, "plugin-items: Physical Resistance Cap patch failed");
		return false;
	}
	if (g_pluginOptions.bEnableElementalResistMaxChange
		&& !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("items.elementalResistCap.maximum"),
			OFF_ElementalResist, EXP_ElementalResistCap, sizeof(EXP_ElementalResistCap),
			&g_pluginOptions.MaxElementalResist, sizeof(g_pluginOptions.MaxElementalResist))) {
		D2RL::LogErrorF(context, "plugin-items: Elemental Resistance Cap patch failed");
		return false;
	}
	if (g_pluginOptions.bEnableAbsorbCapChange
		&& !PSh_ManifestPatchBytes(context, PSH_MANIFEST_SITE("items.absorbCap.maximum"),
			OFF_AbsorbCap, EXP_AbsorbCap, sizeof(EXP_AbsorbCap),
			&g_pluginOptions.MaxAbsorbPct, sizeof(g_pluginOptions.MaxAbsorbPct))) {
		D2RL::LogErrorF(context, "plugin-items: Absorb Percentage Cap patch failed");
		return false;
	}

	// Item Durability is activated last because its optional item-record broker
	// has no cross-plugin unregister contract. All other fallible validation and
	// hook requests have completed before that broker can receive our callback.
	if (!RuffnecKk::ItemDurability::Load(context, itemsConfig)) {
		return false;
	}

	const auto commit = hookTransaction.Commit(context);
	if (!commit.success) {
		if (commit.rollbackFailures != 0) {
			D2RL::LogErrorF(context,
				"plugin-items: deferred hook commit failed with %zu rollback errors; the inactive DLL remains loaded as a safety barrier.",
				commit.rollbackFailures);
			return true;
		}
		context->LogError(
			"plugin-items: deferred hook commit failed; direct writes were restored and guarded detours were deactivated.");
		return false;
	}
	RuffnecKk::MassIdentify::Activate();

	return true;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderUnloadPlugin() noexcept {
	PSh_HookTransactionDeactivate();
	CleanupPluginItemsState();
}
