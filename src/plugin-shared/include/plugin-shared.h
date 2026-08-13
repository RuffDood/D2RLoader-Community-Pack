#pragma once
#include <D2RLPlugin/version.h>
#include <D2RLPlugin/context.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#define PLUGINID_ITEMS	0xEE000001
#define PLUGINID_LEVELS 0xEE000002
#define PLUGINID_MISC   0xEE000003
#define PLUGINID_QUESTS 0xEE000004
#define PLUGINID_SKILLS 0xEE000005

inline constexpr uint32_t PSh_SupportedD2RBuild = 92777;
inline constexpr char PSh_PluginPackVersion[] = "2.1.0";

inline bool PSh_ValidatePluginTarget(const D2RL::PluginContext* context) noexcept
{
	if (context == nullptr)
		return false;
	if (context->exeBase == 0)
	{
		context->LogError("PluginPack: D2R executable base is unavailable.");
		return false;
	}
	if (context->modDataVersionBuild != 0
		&& context->modDataVersionBuild != PSh_SupportedD2RBuild)
	{
		context->LogError("PluginPack: only D2R build 92777 is supported.");
		return false;
	}
	return true;
}

// ── D2R types ───────────────────────────────────────────────────────

enum class D2Difficulty : uint8_t {
	Normal,
	Nightmare,
	Hell,
};

enum class D2UnitType : uint32_t {
	Player,
	Monster,
	Object,
	Missile,
	Item,
	Tile,
	Max,
};

enum class D2ItemQuality : uint32_t {
	None,
	LowQuality,
	Normal,
	Superior,
	Magic,
	Set,
	Rare,
	Unique,
	Crafted,
	Tempered,
};

enum class D2Vendor : uint8_t {
	Akara,
	Gheed,
	Charsi,
	Fara,
	Lysander,
	Drognan,
	Hratli,
	Alkor,
	Ormus,
	Elzix,
	Asheara,
	Cain,
	Halbu,
	Jamella,
	Malah,
	Larzuk,
	Anya,
};

//
// TXT types
//

// Descriptor for one column in a D2R .txt data table (32 bytes).
// Passed as an array (null-pName terminated) to DATATBLS_CompileTxt.
// For bit-field columns (boolean flags packed into a uint64_t):
//   offset = byte offset of the containing uint64_t in the record
//   count  = bit index within that uint64_t (0 = LSB)
//   type   = determined at runtime by scanning existing bool descriptors
struct D2TxtFieldDesc {
	const char* pName;   // +0x00  column header (nullptr = array terminator)
	uint32_t    type;    // +0x08  1 = char[], 2 = int/short, 3+ = bit-field (see above)
	uint32_t    count;   // +0x0c  strings: max chars; bit-fields: bit index; ints: 0
	uint64_t    offset;  // +0x10  byte offset of field within the record struct
	uint64_t    _pad;    // +0x18  always 0
};
static_assert(sizeof(D2TxtFieldDesc) == 32, "D2TxtFieldDesc must be 32 bytes");

// Receives the compiled record array after a DATATBLS_CompileTxt call.
struct D2TxtDataArea {
	void*    pRecords;  // heap pointer to record array (game-owned)
	uint64_t nCount;    // number of compiled records
	uint64_t flags;     // high bit set = uses external/inline storage
};

// Wraps D2TxtDataArea with a vtable for internal heap allocation.
// Set vtable = exeBase + 0x16df480 (reuse the game's container vtable).
struct D2TxtContainer {
	void*          vtable;
	D2TxtDataArea* pData;
};

//
// Individual game types
//
#pragma pack(1)
struct D2ItemsTxt
{
	char     szFlippyFile[32];              // 0x000  flippyfile   (type 1, count=0x1f)
	char     szInvFile[32];                 // 0x020  invfile
	char     szUniqueInvFile[32];           // 0x040  uniqueinvfile
	char     szSetInvFile[32];              // 0x060  setinvfile

	union { uint32_t dwCode; char szCode[4]; }; // 0x080  code  (type 0a)
	uint32_t dwNormCode;                    // 0x084  normcode
	uint32_t dwUberCode;                    // 0x088  ubercode
	uint32_t dwUltraCode;                   // 0x08C  ultracode
	uint32_t dwAlternateGfx;               // 0x090  alternategfx
	uint32_t dwPspell;                      // 0x094  pSpell
	uint16_t wState;                        // 0x098  state
	uint16_t wCurseState[2];               // 0x09A  cstate1, cstate2
	uint16_t wStat[3];                      // 0x09E  stat1, stat2, stat3
	uint32_t dwCalc[3];                     // 0x0A4  calc1, calc2, calc3
	uint32_t dwLen;                         // 0x0B0  len
	uint8_t  nSpellDesc;                    // 0x0B4  spelldesc
	uint8_t  pad0xB5;                       // 0x0B5
	uint16_t wSpellDescStr;                 // 0x0B6  spelldescstr
	uint16_t wSpellDescStr2;               // 0x0B8  spelldescstr2
	uint8_t  pad0xBA[2];                   // 0x0BA
	uint32_t dwSpellDescCalc;              // 0x0BC  spelldesccalc
	uint8_t  nSpellDescColor;              // 0x0C0  spelldesccolor
	uint8_t  pad0xC1[3];                   // 0x0C1
	uint32_t dwBetterGem;                  // 0x0C4  BetterGem
	uint32_t dwWeapClass;                  // 0x0C8  wclass
	uint32_t dwWeapClass2Hand;             // 0x0CC  2handedwclass
	uint32_t dwTransmogrifyType;           // 0x0D0  TMogType  (NEW vs classic)
	int32_t  dwMinAc;                       // 0x0D4  minac
	int32_t  dwMaxAc;                       // 0x0D8  maxac
	uint32_t dwGambleCost;                 // 0x0DC  gamble cost
	int32_t  dwSpeed;                       // 0x0E0  speed
	uint32_t dwBitField1;                  // 0x0E4  bitfield1
	uint32_t dwCost;                        // 0x0E8  cost
	uint32_t dwMinStack;                    // 0x0EC  minstack
	uint32_t dwMaxStack;                    // 0x0F0  maxstack
	uint32_t dwSpawnStack;                  // 0x0F4  spawnstack
	uint32_t dwGemOffset;                  // 0x0F8  gemoffset
	uint16_t wNameStr;                      // 0x0FC  namestr
	uint16_t wVersion;                      // 0x0FE  version
	uint16_t wAutoPrefix;                  // 0x100  auto prefix
	uint16_t wMissileType;                 // 0x102  missiletype
	uint32_t dwDropConditionCalc;          // 0x104  DropConditionCalc  (NEW)
	uint32_t dwUsageConditionCalc;         // 0x108  UsageConditionCalc (NEW)
	uint8_t  nRarity;                       // 0x10C  rarity
	uint8_t  nLevel;                        // 0x10D  level
	uint8_t  nShowLevel;                   // 0x10E  ShowLevel  (NEW)
	int8_t   nMinDam;                       // 0x10F  mindam
	int8_t   nMaxDam;                       // 0x110  maxdam
	uint8_t  nMinMisDam;                   // 0x111  minmisdam
	uint8_t  nMaxMisDam;                   // 0x112  maxmisdam
	int8_t   n2HandMinDam;                 // 0x113  2handmindam
	int8_t   n2HandMaxDam;                 // 0x114  2handmaxdam
	int8_t   nRangeAdder;                  // 0x115  rangeadder
	int16_t  nStrBonus;                     // 0x116  strbonus
	int16_t  nDexBonus;                     // 0x118  dexbonus
	uint16_t wReqStr;                       // 0x11A  reqstr
	uint16_t wReqDex;                       // 0x11C  reqdex
	uint8_t  nInvWidth;                     // 0x11E  invwidth
	uint8_t  nInvHeight;                    // 0x11F  invheight
	int8_t   nBlock;                        // 0x120  block
	int8_t   nDurability;                   // 0x121  durability
	uint8_t  nNoDurability;                // 0x122  nodurability
	int8_t   nMissile;                      // 0x123  missile
	uint8_t  nComponent;                    // 0x124  component
	int8_t   nArmorComp[6];               // 0x125  rArm, lArm, torso, legs, rspad, lspad
	int8_t   n2Handed;                      // 0x12B  2handed
	uint8_t  nUseable;                      // 0x12C  useable
	uint8_t  pad0x12D;                      // 0x12D
	uint16_t wType[2];                      // 0x12E  type, type2  (type 0f)
	int8_t   nSubType;                      // 0x132  subtype
	uint8_t  pad0x133;                      // 0x133
	uint16_t wDropSound;                    // 0x134  dropsound
	uint16_t wUseSound;                     // 0x136  usesound
	uint8_t  nDropSfxFrame;               // 0x138  dropsfxframe
	uint8_t  nUnique;                       // 0x139  unique
	uint8_t  nQuest;                        // 0x13A  quest
	uint8_t  nQuestDiffCheck;             // 0x13B  questdiffcheck
	uint8_t  nTransparent;                 // 0x13C  transparent
	uint8_t  nTransTbl;                     // 0x13D  transtbl
	uint8_t  pad0x13E;                      // 0x13E
	uint8_t  nLightRadius;                 // 0x13F  lightradius
	uint8_t  nBelt;                         // 0x140  belt
	uint8_t  nAutoBelt;                     // 0x141  autobelt
	uint8_t  nStackable;                    // 0x142  stackable
	uint8_t  nSpawnable;                    // 0x143  spawnable
	int8_t   nSpellIcon;                    // 0x144  spellicon
	uint8_t  nDurWarning;                  // 0x145  durwarning
	uint8_t  nQuantityWarning;             // 0x146  qntwarning
	int8_t   nHasInv;                       // 0x147  hasinv
	int8_t   nGemSockets;                  // 0x148  gemsockets
	int8_t   nTransmogrify;               // 0x149  Transmogrify
	int8_t   nTmogMin;                      // 0x14A  TMogMin
	int8_t   nTmogMax;                      // 0x14B  TMogMax
	uint8_t  nHitClass;                     // 0x14C  hit class  (type 0d)
	int8_t   n1or2Handed;                  // 0x14D  1or2handed
	uint8_t  nGemApplyType;               // 0x14E  gemapplytype
	uint8_t  nLevelReq;                     // 0x14F  levelreq
	uint8_t  nMagicLevel;                  // 0x150  magic lvl
	int8_t   nTransform;                    // 0x151  Transform
	int8_t   nInvTrans;                     // 0x152  InvTrans
	int8_t   nCompactSave;                 // 0x153  compactsave
	uint8_t  nSkipName;                     // 0x154  SkipName
	uint8_t  nNameable;                     // 0x155  Nameable
	uint8_t  nEventItem;                    // 0x156  EventItem  (NEW)
	// Vendor min/max quantities and magic ranges (17 NPCs each)
	uint8_t  nVendorMin[17];               // 0x157  Akara..Anya Min
	uint8_t  nVendorMax[17];               // 0x168  Akara..Anya Max
	uint8_t  nVendorMagicMin[17];          // 0x179  Akara..Anya MagicMin
	uint8_t  nVendorMagicMax[17];          // 0x18A  Akara..Anya MagicMax
	uint8_t  nVendorMagicLvl[17];          // 0x19B  Akara..Anya MagicLvl
	uint32_t dwNightmareUpgrade;           // 0x1AC  NightmareUpgrade
	uint32_t dwHellUpgrade;               // 0x1B0  HellUpgrade
	uint8_t  nPermStoreItem;               // 0x1B4  PermStoreItem
	uint8_t  nMultibuy;                    // 0x1B5  multibuy
	uint8_t  pad0x1B6[2];                  // 0x1B6
	uint32_t dwDiabloCloneWeight;          // 0x1B8  diablocloneweight  (NEW)
	uint8_t  nUICatOverride;              // 0x1BC  UICatOverride  (NEW, enum lookup)
	uint8_t  nAdvancedStashStackable;     // 0x1BD  AdvancedStashStackable  (NEW)
	uint8_t  pad0x1BE[2];                  // 0x1BE  padding to 0x1C0
};
static_assert(sizeof(D2ItemsTxt) == 0x1C0, "D2ItemsTxt size mismatch");
#pragma pack()

// D2ItemDataTbl — lives at sgptDataTable[context*2] + offsets below
// (struct layout in sgptDataTable not yet mapped; access via loader fields)
struct D2ItemDataTbl
{
	uint64_t nItemsTxtRecordCount;          // total record count (weapons+armor+misc)
	D2ItemsTxt* pItemsTxt;                  // pointer to combined record array
	uint64_t nWeaponsCount;
	D2ItemsTxt* pWeapons;                   // start of weapons sub-array
	uint64_t nArmorCount;
	D2ItemsTxt* pArmor;                     // start of armor sub-array
	uint64_t nMiscCount;
	D2ItemsTxt* pMisc;                      // start of misc sub-array
};

// Compiled skills.txt record (748 bytes, stride confirmed against the game's own
// per-context data table). Field names/offsets recovered via Ghidra.
//
// One deliberate correction to Ghidra's raw output: Ghidra reports the field at
// offset 0x24 as a 4-byte dwFlags, with an unlabeled 4-byte gap before bCharclass
// at 0x2C. plugin-skills' ManaCostsLife/ManaCostsStamina feature reads/writes that
// region as a full 8-byte uint64_t (bits 47/48) and is confirmed working against
// the real game — so dwFlags is declared here as uint64_t spanning 0x24-0x2B,
// which absorbs that gap exactly and lines back up with bCharclass at 0x2C
// (Ghidra's own reported offset), with no other field shifted.
#pragma pack(1)
struct D2SkillsTxt
{
	uint16_t wSkillId;                          // 0x000
	uint8_t  _unk002[34];                       // 0x002  unmapped
	uint64_t dwFlags;                            // 0x024  flags QWORD -- bits 47/48 = ManaCostsLife/Stamina (plugin-skills)
	uint8_t  bCharclass;                         // 0x02C
	uint8_t  _pad02D[3];                         // 0x02D
	uint8_t  b_unk030;                           // 0x030
	uint8_t  bMonanim;                           // 0x031
	uint8_t  bSeqtrans;                          // 0x032
	uint8_t  bSeqnum;                            // 0x033
	uint8_t  bRange;                             // 0x034
	uint8_t  bSelectProc;                        // 0x035
	uint8_t  bSeqinput;                          // 0x036
	uint8_t  _pad037;                            // 0x037
	int16_t  nItypea1, nItypea2, nItypea3;       // 0x038, 0x03A, 0x03C
	int16_t  nItypeb1, nItypeb2, nItypeb3;       // 0x03E, 0x040, 0x042
	int16_t  nEtypea1, nEtypea2;                 // 0x044, 0x046
	int16_t  nEtypeb1, nEtypeb2;                 // 0x048, 0x04A
	int16_t  nSrvstfunc;                         // 0x04C
	int16_t  nSrvdofunc;                         // 0x04E
	int16_t  nSrvprgfunc1, nSrvprgfunc2, nSrvprgfunc3; // 0x050, 0x052, 0x054
	uint8_t  _pad056[2];                         // 0x056
	int32_t  nPrgcalc1, nPrgcalc2, nPrgcalc3;    // 0x058, 0x05C, 0x060
	uint8_t  bPrgdam;                            // 0x064
	uint8_t  _pad065;                            // 0x065
	int16_t  nSrvmissile, nSrvmissilea, nSrvmissileb, nSrvmissilec; // 0x066, 0x068, 0x06A, 0x06C
	int16_t  nSrvoverlay;                        // 0x06E
	int32_t  nAuraFilter;                        // 0x070
	int16_t  nAuraStat1, nAurastat2, nAurastat3, nAurastat4, nAurastat5, nAurastat6; // 0x074..0x07E
	int32_t  nAuraLenCalc, nAuraRangeCalc;       // 0x080, 0x084
	int32_t  nAuraStatCalc1, nAurastatcalc2, nAurastatcalc3, nAurastatcalc4, nAurastatcalc5, nAurastatcalc6; // 0x088..0x09C
	int16_t  nAurastate, nAuraTargetState;       // 0x0A0, 0x0A2
	int16_t  nAuraevent1, nAuraevent2, nAuraevent3, nAuraevent4; // 0x0A4..0x0AA
	int16_t  nAuraeventfunc1, nAuraeventfunc2, nAuraeventfunc3, nAuraeventfunc4; // 0x0AC..0x0B2
	int16_t  nPassivestate, nPassiveitype;       // 0x0B4, 0x0B6
	uint8_t  bPassivereqweaponcount;             // 0x0B8
	uint8_t  _pad0B9;                            // 0x0B9
	int16_t  nPassivestat1, nPassivestat2, nPassivestat3, nPassivestat4, nPassivestat5, nPassivestat6, nPassivestat7, nPassivestat8, nPassivestat9, nPassivestat10, nPassivestat11, nPassivestat12, nPassivestat13, nPassivestat14; // 0x0BA..0x0D4
	uint8_t  _pad0D6[2];                         // 0x0D6
	int32_t  nPassivecalc1, nPassivecalc2, nPassivecalc3, nPassivecalc4, nPassivecalc5, nPassivecalc6, nPassivecalc7, nPassivecalc8, nPassivecalc9, nPassivecalc10, nPassivecalc11, nPassivecalc12, nPassivecalc13, nPassivecalc14; // 0x0D8..0x10C
	int16_t  nSummon;                            // 0x110
	uint8_t  bPettype, bSummode;                 // 0x112, 0x113
	int32_t  nPetmax;                            // 0x114
	int16_t  nSumskill1, nSumskill2, nSumskill3, nSumskill4, nSumskill5; // 0x118..0x120
	uint8_t  _pad122[2];                         // 0x122
	int32_t  nSumsk1calc, nSumsk2calc, nSumsk3calc, nSumsk4calc, nSumsk5calc; // 0x124..0x134
	int32_t  nSumumod;                           // 0x138
	int16_t  nSumoverlay;                        // 0x13C
	int16_t  nCltmissile, nCltmissilea, nCltmissileb, nCltmissilec, nCltmissiled; // 0x13E..0x146
	int16_t  nCltstfunc, nCltdofunc;             // 0x148, 0x14A
	int16_t  nCltprgfunc1, nCltprgfunc2, nCltprgfunc3; // 0x14C, 0x14E, 0x150
	int16_t  nStsound, nStsoundclass;            // 0x152, 0x154
	int16_t  nDosound, nDosound_a, nDosound_b;   // 0x156, 0x158, 0x15A
	int16_t  nCastoverlay, nTgtoverlay, nTgtsound; // 0x15C, 0x15E, 0x160
	int16_t  nPrgoverlay, nPrgsound;             // 0x162, 0x164
	int16_t  nCltoverlaya, nCltoverlayb;         // 0x166, 0x168
	uint8_t  _pad16A[2];                         // 0x16A
	int32_t  nCltcalc1, nCltcalc2, nCltcalc3;    // 0x16C, 0x170, 0x174
	uint8_t  bItemTarget;                        // 0x178
	uint8_t  _pad179;                            // 0x179
	int16_t  nItemCastSound, nItemCastOverlay;   // 0x17A, 0x17C
	uint8_t  _pad17E[2];                         // 0x17E
	int32_t  nPerdelay;                          // 0x180
	int16_t  nMaxlvl, nResultFlags;              // 0x184, 0x186
	int32_t  nHitFlags, nHitClass;               // 0x188, 0x18C
	int32_t  nCalc1, nCalc2, nCalc3, nCalc4, nCalc5, nCalc6, nCalc7, nCalc8, nCalc9, nCalc10; // 0x190..0x1B4
	int32_t  nParam1, nParam2, nParam3, nParam4, nParam5, nParam6, nParam7, nParam8, nParam9, nParam10,
	         nParam11, nParam12, nParam13, nParam14, nParam15, nParam16, nParam17, nParam18, nParam19, nParam20; // 0x1B8..0x204
	uint8_t  bWeapsel;                            // 0x208
	uint8_t  _pad209;                             // 0x209
	int16_t  nItemEffect, nItemCltEffect;        // 0x20A, 0x20C
	uint8_t  _pad20E[2];                         // 0x20E
	int32_t  nSkpoints;                          // 0x210
	int16_t  nReqlevel, nReqstr, nReqdex, nReqint, nReqvit; // 0x214..0x21C
	int16_t  nReqskill1, nReqskill2, nReqskill3; // 0x21E, 0x220, 0x222
	int16_t  nStartmana, nMinmana, nManashift, nMana, nLvlmana; // 0x224..0x22C
	uint8_t  bPrgchargestocast, bPrgchargesconsumed, bAttackrank, bLineofsight; // 0x22E..0x231
	uint8_t  _pad232[2];                         // 0x232
	int32_t  nGlobalDelay, nLocaldelay;          // 0x234, 0x238
	int16_t  nSkilldesc;                         // 0x23C
	uint8_t  _pad23E[2];                         // 0x23E
	int32_t  nToHit, nLevToHit, nToHitCalc;      // 0x240, 0x244, 0x248
	uint8_t  bHitShift, bSrcDam;                 // 0x24C, 0x24D
	uint8_t  _pad24E[2];                         // 0x24E
	int32_t  nMinDam, nMaxDam;                   // 0x250, 0x254
	int32_t  nMinLevDam1, nMinLevDam2, nMinLevDam3, nMinLevDam4, nMinLevDam5; // 0x258..0x268
	int32_t  nMaxLevDam1, nMaxLevDam2, nMaxLevDam3, nMaxLevDam4, nMaxLevDam5; // 0x26C..0x27C
	int32_t  nDmgSymPerCalc;                     // 0x280
	uint8_t  bEType;                             // 0x284
	uint8_t  _pad285[3];                         // 0x285
	int32_t  nEMinDam, nEMaxDam;                 // 0x288, 0x28C
	int32_t  nEMinLev1, nEMinLev2, nEMinLev3, nEMinLev4, nEMinLev5; // 0x290..0x2A0
	int32_t  nEMaxLev1, nEMaxLev2, nEMaxLev3, nEMaxLev4, nEMaxLev5; // 0x2A4..0x2B4
	int32_t  nEDmgSymPerCalc;                    // 0x2B8
	int32_t  nELevLen, nELevLen1, nELevLen2, nELevLen3;  // 0x2BC..0x2C8
	int32_t  nELenSymPerCalc;                    // 0x2CC
	uint8_t  bRestrict_;                         // 0x2D0
	uint8_t  _pad2D1;                            // 0x2D1
	int16_t  nState1, nState2, nState3;          // 0x2D2, 0x2D4, 0x2D6
	uint8_t  bAitype;                            // 0x2D8
	uint8_t  _pad2D9;                            // 0x2D9
	int16_t  nAibonus;                           // 0x2DA
	int32_t  nCost_mult, nCost_add;              // 0x2DC, 0x2E0
	uint8_t  bUseServerMissilesOnRemoteClients;  // 0x2E4
	uint8_t  _pad2E5;                            // 0x2E5
	int16_t  nSrvstopfunc, nCltstopfunc;         // 0x2E6, 0x2E8
	uint8_t  _pad2EA[2];                         // 0x2EA
};
static_assert(sizeof(D2SkillsTxt) == 0x2EC, "D2SkillsTxt size mismatch");
#pragma pack()

//
// D2Game types
//

// Proxy item entry in a vendor's item cache (12 bytes, stride confirmed by Ghidra).
struct NpcItemCacheEntry {
	uint8_t  nMin;        // +0x00
	uint8_t  nMax;        // +0x01
	uint8_t  nMagicMin;   // +0x02
	uint8_t  nMagicMax;   // +0x03
	uint32_t dwCode;      // +0x04
	uint8_t  nMagicLevel; // +0x08
	uint8_t  _pad[3];     // +0x09..+0x0b
};
static_assert(sizeof(NpcItemCacheEntry) == 12, "NpcItemCacheEntry size mismatch");

// Per-NPC vendor state entry (0x78 bytes, stride confirmed by Ghidra).
struct VendorChainEntry {
	uint16_t         npcId;        // +0x00  matches the low 16 bits of pNpc->classId
	uint8_t          _pad0[0x36];  // +0x02..+0x37
	uint64_t         qwTicks;      // +0x38  GetTickCount64() refresh timestamp
	NpcItemCacheEntry* pItemCache; // +0x40
	uint64_t         nItems;       // +0x48
	uint8_t          _pad1[0x08];  // +0x50..+0x57
	uint32_t*        pPermCache;   // +0x58  array of uint32_t item codes
	uint64_t         nPerms;       // +0x60
	uint8_t          _pad2[0x10];  // +0x68..+0x77
};
static_assert(sizeof(VendorChainEntry) == 0x78, "VendorChainEntry size mismatch");

struct D2GameStrc {
	uint8_t          _unk0[0x104];         // +0x000
	D2Difficulty     difficultyLevel;      // +0x104
	uint8_t          _unk105;              // +0x105
	uint8_t          expansion;            // +0x106
	uint8_t          _unk107[0x21];        // +0x107..+0x127
	uint16_t         wItemFormat;          // +0x128
	uint8_t          _unk12a[0x370e];      // +0x12a..+0x3837
	VendorChainEntry* pVendorChain;        // +0x3838
	uint64_t         nVendorChain;         // +0x3840
	uint8_t          _unk3848[0x1e08];     // +0x3848..+0x564f
	uint32_t         rngSeedLow;           // +0x5650
	uint32_t         rngSeedHigh;          // +0x5654
};
static_assert(offsetof(D2GameStrc, difficultyLevel) == 0x104, "D2GameStrc layout mismatch");
static_assert(offsetof(D2GameStrc, expansion)       == 0x106, "D2GameStrc layout mismatch");
static_assert(offsetof(D2GameStrc, wItemFormat)     == 0x128, "D2GameStrc layout mismatch");
static_assert(offsetof(D2GameStrc, pVendorChain)    == 0x3838, "D2GameStrc layout mismatch");
static_assert(offsetof(D2GameStrc, nVendorChain)    == 0x3840, "D2GameStrc layout mismatch");
static_assert(offsetof(D2GameStrc, rngSeedLow)      == 0x5650, "D2GameStrc layout mismatch");
static_assert(offsetof(D2GameStrc, rngSeedHigh)     == 0x5654, "D2GameStrc layout mismatch");

struct D2QuestDataStrc {
	int32_t nQuestNo;     // +00
	int32_t nUnk0x04;     // +04, possibly padding
	D2GameStrc* pGame;    // +08
};

struct D2UnitStrc;

// Mirrors the Ghidra-recovered D2StatListStrc layout (112 bytes). Only fields
// confirmed via Ghidra are named; the trailing region is internal engine
// linkage (sibling stat-list pointers) that callers don't need directly.
struct D2StatListStrc {
	void*        pMemPool;    // +0x00
	D2UnitStrc*  pUnit;       // +0x08
	uint32_t     ownerType;   // +0x10
	uint32_t     ownerId;     // +0x14
	uint32_t     flags;       // +0x18
	uint32_t     expireFrame; // +0x1c
	int32_t      stateNumber; // +0x20
	uint32_t     skillNumber; // +0x24
	uint32_t     skillLevel;  // +0x28
	uint8_t      _pad0[68];   // +0x2c..+0x6f
};
static_assert(offsetof(D2StatListStrc, expireFrame) == 0x1c, "D2StatListStrc layout mismatch");
static_assert(sizeof(D2StatListStrc) == 112, "D2StatListStrc must be 112 bytes");

// Mirrors the Ghidra-recovered D2UnitStrc layout (448 bytes). Only fields with
// known uses are named; gaps are explicit padding so offsets stay correct.
struct D2UnitStrc {
	D2UnitType       dwUnitType;     // +0x00
	uint32_t         classId;        // +0x04, class/TXT record ID (UNITS_GetClassId)
	uint8_t          _pad0[31];      // +0x09..+0x27 (skips an unnamed byte at +0x08)
	uint32_t         seedLow;        // +0x28
	uint32_t         seedHigh;       // +0x2c
	uint8_t          _pad1[88];      // +0x30..+0x87
	D2StatListStrc*  statList;       // +0x88
	uint8_t          _pad2[112];     // +0x90..+0xff
	int64_t     _unk0x100;      // +0x100
	uint8_t     _pad3[28];      // +0x108..+0x123
	uint32_t    dwFlags;        // +0x124
	uint8_t     _pad4[149];     // +0x128..+0x1bc
	uint8_t     itemTableEntry; // +0x1bd
	uint8_t     _pad5[2];       // +0x1be..+0x1bf
};
static_assert(offsetof(D2UnitStrc, dwUnitType)     == 0x00,  "D2UnitStrc layout mismatch");
static_assert(offsetof(D2UnitStrc, classId)        == 0x04,  "D2UnitStrc layout mismatch");
static_assert(offsetof(D2UnitStrc, seedLow)        == 0x28,  "D2UnitStrc layout mismatch");
static_assert(offsetof(D2UnitStrc, seedHigh)       == 0x2c,  "D2UnitStrc layout mismatch");
static_assert(offsetof(D2UnitStrc, statList)       == 0x88,  "D2UnitStrc layout mismatch");
static_assert(offsetof(D2UnitStrc, dwFlags)        == 0x124, "D2UnitStrc layout mismatch");
static_assert(offsetof(D2UnitStrc, itemTableEntry) == 0x1bd, "D2UnitStrc layout mismatch");
static_assert(sizeof(D2UnitStrc) == 448, "D2UnitStrc must be 448 bytes");

// Keep shared consumers on the canonical header fields instead of recreating
// partial D2UnitStrc layouts in individual plugin DLLs.
[[nodiscard]] constexpr D2UnitType PSh_UnitType(const D2UnitStrc& unit) noexcept {
	return unit.dwUnitType;
}

[[nodiscard]] constexpr uint32_t PSh_UnitClassId(const D2UnitStrc& unit) noexcept {
	return unit.classId;
}

// ── Memory / call-site patching ─────────────────────────────────────────────
//
// context->PatchRel32 fails at runtime ("target rva is out of rel32 range")
// whenever the redirect target lives more than ~2GB away from the call site —
// which is always true for a redirect into this plugin DLL's own code, since
// Windows loads DLLs far from the exe with no proximity guarantee. The loader
// has no code-cave/trampoline primitive to work around this (confirmed with
// the loader author: a "Rel64" patch isn't possible — x64 CALL/JMP only have
// rel32 encodings). PSh_PatchCallSite solves it entirely on the plugin side,
// the same way plugin-shared did on the pre-v1.0.0 loader API (see git
// history — this was dropped when plugin-shared stopped being its own
// loadable plugin): allocate a small stub within rel32 range of the call
// site (via PSh_AllocNear) containing an FF25 absolute indirect jmp to the
// real hook function, then point the call site's E8 rel32 at that stub
// instead. The call site's CALL instruction still executes as a real CALL
// (the CPU auto-pushes the correct return address), so hookFn runs and
// returns exactly as if it had been called directly — no change needed to
// hookFn itself.

// Allocates `size` bytes of PAGE_EXECUTE_READWRITE memory within ±2GB of
// `hint`, required so a 5-byte rel32 CALL/JMP at `hint` can reach it. Returns
// nullptr on failure. Free with VirtualFree(ptr, 0, MEM_RELEASE).
extern "C" void* PSh_AllocNear(void* hint, size_t size) noexcept;

// Redirects the 5-byte E8 CALL at (context->exeBase + callOffset) to hookFn
// via a near FF25 stub (see the comment above), leaving the real callee
// function itself untouched. hookFn's signature must match the callee's
// calling convention and parameters — it's simply substituted for whatever
// the CALL used to target. expected/expectedSize are the call site's current
// 5 bytes (E8 + rel32), verified before patching; pass the site's known
// EXP_* array. Every executable write must carry exactly one manifest ID;
// CMake audits these IDs against hook-manifest.json and rejects raw patch API
// calls outside plugin-shared.
#define PSH_MANIFEST_SITE(id) id

struct PSh_HookTransactionCommitResult {
	bool success{};
	size_t completedOperations{};
	size_t totalOperations{};
	size_t rolledBackOperations{};
	size_t rollbackFailures{};
};

using PSh_HookTransactionCleanupFn = void(*)() noexcept;

bool PSh_HookTransactionBegin() noexcept;
void PSh_HookTransactionAbort() noexcept;
void PSh_HookTransactionDeactivate() noexcept;
bool PSh_HookTransactionIsCollecting() noexcept;
bool PSh_HookTransactionIsOperational() noexcept;
bool PSh_HookTransactionEnqueue(
	const char* label,
	bool required,
	std::function<bool()> action,
	std::function<bool()> rollback = {}) noexcept;
PSh_HookTransactionCommitResult PSh_HookTransactionCommit(
	const D2RL::PluginContext* context) noexcept;

bool PSh_RestoreExecutableBytes(
	const D2RL::PluginContext* context,
	uint64_t rva,
	const void* applied,
	const void* original,
	uint32_t size) noexcept;

bool PSh_InstallGuardedInlineHook(
	const D2RL::PluginContext* context,
	uint64_t rva,
	const void* expected,
	uint32_t expectedSize,
	void* target,
	void** original) noexcept;

class PSh_HookTransactionScope {
public:
	explicit PSh_HookTransactionScope(PSh_HookTransactionCleanupFn cleanup) noexcept
		: cleanup_(cleanup), active_(PSh_HookTransactionBegin())
	{
	}

	PSh_HookTransactionScope(const PSh_HookTransactionScope&) = delete;
	PSh_HookTransactionScope& operator=(const PSh_HookTransactionScope&) = delete;

	~PSh_HookTransactionScope() noexcept
	{
		if (!active_ || finalized_) {
			return;
		}
		PSh_HookTransactionAbort();
		if (cleanup_) {
			cleanup_();
		}
	}

	[[nodiscard]] bool IsActive() const noexcept { return active_; }

	PSh_HookTransactionCommitResult Commit(
		const D2RL::PluginContext* context) noexcept
	{
		finalized_ = true;
		auto result = PSh_HookTransactionCommit(context);
		if (!result.success && cleanup_) {
			cleanup_();
		}
		return result;
	}

private:
	PSh_HookTransactionCleanupFn cleanup_{};
	bool active_{};
	bool finalized_{};
};

inline bool PSh_ManifestSiteIsValid(
	const D2RL::PluginContext* context,
	const char* manifestId,
	uint64_t rva,
	const void* expected,
	uint32_t expectedSize) noexcept
{
	return context != nullptr && manifestId != nullptr && *manifestId != '\0'
		&& (expected == nullptr || expectedSize == 0
			|| context->CheckExpectedBytes(rva, expected, expectedSize));
}

inline std::vector<uint8_t> PSh_CopyHookBytes(const void* bytes, uint32_t size)
{
	if (bytes == nullptr || size == 0) {
		return {};
	}
	const auto* begin = static_cast<const uint8_t*>(bytes);
	return { begin, begin + size };
}

inline bool PSh_ManifestPatchBytes(
	const D2RL::PluginContext* context,
	const char* manifestId,
	uint64_t rva,
	const void* expected,
	uint32_t expectedSize,
	const void* bytes,
	uint32_t size) noexcept
{
	if (!PSh_ManifestSiteIsValid(context, manifestId, rva, expected, expectedSize)
		|| bytes == nullptr || size == 0) {
		return false;
	}
	if (!PSh_HookTransactionIsCollecting()) {
		return context->PatchBytes(rva, expected, expectedSize, bytes, size);
	}
	try {
		auto expectedCopy = PSh_CopyHookBytes(expected, expectedSize);
		auto bytesCopy = PSh_CopyHookBytes(bytes, size);
		if (expectedCopy.size() < bytesCopy.size()) {
			return false;
		}
		auto originalCopy = std::vector<uint8_t>(
			expectedCopy.begin(), expectedCopy.begin() + bytesCopy.size());
		return PSh_HookTransactionEnqueue(manifestId, true,
			[context, rva, expectedCopy = std::move(expectedCopy),
				bytesCopy = std::move(bytesCopy)]() noexcept {
				return context->PatchBytes(
					rva,
					expectedCopy.empty() ? nullptr : expectedCopy.data(),
					static_cast<uint32_t>(expectedCopy.size()),
					bytesCopy.data(),
					static_cast<uint32_t>(bytesCopy.size()));
			},
			[context, rva, appliedCopy = PSh_CopyHookBytes(bytes, size),
				originalCopy = std::move(originalCopy)]() noexcept {
				return PSh_RestoreExecutableBytes(
					context,
					rva,
					appliedCopy.data(),
					originalCopy.data(),
					static_cast<uint32_t>(originalCopy.size()));
			});
	}
	catch (...) {
		return false;
	}
}

inline bool PSh_ManifestPatchNop(
	const D2RL::PluginContext* context,
	const char* manifestId,
	uint64_t rva,
	const void* expected,
	uint32_t expectedSize,
	uint32_t size) noexcept
{
	if (!PSh_ManifestSiteIsValid(context, manifestId, rva, expected, expectedSize)
		|| size == 0) {
		return false;
	}
	if (!PSh_HookTransactionIsCollecting()) {
		return context->PatchNop(rva, expected, expectedSize, size);
	}
	try {
		auto expectedCopy = PSh_CopyHookBytes(expected, expectedSize);
		if (expectedCopy.size() < size) {
			return false;
		}
		auto originalCopy = std::vector<uint8_t>(
			expectedCopy.begin(), expectedCopy.begin() + size);
		auto appliedCopy = std::vector<uint8_t>(size, 0x90);
		return PSh_HookTransactionEnqueue(manifestId, true,
			[context, rva, size, expectedCopy = std::move(expectedCopy)]() noexcept {
				return context->PatchNop(
					rva,
					expectedCopy.empty() ? nullptr : expectedCopy.data(),
					static_cast<uint32_t>(expectedCopy.size()),
					size);
			},
			[context, rva, appliedCopy = std::move(appliedCopy),
				originalCopy = std::move(originalCopy)]() noexcept {
				return PSh_RestoreExecutableBytes(
					context,
					rva,
					appliedCopy.data(),
					originalCopy.data(),
					static_cast<uint32_t>(originalCopy.size()));
			});
	}
	catch (...) {
		return false;
	}
}

inline bool PSh_ManifestPatchWriteU8(
	const D2RL::PluginContext* context,
	const char* manifestId,
	uint64_t rva,
	const void* expected,
	uint32_t expectedSize,
	uint8_t value) noexcept
{
	if (!PSh_ManifestSiteIsValid(context, manifestId, rva, expected, expectedSize)) {
		return false;
	}
	if (!PSh_HookTransactionIsCollecting()) {
		return context->PatchWriteU8(rva, expected, expectedSize, value);
	}
	try {
		auto expectedCopy = PSh_CopyHookBytes(expected, expectedSize);
		if (expectedCopy.empty()) {
			return false;
		}
		const auto originalValue = expectedCopy.front();
		return PSh_HookTransactionEnqueue(manifestId, true,
			[context, rva, value, expectedCopy = std::move(expectedCopy)]() noexcept {
				return context->PatchWriteU8(
					rva,
					expectedCopy.empty() ? nullptr : expectedCopy.data(),
					static_cast<uint32_t>(expectedCopy.size()),
					value);
			},
			[context, rva, value, originalValue]() noexcept {
				return PSh_RestoreExecutableBytes(
					context,
					rva,
					&value,
					&originalValue,
					1);
			});
	}
	catch (...) {
		return false;
	}
}

inline bool PSh_ManifestPatchRel32(
	const D2RL::PluginContext* context,
	const char* manifestId,
	uint64_t rva,
	const void* expected,
	uint32_t expectedSize,
	uint64_t targetRva,
	uint32_t size,
	D2RL::Rel32PatchKind kind) noexcept
{
	if (!PSh_ManifestSiteIsValid(context, manifestId, rva, expected, expectedSize)
		|| size == 0) {
		return false;
	}
	if (!PSh_HookTransactionIsCollecting()) {
		return context->PatchRel32(rva, expected, expectedSize, targetRva, size, kind);
	}
	try {
		auto expectedCopy = PSh_CopyHookBytes(expected, expectedSize);
		if (expectedCopy.size() < size) {
			return false;
		}
		auto originalCopy = std::vector<uint8_t>(
			expectedCopy.begin(), expectedCopy.begin() + size);
		auto appliedCopy = std::make_shared<std::vector<uint8_t>>();
		return PSh_HookTransactionEnqueue(manifestId, true,
			[context, rva, targetRva, size, kind,
				expectedCopy = std::move(expectedCopy), appliedCopy]() noexcept {
				if (!context->PatchRel32(
					rva,
					expectedCopy.empty() ? nullptr : expectedCopy.data(),
					static_cast<uint32_t>(expectedCopy.size()),
					targetRva,
					size,
					kind)) {
					return false;
				}
				const auto* appliedBegin = reinterpret_cast<const uint8_t*>(
					context->exeBase + rva);
				appliedCopy->assign(appliedBegin, appliedBegin + size);
				return true;
			},
			[context, rva, appliedCopy,
				originalCopy = std::move(originalCopy)]() noexcept {
				return appliedCopy->size() == originalCopy.size()
					&& PSh_RestoreExecutableBytes(
						context,
						rva,
						appliedCopy->data(),
						originalCopy.data(),
						static_cast<uint32_t>(originalCopy.size()));
			});
	}
	catch (...) {
		return false;
	}
}

inline bool PSh_ManifestPatchCallRel32(
	const D2RL::PluginContext* context,
	const char* manifestId,
	uint64_t rva,
	const void* expected,
	uint32_t expectedSize,
	uint64_t targetRva,
	uint32_t size = 5) noexcept
{
	return PSh_ManifestPatchRel32(
		context,
		manifestId,
		rva,
		expected,
		expectedSize,
		targetRva,
		size,
		D2RL::Rel32PatchKind::Call);
}

template <typename Function>
inline bool PSh_ManifestInstallInlineHook(
	const D2RL::PluginContext* context,
	const char* manifestId,
	uint64_t rva,
	const void* expected,
	uint32_t expectedSize,
	Function target,
	Function* original = nullptr) noexcept
{
	if (!PSh_ManifestSiteIsValid(context, manifestId, rva, expected, expectedSize)
		|| target == nullptr) {
		return false;
	}
	if (!PSh_HookTransactionIsCollecting()) {
		return context->InstallInlineHook(rva, expected, expectedSize, target, original);
	}
	try {
		auto expectedCopy = PSh_CopyHookBytes(expected, expectedSize);
		return PSh_HookTransactionEnqueue(manifestId, true,
			[context, rva, target, original,
				expectedCopy = std::move(expectedCopy)]() noexcept {
				void* originalAddress{};
				if (!PSh_InstallGuardedInlineHook(
					context,
					rva,
					expectedCopy.empty() ? nullptr : expectedCopy.data(),
					static_cast<uint32_t>(expectedCopy.size()),
					reinterpret_cast<void*>(target),
					&originalAddress)) {
					return false;
				}
				if (original != nullptr) {
					*original = reinterpret_cast<Function>(originalAddress);
				}
				return true;
			});
	}
	catch (...) {
		return false;
	}
}

inline bool PSh_RegisterConsoleCommand(
	const D2RL::PluginContext* context,
	const char* name,
	D2RL::ConsoleCommandCallback callback,
	const char* description = nullptr,
	void* userData = nullptr) noexcept
{
	if (!context || !name || *name == '\0' || !callback) {
		return false;
	}
	if (!PSh_HookTransactionIsCollecting()) {
		return context->RegisterConsoleCommand(name, callback, description, userData);
	}
	try {
		std::string nameCopy(name);
		std::string descriptionCopy(description ? description : "");
		return PSh_HookTransactionEnqueue(name, false,
			[context, callback, userData, nameCopy = std::move(nameCopy),
				descriptionCopy = std::move(descriptionCopy)]() noexcept {
				return context->RegisterConsoleCommand(
					nameCopy.c_str(),
					callback,
					descriptionCopy.empty() ? nullptr : descriptionCopy.c_str(),
					userData);
			});
	}
	catch (...) {
		return false;
	}
}

extern "C" bool PSh_ManifestPatchCallSite(
	const D2RL::PluginContext* context,
	const char* manifestId,
	uint64_t callOffset,
	const void* expected,
	uint32_t expectedSize,
	void* hookFn) noexcept;

// ── RNG ───────────────────────────────────────────────────────────────────────

// Advances unit's RNG seed pair (seedLow/seedHigh) and returns the raw 64-bit
// result, using the same LCG the engine uses for per-unit rolls (see
// SKILLS_FindPotion_DropPotion @ 0x140417018): next = seedLow * 0x6AC690C5 + seedHigh.
// Callers reduce (uint32_t)result as needed (e.g. % 100 for a percent roll).
extern "C" uint64_t PSh_RollUnit(D2UnitStrc* unit) noexcept;

// ── Stats ─────────────────────────────────────────────────────────────────────

// Calls the game's own stat-lookup wrapper (debug build RVA 0x2f5020, confirmed
// via decompile of D2Common_SKILLMANA_CheckStat, which fetches mana/life exactly
// this way: FUN_1402f5020(unit, statId)) and returns the stat's current value, or
// 0 if absent. Takes the UNIT pointer directly, not its statList -- the real
// function derives the statlist lookup internally (via an unexported low-level
// helper at RVA 0x2f9b10 that isn't safe to call directly: its 3rd argument is an
// internal lookup pointer obtained via another private call, not a caller-
// supplied value). Requires exeBase (context->exeBase) since the call target
// lives in the game executable, not plugin-shared.
//
// History: earlier revisions of this constant were 0xf9b10 (a digit-dropped
// transcription of the real low-level helper's RVA 0x2f9b10 -- calling that
// address directly landed inside STATLIST_GetStatValue's *body*, not its entry)
// and 0x224720 (STATLIST_GetStatValue's entry point, but in the PROFILE build --
// wrong exe target entirely for a debug-build plugin). Both produced a crash
// exactly where the digit/exe mismatch put execution: mid-function garbage.
extern "C" int PSh_GetStat(uintptr_t exeBase, D2UnitStrc* unit, int statId) noexcept;

// ── Utilities ─────────────────────────────────────────────────────────────────
constexpr uint32_t PSh_EncodeItemCode(const char* itemCode)
{
	if (!itemCode || itemCode[0] == '\0' || itemCode[1] == '\0' || itemCode[2] == '\0') return 0;
	const char lastChar = 0x20;
	return ((uint32_t)itemCode[0]) |
		(((uint32_t)itemCode[1]) << 8) |
		(((uint32_t)itemCode[2]) << 16) |
		(((uint32_t)lastChar) << 24);
}

constexpr uint32_t PSh_EncodeItemTypeCode(const char* itemTypeCode)
{
	if (!itemTypeCode) return 0;
	const char lastChar = itemTypeCode[3] == 0x20 || itemTypeCode[3] == 0x00 ? 0x20 : itemTypeCode[3];
	return lastChar |
		(((uint32_t)itemTypeCode[2]) << 8) |
		(((uint32_t)itemTypeCode[1]) << 16) |
		(((uint32_t)itemTypeCode[0]) << 24);
}
