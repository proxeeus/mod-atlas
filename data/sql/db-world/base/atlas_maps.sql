-- mod-atlas — Echo Sigils, the Timeways Lens, rifts and the Keepers' cache (PLAN 1.4).
--
-- Reserved IDs (DESIGN §19): items 90001-90216, gameobject entries 900000-900003,
-- gameobject guid 8000101, npc_text 500001-500003. Every DELETE matches the
-- module's own rows only, so re-applying this file is safe.
--
-- The client has no Item.dbc rows for these entries, so an unpatched client
-- shows a question mark on the bag button (the tooltip, name and colour come
-- from the server and are right). The AtlasChronicle addon puts the right icon
-- on the button; the client patch (stage 2) will add the Item.dbc rows.

-- Echo Sigils: one entry per tier and rarity (AtlasMapItem.h).
DELETE FROM `item_template` WHERE `entry` BETWEEN 90001 AND 90216 AND `name` LIKE 'Echo Sigil%';
INSERT INTO `item_template` (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `Flags`, `BuyPrice`,
    `SellPrice`, `InventoryType`, `ItemLevel`, `RequiredLevel`, `maxcount`, `stackable`, `bonding`, `description`,
    `Material`) VALUES
(90001, 15, 4, 'Echo Sigil (Tier 1)', 946, 1, 0, 0, 0, 0, 181, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90002, 15, 4, 'Echo Sigil (Tier 2)', 946, 1, 0, 0, 0, 0, 182, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90003, 15, 4, 'Echo Sigil (Tier 3)', 946, 1, 0, 0, 0, 0, 183, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90004, 15, 4, 'Echo Sigil (Tier 4)', 946, 1, 0, 0, 0, 0, 184, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90005, 15, 4, 'Echo Sigil (Tier 5)', 946, 1, 0, 0, 0, 0, 185, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90006, 15, 4, 'Echo Sigil (Tier 6)', 946, 1, 0, 0, 0, 0, 186, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90007, 15, 4, 'Echo Sigil (Tier 7)', 946, 1, 0, 0, 0, 0, 187, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90008, 15, 4, 'Echo Sigil (Tier 8)', 946, 1, 0, 0, 0, 0, 188, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90009, 15, 4, 'Echo Sigil (Tier 9)', 946, 1, 0, 0, 0, 0, 189, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90010, 15, 4, 'Echo Sigil (Tier 10)', 946, 1, 0, 0, 0, 0, 190, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90011, 15, 4, 'Echo Sigil (Tier 11)', 946, 1, 0, 0, 0, 0, 191, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90012, 15, 4, 'Echo Sigil (Tier 12)', 946, 1, 0, 0, 0, 0, 192, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90013, 15, 4, 'Echo Sigil (Tier 13)', 946, 1, 0, 0, 0, 0, 193, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90014, 15, 4, 'Echo Sigil (Tier 14)', 946, 1, 0, 0, 0, 0, 194, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90015, 15, 4, 'Echo Sigil (Tier 15)', 946, 1, 0, 0, 0, 0, 195, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90016, 15, 4, 'Echo Sigil (Tier 16)', 946, 1, 0, 0, 0, 0, 196, 80, 0, 1, 1, 'An echo the Keepers of Time sealed. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90101, 15, 4, 'Echo Sigil (Tier 1)', 20774, 2, 0, 0, 0, 0, 181, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90102, 15, 4, 'Echo Sigil (Tier 2)', 20774, 2, 0, 0, 0, 0, 182, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90103, 15, 4, 'Echo Sigil (Tier 3)', 20774, 2, 0, 0, 0, 0, 183, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90104, 15, 4, 'Echo Sigil (Tier 4)', 20774, 2, 0, 0, 0, 0, 184, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90105, 15, 4, 'Echo Sigil (Tier 5)', 20774, 2, 0, 0, 0, 0, 185, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90106, 15, 4, 'Echo Sigil (Tier 6)', 20774, 2, 0, 0, 0, 0, 186, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90107, 15, 4, 'Echo Sigil (Tier 7)', 20774, 2, 0, 0, 0, 0, 187, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90108, 15, 4, 'Echo Sigil (Tier 8)', 20774, 2, 0, 0, 0, 0, 188, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90109, 15, 4, 'Echo Sigil (Tier 9)', 20774, 2, 0, 0, 0, 0, 189, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90110, 15, 4, 'Echo Sigil (Tier 10)', 20774, 2, 0, 0, 0, 0, 190, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90111, 15, 4, 'Echo Sigil (Tier 11)', 20774, 2, 0, 0, 0, 0, 191, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90112, 15, 4, 'Echo Sigil (Tier 12)', 20774, 2, 0, 0, 0, 0, 192, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90113, 15, 4, 'Echo Sigil (Tier 13)', 20774, 2, 0, 0, 0, 0, 193, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90114, 15, 4, 'Echo Sigil (Tier 14)', 20774, 2, 0, 0, 0, 0, 194, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90115, 15, 4, 'Echo Sigil (Tier 15)', 20774, 2, 0, 0, 0, 0, 195, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90116, 15, 4, 'Echo Sigil (Tier 16)', 20774, 2, 0, 0, 0, 0, 196, 80, 0, 1, 1, 'An unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90201, 15, 4, 'Echo Sigil (Tier 1)', 35465, 3, 0, 0, 0, 0, 181, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90202, 15, 4, 'Echo Sigil (Tier 2)', 35465, 3, 0, 0, 0, 0, 182, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90203, 15, 4, 'Echo Sigil (Tier 3)', 35465, 3, 0, 0, 0, 0, 183, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90204, 15, 4, 'Echo Sigil (Tier 4)', 35465, 3, 0, 0, 0, 0, 184, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90205, 15, 4, 'Echo Sigil (Tier 5)', 35465, 3, 0, 0, 0, 0, 185, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90206, 15, 4, 'Echo Sigil (Tier 6)', 35465, 3, 0, 0, 0, 0, 186, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90207, 15, 4, 'Echo Sigil (Tier 7)', 35465, 3, 0, 0, 0, 0, 187, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90208, 15, 4, 'Echo Sigil (Tier 8)', 35465, 3, 0, 0, 0, 0, 188, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90209, 15, 4, 'Echo Sigil (Tier 9)', 35465, 3, 0, 0, 0, 0, 189, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90210, 15, 4, 'Echo Sigil (Tier 10)', 35465, 3, 0, 0, 0, 0, 190, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90211, 15, 4, 'Echo Sigil (Tier 11)', 35465, 3, 0, 0, 0, 0, 191, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90212, 15, 4, 'Echo Sigil (Tier 12)', 35465, 3, 0, 0, 0, 0, 192, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90213, 15, 4, 'Echo Sigil (Tier 13)', 35465, 3, 0, 0, 0, 0, 193, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90214, 15, 4, 'Echo Sigil (Tier 14)', 35465, 3, 0, 0, 0, 0, 194, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90215, 15, 4, 'Echo Sigil (Tier 15)', 35465, 3, 0, 0, 0, 0, 195, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1),
(90216, 15, 4, 'Echo Sigil (Tier 16)', 35465, 3, 0, 0, 0, 0, 196, 80, 0, 1, 1, 'A deeply unstable echo. Place it in the Timeways Lens in Dalaran to open it.', -1);

-- The Timeways Lens (the Caverns of Time hourglass), the rifts and the cache.
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 900000 AND 900003 AND `ScriptName` LIKE 'go_atlas_%';
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `size`, `ScriptName`) VALUES
(900000,  2, 7133, 'The Timeways Lens',         'Speak',    0.5, 'go_atlas_lens'),
(900001, 10, 4713, 'Rift in Time',              'Interact', 0.8, 'go_atlas_rift'),
(900002, 10, 4713, 'Rift to Dalaran',           'Interact', 0.8, 'go_atlas_rift_home'),
(900003, 10, 1387, 'Cache of the Keepers',      'Interact', 1.0, 'go_atlas_cache');

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
