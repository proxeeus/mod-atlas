-- mod-atlas — Echo Sigil data and starter grants (PLAN 1.4, decision D2).
-- Only creates the module's own tables; re-applying keeps their rows.

-- What makes a sigil unique, keyed by its item_instance guid. Rows whose item
-- no longer exists are pruned at startup.
CREATE TABLE IF NOT EXISTS `atlas_map_item` (
    `item_guid`  INT UNSIGNED     NOT NULL,
    `owner_guid` INT UNSIGNED     NOT NULL DEFAULT 0,
    `tier`       TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `rarity`     TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 normal, 1 magic, 2 rare',
    `source_map` INT UNSIGNED     NOT NULL DEFAULT 0 COMMENT 'dungeon map the sigil opens',
    `theme_id`   INT UNSIGNED     NOT NULL DEFAULT 0,
    `seed`       BIGINT UNSIGNED  NOT NULL DEFAULT 0,
    `affixes`    VARCHAR(255)     NOT NULL DEFAULT '' COMMENT 'affix id:value;...',
    `created`    INT UNSIGNED     NOT NULL DEFAULT 0,
    PRIMARY KEY (`item_guid`),
    KEY `idx_owner` (`owner_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-atlas: Echo Sigils';

-- Characters who received their starter sigils from the Chronicler.
CREATE TABLE IF NOT EXISTS `atlas_map_grant` (
    `guid`    INT UNSIGNED NOT NULL,
    `granted` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-atlas: starter sigils given';
