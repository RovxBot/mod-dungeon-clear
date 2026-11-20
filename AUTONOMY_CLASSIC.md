# Trusted RFC pilot candidate

This changes only the default-off trusted integration. It is not a completed
dungeon run. Ordinary player-led commands, GM tests, RDF and BG queue-fill
implementations are unchanged. No playerbots or core patch is required.

The v2 API intentionally rejects v1 providers/consumers. Profile v1/id 1 allows
only Ragefire Chasm (389), normal difficulty, Classic content. Characters need
level >=15 and currently equipped average >=13, an unbroken main hand and no
broken equipped items. The conservative realm ceiling is Classic (60) or TBC
(70); Wrath ceilings and other maps/difficulties/profiles are refused. Floors
are admission policy, not evidence the party can clear. Both owner and provider
must select the same cap; `DungeonClear.Autonomy.MaxExpansion=0` is the default.
`DungeonClear.Autonomy.Enable=0` remains the default.

## Source audit

Audited owner base `565086c` (same tree as the prior provider
`f9201249e287027f0d393da17eb85ef588c49847`), with read-only core validation
snapshot `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6` and owner playerbots
`696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`. These are source validation pins,
not proof of the realm's current binary revisions or runtime data.

Core `instance_encounters.sql` defines four kill-credit encounters:

| Definition | Creature | Encounter | Normal spawn |
| --- | --- | --- | --- |
| 430 | 11517 | Oggleflint | 48644 |
| 431 | 11520 | Taragaman the Hungerer | 48706 |
| 432 | 11518 | Jergosh the Invoker | 48740 |
| 433 | 11519 | Bazzalan | 48750 |

The core RFC instance script is an empty `InstanceScript`, with no internal
boss-state slots to inspect. Normal access template 23 requires level 8, has no
item-level floor and has no extra requirement rows in this snapshot. The pilot
adds stricter eligibility and still calls `PlayerCannotEnter`, which invokes
`OnPlayerCanEnterMap`, instance-count and `Satisfy` progression checks. No access
grants or data changes are made. Existing group/instance identity validation is
retained; ordinary portal admission must still occur normally.

`BossSpawnIndex.cpp` discovers bosses from the loaded DBC/SQL encounter join and
normal spawn masks, sorted by loaded encounter index. `NextDungeonBossValue.cpp`
uses that order and `DcBossOrdering::PickTarget` (commit/forward/wrap), including
not-yet-streamed bosses. Normal advance/navigation, trash and engage actions
execute the clear. RFC has no event, wing or boss-roster override; no additional
encounter-specific action is justified by this source. Its three-leg funnel-wall
correction in `DcNavPenaltyRegistry.cpp` already has `TestNavPenalty.cpp`
coverage. No new navigation implementation is included. The existing `rfc`
GM test registration is discovery evidence only; the trusted bridge never calls
that provisioning/teleport harness.

The loaded DBC is required: SQL encounter IDs do not identify completion bits.
`RequiredMask` validates exactly these four kill definitions, unique indices
below 32 and corresponding normal DC anchors, then derives the required mask.
Missing/incompatible data refuse launch. `KillRewarder.cpp` calls
`Map::UpdateEncounterState`, which records those bits in the instance script.
Taragaman's `lastEncounterDungeon=4` is an RDF reward marker, not proof that the
other three encounters died. A fresh pilot refuses any already-set required bit.
Completion needs all four bits AND normal `all_cleared`; skipping, arriving,
generic disabled state or the old UK mask 7 cannot succeed. Stockade is not
selected because no source blocker was found for RFC; neither instance has
script boss-state slots, so changing maps would not remove the data-validation
requirement.

## Verification and remaining gates

`python3 t/check_autonomy.py` runs the production controller, unchanged chat
authorization body and actual `RequiredMask` body against engine data fakes in
C++17/C++20. Cases include unknown profiles, heroic/UK/Stockade maps, Wrath cap,
underlevel/over-cap/low equipment, forged/replayed/expired permits, missing or
wrong-version providers, missing/duplicate/mismatched encounter/spawn evidence,
non-contiguous indices, partial completion, observation identity and stop/disable.
Config-read, portability and determinism checks pass. The consumer's production
store/controller MySQL tests and exact final pin are recorded with its PR.

GNU C++20 syntax checks against those actual dependency headers pass for the
provider and its unchanged shared initialization/recovery boundaries. This is
not compilation/linking/startup. No combined image build, deployment, live SQL
or game execution is part of this work. Actual extracted DBC/mmap validity,
masterless reactions, ordinary portal entry/exit, real rewards, cleanup and
manual player/GM/RDF/BG regression remain acceptance gates. Travel/exit recovery
is separate M3B work; uncertain restart groups are never adopted.
