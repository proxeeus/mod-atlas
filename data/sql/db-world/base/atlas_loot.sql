-- mod-atlas — the rewards ladder of map runs (PLAN 1.5, DESIGN §14).
-- Gear of a map run comes from an item-level band of the existing WotLK gear
-- (item_template, PvP gear excluded): no rows in AC's loot tables. Edit the
-- bands here and `.atlas reload`. Re-applying this file restores the defaults.

CREATE TABLE IF NOT EXISTS `atlas_loot_tier` (
    `tier`           TINYINT UNSIGNED  NOT NULL,
    `min_item_level` SMALLINT UNSIGNED NOT NULL DEFAULT 200,
    `max_item_level` SMALLINT UNSIGNED NOT NULL DEFAULT 200,
    `epic_share`     TINYINT UNSIGNED  NOT NULL DEFAULT 30 COMMENT '% of gear that is epic, before rarity',
    `gear_count`     TINYINT UNSIGNED  NOT NULL DEFAULT 1 COMMENT 'gear in each player share of the cache',
    `comment`        VARCHAR(100)      NOT NULL DEFAULT '',
    PRIMARY KEY (`tier`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-atlas: gear by map tier';

DELETE FROM `atlas_loot_tier` WHERE `tier` BETWEEN 1 AND 16;
INSERT INTO `atlas_loot_tier` (`tier`, `min_item_level`, `max_item_level`, `epic_share`, `gear_count`, `comment`) VALUES
(1, 200, 200, 30, 1, 'heroic dungeons, Naxxramas 10'),
(2, 200, 200, 36, 1, ''),
(3, 200, 200, 42, 1, ''),
(4, 200, 213, 48, 1, '+ Naxxramas 25'),
(5, 200, 213, 54, 1, ''),
(6, 213, 213, 60, 1, 'Naxxramas 25'),
(7, 213, 219, 66, 1, 'Naxxramas 25, Ulduar 10'),
(8, 219, 219, 72, 1, 'Ulduar 10'),
(9, 219, 226, 78, 1, 'Ulduar 10 and 25'),
(10, 226, 226, 84, 1, 'Ulduar 25'),
(11, 232, 232, 90, 2, 'Trial of the Crusader 10'),
(12, 232, 239, 96, 2, 'Trial of the Crusader 10, hard Ulduar'),
(13, 239, 239, 100, 2, 'Trial of the Crusader 10 heroic'),
(14, 239, 245, 100, 2, 'Trial of the Crusader 25'),
(15, 245, 245, 100, 2, 'Icecrown 10'),
(16, 251, 264, 100, 2, 'Icecrown 25, heroic');
