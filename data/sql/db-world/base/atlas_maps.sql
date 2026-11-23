-- mod-atlas — Echo Sigils, the Timeways Lens, rifts and the Keepers' cache (PLAN 1.4).
--
-- Reserved IDs (DESIGN §19): items 90001-90216, gameobject entries 900000-900004,
-- gameobject guid 8000101, npc_text 500001-500003. Every DELETE matches the
-- module's own rows only, so re-applying this file is safe.
--
-- The client has no Item.dbc rows for these entries, so an unpatched client
-- shows a question mark on the bag button (the tooltip, name and colour come
-- from the server and are right). The AtlasChronicle addon puts the right icon
-- on the button; the client patch (stage 2) will add the Item.dbc rows.

-- Echo Sigils: one entry per tier and rarity (AtlasMapItem.h). Icons: the
-- pocket watches INV_Misc_PocketWatch_01/02/03 (displays 2820, 1270, 3113);
-- never INV_Misc_Rune_01 (display 946), the Hearthstone's.
DELETE FROM `item_template` WHERE `entry` BETWEEN 90001 AND 90216 AND `name` LIKE 'Echo Sigil%';
INSERT INTO `item_template` (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `Flags`, `BuyPrice`,
    `SellPrice`, `InventoryType`, `ItemLevel`, `RequiredLevel`, `maxcount`, `stackable`, `bonding`, `description`,
    `Material`) VALUES
(90001, 15, 4, 'Echo Sigil (Tier 1)', 2820, 1, 0, 0, 0, 0, 181, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90002, 15, 4, 'Echo Sigil (Tier 2)', 2820, 1, 0, 0, 0, 0, 182, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90003, 15, 4, 'Echo Sigil (Tier 3)', 2820, 1, 0, 0, 0, 0, 183, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90004, 15, 4, 'Echo Sigil (Tier 4)', 2820, 1, 0, 0, 0, 0, 184, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90005, 15, 4, 'Echo Sigil (Tier 5)', 2820, 1, 0, 0, 0, 0, 185, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90006, 15, 4, 'Echo Sigil (Tier 6)', 2820, 1, 0, 0, 0, 0, 186, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90007, 15, 4, 'Echo Sigil (Tier 7)', 2820, 1, 0, 0, 0, 0, 187, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90008, 15, 4, 'Echo Sigil (Tier 8)', 2820, 1, 0, 0, 0, 0, 188, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90009, 15, 4, 'Echo Sigil (Tier 9)', 2820, 1, 0, 0, 0, 0, 189, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90010, 15, 4, 'Echo Sigil (Tier 10)', 2820, 1, 0, 0, 0, 0, 190, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90011, 15, 4, 'Echo Sigil (Tier 11)', 2820, 1, 0, 0, 0, 0, 191, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90012, 15, 4, 'Echo Sigil (Tier 12)', 2820, 1, 0, 0, 0, 0, 192, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90013, 15, 4, 'Echo Sigil (Tier 13)', 2820, 1, 0, 0, 0, 0, 193, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90014, 15, 4, 'Echo Sigil (Tier 14)', 2820, 1, 0, 0, 0, 0, 194, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90015, 15, 4, 'Echo Sigil (Tier 15)', 2820, 1, 0, 0, 0, 0, 195, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90016, 15, 4, 'Echo Sigil (Tier 16)', 2820, 1, 0, 0, 0, 0, 196, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90101, 15, 4, 'Echo Sigil (Tier 1)', 1270, 2, 0, 0, 0, 0, 181, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90102, 15, 4, 'Echo Sigil (Tier 2)', 1270, 2, 0, 0, 0, 0, 182, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90103, 15, 4, 'Echo Sigil (Tier 3)', 1270, 2, 0, 0, 0, 0, 183, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90104, 15, 4, 'Echo Sigil (Tier 4)', 1270, 2, 0, 0, 0, 0, 184, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90105, 15, 4, 'Echo Sigil (Tier 5)', 1270, 2, 0, 0, 0, 0, 185, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90106, 15, 4, 'Echo Sigil (Tier 6)', 1270, 2, 0, 0, 0, 0, 186, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90107, 15, 4, 'Echo Sigil (Tier 7)', 1270, 2, 0, 0, 0, 0, 187, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90108, 15, 4, 'Echo Sigil (Tier 8)', 1270, 2, 0, 0, 0, 0, 188, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90109, 15, 4, 'Echo Sigil (Tier 9)', 1270, 2, 0, 0, 0, 0, 189, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90110, 15, 4, 'Echo Sigil (Tier 10)', 1270, 2, 0, 0, 0, 0, 190, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90111, 15, 4, 'Echo Sigil (Tier 11)', 1270, 2, 0, 0, 0, 0, 191, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90112, 15, 4, 'Echo Sigil (Tier 12)', 1270, 2, 0, 0, 0, 0, 192, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90113, 15, 4, 'Echo Sigil (Tier 13)', 1270, 2, 0, 0, 0, 0, 193, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90114, 15, 4, 'Echo Sigil (Tier 14)', 1270, 2, 0, 0, 0, 0, 194, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90115, 15, 4, 'Echo Sigil (Tier 15)', 1270, 2, 0, 0, 0, 0, 195, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90116, 15, 4, 'Echo Sigil (Tier 16)', 1270, 2, 0, 0, 0, 0, 196, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90201, 15, 4, 'Echo Sigil (Tier 1)', 3113, 3, 0, 0, 0, 0, 181, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90202, 15, 4, 'Echo Sigil (Tier 2)', 3113, 3, 0, 0, 0, 0, 182, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90203, 15, 4, 'Echo Sigil (Tier 3)', 3113, 3, 0, 0, 0, 0, 183, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90204, 15, 4, 'Echo Sigil (Tier 4)', 3113, 3, 0, 0, 0, 0, 184, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90205, 15, 4, 'Echo Sigil (Tier 5)', 3113, 3, 0, 0, 0, 0, 185, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90206, 15, 4, 'Echo Sigil (Tier 6)', 3113, 3, 0, 0, 0, 0, 186, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90207, 15, 4, 'Echo Sigil (Tier 7)', 3113, 3, 0, 0, 0, 0, 187, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90208, 15, 4, 'Echo Sigil (Tier 8)', 3113, 3, 0, 0, 0, 0, 188, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90209, 15, 4, 'Echo Sigil (Tier 9)', 3113, 3, 0, 0, 0, 0, 189, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90210, 15, 4, 'Echo Sigil (Tier 10)', 3113, 3, 0, 0, 0, 0, 190, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90211, 15, 4, 'Echo Sigil (Tier 11)', 3113, 3, 0, 0, 0, 0, 191, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90212, 15, 4, 'Echo Sigil (Tier 12)', 3113, 3, 0, 0, 0, 0, 192, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90213, 15, 4, 'Echo Sigil (Tier 13)', 3113, 3, 0, 0, 0, 0, 193, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90214, 15, 4, 'Echo Sigil (Tier 14)', 3113, 3, 0, 0, 0, 0, 194, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90215, 15, 4, 'Echo Sigil (Tier 15)', 3113, 3, 0, 0, 0, 0, 195, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90216, 15, 4, 'Echo Sigil (Tier 16)', 3113, 3, 0, 0, 0, 0, 196, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1);

-- The Timeways Lens, the rifts and the cache. Models by their size in the
-- client's GameObjectDisplayInfo.dbc (bounding boxes): the scrying orb on its
-- stand (7150, about 2 yd), the mage portals to the Caverns of Time (8475) and
-- to Dalaran (8111, about 3.4 yd), the titan chest (1387). 900004 is the
-- non-interactive rift a run's master steps out of (generic gameobject). The Caverns'
-- hourglass and time rift are landmarks of 150 and 50 yd: never use them here.
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 900000 AND 900004
    AND (`ScriptName` LIKE 'go_atlas_%' OR `name` = 'Rift of the Master');
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `size`, `ScriptName`) VALUES
(900000,  2, 7150, 'The Timeways Lens',         'Speak',    1.0, 'go_atlas_lens'),
(900001, 10, 8475, 'Rift in Time',              'Interact', 1.0, 'go_atlas_rift'),
(900002, 10, 8111, 'Rift to Dalaran',           'Interact', 1.0, 'go_atlas_rift_home'),
(900003, 10, 1387, 'Cache of the Keepers',      'Interact', 0.8, 'go_atlas_cache'),
(900004,  5, 8475, 'Rift of the Master',        '',         1.4, '');

-- The Lens stands beside Chronicler Esmadormu in Dalaran (creature guid 8000010).
DELETE FROM `gameobject` WHERE `guid` = 8000101 AND `id` = 900000;
INSERT INTO `gameobject` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`,
    `orientation`, `rotation0`, `rotation1`, `rotation2`, `rotation3`, `spawntimesecs`, `animprogress`, `state`,
    `Comment`) VALUES
(8000101, 900000, 571, 1, 1, 5807.76, 801.40, 662.14, 0.35, 0, 0, 0.17432, 0.98469, 180, 100, 1,
    'mod-atlas: the Timeways Lens');

DELETE FROM `npc_text` WHERE `ID` BETWEEN 500001 AND 500003 AND `text0_0` LIKE 'The Lens%';
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
(500001,
 'The Lens turns its sand upward as you approach. Every sigil you carry hums in answer.$B$BPlace one, and the Keepers will open a rift into the echo it holds. The rift will carry you, and those who travel with you, a few times; spend it carefully.',
 'The Lens turns its sand upward as you approach. Every sigil you carry hums in answer.$B$BPlace one, and the Keepers will open a rift into the echo it holds. The rift will carry you, and those who travel with you, a few times; spend it carefully.',
 1),
(500002,
 'The Lens is still. You carry no sigil it can read.$B$BChronicler Esmadormu seals echoes for those who have come into their strength, and every echo you mend yields more.',
 'The Lens is still. You carry no sigil it can read.$B$BChronicler Esmadormu seals echoes for those who have come into their strength, and every echo you mend yields more.',
 1),
(500003,
 'The Lens holds the echo you opened. Its rift waits beside it.',
 'The Lens holds the echo you opened. Its rift waits beside it.',
 1);
