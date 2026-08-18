# D2RLoader Community Pack 1.0.0

Community Pack 1.0.0 combines eezstreet's original PluginPack with 22
RuffnecKk features inside the same five-DLL layout and one central
`D2RPlugins.json` file.

THIS PACK IS NOT SUPPORTED ANYMORE. PLEASE CHECK OUT THIS REPO:
(**https://github.com/RuffDood/D2RLoader-Community-Pack/releases/latest**)

## Download

Open the [latest release](https://github.com/RuffDood/D2RLoader-Community-Pack/releases/latest)
and download `Community-Pack-1.0.0-Windows-x64.zip`.

GitHub's **Code -> Download ZIP** option contains source code only. It does not
contain the compiled DLLs or the ready-to-use configuration. Use the ZIP listed
under **Releases** to download installable files.

Community Pack 1.0.0 requires:

- **D2RLoader 1.0.1 or later**;
- **Diablo II: Resurrected 3.2.92777**.

The installable package contains these five plugins:

- `plugin-items.dll`
- `plugin-levels.dll`
- `plugin-misc.dll`
- `plugin-quests.dll`
- `plugin-skills.dll`


## Installation

Choose either a global installation or a mod-local installation. Install the
pack in one scope only.

### Global installation

Copy the packaged `d2rloader` directory into the Diablo II: Resurrected
installation directory. The result should look like this:

```text
<D2R>/d2rloader/plugins/plugin-items.dll
<D2R>/d2rloader/plugins/plugin-levels.dll
<D2R>/d2rloader/plugins/plugin-misc.dll
<D2R>/d2rloader/plugins/plugin-quests.dll
<D2R>/d2rloader/plugins/plugin-skills.dll
<D2R>/d2rloader/config/D2RPlugins.json
```

### Mod-local installation

Copy the packaged `d2rloader` directory into the selected mod directory:

```text
<D2R>/mods/<mod>/d2rloader/plugins/plugin-items.dll
<D2R>/mods/<mod>/d2rloader/plugins/plugin-levels.dll
<D2R>/mods/<mod>/d2rloader/plugins/plugin-misc.dll
<D2R>/mods/<mod>/d2rloader/plugins/plugin-quests.dll
<D2R>/mods/<mod>/d2rloader/plugins/plugin-skills.dll
<D2R>/mods/<mod>/d2rloader/config/D2RPlugins.json
```

When both configuration files exist, the mod-local file takes priority over
the global file. Restart D2RLoader after replacing a DLL or changing the
configuration.

The original PluginPack expected its JSON inside a mod's MPQ/data structure.
Community Pack does not. Install `D2RPlugins.json` in the `d2rloader/config`
directory shown above.

## Before updating from standalone plugins or patches

Remove every standalone DLLor patch that implements one of the 22
integrated features before starting the game.

Standalone plugins to remove include Bulk Skill Point Allocation,
AdvancedItemTooltips, RemoteStash, MassID, Durability Resistance,
NoEtherealItemTypes, FloatingDamage, Enhanced Damage
Min/Max Fix, Charm Aura Trigger Fix.

The only patch that needs removal is Ethereal Item Rules.

Do not rely on disabling the equivalent Community Pack feature to make an old
standalone DLL safe to load beside the pack. Remove the duplicate DLL and copy
any settings you want to keep into the matching block of `D2RPlugins.json`.

## Shipped defaults

No-brainer features are enabled within the provided json config :

- Charm Aura Trigger Fix;
- Enhanced Damage Min/Max Fix;
- Qty Display Fix;
- Equipped Item to Cube;

Every other configurable RuffnecKk integration ships disabled or in its vanilla-preserving
mode.

## The 22 integrated RuffnecKk features

### `plugin-items.dll`

#### 1. Gamble Screen Limit

Fills all 32 gambling slots instead of the vanilla 14.

- **JSON:** `items.gambleScreenLimit`
- **Shipped default:** disabled
- **Options:** `enabled`

#### 2. Ground Item Label Limit

Raises the number of ground-item labels that can be displayed at once from 32
to either 64 or 128.

- **JSON:** `items.groundItemLabels`
- **Shipped default:** disabled; preset limit `64`
- **Options:** `enabled`, `limit`

#### 3. Item Durability

Configures durability-loss resistance for normal and ethereal items, ethereal
maximum durability, and optional durability for bows and crossbows.

- **JSON:** `items.itemDurability`
- **Shipped default:** disabled, preserving vanilla durability behavior
- **Options:** `enabled`, `normalResistancePercent`, `etherealResistancePercent`,
  `etherealMaximumPercent`, `forceMaximumDurability`, and
  `bowsAndCrossbowsHaveDurability`
- **Preset values:** normal and ethereal resistance `0`, ethereal maximum `50`,
  and both boolean overrides disabled
- **Allowed ranges:** resistance percentages `0` to `100`; ethereal maximum
  percentage `1` to `200`
- **Useful note:** `forceMaximumDurability` gives affected ethereal items 255
  maximum durability instead of using `etherealMaximumPercent`.

#### 4. Charm Aura Trigger Fix

Restores inventory-charm auras after changing areas, recovering a corpse, or
respawning in town.

- **JSON:** `items.charmAuraTriggerFix`
- **Shipped default:** enabled
- **Options:** `enabled`

#### 5. Enhanced Damage Min/Max Fix

Corrects off-weapon Enhanced Damage when flat minimum or maximum damage is also
present.

- **JSON:** `items.enhancedDamageMinMaxFix`
- **Shipped default:** enabled
- **Options:** `enabled`

#### 6. Magic Find Formula

Offers exactly two case-sensitive calculation modes:

- `vanilla` uses regular D2R Magic Find behavior, including diminishing returns
  for Unique, Set, and Rare quality rolls;
- `linear` applies positive Magic Find directly to Unique, Set, and Rare rolls
  without those diminishing returns.

Magic-quality rolls and non-positive Magic Find keep their regular behavior in
both modes.

- **JSON:** `items.magicFindFormula.mode`
- **Shipped default:** `vanilla`
- **Allowed values:** `vanilla`, `linear`

#### 7. Ethereal Item Rules

Combines the former Ethereal Item Rules and NoEtherealItemTypes work into one
feature. It controls the ethereal generation chance, excluded item types, and
whether Set or indestructible items may become ethereal.

- **JSON:** `items.etherealItemRules`
- **Shipped default:** disabled, preserving vanilla behavior
- **Preset values:** `chancePercent: 5`, empty `excludedItemTypes`,
  `allowSetItems: false`, and `allowIndestructibleItems: false`
- **Options:** `enabled`, `excludedItemTypes`, `chancePercent`,
  `allowSetItems`, and `allowIndestructibleItems`
- **Allowed range:** `chancePercent` accepts `0` to `100`
- **Useful note:** excluded values use item-type codes; excluding a parent type
  also covers its descendants.

#### 8. Extended Item Stats

Shows complete item-stat information in bounded, scrollable tooltips with a
visible scrollbar.

- **JSON:** none
- **Shipped default:** always active (doesn't show up in the config file)
- **Useful note:** it coexists with AdvancedTooltips;
#### 9. Repair Costs Cap

Caps repair prices and can optionally make repaired items permanently lose
maximum durability.

- **JSON:** `items.repairCostsCap`
- **Shipped default:** disabled; preset cap `2147483647`
- **Options:** `enabled`, `maximumGold`, and the
  `durabilityWear.enabled`/`durabilityWear.chance` settings
- **Useful note:** durability wear also ships disabled with a `0.0` chance.

#### 10. Qty Display Fix

Shows quantity on socketed stackable items (throwables/quiver/bolts)

- **JSON:** `items.qtyDisplayIssue`
- **Shipped default:** enabled
- **Options:** `enabled`

#### 11. Vendor Stock Refresh

Adds a manual refresh button for normal vendor stock.

- **JSON:** `items.vendorStockRefresh`
- **Shipped default:** disabled
- **Options:** `enabled`

#### 12. AdvancedTooltips

Adds exact item information such as maximum sockets, base-defense ranges, and
intrinsic property-roll ranges.

- **JSON:** `items.advancedTooltips`
- **Shipped default:** disabled; display mode `HoldHotkey`; hotkey `Shift`
- **Options:** `enabled`, `showMaxSockets`, `showMaxSocketsOnSocketedItems`,
  `showBaseDefenseRange`, `showPropertyRanges`,
  `includeSocketedContributionsInRanges`, `propertyRangeColor`,
  `rangeDisplayMode`, and `holdToDisplayHotkey`
- **Preset details:** maximum sockets, base-defense ranges, and property ranges
  are selected; socketed-item socket counts and socketed contributions are not;
  the preset color is `ChronicleColor` (teal/light blue). The only other public
  color value is `BHDarkGreen`, the legacy BH dark green.
- **Useful note:** with `HoldHotkey`, the configured details appear only while
  the key is held. `Always` keeps them visible continuously. These mode and
  color names are case-sensitive.

#### 13. PotionAutoPickUp

Automatically routes explicitly selected healing, mana, and rejuvenation
potions from the ground into allowed belt columns or, when that potion tier is
listed in `overflowTiers`, into the character inventory.

- **JSON:** `items.potionAutoPickUp`
- **Shipped default:** completely inactive
- **Main options:** `enabled`, `pickupDistance`, `familyPriority`, and separate
  `healing`, `mana`, and `rejuvenation` rules
- **Per-family options:** `enabled`, `tiers`, `columns`,
  `overflowTiers`, and `tierPriority`
- **Useful note:** `pickupDistance` accepts `1` to `4` in D2R's native
  collision/unit grid; it is not a screen-pixel or large-map-tile count.
  `columns` means belt columns `1` (leftmost) through `4` (rightmost).
  `familyPriority` orders potion families, while `tierPriority` orders selected
  potion codes inside one family. The JSON contains valid examples.
- **Safe default:** all supplied family and tier lists are empty. Installing
  the pack cannot pick up a potion automatically until the player explicitly
  configures one. If no allowed belt or inventory destination has space, the
  potion remains on the ground.

#### 14. MassID

Identifies eligible items by holding Shift and right-clicking a Tome of
Identify. Already identified items are skipped.

- **JSON:** `items.massIdentify`
- **Shipped default:** enabled; normal Tome consumption; character inventory
  only
- **Options:** `enabled`, `freeIdentification`, `includeCube`,
  `includePersonalStash`, and `includeSharedStash`
- **Default values:** `freeIdentification: false` and all three `include...`
  options set to `false`; character inventory is always included
- **Useful note:** with `freeIdentification: false`, one Tome charge is consumed
  for every newly identified item and processing stops when no charge remains.
  With `freeIdentification: true`, eligible items are identified without
  consuming charges. Optional containers are processed after character
  inventory in this order: Cube, personal stash, then shared stash.

### `plugin-misc.dll`

#### 15. Cube Quick Move Bottom-Right

Places items quick-moved into the Horadric Cube into free slots beginning at
the bottom-right.

- **JSON:** `misc.cubeQuickMoveBottomRight`
- **Shipped default:** disabled
- **Options:** `enabled`

#### 16. Equipped Item to Cube

Moves equipped gear directly into an open Horadric Cube with Ctrl-left-click
when a valid Cube slot is available.

- **JSON:** `misc.equippedItemToCube`
- **Shipped default:** enabled
- **Options:** `enabled`

#### 17. Assign Transmute Hotkey

Activates the Horadric Cube's Transmute button from a configurable keyboard or
mouse shortcut.

- **JSON:** `misc.transmuteHotkey`
- **Shipped default:** disabled; preset shortcut `SHIFT+T`
- **Options:** `enabled`, `hotkey`
- **Useful note:** the shortcut accepts A-Z, 0-9, F1-F24, common navigation
  keys, MOUSE3-MOUSE5, and optional Ctrl, Shift, or Alt modifiers. Key names
  are case-insensitive, but the configured modifier combination must match
  exactly. Input refused by an active chat or modal is returned to the game.

#### 18. Prevent Merc Death in Town

Prevents ongoing damage from killing the player's mercenary while the mercenary
is in town.

- **JSON:** `misc.preventMercDeathInTown`
- **Shipped default:** disabled
- **Options:** `enabled`

#### 19. RemoteStash

Opens and closes the normal personal and shared tabs including outside town. The game remains responsible for the real stash tabs,
items, and persistence.

- **JSON:** `misc.remoteStash`
- **Shipped default:** optional hotkey disabled; hotkey `None`
- **Options:** `enabled`, `hotkey`
- **Useful note:** `None` leaves the optional keyboard shortcut unbound. A mod
  may still expose a compatible inventory-layout button because `enabled`
  controls only the optional hotkey input. The same configured shortcut toggles
  the stash; Escape and the normal close control still work. RemoteStash accepts
  the same keys and modifiers as the Transmute shortcut, plus `Semicolon`;
  names are case-insensitive and modifiers must match exactly.

#### 20. FloatingDamage

Displays visual damage numbers dealt above monsters
- **JSON:** `misc.floatingDamage`
- **Shipped default:** disabled; toggle shortcut `SHIFT+Z`
- **Main options:** visibility shortcut, number limits, font and sizing,
  damage-type colors, animation, hit combining, columns/stacks, DPS sampling,
  preview, and colors
- **Useful note:** the toggle changes overlay visibility for the current game
  session. Game-world positioning, text size, outline/shadow, and DPS placement
  scale automatically with the display. Text uses 2160p as a reference
  (`1080p = 0.5x`, `1440p = 0.667x`, `2160p = 1x`, with a 720p minimum), so a
  1080p player does not need to change the config. Drift, spread, popup travel,
  and stack spacing are raw screen-pixel preferences and may be tuned visually.

### `plugin-quests.dll`

#### 21. Force Larzuk Sockets

Configures how many sockets Larzuk adds for Magic, Rare, Set, Unique, and
Crafted items in Normal, Nightmare, and Hell.

- **JSON:** `quests.larzukSockets`
- **Shipped default:** disabled, so Larzuk uses vanilla behavior
- **Options:** `enabled` plus `minSockets`/`maxSockets` for each supported
  quality in each difficulty
- **Useful note:** equal minimum and maximum values force a fixed result; a
  range rolls between both values. Set one quality rule to `null` to use its
  vanilla result. The supplied table contains the visible vanilla socket rules
  but installs no change while disabled.

### `plugin-skills.dll`

#### 22. Bulk Skill Point Allocation

Spends several skill points with Ctrl-click or all currently usable points with
Shift-click.

- **JSON:** `skills.bulkSkillPointAllocation`
- **Shipped default:** disabled; Ctrl-click amount `5`; Shift confirmation off
- **Options:** `enabled`, `skillPointsPerCtrlClick`,
  `confirmShiftAllocation`, `shiftConfirmationKey`, and
  `shiftConfirmationFallback`
- **Useful note:** enabling confirmation asks before the Shift-click allocation;
  the fallback text is used when the configured localization key is unavailable.
## Credits and licenses

- **eezstreet** - creator and maintainer of the original PluginPack.
- **RuffnecKk** - Community Pack integration and all 22 contributed features
  listed above
- **D2RLAN/D2RHUD** - original FloatingDamage renderer and D2R 2.4 behavioral
  reference used by the port.
- **D2MOO project** - semantic Diablo II reference used by RemoteStash,
  PotionAutoPickUp, FloatingDamage, and MassID.

Community Pack 1.0.0 is published with eezstreet's express permission. The pack
is distributed under the included [MIT License](LICENSE). Dear ImGui, MinHook,
and the bundled OFL fonts retain their notices in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
