-- mod-atlas: removes the fog rows of atlas_fogs.sql (tools/clones/gen_sql.py; reserved IDs only).
-- Not applied automatically. Run it by hand against acore_world, then restart. While
-- data/sql/db-world/base/atlas_fogs.sql is in the module, the next update installs the rows again: move
-- it back to data/sql/staged/ first to keep them out.

DELETE FROM `spell_dbc` WHERE `ID` BETWEEN 90400 AND 90449;
DROP TABLE IF EXISTS `atlas_fog`;
DELETE FROM `updates` WHERE `name` = 'atlas_fogs.sql';
