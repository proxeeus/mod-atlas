-- mod-atlas — Echo Sigil data, starter grants (PLAN 1.4, decision D2) and the
-- atlas progress of each account (PLAN 1.5: first clears).
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

-- Which tiers of each dungeon an account has mended (bit tier-1): the first
-- clear of a dungeon at a tier gives a sigil of the next tier (DESIGN §14),
-- and atlas progression (DESIGN §15) builds on it.
CREATE TABLE IF NOT EXISTS `atlas_account_progress` (
    `account`    INT UNSIGNED NOT NULL,
    `source_map` INT UNSIGNED NOT NULL,
    `tiers`      INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'bit (tier - 1) set once mended',
    PRIMARY KEY (`account`, `source_map`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-atlas: dungeons mended by tier';
