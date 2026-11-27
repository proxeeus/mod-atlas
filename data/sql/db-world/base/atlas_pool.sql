-- mod-atlas: generated maps (the Timeways, map 950) are scrapped (2026-11-27): their pieces never looked right.
-- This file once installed them; it now removes every row and table they had, so the next update (which re-runs
-- a changed file) cleans the database. Reserved IDs only. Safe to run again.

DELETE FROM `instance_template` WHERE `map` BETWEEN 950 AND 999;
DELETE FROM `mapdifficulty_dbc` WHERE `MapID` BETWEEN 950 AND 999;
DELETE FROM `map_dbc` WHERE `ID` BETWEEN 950 AND 999;
DELETE FROM `mapdifficulty_dbc` WHERE `ID` = 759;
DELETE FROM `areatable_dbc` WHERE `ID` BETWEEN 7900 AND 7949;
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 900100 AND 900199;
DELETE FROM `gameobjectdisplayinfo_dbc` WHERE `ID` BETWEEN 9900 AND 9999;
DROP TABLE IF EXISTS `atlas_pool_socket`;
DROP TABLE IF EXISTS `atlas_pool_piece`;
DROP TABLE IF EXISTS `atlas_pool_map`;
