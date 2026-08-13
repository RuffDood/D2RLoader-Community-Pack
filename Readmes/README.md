# Community Pack 1.0.0

Community Pack 1.0.0 brings community additions into eezstreet's original
PluginPack while keeping its established five-DLL layout and one central
configuration file. It is intended for players and mod authors who want the
pack's fixes, quality-of-life options, and configurable gameplay extensions
without managing a collection of overlapping standalone plugins.

This release requires **D2RLoader 1.0.1 or later** and supports
**Diablo II: Resurrected 3.2.92777**. It refuses unsupported game builds rather
than installing unverified native hooks.

The package contains exactly these runtime plugins:

- `plugin-items.dll`
- `plugin-levels.dll`
- `plugin-misc.dll`
- `plugin-quests.dll`
- `plugin-skills.dll`

`plugin-shared` is linked into the five DLLs and is not a sixth runtime plugin.

## Installation

Choose one installation scope. The packaged `d2rloader` directory can be copied
directly into the corresponding destination.

### Global

Copy `d2rloader` into the Diablo II: Resurrected installation directory:

```text
<D2R>/d2rloader/plugins/plugin-items.dll
<D2R>/d2rloader/plugins/plugin-levels.dll
<D2R>/d2rloader/plugins/plugin-misc.dll
<D2R>/d2rloader/plugins/plugin-quests.dll
<D2R>/d2rloader/plugins/plugin-skills.dll
<D2R>/d2rloader/config/D2RPlugins.json
```

### Mod-local

Copy `d2rloader` into the selected mod directory:

```text
<D2R>/mods/<mod>/d2rloader/plugins/plugin-items.dll
<D2R>/mods/<mod>/d2rloader/plugins/plugin-levels.dll
<D2R>/mods/<mod>/d2rloader/plugins/plugin-misc.dll
<D2R>/mods/<mod>/d2rloader/plugins/plugin-quests.dll
<D2R>/mods/<mod>/d2rloader/plugins/plugin-skills.dll
<D2R>/mods/<mod>/d2rloader/config/D2RPlugins.json
```

When both configuration files exist, the mod-local file takes priority over
the global file. Restart D2RLoader after changing the configuration.

## Features by DLL

### `plugin-items.dll`

- Item identification, Magic Find formulas, durability, repair-cost and gold
  rules, expanded runeword qualities, gambling controls, vendor controls,
  resistance and absorb caps, ethereal-item rules, and related item fixes.
- Charm Aura Trigger Fix, Enhanced Damage Min/Max Fix, Qty Display Fix, Ground
  Item Label Limit, Gamble Screen Limit, Vendor Stock Refresh, and Extended
  Item Stats with complete scrollable stat lists.
- **AdvancedTooltips** under `items.advancedTooltips`.
- **PotionAutoPickUp** under `items.potionAutoPickUp`.

### `plugin-levels.dll`

- `levels.disableAct1Path` removes the dirt-path overlay covering the Blood
  Moor.

### `plugin-misc.dll`

- Configurable `/players` limit and independent monster HP/experience scaling
  caps.
- Cube Quick Move Bottom-Right, Equipped Item to Cube, Transmute Hotkey, and
  Prevent Merc Death in Town.
- **RemoteStash** under `misc.remoteStash`.
- **FloatingDamage** under `misc.floatingDamage`.

### `plugin-quests.dll`

- Configurable rewards for the Den of Evil, Izual, the Black Book, the Golden
  Bird, Radament's Skill Book, Akara/Cain, Ormus/Gidbinn, and Qual-Kehk.
- Configurable Larzuk socket rewards and an option to let Imbue accept socketed
  items.

### `plugin-skills.dll`

- Skills may spend life or stamina instead of mana through `skills.txt`, use
  classic Whirlwind timing, allow Whirlwind Chance to Cast, and broaden
  Telekinesis pickup.
- Bulk Skill Point Allocation, configurable charged-item drain avoidance, and
  extended self-heal parameters.

## New features

### RemoteStash

RemoteStash opens and closes the player's normal stash remotely, including
outside town, while the game remains responsible for the real stash tabs,
inventory operations, and item persistence. The same configured shortcut
toggles the stash; Escape and the normal close control still work.

- JSON key: `misc.remoteStash`
- Public default: `enabled: false` and `hotkey: "None"`
- Main options: `enabled` and `hotkey`

`None` does not capture a key, and `enabled` controls the optional hotkey input.
A mod may still provide a compatible inventory-layout button. RemoteStash is
independent of all other features in the pack.

### AdvancedTooltips

AdvancedTooltips adds exact item information such as maximum sockets, base
defense ranges, and intrinsic property roll ranges. It preserves the existing
coexistence design with Extended Item Stats, including its long-tooltip
scrolling path.

- JSON key: `items.advancedTooltips`
- Public default: `enabled: false` and `rangeDisplayMode: "HoldHotkey"`
- Hold-to-display default: `Shift`
- Main options: `showMaxSockets`, `showMaxSocketsOnSocketedItems`,
  `showBaseDefenseRange`, `showPropertyRanges`,
  `includeSocketedContributionsInRanges`, `rangeDisplayMode`,
  `holdToDisplayHotkey`, and `propertyRangeColor`

The pack ships disabled. With the default `HoldHotkey` mode, ranges appear only
while the configured hold-to-display key is pressed; `Always` remains available
for players who prefer persistent ranges.

### PotionAutoPickUp

PotionAutoPickUp routes explicitly selected healing, mana, and rejuvenation
potions from the ground into allowed belt columns or, when configured, into the
inventory. It uses the game's normal pickup path.

- JSON key: `items.potionAutoPickUp`
- Public default: `enabled: false`
- Main options: `pickupDistance`, `minimumIntervalActions`, `familyPriority`,
  and the `healing`, `mana`, and `rejuvenation` blocks. Each family selects
  `enabled`, `tiers`, `columns`, `overflowToInventory`, `overflowTiers`, and
  `tierPriority` independently.

The public JSON intentionally ships as an empty skeleton: no potion family,
potion code, belt column, overflow rule, or priority list is active. Examples
in the JSON comments are documentation only. If no configured destination has
space, the item remains on the ground; it is not deleted or duplicated.

### FloatingDamage

FloatingDamage shows visual damage numbers above monsters, can combine rapid
hits, animates and expires the text, and can display rolling DPS. It is a visual
overlay and does not change combat simulation.

- JSON key: `misc.floatingDamage`
- Public default: `enabled: false`
- Toggle hotkey default: `Shift+Z`
- Main options: general limits, toggle hotkey, appearance, animation, hit
  combining, layout, DPS, preview, and colors

The toggle changes overlay visibility for the current session.

## Configuration

`D2RPlugins.json` is the only public configuration file for the pack. The five
DLLs resolve it with the same priority:

1. `<D2R>/mods/<mod>/d2rloader/config/D2RPlugins.json`
2. `<D2R>/d2rloader/config/D2RPlugins.json`

Each DLL logs the file it actually loaded. A present but invalid file is
reported with an understandable error instead of silently selecting a
different configuration.

The shipped file contains comments explaining the available values. In
particular, the PotionAutoPickUp section contains an inactive example and empty
lists so installing the pack cannot pick up any item automatically by default.

The former public hotkey option `consume` has been removed. When a pack hotkey
actually performs its action, the triggering input is handled internally so it
does not also activate an unrelated game action. When the action is not
applicable, the key retains its normal game behavior. A leftover `consume`
field in an older JSON is ignored and does not replace this internal policy.

## Migration from standalone plugins

Before starting the game, remove the old standalone copies of the integrated
features, including `RemoteStash.dll`, `AdvancedItemTooltips.dll`,
`PotionAutoPickup.dll`, and `FloatingDamage.dll`. Also remove their separate
configuration files (`RemoteStash.json`, `AdvancedItemTooltips.json`,
`PotionAutoPickup.toml`, and `floating-damage.toml`) after transferring any
settings you still want into the matching blocks of `D2RPlugins.json`.

Move an existing central `D2RPlugins.json` from its old location to one of the
new `d2rloader/config` paths documented above. Do not keep global and mod-local
copies unless you intentionally want the mod-local configuration to override
the global one.

## Integration and compatibility notes

- All additions live inside the five existing PluginPack DLLs; no extra runtime
  DLL or manual load order is required.
- Each executable write has one manifest owner. Hooks use expected-byte guards,
  overlap validation, and fail-closed behavior.
- Shared runtime structures and shared call paths are centralized so features
  do not need another feature to be enabled.
- The four additions can be enabled together, but each also works
  independently.
- An external plugin that modifies exactly the same native hook or patch span
  may still conflict. Do not load an old standalone version alongside the
  integrated owner.

Community Pack 1.0.0 is published with eezstreet's express permission.

## Credits and licenses

- **eezstreet** — creator and maintainer of the original PluginPack.
- **RuffnecKk** — Community Pack integration and the contributed feature work,
  including RemoteStash, AdvancedTooltips, PotionAutoPickUp, and the D2RLoader
  3.2 FloatingDamage port.
- **D2RLAN/D2RHUD** — original FloatingDamage renderer and 2.4 behavioral
  reference used by the port.
- **D2MOO project** — semantic Diablo II reference used by RemoteStash,
  PotionAutoPickUp, and FloatingDamage; no D2R 3.2 address or ABI was copied
  from it.

The pack is distributed under the included [MIT License](../LICENSE). Dear
ImGui, MinHook, and the bundled OFL fonts retain the notices in
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).
