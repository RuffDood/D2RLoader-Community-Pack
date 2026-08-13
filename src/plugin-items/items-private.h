#pragma once

#include <plugin-shared.h>
#include <plugin-shared-json.h>

enum class GoldOption {
	Disabled,
	PerLevel,
	Flat,
};

enum class GambleOption {
	Disabled,
	NoRingAmuletGuarantee,  // remove forced ring/amulet in first 2 gamble slots
	Bitfield,     // only allow items with dwBitField1 & 4 on the gamble screen
};

struct ItemPluginOptions {
	bool bMagicItemsSpawnIdentified;
	bool bRareItemsSpawnIdentified;
	bool bDisableGoldPenalty;
	GoldOption InventoryGoldLimitChange;
	uint32_t InventoryGoldLimit;

	// Index 0=Magic 1=Set 2=Rare 3=Unique 4=Crafted 5=Tempered
	bool bRunewordQualities[6];

	GambleOption GambleFilter;
	int GambleBitfield;

	bool bEnableVendorOverhaul;
	uint32_t VOHNormalItemLevelMaxThreshold;	// Item level where items stop being normal (and superior+lowquality)
	uint32_t VOHMagicMinLevelThreshold; // Item level where items start being magic
	uint32_t VOHSuperiorLevelMinLevelThreshold; // Item level where items start being superior
	uint32_t VOHLowQualityMaxLevelThreshold; // Item level where items stop being low quality
	uint32_t VOHSuperiorUpgradeChance;
	uint32_t VOHLowQualityDowngradeChance;
	bool bEnableVOHRandomRareVendorItems;
	uint32_t VOHRareItemChance;	// Denominator for the 1-in-N rare-item chance

	// Vendor difficulty upgrade patches (0 = keep vanilla, >0 = override).
	// "Nightmare upgrade" = uberCode (exceptional) path.
	// "Hell uber upgrade" = uberCode path when in Hell difficulty.
	// "Hell upgrade"      = ultraCode (elite) path, Hell only.
	// Level scale is the actual multiplier (power-of-2; e.g. 64 → SHL 6). Base is added after.
	int VendorNightmareUpgradeBaseChance;     // vanilla = 4000
	int VendorHellUberUpgradeBaseChance;      // vanilla = 5000
	int VendorHellUpgradeBaseChance;          // vanilla = 1000
	int VendorNightmareUpgradeLevelScale;     // vanilla = 64
	int VendorHellUberUpgradeLevelScale;      // vanilla = 128
	int VendorHellUpgradeLevelScale;          // vanilla = 16

	bool bEnablePlayerConditionCalc;

	bool bEnablePhysResistMaxChange; // one byte patch, see OFF_PhysResist
	uint8_t MaxPhysResist;
	bool bEnableElementalResistMaxChange; // one byte patch, see OFF_ElementalResist
	uint8_t MaxElementalResist;
	bool bEnableAbsorbCapChange; // one byte patch, see OFF_AbsorbCap
	uint8_t MaxAbsorbPct;

	bool Load(const D2RL::PluginContext* context, const nlohmann::json& cfg);
};

inline constexpr bool PSh_Items_IsValidVendorLevelScale(int scale) noexcept
{
	return scale == 0 || (scale > 0 && (scale & (scale - 1)) == 0);
}

inline constexpr bool PSh_Items_ShouldGenerateRareVendorItem(
	bool enabled,
	uint32_t randomValue,
	uint32_t chanceDenominator) noexcept
{
	return enabled && chanceDenominator != 0 && randomValue % chanceDenominator == 0;
}
