-- mod-atlas: removes the clone map rows of atlas_clones.sql (tools/clones/gen_sql.py; reserved IDs only).
-- Not applied automatically. Run it by hand against acore_world after Atlas.Clones.Enable = 0, the client
-- patch is removed and the data links are gone (tools/clones/link_data.sh --remove), then restart.
-- While data/sql/db-world/base/atlas_clones.sql is in the module, the next start installs the rows again: move
-- it back to data/sql/staged/ first to keep them out.

DELETE FROM `atlas_map_source` WHERE `clone_map` BETWEEN 900 AND 949;
DELETE FROM `instance_template` WHERE `map` BETWEEN 900 AND 949;
DELETE FROM `mapdifficulty_dbc` WHERE `MapID` BETWEEN 900 AND 949;
DELETE FROM `map_dbc` WHERE `ID` BETWEEN 900 AND 949;
DELETE FROM `updates` WHERE `name` = 'atlas_clones.sql';
