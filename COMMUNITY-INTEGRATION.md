# Community PluginPack integration notes

This contribution follows the community PluginPack direction: compatible
features are integrated into the existing D2RL-Plugins architecture instead of
being distributed as overlapping runtime DLLs that independently redefine
shared game structures or patch the same executable sites.

The contributed implementations were developed by RuffnecKk, but the resulting
runtime remains eezstreet's existing PluginPack. The five existing DLLs, plugin
IDs, names, author metadata, and single `D2RPlugins.json` are retained. RuffnecKk
attribution is limited to the contributed feature modules and their logs.

This branch integrates 17 RuffnecKk features into
the five existing eezstreet PluginPack DLLs. It does not add a sixth runtime
DLL, change the PluginPack installation layout, or replace the single
`D2RPlugins.json` configuration file.

## Inventory

| Owner | Feature | Configuration |
|---|---|---|
| `plugin-items.dll` | Gamble Screen Limit | `items.gambleScreenLimit` |
| `plugin-items.dll` | Ground Item Label Limit | `items.groundItemLabels` |
| `plugin-items.dll` | Item Durability | `items.itemDurability` |
| `plugin-items.dll` | Charm Aura Trigger Fix | `items.charmAuraTriggerFix` |
| `plugin-items.dll` | Enhanced Damage Min/Max Fix | `items.enhancedDamageMinMaxFix` |
| `plugin-items.dll` | Magic Find Formula | `items.magicFindFormula` |
| `plugin-items.dll` | unified EthItemRules | `items.etherealItemRules` |
| `plugin-items.dll` | Extended Item Stats | Always active; no JSON key |
| `plugin-items.dll` | Repair Costs Cap | `items.repairCostsCap` |
| `plugin-items.dll` | Qty Display Fix | `items.qtyDisplayIssue` |
| `plugin-misc.dll` | Cube Quick Move Bottom-Right | `misc.cubeQuickMoveBottomRight` |
| `plugin-misc.dll` | Equipped Item to Cube | `misc.equippedItemToCube` |
| `plugin-misc.dll` | Assign Transmute Hotkey | `misc.transmuteHotkey` |
| `plugin-items.dll` | Vendor Stock Refresh | `items.vendorStockRefresh` |
| `plugin-misc.dll` | Prevent Merc Death in Town | `misc.preventMercDeathInTown` |
| `plugin-quests.dll` | Force Larzuk Sockets | `quests.larzukSockets` |
| `plugin-skills.dll` | Bulk Skill Point Allocation | `skills.bulkSkillPointAllocation` |

`NoEtherealItemTypes` and the former Ethereal Item Rules implementation are one
feature and one JSON block here.

## Default behavior

The shipped `D2RPlugins.json` is the player-facing default. Charm Aura Trigger
Fix, Enhanced Damage Min/Max Fix, Qty Display Fix, and Equipped Item to Cube are
enabled by default. Every other newly added configurable effect remains disabled,
and its remaining values match vanilla where a vanilla value exists. Magic Find
Formula therefore ships in `vanilla` mode; `linear` removes only the positive
Unique, Set, and Rare diminishing-return branches while preserving native Magic
quality and non-positive MF behavior.
The Larzuk table contains the 15 visible vanilla socket rules but its independent
switch is disabled, so it installs no hook. Extended Item Stats is an always-active
patch with bounded scrollable full-stat tooltips, bounded 4096-byte item transport,
and a visible graphical scroll bar. It has no public configuration key.

## Migration from the RuffnecKk ethereal memory patch

Modders currently using RuffnecKk's `ethereal-item-rules.json` memory patch
should remove it when upgrading to this PluginPack. Its behavior is now built
directly into `plugin-items.dll` under `items.etherealItemRules`.

The integrated configuration replaces hexadecimal byte values with a decimal
`chancePercent` from 0 through 100. It also exposes `allowSetItems`,
`allowIndestructibleItems`, and `excludedItemTypes` in the same player-facing
block. The legacy memory patch and the integrated feature must not be enabled
together because they target the same executable write sites and would create
duplicate ownership with an ambiguous effective configuration.

## Corrections to existing PluginPack behavior

The integration audit also found and corrected two player-facing issues in
existing PluginPack options. These corrections are separate from the 16
contributed features.

The existing physical resistance, elemental resistance, and absorb cap settings
were parsed and stored, but their executable patches were not installed during
`plugin-items.dll` startup. They now install guarded, manifest-owned writes when
enabled. A live-process validation confirmed non-vanilla test values of 51, 96,
and 41 at the intended sites. The shipped configuration still leaves all three
options disabled with their original vanilla values of 50, 95, and 40.

`items.vendorOverhaul.rareItemChance` was documented as a 1-in-N denominator,
but the previous implementation compared a 0-through-1023 roll directly against
N. With `rareItemChance: 1024`, every eligible vendor slot therefore became Rare
when `randomRareItems` was enabled. The policy now implements the documented
denominator: 1024 means 1-in-1024 and 1 means every eligible item. Dedicated
tests cover the disabled state, zero protection, 1, 1024, and successive RNG
boundaries.

## Hardening of existing PluginPack paths

All five DLLs now reject unsupported D2R builds consistently. A malformed or
unreadable mod-local JSON is no longer treated as missing and silently replaced
by a global configuration. Invalid section types, modes, probabilities, caps,
player counts, quest rewards, and stat IDs are rejected with the exact path and
reason.

Existing hook and patch return values are checked instead of allowing a DLL to
report success after a refused write. Existing features participate in the same
signature preflight and transactional startup as the contributed features.
Seven duplicate quest-reward write aliases were consolidated so each executable
site is written once, and `D2UnitStrc+0x04` is correctly modeled as the unit
class/TXT record ID instead of being mislabeled as `unitFlags`.

The all-features validation also exposed an unsigned-distance error when a near
relay was allocated below `D2R.exe`. Relay calls now encode a range-checked
signed `E8 rel32` displacement and submit through the transactional patch path.
The corrected path completed the five-plugin cold-start matrix.

## Internal hook safety

`hook-manifest.json` contains 136 uniquely owned write sites. Configuration and
every build fail if two owners overlap. Shared call paths use one owner and
call-through consumers:

- `0x373890` belongs to EthItemRules; Enhanced Damage and Charm Aura call its
  live entry instead of installing another hook.
- `0x2F48C0` belongs to Item Durability when maximum-durability behavior needs
  it. Prevent Merc Death in Town validates the untouched body at `+5` and calls
  the live entry, so both features load together.
- `0x2A7810` belongs to Extended Item Stats; Equipped Item to Cube calls through
  that resolver.
- Item-record and tooltip consumers use shared owner-and-consumer pipelines.
  Optional features therefore compose without requiring a player-selected load
  order or compatibility setting.

## Configuration and startup safety

The loader accepts a missing mod-local JSON and then checks the global fallback.
Once a file exists, malformed JSON, an unreadable file, a non-object root, an
invalid named section, or an out-of-range high-risk value rejects that DLL with
the exact file and reason in its log. An invalid mod-local file never silently
falls back to a different global configuration.

D2RLoader does not undo a successful executable-memory write when a plugin later
returns a load failure. The five DLLs therefore preflight all required signatures
first and defer their hook, patch, and console-command requests into one startup
transaction. A deterministic preflight failure applies nothing. Required
operations run before optional commands. A commit refusal deactivates guarded
detours and restores direct writes in reverse order; the DLL is rejected after a
complete rollback and remains loaded but inactive only if a restoration fails.

The current D2R `3.2.92777` validation builds all five DLLs in Debug and Release
and passes 25/25
CTest tests. Isolated cold starts were completed from both supported locations:
all five DLLs mod-local with a mod-local JSON, and all five DLLs global with the
global fallback JSON. Each run reported
`scanned=5 active=5 disabled=0 rejected=0 failed=0`. A deliberately malformed mod-local JSON
made all five DLLs fail closed while the sampled vanilla resistance-cap bytes
remained unchanged. Every runtime test restored the 36 temporarily neutralized
files byte-for-byte and left no game process running.

## Final checkpoint - July 30, 2026

The final player-default build validates `136/136` executable writes with a
unique manifest owner and no overlapping spans. Debug and Release each build
all five DLLs and pass `25/25` CTest tests. An automated configuration audit
also confirms that every original eezstreet JSON value is preserved,
`skills.selfHealParams` remains `true`, all 15 contributed defaults match the
documented policy, and Extended Item Stats exposes no public configuration key.

The exact Release DLLs and player-default JSON were hash-matched to two runtime
deployments. The isolated global vanilla cold start reported
`scanned=5 active=5 disabled=0 rejected=0 failed=0`, reached startup `24/24`,
and emitted no error or assertion. The mod-local cold start reported the same
five-plugin result and startup completion. SHA-256 values for the reviewed
artifacts are:

| Artifact | SHA-256 |
|---|---|
| `D2RPlugins.json` | `2B55B3661E654B28C4A98A732325EF54E8BAF2A3635FD1B13D541CE914C296B5` |
| `plugin-items.dll` | `C4FAC30552E7E26788A374FF2FABB3F0A6D0AA847EAD286D0B2DD13AC99DA462` |
| `plugin-levels.dll` | `2E9146802ECF8F3967A30EBB4A4A4B530AF1383CFA5556DBB161B3EC6ECB5000` |
| `plugin-misc.dll` | `FFA7338126F27FFE752CB6223828995A3C44249123C678CF1DFADA5655E3E58D` |
| `plugin-quests.dll` | `FB134ECAB64EC952BC2B9CE11CFB2654A9C29E9B34867350A3529712E544B721` |
| `plugin-skills.dll` | `23548D8CD916BF83E9CCB74E44A6C651FBA2EC2FD05E58F3572178A7263B57FF` |

Integrated gameplay passed the nominal paths for Extended Item Stats, Gamble
Screen Limit, Ground Item Labels at 64 and 128, Qty Display Fix, 100% ethereal
generation with ethereal set items, Enhanced Damage Min/Max Fix, Charm Aura
Trigger Fix, Cube Quick Move, Equipped Item to Cube, Transmute Hotkey, Vendor
Stock Refresh, Repair Costs Cap with wear, Prevent Merc Death in Town, and Bulk
Skill Point Allocation. Extended Item Stats was the only feature to expose a
gameplay integration regression; its duplicate-tooltip, stable-width,
scrollbar, wheel, drag-lifetime, and crash defects were corrected and retested.
Force Larzuk Sockets and Item Durability retain standalone and technical proof
but were not rerun as exact integrated gameplay witnesses in this final batch.

## Player test plan

The automated gates prove compilation, signatures, hook ownership, load order,
configuration parsing, cold start, and the absence of loader rejection. They do
not replace visible gameplay tests. A player does not need to test every feature
in a separate game launch; use these focused batches:

1. **Item UI and limits:** Gamble Screen Limit, Ground Item Label Limit, Qty
   Display Fix, and Extended Item Stats tooltips.
2. **Item rules and durability:** EthItemRules, Item Durability, Repair Costs
   Cap, Enhanced Damage Min/Max Fix, and Charm Aura Trigger Fix. Include repair
   individual/Repair All, transition/corpse recovery, save/reload, and one
   host/joiner session.
3. **Cube workflow:** Cube Quick Move, Equipped Item to Cube, and Transmute
   Hotkey with open, absent, and full Cube cases plus keyboard and mouse input.
4. **Town services:** Vendor Stock Refresh and Prevent Merc Death in Town,
   including vendor reopen, poison/Open Wounds, town exit, portal, and waypoint.
5. **Quest and skills:** Larzuk across quality/difficulty boundaries and Bulk
   Skill Point Allocation with Ctrl, Shift, insufficient points, and optional
   confirmation.

Keep a vanilla-default launch as the control, then enable only the batch being
tested. Reuse the already validated standalone behavior as the comparison
oracle when an integrated result is uncertain.

## Upstream review

This branch deliberately follows eezstreet's existing structure: five DLLs,
the original plugin IDs and metadata, one JSON file, internal feature modules,
canonical shared ABI types, and a build-time ownership manifest. RuffnecKk
credit is attached to the contributed feature sources and logs without
rebranding the eezstreet DLLs. Upstream can review or merge the work feature by
feature even though the branch also supports one complete build.
