# atlas-endgame: analysis of mod-dungeon-master

Static review of `modules/mod-dungeon-master/` (2026-11-22): every source file read, its core calls and SQL
checked against this tree. Nothing was built, run or installed; no database was touched.

**What it is.** Your fork (`proxeeus/mod-dungeon-master`) of a third-party module (upstream by trauntrow /
InstanceForge, with community fixes; last merge 2026-09-06). AGPL-3.0, ~8,800 lines. Players talk to a
"Dungeon Master" NPC in a capital, pick difficulty / theme / dungeon, and are teleported into the **real
stock dungeon**, which the module empties and refills with creatures of one creature type, scaled to the
party, plus one boss. A roguelike mode chains dungeons with rising difficulty, 5 stat affixes and a stacking
stat buff. Leaderboards in the characters DB.

---

## 1. Compared to this AC tree

| Point | Result |
|---|---|
| Core API calls (`SetAI`, `SetCorpseDelay`, `SetWanderDistance`, `GetCreatureBySpawnIdStore`, `LootStoreItem`, `Group::GroupLoot`, `SetLootRecipient`, …) | All exist with matching signatures. |
| Hooks (`OnBeforeCreatureSelectLevel`, `OnCreatureAddWorld`, `OnAllCreatureUpdate`, `OnCreatureRemoveWorld`, `ModifySpellDamageTaken`, `ModifyMeleeDamage`, `ModifyPeriodicDamageAurasTick`, `OnUnitDeath`, `OnPlayerKilledByCreature`, `OnPlayerEnterAll`) | All exist (`AllCreatureScript.h:30-50`, `UnitScript.h:68-111`, `PlayerScript.h:279`, `AllMapScript.h:49`). |
| SQL `creature.id` | Matches this tree: `id1` was renamed to `id` by `data/sql/updates/db_world/2026_06_16_00.sql` (the `base/` dump still shows `id1`). |
| Other columns queried (`creature_classlevelstats.basehp0/damage_base/basearmor/attackpower`, `creature_template_movement.Ground`, `creature_immunities.MechanicsMask`, `item_template` fields) | Present. |
| Core edits needed | None. |
| Likely to compile | Yes, as far as static checks go (not built). |
| README accuracy | Partly stale: it says bosses keep their scripted AI; the code replaces every creature's AI, bosses included, with a melee-only AI (`DungeonMasterMgr.cpp:1252-1256`). |

## 2. Risks

Ranked by what they could do to the play server.

### R1. It installs itself into the play DB on the next image rebuild (critical)
- The play server's images are built from this checkout (`docker-compose.yml`: `build: context: .`), so
  anything in `modules/` is compiled in on the next `docker compose build`.
- For compiled-in modules, AC's DB updater applies everything under `modules/<mod>/data/sql/db-world` and
  `db-characters` at startup (`UpdateFetcher.cpp:157-187`, list = `AC_MODULES_LIST`, `worldserver/Main.cpp:448`).
- That would run, on the **play** databases:
  - world: `DELETE` + `INSERT` of `creature_template` 500000/500001, `creature_template_model`, 11 city spawns
    in `creature` (guids 8000001–8000011), 80 `npc_vendor` rows;
  - characters: 4 new `dm_*` tables plus a stored procedure.
  Entries 500000/500001 and those guids are free in the repo's SQL, but the live DB was not checked: if a
  custom row already uses them, the `DELETE` removes it.
- `DungeonMaster.Enable` defaults to **on** in code (`DMConfig.cpp:71`), so no config file is needed for it
  to run. It was cloned today (17:03); the running image is older, so it is most likely not active yet.

### R2. Server-wide Greater Blessing of Kings stacking (critical, gameplay)
- At startup it changes spell 25898 (Greater Blessing of Kings) to stack 255 times, for the whole server
  (`dm_world_script.cpp:40-46`), to show a stack counter on its roguelike buff.
- In core, recasting an aura by the same caster adds a stack (`Unit.cpp:4468`), and an aura's amount is
  multiplied by its stack count (`SpellAuraEffects.cpp:580`). So every paladin's GBoK refresh adds +10% to all
  stats, up to 255 stacks.
- Bot paladins cast GBoK (`mod-playerbots/src/Ai/Class/Paladin/PaladinBlessingTypes.h`), and they rebuff
  often. Expect players and bots to snowball stats within minutes of the module going live.
- It also removes 25898 from roguelike players at run end, which strips a real paladin's blessing too.

### R3. Permanent buff saved to characters (high)
The roguelike buff is GBoK applied with infinite duration (`RoguelikeMgr.cpp:832-845`). There is no logout
hook; buffs are only removed from players who are online when the run ends. A player who logs out mid-run
very likely keeps a permanent, stacked GBoK in `character_aura` (core saves self-cast, non-passive auras,
`SpellAuras.cpp:1163`). To confirm on a test stack.

### R4. It operates on the real stock instances (high)
- Runs happen in the stock instance the player is bound to. On populate it:
  - deletes every door and button gameobject in the instance (`go->Delete()`, `:1040-1059`);
  - despawns every stock creature, and every second despawns any creature it doesn't track (`:3527-3556`).
    In this fork's dynamic spawn mode a despawn writes the respawn time to `creature_respawn` in the
    characters DB (`Creature.cpp:466-473`), so each run leaves rows for that instance;
  - sets every encounter to DONE, then back to NOT_STARTED (`:1077-1098`, `:1458-1479`). Each change saves
    instance data to the characters DB (`InstanceScript.cpp:430`), runs each dungeon script's own
    `SetBossState` side effects, and fires `OnBeforeSetBossState` for other modules.
- After a session the instance stays bound and gutted (no doors, stock creatures on respawn timers) until it
  resets or unloads: entering that dungeon normally afterwards can give an empty or broken run.
- The player's dungeon difficulty is not forced to normal. In a heroic instance, killing a summoned stock boss
  that carries `CREATURE_FLAG_EXTRA_INSTANCE_BIND` perm-binds the group (`Unit.cpp:13922-13926`), i.e. a
  heroic lockout.
- `mod-dungeon-clear` and `mod-playerbot-reset-instances` also act on stock instances; interactions were not
  examined one by one.

### R5. Gameplay correctness (medium)
- Every creature, bosses included, gets the module's melee-only AI: no spells, no boss mechanics.
- Damage to a session player from any creature the module doesn't track (boss adds before promotion, script
  summons) is capped at 3% of max HP per hit (`dm_unit_script.cpp:74-120`).
- `Unit::SetAI` only swaps the pointer (`Unit.h:700`), so the original AI object leaks for every spawn.
- Oculus and Violet Hold are in the dungeon list although they need vehicles/events.

### R6. Performance with playerbots (medium)
- Every damage event to any player on the server (bots included) takes the module's global session mutex
  (`ScaleDamage` → `GetSessionByPlayer`). With many bots fighting on parallel map threads, that's lock
  contention on a hot path, even with no session running.
- Per session, once per second, a 5,000-yard creature grid search (`:3422`) and a full sweep of the instance.
- Every spawned creature is `setActive(true)`.
- Synchronous world-DB queries during populate (spawn points, entrances) on the world update tick, and
  synchronous characters-DB queries from the NPC's leaderboard menus.
- State is in memory only: a restart forgets every session (players log back into a gutted stock instance).

### R7. Licence
AGPL-3.0. Fine for a private server. Copying its code into mod-atlas would make mod-atlas AGPL too; reusing
ideas does not. The repo's credit rule applies to any mechanism mirrored from it.

**Status 2026-11-22 (later):** the module is now **mod-atlas V0** (repo `proxeeus/mod-atlas`, folder
`modules/mod-atlas`; identifiers, config keys `Atlas.*`, tables `atlas_*`, command `.atlas`), re-themed as
the Keepers of Time. It is still in `modules/`, so R1 applies to it: keep it out of the play image build
until it has compiled and played on the test stack.

**Status 2026-11-22:** fixes for R2–R4, R6 (lock gate), the AI leak, the off-by-default part of R1 and
upstream issues #4, #17, #22, #23, #24 are committed on the fork's local `main` (`a0270ce`), not compiled,
not run, not pushed. The roguelike buff is now the module's own passive spell 91001. Still open: R1's
auto-applied SQL (inherent to being in `modules/`), melee-only bosses and the 3% damage cap (R5),
in-memory sessions.

**Recommendation:** keep it out of the play server's build until R1–R4 are fixed. The simplest safe step is
to move the folder out of `modules/` (or list it in `DISABLED_AC_MODULES`) before any image rebuild. Fixing
it is possible (drop the GBoK patch and use a module spell; force normal difficulty; reset the instance on
exit; strip the buff on logout; move the damage check behind a cheap map-ID gate), but it is a separate
project.

---

## 3. What it means for the Atlas design

### Same problem, opposite map strategy

| | mod-dungeon-master | Atlas |
|---|---|---|
| Where runs happen | The real stock instance, emptied in place | Clone map IDs with no stock spawns |
| Client patch | None | Required (Map.dbc etc.) |
| Side effects on stock content | Doors deleted, encounters toggled, respawn rows, binds | None by construction |
| Creature AI | Replaced by melee-only AI | Stock SmartAI kept; shadow entries for the rest |
| Theme | One creature type, any entry in the DB | Curated pools with pack roles |
| Bosses | Random stock boss, melee only | Curated stock bosses (SmartAI) or library bosses |
| Scaling | `creature_classlevelstats` at the target level | Same idea, plus role multipliers |
| Affixes | 5 stat multipliers | 91 affixes, mechanisms and bot safety |
| Persistence | In memory | Run record in DB, rebuild from seed |
| Bots | Group members come along as players | Same, plus bot-safe rules |

Its problems in R4 are exactly what the clone approach avoids. That confirms the Atlas choice; nothing here
argues for switching to stock instances on the play server.

### Lessons worth taking (ideas, not code)

From its fix history (`296f579`, `5337c77`) and code:

1. **Model sanity filter.** Exclude entries with any `creature_template_model` whose `creature_model_info`
   has `BoundingRadius <= 0` or `CombatReach <= 0`: they spawn invisible but fight (~2,100 entries in stock
   data). Add to Atlas curation as an automatic `blocked` rule.
2. **Level ownership.** Other code (respawn, `UpdateEntry`, other modules) can re-run `SelectLevel` and
   revert a scaled creature. Atlas already scales in `OnBeforeCreatureSelectLevel` / `OnCreatureSelectLevel`;
   keep it the single source of truth and test respawn and entry changes.
3. **Adds.** Scale creatures created on the run map in `OnCreatureAddWorld` (already in THEMES §1 as role
   `add`); skip player pets, totems and guardians as they do.
4. **Evade at boundaries.** Creatures spawned away from their home position evade with
   `EVADE_REASON_BOUNDARY` / `NO_PATH` mid-fight and come back at full HP. Atlas packs spawn at harvested
   positions (on the navmesh), but the module's generic AI and shadow entries should refuse those evades
   while in combat, as theirs does.
5. **Loot timing (Q7).** Loot filled in `JustDied` is wiped by the core afterwards; filling it from
   `OnUnitDeath` works. That's an answer to Q7 from someone who hit it in practice.
6. **Group loot.** Their `FillCreatureLoot` shows how to hand custom loot to Need/Greed rolls
   (`SetLootRecipient` + `Group::GroupLoot`).
7. **Auto-revive at the entrance out of combat.** A simple bot-friendly death rule (covers Q12) that Atlas
   can combine with the portal budget.
8. **Multi-phase bosses.** After a boss dies, wait a few seconds and adopt any new elite near the corpse as
   the next phase. Useful if curated stock bosses transform (Dathrohan → Balnazzar).

### A shortcut for Atlas phase 0/1 (test stack only)
Atlas phase 1 waits on the client patch (clones need Map.dbc rows). DM shows that population, scaling, loot
and bot groups can be developed inside a **stock instance** without any client change. On the isolated test
stack, Atlas could run its population and affix code in a stock Deadmines instance first ("prototype mode"),
then switch the same code to clone IDs once the patch exists. Never on the play server (R4).

### Bonus finding: playerbot strategies from another module are possible
`mod-dungeon-clear` (already on this server) registers its own playerbot strategies, actions and triggers
without editing mod-playerbots (`modules/mod-dungeon-clear/src/AiObjectContextAccess.h`). It takes the
addresses of the private static context lists with the standard explicit-instantiation access technique;
the lists are public types held by reference, so appending after startup reaches every bot. If playerbots
renames those members, the build fails loudly (no silent runtime break).

That changes Atlas design section 16: an `atlas` bot strategy reading the hint blackboard (spread, stack,
kill priority, leave zone) is possible now, at the cost of a compile-time coupling to playerbots internals
that already exists on this server.

---

## 4. Suggested follow-ups

1. Decide what to do with mod-dungeon-master before the next play-server image rebuild (R1).
2. Atlas doc updates (done with this analysis): curation rule (model sanity), Q7 note, section 16 note on
   strategy registration, prototype-mode option in the roadmap.
3. Later, optional: a fix list for DM if you want it playable on its own.
