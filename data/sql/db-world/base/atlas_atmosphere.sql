-- mod-atlas — the sky and fog of echoes and maps (PLAN 1.6, ATMOSPHERE appendix).
--
-- A look is an existing client light (Light.dbc id), sent as an override of an
-- anchor light that covers any map (Atlas.Atmosphere.Anchor), so it needs no
-- client change; the client drops it when it leaves the map. Lookup order:
-- the run's theme at its corruption level, then any theme (0) at that level,
-- then the theme at any level (255), then the default. Rows with an affix_id
-- give a second look on Atlas.Atmosphere.AffixAnchor while the run has that
-- affix. Fog end is at noon, in yards: lower is heavier.
-- Try looks in game with `.atlas light <id>`, edit here, `.atlas reload`.
-- Re-applying this file restores these rows.

CREATE TABLE IF NOT EXISTS `atlas_atmosphere` (
    `id`         INT UNSIGNED     NOT NULL,
    `theme_id`   INT UNSIGNED     NOT NULL DEFAULT 0   COMMENT '0 = any theme',
    `corruption` TINYINT UNSIGNED NOT NULL DEFAULT 255 COMMENT 'corruption level 0-5, 255 = any',
    `affix_id`   INT UNSIGNED     NOT NULL DEFAULT 0   COMMENT '> 0: an affix look on the second anchor',
    `light_id`   INT UNSIGNED     NOT NULL DEFAULT 0   COMMENT 'Light.dbc id',
    `music_id`   INT UNSIGNED     NOT NULL DEFAULT 0   COMMENT 'SoundEntries id played on entry, 0 = none',
    `comment`    VARCHAR(100)     NOT NULL DEFAULT '',
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-atlas: run looks';

DELETE FROM `atlas_atmosphere` WHERE `id` BETWEEN 1 AND 999;
INSERT INTO `atlas_atmosphere` (`id`, `theme_id`, `corruption`, `affix_id`, `light_id`, `music_id`, `comment`) VALUES
-- The echo signature: the Caverns of Time's smoky bronze (also "the timeline as it was").
(1,  0, 255, 0,   84,   0, 'default: Opening of the Dark Portal, fog 111, #231C25'),
-- Corruption takes over from level 3 (tiers 10+), whatever the theme.
(2,  0, 3,   0,   27,   0, 'plague: Western Plaguelands grey ash, fog 181'),
(3,  0, 4,   0,   333,  0, 'plague: Scholomance near-black, fog 111'),
(4,  0, 5,   0,   1673, 0, 'overrun: Culling of Stratholme, fog 25, the heaviest in the game'),
-- Themes (config Atlas.Theme.N, database themes 50+).
(10, 1,  255, 0,  1374, 0, 'beasts: Grizzly Hills forest green, fog 278'),
(11, 2,  255, 0,  1769, 0, 'dragonkin: Obsidian Sanctum dark red, fog 417'),
(12, 3,  255, 0,  214,  0, 'the Legion: Felwood fel green, fog 389'),
(13, 4,  255, 0,  670,  0, 'elements: Netherstorm dark magenta, fog 361'),
(14, 5,  255, 0,  61,   0, 'giants: Alterac teal, fog 139'),
(15, 6,  255, 0,  1607, 0, 'mortal warbands: Culling of Stratholme rust red, fog 333'),
(16, 7,  255, 0,  2538, 0, 'rogue machines: Ruby Sanctum ember, fog 333'),
(17, 8,  255, 0,  189,  0, 'shattered: Emerald Dream nightmare purple, fog 361'),
(18, 51, 255, 0,  2508, 0, 'the Scourge: Icecrown Citadel deep frost teal, fog 278'),
(19, 51, 2,   0,  1817, 0, 'the Scourge, deeper: Icecrown Glacier grey-brown, fog 250'),
-- Affix looks (second anchor).
(50, 0, 255, 202, 56,   0, 'Blizzard-swept: Hillsbrad dark teal, fog 111'),
(51, 0, 255, 203, 43,   0, 'Plague-cloud: Eastern Plaguelands sick amber, fog 222');
