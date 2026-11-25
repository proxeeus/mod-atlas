-- mod-atlas: removes the pool rows of atlas_pool.sql (tools/clones/gen_sql.py; reserved IDs only).
-- Not applied automatically. Run it by hand against acore_world, then restart. While
-- data/sql/db-world/base/atlas_pool.sql is in the module, the next update installs the rows again: move
-- it back to data/sql/staged/ first to keep them out.

DELETE FROM `instance_template` WHERE `map` BETWEEN 950 AND 999;
DELETE FROM `mapdifficulty_dbc` WHERE `MapID` BETWEEN 950 AND 999;
DELETE FROM `map_dbc` WHERE `ID` BETWEEN 950 AND 999;
DELETE FROM `mapdifficulty_dbc` WHERE `ID` = 759;
DELETE FROM `areatable_dbc` WHERE `ID` BETWEEN 7900 AND 7949;
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 900100 AND 900199;
DROP TABLE IF EXISTS `atlas_pool_socket`;
DROP TABLE IF EXISTS `atlas_pool_piece`;
DROP TABLE IF EXISTS `atlas_pool_map`;
DELETE FROM `updates` WHERE `name` = 'atlas_pool.sql';
