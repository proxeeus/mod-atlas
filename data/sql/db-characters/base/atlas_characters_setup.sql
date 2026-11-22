-- =============================================
-- mod-atlas (V0) - Characters DB Setup
-- =============================================
-- Run this on your CHARACTERS database (not world).
-- Creates tables for persistent player statistics, dungeon clear-time
-- leaderboards, runs in progress and the run log.
-- =============================================

-- ---------------------------------------------------------------------------
-- Normal run player stats (standalone challenges only)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `atlas_player_stats` (
    `guid`               INT UNSIGNED NOT NULL,
    `total_runs`         INT UNSIGNED NOT NULL DEFAULT 0,
    `completed_runs`     INT UNSIGNED NOT NULL DEFAULT 0,
    `failed_runs`        INT UNSIGNED NOT NULL DEFAULT 0,
    `total_mobs_killed`  INT UNSIGNED NOT NULL DEFAULT 0,
    `total_bosses_killed` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_deaths`       INT UNSIGNED NOT NULL DEFAULT 0,
    `fastest_clear`      INT UNSIGNED NOT NULL DEFAULT 0,  -- seconds (best across all dungeons)
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------------------------------------------------------------------
-- Normal run leaderboard (fastest clears per dungeon/difficulty)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `atlas_leaderboard` (
    `id`              INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `guid`            INT UNSIGNED NOT NULL,
    `char_name`       VARCHAR(48)  NOT NULL DEFAULT '',
    `map_id`          INT UNSIGNED NOT NULL,
    `difficulty_id`   INT UNSIGNED NOT NULL,
    `clear_time`      INT UNSIGNED NOT NULL,  -- seconds
    `party_size`      TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `scaled`          TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `effective_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `mobs_killed`     INT UNSIGNED NOT NULL DEFAULT 0,
    `bosses_killed`   INT UNSIGNED NOT NULL DEFAULT 0,
    `deaths`          INT UNSIGNED NOT NULL DEFAULT 0,
    `completed_at`    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    INDEX `idx_map_diff` (`map_id`, `difficulty_id`, `clear_time`),
    INDEX `idx_overall`  (`clear_time`),
    INDEX `idx_guid`     (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------------------------------------------------------------------
-- Roguelike player stats (roguelike mode only — separate from normal)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `atlas_roguelike_player_stats` (
    `guid`                 INT UNSIGNED NOT NULL,
    `total_runs`           INT UNSIGNED NOT NULL DEFAULT 0,
    `highest_tier`         INT UNSIGNED NOT NULL DEFAULT 0,
    `most_floors_cleared`  INT UNSIGNED NOT NULL DEFAULT 0,
    `total_floors_cleared` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_mobs_killed`    INT UNSIGNED NOT NULL DEFAULT 0,
    `total_bosses_killed`  INT UNSIGNED NOT NULL DEFAULT 0,
    `total_deaths`         INT UNSIGNED NOT NULL DEFAULT 0,
    `longest_run_time`     INT UNSIGNED NOT NULL DEFAULT 0,  -- seconds
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------------------------------------------------------------------
-- Runs in progress, one row per instance a run is using. Runs empty a real
-- dungeon instance; at startup the rows left by a restart (or crash) mid-run
-- are logged as interrupted and their binds released, so nobody is left saved
-- to an emptied dungeon. The seed lets an interrupted run be replayed.
-- Rows are removed as soon as a run ends normally.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `atlas_run_active` (
    `instance_id`     INT UNSIGNED      NOT NULL,
    `map_id`          SMALLINT UNSIGNED NOT NULL,
    `session_id`      INT UNSIGNED      NOT NULL DEFAULT 0,
    `kind`            TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '0 echo, 1 Endless Timeways floor',
    `endless_run_id`  INT UNSIGNED      NOT NULL DEFAULT 0,
    `depth`           INT UNSIGNED      NOT NULL DEFAULT 0,
    `endless_seed`    BIGINT UNSIGNED   NOT NULL DEFAULT 0,
    `seed`            BIGINT UNSIGNED   NOT NULL DEFAULT 0,
    `difficulty_id`   INT UNSIGNED      NOT NULL DEFAULT 0,
    `theme_id`        INT UNSIGNED      NOT NULL DEFAULT 0,
    `leader_guid`     INT UNSIGNED      NOT NULL DEFAULT 0,
    `party_size`      TINYINT UNSIGNED  NOT NULL DEFAULT 1,
    `effective_level` TINYINT UNSIGNED  NOT NULL DEFAULT 0,
    `scaled`          TINYINT UNSIGNED  NOT NULL DEFAULT 1,
    `started_at`      INT UNSIGNED      NOT NULL DEFAULT 0 COMMENT 'unix time',
    `opened_at`       DATETIME          NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`instance_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------------------------------------------------------------------
-- Every finished run, with the seed it was built from (replays, comparisons,
-- stats). result: 0 completed, 1 failed (party fell), 2 abandoned or ended,
-- 3 interrupted (server stopped mid-run).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `atlas_run_log` (
    `id`              INT UNSIGNED      NOT NULL AUTO_INCREMENT,
    `kind`            TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '0 echo, 1 Endless Timeways floor',
    `endless_run_id`  INT UNSIGNED      NOT NULL DEFAULT 0,
    `depth`           INT UNSIGNED      NOT NULL DEFAULT 0,
    `endless_seed`    BIGINT UNSIGNED   NOT NULL DEFAULT 0,
    `seed`            BIGINT UNSIGNED   NOT NULL DEFAULT 0,
    `map_id`          SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `difficulty_id`   INT UNSIGNED      NOT NULL DEFAULT 0,
    `theme_id`        INT UNSIGNED      NOT NULL DEFAULT 0,
    `leader_guid`     INT UNSIGNED      NOT NULL DEFAULT 0,
    `party_size`      TINYINT UNSIGNED  NOT NULL DEFAULT 1,
    `effective_level` TINYINT UNSIGNED  NOT NULL DEFAULT 0,
    `scaled`          TINYINT UNSIGNED  NOT NULL DEFAULT 1,
    `result`          TINYINT UNSIGNED  NOT NULL DEFAULT 0,
    `started_at`      INT UNSIGNED      NOT NULL DEFAULT 0 COMMENT 'unix time',
    `duration`        INT UNSIGNED      NOT NULL DEFAULT 0 COMMENT 'seconds',
    `total_mobs`      INT UNSIGNED      NOT NULL DEFAULT 0,
    `mobs_killed`     INT UNSIGNED      NOT NULL DEFAULT 0,
    `bosses_killed`   INT UNSIGNED      NOT NULL DEFAULT 0,
    `deaths`          INT UNSIGNED      NOT NULL DEFAULT 0,
    `affixes`         VARCHAR(255)      NOT NULL DEFAULT '' COMMENT 'Endless affix ids, comma-separated',
    `logged_at`       DATETIME          NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    INDEX `idx_leader` (`leader_guid`),
    INDEX `idx_map`    (`map_id`, `result`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Retired: atlas_open_instance (V0 before run records). Carry over any open
-- instances so their binds are still released at the next startup, then drop
-- it. The CREATE only makes the copy safe on databases that never had it.
CREATE TABLE IF NOT EXISTS `atlas_open_instance` (
    `instance_id` INT UNSIGNED NOT NULL,
    `map_id`      SMALLINT UNSIGNED NOT NULL,
    `opened_at`   DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`instance_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
INSERT IGNORE INTO `atlas_run_active` (`instance_id`, `map_id`, `opened_at`)
    SELECT `instance_id`, `map_id`, `opened_at` FROM `atlas_open_instance`;
DROP TABLE `atlas_open_instance`;

-- ---------------------------------------------------------------------------
-- Roguelike leaderboard (sorted by tier reached, then floors, then speed)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `atlas_roguelike_leaderboard` (
    `id`               INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `guid`             INT UNSIGNED NOT NULL,
    `char_name`        VARCHAR(64)  NOT NULL DEFAULT '',
    `tier_reached`     INT UNSIGNED NOT NULL DEFAULT 0,
    `dungeons_cleared` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_kills`      INT UNSIGNED NOT NULL DEFAULT 0,
    `total_bosses`     INT UNSIGNED NOT NULL DEFAULT 0,
    `total_deaths`     INT UNSIGNED NOT NULL DEFAULT 0,
    `run_duration`     INT UNSIGNED NOT NULL DEFAULT 0,
    `party_size`       TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `completed_at`     DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    INDEX `idx_tier`   (`tier_reached` DESC, `dungeons_cleared` DESC, `run_duration` ASC),
    INDEX `idx_floors` (`dungeons_cleared` DESC, `tier_reached` DESC),
    INDEX `idx_guid`   (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
