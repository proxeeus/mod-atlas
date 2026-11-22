-- =============================================
-- mod-atlas (V0) - World DB Setup
-- =============================================
-- Lore frame: the Keepers of Time (bronze dragonflight) open "echoes" of
-- dungeons from fractured timelines. Player-facing names follow that frame;
-- code identifiers use "atlas" (npc_atlas_chronicler, Atlas.* config keys).
--
-- This script creates:
--   1. creature_template         - Chronicler Esmadormu (entry 500000)
--   2. creature_template_model   - a bronze dragon in mortal guise (display 20222)
--   3. npc_text                  - her greeting (ID 500000)
--   4. creature                  - city spawns (guids 8000001-8000011)
--   5. the Endless Timeways vendor (entry 500001) and the run buff spell (91001)
--
-- Idempotent: re-running replaces only this module's own rows.
-- spawnMask=1 and phaseMask=1 are set explicitly for visibility.
-- =============================================

-- -----------------------------------------------
-- Step 1: Clean slate  (safe DELETE, not TRUNCATE)
-- -----------------------------------------------
-- Every DELETE is restricted to rows this module created (its own guids, or
-- an entry whose template carries this module's name/ScriptName). If another
-- module or a custom edit already uses entry 500000 or guids 8000001-8000011,
-- its rows are left alone and the INSERTs below fail loudly instead of
-- silently replacing someone else's data.
DELETE FROM `creature` WHERE `guid` BETWEEN 8000001 AND 8000011 AND `id` = 500000;
DELETE FROM `creature_template_model` WHERE `CreatureID` = 500000
    AND EXISTS (SELECT 1 FROM `creature_template` ct
                WHERE ct.`entry` = 500000 AND ct.`ScriptName` = 'npc_atlas_chronicler');
DELETE FROM `creature_template` WHERE `entry` = 500000 AND `ScriptName` = 'npc_atlas_chronicler';
DELETE FROM `npc_text` WHERE `ID` = 500000 AND `text0_0` LIKE 'Time is fracturing%';

-- -----------------------------------------------
-- Step 2: NPC Template  (Chronicler Esmadormu, a bronze dragon in mortal form)
-- -----------------------------------------------
-- npcflag  = 1          (UNIT_NPC_FLAG_GOSSIP)
-- faction  = 35         (friendly to all, like the other Keepers of Time NPCs)
-- unit_flags = 2        (NON_ATTACKABLE)
-- flags_extra = 2       (CIVILIAN - no aggro radius)
-- type = 2              (Dragonkin, as other Keepers of Time)
-- ScriptName must match the CreatureScript registered in C++
INSERT INTO `creature_template` (
    `entry`,
    `name`, `subname`,
    `minlevel`, `maxlevel`,
    `faction`, `npcflag`,
    `speed_walk`, `speed_run`,
    `unit_class`, `unit_flags`, `unit_flags2`,
    `type`, `flags_extra`,
    `ScriptName`
) VALUES (
    500000,
    'Chronicler Esmadormu',
    'Keepers of Time',
    80, 80,
    35,    -- friendly to all
    1,     -- gossip flag
    1.0, 1.14286,
    8,     -- mage
    2,     -- NON_ATTACKABLE
    0,
    2,     -- dragonkin
    2,     -- CIVILIAN
    'npc_atlas_chronicler'
);

-- -----------------------------------------------
-- Step 3: Display Model
-- -----------------------------------------------
-- DisplayID 20222 = a bronze dragon in her blood elf guise, in Keepers of
-- Time robes (the look of Soridormi's mortal form). Person-sized; the old
-- 19373 was the Steward of Time drake and towered over the city NPCs.
INSERT INTO `creature_template_model` (
    `CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`
) VALUES (
    500000, 0, 20222, 1.0, 1.0
);

-- -----------------------------------------------
-- Step 4: Greeting (gossip text shown by npc_atlas_chronicler)
-- -----------------------------------------------
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
(500000,
 'Time is fracturing, $c. In countless timelines the halls you know belong to other masters: beasts, demons, the restless dead. The Keepers of Time can open an echo of such a place.$B$BWalk it, defeat whoever rules there, and the echo mends. Mend enough of them, and the timeways grow a little steadier. We reward those who help.',
 'Time is fracturing, $c. In countless timelines the halls you know belong to other masters: beasts, demons, the restless dead. The Keepers of Time can open an echo of such a place.$B$BWalk it, defeat whoever rules there, and the echo mends. Mend enough of them, and the timeways grow a little steadier. We reward those who help.',
 1);

-- -----------------------------------------------
-- Step 5: City Spawns
-- -----------------------------------------------
-- Where Chronomancer Noriol (mod-playerbot-reset-instances) stands, the
-- Chronicler stands 3 yd to her left, facing the same way: one bronze corner
-- per city. Noriol's positions are this server's own `.npc add` spawns; check
-- these in game and nudge with `.npc move` if one clips a wall.
-- Silvermoon and Shattrath (no Noriol there) keep the module's original spots;
-- the Caverns of Time entrance replaces the old Booty Bay spot.
--
-- guid range: 8000001-8000011  (high range to avoid collisions)
-- spawntimesecs = 180           (3 min respawn if somehow killed)

INSERT INTO `creature` (
    `guid`, `id`, `map`, `spawnMask`, `phaseMask`,
    `position_x`, `position_y`, `position_z`, `orientation`,
    `spawntimesecs`
) VALUES
-- Beside Chronomancer Noriol
(8000001, 500000, 0,   1, 1,  -8998.05,   871.70,  148.60, 5.69, 180),  -- Stormwind
(8000002, 500000, 0,   1, 1,  -4620.79,  -905.76,  501.07, 5.33, 180),  -- Ironforge
(8000003, 500000, 1,   1, 1,   9970.96,  2292.60, 1341.39, 5.73, 180),  -- Darnassus
(8000004, 500000, 530, 1, 1,  -3972.38, -11639.35, -138.89, 0.99, 180), -- The Exodar
(8000005, 500000, 1,   1, 1,   1653.78, -4384.47,   22.22, 4.81, 180),  -- Orgrimmar
(8000006, 500000, 1,   1, 1,  -1242.15,   116.15,  130.19, 2.71, 180),  -- Thunder Bluff
(8000007, 500000, 0,   1, 1,   1593.25,   279.66,  -43.10, 4.37, 180),  -- Undercity
(8000010, 500000, 571, 1, 1,   5808.61,   799.05,  662.14, 0.35, 180),  -- Dalaran
-- No Noriol: original spots
(8000008, 500000, 530, 1, 1,   9738.00, -7454.00,   13.56, 0.50, 180),  -- Silvermoon, Walk of Elders
(8000009, 500000, 530, 1, 1,  -1850.00,  5436.00,  -12.10, 5.50, 180),  -- Shattrath, Terrace of Light
-- Caverns of Time entrance, Tanaris (game_tele CavernsOfTime, 3 yd aside)
(8000011, 500000, 1,   1, 1,  -8201.88, -4495.20,    9.01, 4.73, 180);


-- =============================================
-- Endless Timeways vendor (entry 500001): Timeways Quartermaster
-- =============================================
-- Friendly vendor that spawns at echo entrances during Endless Timeways runs.
-- Provides: sell/buy (general goods, reagents, ammo), repair.
-- Spawned programmatically by PopulateDungeon() — no world spawn entries needed.
-- =============================================
 
-- -----------------------------------------------
-- Clean slate for vendor NPC
-- -----------------------------------------------
-- Same rule as above: only touch entry 500001 if it is this module's vendor.
DELETE FROM `npc_vendor` WHERE `entry` = 500001
    AND EXISTS (SELECT 1 FROM `creature_template` ct
                WHERE ct.`entry` = 500001 AND ct.`name` = 'Timeways Quartermaster');
DELETE FROM `creature_template_model` WHERE `CreatureID` = 500001
    AND EXISTS (SELECT 1 FROM `creature_template` ct
                WHERE ct.`entry` = 500001 AND ct.`name` = 'Timeways Quartermaster');
DELETE FROM `creature_template` WHERE `entry` = 500001 AND `name` = 'Timeways Quartermaster';
 
-- -----------------------------------------------
-- Vendor NPC Template
-- -----------------------------------------------
-- npcflag  = 6785       (GOSSIP|VENDOR|VENDOR_FOOD|VENDOR_REAGENT|REPAIR)
-- faction  = 35         (Friendly to all — green nameplate)
-- unit_flags = 2        (NON_ATTACKABLE)
-- flags_extra = 2       (CIVILIAN)
INSERT INTO `creature_template` (
    `entry`,
    `name`, `subname`,
    `minlevel`, `maxlevel`,
    `faction`, `npcflag`,
    `speed_walk`, `speed_run`,
    `unit_class`, `unit_flags`, `unit_flags2`,
    `type`, `flags_extra`,
    `ScriptName`
) VALUES (
    500001,
    'Timeways Quartermaster',
    'Keepers of Time',
    80, 80,
    35,     -- friendly to all (green nameplate)
    6785,   -- GOSSIP(1) + VENDOR(128) + VENDOR_FOOD(512) + VENDOR_REAGENT(2048) + REPAIR(4096)
    1.0, 1.14286,
    1,      -- warrior (simplest stats)
    2,      -- NON_ATTACKABLE
    0,
    2,      -- dragonkin (Keepers of Time)
    2,      -- CIVILIAN
    ''      -- no custom script; built-in vendor/repair handling
);


 
-- -----------------------------------------------
-- Vendor Display Model
-- -----------------------------------------------
-- DisplayID 21445 = a bronze dragon in his blood elf guise (the look of
-- Tydormu, Keeper of Lost Artifacts). That display is drawn at 1.35x, so
-- DisplayScale 0.75 brings him back to person size.
INSERT INTO `creature_template_model` (
    `CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`
) VALUES (
    500001, 0, 21445, 0.75, 1.0
);
 
-- -----------------------------------------------
-- Vendor Items (npc_vendor)
-- -----------------------------------------------
-- All items have unlimited stock (maxcount=0, incrtime=0).
-- Covers food, water, reagents, ammo, and potions for all level ranges.
 
INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`, `ExtendedCost`) VALUES
-- =========================================================================
-- FOOD (level-tiered)
-- =========================================================================
(500001, 0, 117,    0, 0, 0),   -- Tough Jerky           (lvl 1)
(500001, 0, 2287,   0, 0, 0),   -- Haunch of Meat        (lvl 5)
(500001, 0, 3770,   0, 0, 0),   -- Mutton Chop           (lvl 15)
(500001, 0, 4599,   0, 0, 0),   -- Cured Ham Steak       (lvl 25)
(500001, 0, 8952,   0, 0, 0),   -- Roasted Quail         (lvl 35)
(500001, 0, 27854,  0, 0, 0),   -- Smoked Talbuk Venison (lvl 55)
(500001, 0, 33449,  0, 0, 0),   -- Crusty Flatbread      (lvl 65)
-- =========================================================================
-- WATER (level-tiered)
-- =========================================================================
(500001, 0, 159,    0, 0, 0),   -- Refreshing Spring Water (lvl 1)
(500001, 0, 1179,   0, 0, 0),   -- Ice Cold Milk           (lvl 5)
(500001, 0, 1205,   0, 0, 0),   -- Melon Juice             (lvl 15)
(500001, 0, 1708,   0, 0, 0),   -- Sweet Nectar            (lvl 25)
(500001, 0, 8766,   0, 0, 0),   -- Morning Glory Dew       (lvl 35)
(500001, 0, 27860,  0, 0, 0),   -- Purified Draenic Water  (lvl 55)
(500001, 0, 33444,  0, 0, 0),   -- Pungent Seal Whey       (lvl 65)
-- =========================================================================
-- REAGENTS (all classes)
-- =========================================================================
(500001, 0, 17020,  0, 0, 0),   -- Arcane Powder          (Mage)
(500001, 0, 17021,  0, 0, 0),   -- Wild Berries           (Druid)
(500001, 0, 17026,  0, 0, 0),   -- Wild Thornroot         (Druid)
(500001, 0, 17028,  0, 0, 0),   -- Holy Candle            (Priest)
(500001, 0, 17029,  0, 0, 0),   -- Sacred Candle          (Priest)
(500001, 0, 17030,  0, 0, 0),   -- Ankh                   (Shaman)
(500001, 0, 17031,  0, 0, 0),   -- Rune of Teleportation  (Mage)
(500001, 0, 17032,  0, 0, 0),   -- Rune of Portals        (Mage)
(500001, 0, 17033,  0, 0, 0),   -- Symbol of Divinity     (Paladin)
(500001, 0, 17034,  0, 0, 0),   -- Maple Seed             (Druid)
(500001, 0, 17035,  0, 0, 0),   -- Stranglethorn Seed     (Druid)
(500001, 0, 17036,  0, 0, 0),   -- Ashwood Seed           (Druid)
(500001, 0, 21177,  0, 0, 0),   -- Symbol of Kings        (Paladin)
(500001, 0, 44615,  0, 0, 0),   -- Devout Candle          (Priest)
-- =========================================================================
-- AMMO — Arrows
-- =========================================================================
(500001, 0, 2512,   0, 0, 0),   -- Rough Arrow            (DPS 2.8)
(500001, 0, 2515,   0, 0, 0),   -- Sharp Arrow            (DPS 4.2)
(500001, 0, 3030,   0, 0, 0),   -- Razor Arrow            (DPS 7.5)
(500001, 0, 11285,  0, 0, 0),   -- Jagged Arrow           (DPS 13.0)
(500001, 0, 28053,  0, 0, 0),   -- Warden's Arrow         (DPS 22.0)
(500001, 0, 41584,  0, 0, 0),   -- Terrorshaft Arrow      (DPS 32.5)
-- =========================================================================
-- AMMO — Bullets
-- =========================================================================
(500001, 0, 2516,   0, 0, 0),   -- Light Shot             (DPS 2.8)
(500001, 0, 2519,   0, 0, 0),   -- Heavy Shot             (DPS 4.2)
(500001, 0, 3033,   0, 0, 0),   -- Solid Shot             (DPS 7.5)
(500001, 0, 11284,  0, 0, 0),   -- Accurate Slugs         (DPS 13.0)
(500001, 0, 28060,  0, 0, 0),   -- Impact Shot            (DPS 22.0)
(500001, 0, 41586,  0, 0, 0),   -- Frostbite Bullets      (DPS 32.5)
-- =========================================================================
-- POTIONS
-- =========================================================================
(500001, 0, 118,    0, 0, 0),   -- Minor Healing Potion
(500001, 0, 929,    0, 0, 0),   -- Healing Potion
(500001, 0, 1710,   0, 0, 0),   -- Greater Healing Potion
(500001, 0, 13446,  0, 0, 0),   -- Major Healing Potion
(500001, 0, 33447,  0, 0, 0),   -- Runic Healing Potion
(500001, 0, 2455,   0, 0, 0),   -- Minor Mana Potion
(500001, 0, 3385,   0, 0, 0),   -- Lesser Mana Potion
(500001, 0, 3827,   0, 0, 0),   -- Mana Potion
(500001, 0, 6149,   0, 0, 0),   -- Greater Mana Potion
(500001, 0, 13444,  0, 0, 0),   -- Major Mana Potion
(500001, 0, 33448,  0, 0, 0),   -- Runic Mana Potion
-- =========================================================================
-- ROGUE POISONS — Instant Poison
-- =========================================================================
(500001, 0, 6947,   0, 0, 0),   -- Instant Poison          (lvl 20)
(500001, 0, 6949,   0, 0, 0),   -- Instant Poison II       (lvl 28)
(500001, 0, 6950,   0, 0, 0),   -- Instant Poison III      (lvl 36)
(500001, 0, 8926,   0, 0, 0),   -- Instant Poison IV       (lvl 44)
(500001, 0, 8927,   0, 0, 0),   -- Instant Poison V        (lvl 52)
(500001, 0, 8928,   0, 0, 0),   -- Instant Poison VI       (lvl 60)
(500001, 0, 21927,  0, 0, 0),   -- Instant Poison VII      (lvl 68)
(500001, 0, 43230,  0, 0, 0),   -- Instant Poison VIII     (lvl 73)
(500001, 0, 43231,  0, 0, 0),   -- Instant Poison IX       (lvl 79)
-- =========================================================================
-- ROGUE POISONS — Deadly Poison
-- =========================================================================
(500001, 0, 2892,   0, 0, 0),   -- Deadly Poison           (lvl 30)
(500001, 0, 2893,   0, 0, 0),   -- Deadly Poison II        (lvl 38)
(500001, 0, 8984,   0, 0, 0),   -- Deadly Poison III       (lvl 46)
(500001, 0, 8985,   0, 0, 0),   -- Deadly Poison IV        (lvl 54)
(500001, 0, 20844,  0, 0, 0),   -- Deadly Poison V         (lvl 60)
(500001, 0, 22053,  0, 0, 0),   -- Deadly Poison VI        (lvl 62)
(500001, 0, 22054,  0, 0, 0),   -- Deadly Poison VII       (lvl 70)
(500001, 0, 43232,  0, 0, 0),   -- Deadly Poison VIII      (lvl 76)
(500001, 0, 43233,  0, 0, 0),   -- Deadly Poison IX        (lvl 80)
-- =========================================================================
-- ROGUE POISONS — Utility
-- =========================================================================
(500001, 0, 3775,   0, 0, 0),   -- Crippling Poison        (lvl 20)
(500001, 0, 5237,   0, 0, 0),   -- Mind-numbing Poison     (lvl 24)
(500001, 0, 10918,  0, 0, 0),   -- Wound Poison            (lvl 32)
(500001, 0, 10920,  0, 0, 0),   -- Wound Poison II         (lvl 40)
(500001, 0, 10921,  0, 0, 0),   -- Wound Poison III        (lvl 48)
(500001, 0, 10922,  0, 0, 0),   -- Wound Poison IV         (lvl 56)
(500001, 0, 22055,  0, 0, 0),   -- Wound Poison V          (lvl 64)
(500001, 0, 43234,  0, 0, 0),   -- Wound Poison VI         (lvl 72)
(500001, 0, 43235,  0, 0, 0),   -- Wound Poison VII        (lvl 78)
(500001, 0, 21835,  0, 0, 0);   -- Anesthetic Poison       (lvl 68)


-- =============================================
-- Endless Timeways buff spell "Timeways Resonance" (ID 91001), server-side only
-- =============================================
-- The module's own aura for the roguelike "+10% all stats per cleared floor"
-- buff. It replaces the earlier use of Greater Blessing of Kings, a paladin
-- class spell that had to be patched server-wide to stack (upstream #23).
--
--   Attributes      = 0x000000C0  PASSIVE | DO_NOT_DISPLAY
--   Effect_1        = 6           APPLY_AURA
--   EffectAura_1    = 137         MOD_TOTAL_STAT_PERCENTAGE
--   EffectMiscValue = -1          all stats
--   BasePoints+Die  = 9 + 1       10% per stack
--   CumulativeAura  = 255         max stacks (stack n = n x 10%)
--   DurationIndex   = 21          infinite
--   ImplicitTarget  = 1           caster (applied with Unit::AddAura)
--
-- Passive: the server never sends it to clients (no Spell.dbc entry needed,
-- nothing for unpatched clients to mis-render) and never saves it to
-- character_aura, so the buff cannot outlive a run.
--
-- ID 91001 is free in stock data (stock spells end below 81000; core custom
-- spell_dbc rows sit at 100001+). The DELETE only removes this module's own
-- row; if another row already uses 91001 the INSERT fails loudly.
-- -----------------------------------------------
DELETE FROM `spell_dbc` WHERE `ID` = 91001 AND `Name_Lang_enUS` = 'Timeways Resonance';
INSERT INTO `spell_dbc` (
    `ID`, `Attributes`, `CastingTimeIndex`, `DurationIndex`, `RangeIndex`,
    `CumulativeAura`, `EquippedItemClass`,
    `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`, `ImplicitTargetA_1`,
    `EffectAura_1`, `EffectMiscValue_1`,
    `Name_Lang_enUS`
) VALUES (
    91001, 0x000000C0, 1, 21, 1,
    255, -1,
    6, 1, 9, 1,
    137, -1,
    'Timeways Resonance'
);
