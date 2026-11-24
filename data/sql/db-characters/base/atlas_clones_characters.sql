-- mod-atlas — clone maps (PLAN stage 2): the accounts whose client has the Atlas
-- patch (Data\patch-T.MPQ), set with `.atlas clones on|off`. A map run opens in
-- a clone only when every real player of the party is listed here.
-- Only creates the module's own table; re-applying keeps its rows.

CREATE TABLE IF NOT EXISTS `atlas_clone_optin` (
    `account`  INT UNSIGNED NOT NULL,
    `opted_at` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`account`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='mod-atlas: clone map opt-in';
