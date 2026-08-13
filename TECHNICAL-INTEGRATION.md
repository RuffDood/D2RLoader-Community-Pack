# Community Pack 1.0.0 — Technical Integration and Validation

## Purpose and public release model

Community Pack 1.0.0 is one integrated source release built on eezstreet's
original D2RLoader PluginPack. It retains the established five-DLL architecture
and combines 22 RuffnecKk integrations into that architecture as one public
product.

The original PluginPack design, plugin IDs, DLL names, metadata, and MIT-licensed
base remain credited to eezstreet. The integrated feature implementations and
their guarded D2R 3.2 work are credited to RuffnecKk. The integration does not
rename or replace eezstreet's five modules:

- `plugin-items.dll`
- `plugin-levels.dll`
- `plugin-misc.dll`
- `plugin-quests.dll`
- `plugin-skills.dll`

All five modules read the same `D2RPlugins.json`. Features are implemented as
internal modules of their owning DLL rather than redistributed as additional
standalone DLLs.

## Integrated feature inventory

| Owner | Feature | Configuration |
|---|---|---|
| `plugin-items.dll` | Gamble Screen Limit | `items.gambleScreenLimit` |
| `plugin-items.dll` | Ground Item Label Limit | `items.groundItemLabels` |
| `plugin-items.dll` | Item Durability | `items.itemDurability` |
| `plugin-items.dll` | Charm Aura Trigger Fix | `items.charmAuraTriggerFix` |
| `plugin-items.dll` | Enhanced Damage Min/Max Fix | `items.enhancedDamageMinMaxFix` |
| `plugin-items.dll` | Magic Find Formula | `items.magicFindFormula` |
| `plugin-items.dll` | Unified Ethereal Item Rules | `items.etherealItemRules` |
| `plugin-items.dll` | Extended Item Stats | Always active; no JSON key |
| `plugin-items.dll` | Repair Costs Cap | `items.repairCostsCap` |
| `plugin-items.dll` | Qty Display Fix | `items.qtyDisplayIssue` |
| `plugin-items.dll` | Vendor Stock Refresh | `items.vendorStockRefresh` |
| `plugin-items.dll` | Advanced Tooltips | `items.advancedTooltips` |
| `plugin-items.dll` | Potion Auto Pick Up | `items.potionAutoPickUp` |
| `plugin-items.dll` | MassID | `items.massIdentify` |
| `plugin-misc.dll` | Cube Quick Move Bottom-Right | `misc.cubeQuickMoveBottomRight` |
| `plugin-misc.dll` | Equipped Item to Cube | `misc.equippedItemToCube` |
| `plugin-misc.dll` | Assign Transmute Hotkey | `misc.transmuteHotkey` |
| `plugin-misc.dll` | Prevent Merc Death in Town | `misc.preventMercDeathInTown` |
| `plugin-misc.dll` | Remote Stash | `misc.remoteStash` |
| `plugin-misc.dll` | Floating Damage | `misc.floatingDamage` |
| `plugin-quests.dll` | Force Larzuk Sockets | `quests.larzukSockets` |
| `plugin-skills.dll` | Bulk Skill Point Allocation | `skills.bulkSkillPointAllocation` |

`plugin-levels.dll` remains part of the original five-DLL product even though
none of the 22 RuffnecKk integrations is assigned to it.


## Executable-write ownership

`hook-manifest.json` governs **210 uniquely owned write sites in `D2R.exe`**.
The manifest validator passes with all 210 sites represented and no overlapping
spans. Build-time source coverage requires each manifest identifier to be used
by its declared feature, while runtime expected-byte checks reject an
incompatible executable before a write is committed.

MassID contributes 13 of those 210 sites:

- two guarded inline hooks:
  - `0x1C7A30`, the targeting-packet worker fallback;
  - `0x4C6C90`, the authoritative opcode-`0x34` Cain callback;
- eleven guarded five-byte callsite redirections:
  - Drop: `0x2279BD`, `0x2C552D`;
  - Move: `0x2278DC`, `0x227936`, `0x2C5241`, `0x2C528D`, `0x2C53AB`,
    `0x2CA2E0`;
  - Sell: `0x2C51A9`;
  - Give: `0x2C5455`;
  - UI-state probe: `0x2C55F2`.

MassID also installs a thread-specific Windows message hook to capture and
suppress the recognized right-button down/up pair without replacing another
module's window procedure. That Windows callback is not an executable write
inside `D2R.exe`, so it is intentionally outside `hook-manifest.json` and
outside the count of 210.

Floating Damage and Extended Item Stats also coordinate renderer-level DirectX
12 hooks outside the D2R executable-write manifest. Extended Item Stats owns its
tooltip/rendering pipeline; Floating Damage detects that owner, composes through
the external overlay contract, and installs in deterministic order. The manifest
count must therefore be described as D2R executable writes, not as every callback
or graphics detour used by the product.

## Shared owner-and-consumer contracts

Shared native paths use one writer and explicit call-through consumers:

- `0x373890` belongs to Ethereal Item Rules. Enhanced Damage Min/Max Fix and
  Charm Aura Trigger Fix call the live entry instead of installing another hook.
- `0x2F48C0` belongs to Item Durability when maximum-durability behavior is
  required. Prevent Merc Death in Town validates the untouched body at `+5` and
  calls the live entry.
- `0x2A7810` belongs to Extended Item Stats. Equipped Item to Cube calls through
  that resolver.
- Extended Item Stats owns `ITEMS_BuildItemTooltip` at `0x2BD480`. Advanced
  Tooltips redirects its seven callers, then calls the owner pipeline before
  applying its range transformation.
- Bulk Skill Point Allocation owns the localization entry at `0x5F4B90` and the
  UI dispatcher at `0x843D90`. Remote Stash participates through the explicit
  UI-message interceptor chain instead of installing a competing dispatcher.
- Equipped Item to Cube owns the outgoing-packet entry at `0x0EE2A0`. MassID
  submits through the live entry without requiring its vanilla prologue.
- Remote Stash owns `UI_IsStateOpen` at `0x0CE500`. MassID redirects only its
  independent caller at `0x2C55F2` and calls the current live entry.
- MassID calls the live localization entry at `0x5F4B90`; it does not duplicate
  Bulk Skill Point Allocation's hook.

These contracts make composition internal to the five DLLs. A player does not
select a load order or compatibility mode for the integrated features.

## Runtime migration and duplicate ownership

The integrated five-DLL runtime replaces standalone copies of the same
features. Loading a standalone `MassID.dll` beside this `plugin-items.dll` would
give two modules the same two inline hooks, eleven callsites, and Windows input
gesture. It must therefore be removed when using Community Pack 1.0.0. The same
principle applies to standalone Remote Stash, Advanced Tooltips, Potion Auto
Pick Up, and Floating Damage binaries represented by the integrated modules.



## Corrections to original PluginPack behavior

The same one-release integration includes corrections found while auditing the
original PluginPack paths. They are corrections to retained options, not extra
entries in the 22-feature inventory.

The physical-resistance, elemental-resistance, and absorb-cap settings were
parsed and stored but their executable patches were not installed during
`plugin-items.dll` startup. They now request guarded, manifest-owned writes when
enabled. Their shipped switches remain disabled and retain vanilla values of 50,
95, and 40.

`items.vendorOverhaul.rareItemChance` is documented as a 1-in-N denominator.
The retained implementation previously compared a 0-through-1023 roll directly
against N, which made `1024` succeed for every eligible slot. The corrected
policy treats `1024` as 1-in-1024 and `1` as every eligible item, with zero and
the disabled state handled explicitly.

Remote Stash's layout-owned button now dispatches through the PluginPack UI
path. The corresponding message validation uses the actual PluginPack message
contract, while Bulk Skill Point Allocation remains the unique dispatcher owner
described above.

## Configuration and startup hardening

All five DLLs reject unsupported D2R builds consistently. A missing mod-local
JSON may use the global fallback, but an existing unreadable or malformed file
is never treated as missing. Invalid roots, sections, modes, probabilities,
caps, player counts, quest rewards, stat IDs, unknown MassID keys, and incorrect
value types fail closed with the configuration path and reason.

Existing hook and patch return values are checked. Seven duplicate quest-reward
write aliases were consolidated so each executable site has one owner, and
`D2UnitStrc+0x04` is modeled as the unit class/TXT record ID rather than as unit
flags.

Near relays use a range-checked signed `E8 rel32` displacement. This avoids the
unsigned-distance failure that occurs when a valid relay allocation is below
`D2R.exe` in memory.

D2RLoader does not automatically undo an executable-memory write when a plugin
later returns a load failure. Each DLL therefore preflights its required
signatures and queues writes and console commands into a startup transaction.
A preflight refusal applies nothing. A commit refusal deactivates guarded
detours and restores direct writes in reverse order; a DLL remains loaded as an
inactive safety barrier only if restoration itself fails.

## Validation status for the exact 22-feature tree

The final ownership validator passes for `210/210` D2R executable write sites
with no overlapping spans. This is a source and ownership result, not a claim
that every gameplay path in the complete 22-feature tree has been rerun.

The exact tree was rebuilt in both Debug and Release configurations. All five
DLLs compiled in each configuration, and the complete CTest suite passed
`30/30` in Debug and `30/30` in Release. The manifest and source-coverage gates
both passed at `210/210` sites.

The Release DLL hashes are:

| File | SHA-256 |
|---|---|
| `plugin-items.dll` | `C7D265C955F23C72478BFC5CC3DE3DE7F89B67756288AE06B8C53EA93D09CD49` |
| `plugin-levels.dll` | `91E96375F72C870202F1B23AE0BA30DE15F3331BCEFD6C5B0C42032861E251DE` |
| `plugin-misc.dll` | `B7B789BA7C4D61682DEB4E95C6287C9C931B2F1EA7F70D86F77BC5E12322C9D0` |
| `plugin-quests.dll` | `04AC1F7F20D91E2A8BE66EAFB423ED40EEC5F9300E7D1E86BE31F10B564FF2D8` |
| `plugin-skills.dll` | `ABA225AC6B02C9FC4D34CFD43C6601F6E1C4763FED37B43189495C9E82090BD0` |

A full-stack cold start on D2R `3.2.92777` used a qualification config with all
22 integrations enabled and every optional MassID container selected. After
removing the superseded standalone `MassID.dll`, D2RLoader reported
`scanned=14 active=14 disabled=0 rejected=0 failed=0`; all 17 memory patches
applied, and startup reached frontend stage `24/24` in 4,297 ms. The five pack
transactions committed items `83/83`, levels `0/0`, misc `70/70`, quests `1/1`,
and skills `3/3`. MassID activated inside `plugin-items.dll`, both hotkey
handoffs became ready, and FloatingDamage submitted its first overlay frame.
No fresh error, fatal, rejection, failure, or crash-report marker was produced.

An additional shipped-default gameplay witness activated MassID twice. Each
activation identified one inventory item, identified zero Cube, personal-stash,
or shared-stash items, and consumed exactly one Tome charge. This proves the
inventory-only paid path. Normal right-click, vendor/trade, empty and partial
Tome, save/reload, and host/joiner checks remain open for broader gameplay
qualification.

The packaged `Community-Pack-1.0.0-Windows-x64.zip` contains exactly nine
files: the five DLLs, central JSON, player README, MIT license, and third-party
notices. Every file extracted from the archive matches its staged/source
SHA-256. The archive SHA-256 is
`F0FD13A9148A2FB65730ED4DC690124814641F7A26F74C0E3145413B6C1D978E`.

## Review and attribution

The public product preserves eezstreet's five DLLs, original plugin identities,
single JSON configuration, shared ABI types, and MIT license. RuffnecKk credit is
attached to the 22 integrated feature implementations without rebranding the
base modules. The release is presented as one complete Community Pack 1.0.0
integration.
