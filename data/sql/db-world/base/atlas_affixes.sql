-- =============================================
-- mod-atlas - Affixes (PLAN 1.2, DESIGN section 10, AFFIXES appendix)
-- =============================================
-- atlas_affix: one row per affix. `handler` names the mechanism (AtlasAffix.h);
-- value1/value2/value3 are the values for map tiers 1-5 / 6-10 / 11-16 (the
-- Endless Timeways uses its depth as the tier). `{}` in the description is
-- replaced by the rolled value. Only the phase-1 subset for now, all of it
-- safe for playerbots.
--
-- Stock spells carry the visible effects (extra hits, ground zones, bursts,
-- the absorb). Player-side affixes use this module's own passive spells
-- (90001-90007 below): the server applies them, the client never sees them,
-- and passive auras are never saved to characters.
-- Idempotent: re-running replaces only this module's rows.
-- =============================================

CREATE TABLE IF NOT EXISTS `atlas_affix` (
    `id`              INT UNSIGNED     NOT NULL,
    `code`            VARCHAR(8)       NOT NULL,
    `name`            VARCHAR(64)      NOT NULL,
    `description`     VARCHAR(255)     NOT NULL,
    `handler`         VARCHAR(32)      NOT NULL,
    `spell_id`        INT UNSIGNED     NOT NULL DEFAULT 0,
    `param1`          FLOAT            NOT NULL DEFAULT 0,
    `param2`          FLOAT            NOT NULL DEFAULT 0,
    `value1`          FLOAT            NOT NULL DEFAULT 0,
    `value2`          FLOAT            NOT NULL DEFAULT 0,
    `value3`          FLOAT            NOT NULL DEFAULT 0,
    `min_tier`        TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `max_tier`        TINYINT UNSIGNED NOT NULL DEFAULT 16,
    `weight`          INT UNSIGNED     NOT NULL DEFAULT 100,
    `exclusive_group` VARCHAR(32)      NOT NULL DEFAULT '',
    `bot_safe`        TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `quantity_bonus`  INT UNSIGNED     NOT NULL DEFAULT 0,
    `rarity_bonus`    INT UNSIGNED     NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_code` (`code`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `atlas_affix` WHERE `id` BETWEEN 1 AND 999;
INSERT INTO `atlas_affix` (`id`, `code`, `name`, `description`, `handler`, `spell_id`, `param1`, `param2`,
    `value1`, `value2`, `value3`, `min_tier`, `max_tier`, `weight`, `exclusive_group`, `bot_safe`,
    `quantity_bonus`, `rarity_bonus`) VALUES
-- Monster: damage
(1,   'M01', 'Brutal',       'Monsters deal {}% increased physical damage', 'MELEE_DEALT', 0, 0, 0, 15, 25, 40, 1, 16, 100, 'dmg_phys', 1, 8, 4),
(2,   'M02', 'Searing',      'Monsters deal {}% of their damage as extra Fire', 'EXTRA_HIT', 13897, 0, 0, 10, 20, 30, 1, 16, 100, 'dmg_extra', 1, 8, 4),
(3,   'M03', 'Frostbitten',  'Monsters deal {}% of their damage as extra Frost', 'EXTRA_HIT', 62867, 0, 0, 10, 20, 30, 1, 16, 100, 'dmg_extra', 1, 8, 4),
(4,   'M04', 'Necrotic',     'Monsters deal {}% of their damage as extra Shadow', 'EXTRA_HIT', 27286, 0, 0, 10, 20, 30, 1, 16, 100, 'dmg_extra', 1, 8, 4),
(5,   'M05', 'Blighted',     'Monsters deal {}% of their damage as extra Nature', 'EXTRA_HIT', 6647, 0, 0, 10, 20, 30, 1, 16, 100, 'dmg_extra', 1, 8, 4),
(8,   'M08', 'Frenzied',     'Monsters have {}% increased attack and casting speed', 'ATTACK_SPEED', 0, 0, 0, 10, 18, 25, 2, 16, 100, 'speed', 1, 10, 5),
(9,   'M09', 'Fleet',        'Monsters have {}% increased movement speed', 'MOVE_SPEED', 0, 0, 0, 15, 25, 35, 1, 16, 100, 'move', 1, 5, 2),
-- Monster: defence
(11,  'M11', 'Fecund',       'Monsters have {}% more life', 'MONSTER_LIFE', 0, 0, 0, 25, 45, 70, 1, 16, 100, 'life', 1, 10, 5),
(12,  'M12', 'Armoured',     'Monsters take {}% reduced physical damage', 'PHYS_TAKEN', 0, 0, 0, 10, 18, 25, 2, 16, 100, 'def_phys', 1, 8, 4),
(13,  'M13', 'Warded',       'Monsters take {}% reduced spell damage', 'SPELL_TAKEN', 0, 0, 0, 10, 18, 25, 2, 16, 100, 'def_spell', 1, 8, 4),
(14,  'M14', 'Ice-shielded', 'Monsters gain an absorb shield of {}% of their life when combat starts', 'COMBAT_SHIELD', 33245, 0, 0, 10, 20, 30, 4, 16, 100, 'shield', 1, 8, 4),
-- Monster: on death
(24,  'M24', 'Bursting',     'Monsters burst on death for {}% of their life as Shadow damage', 'DEATH_BURST', 34234, 1500, 0, 3, 5, 8, 5, 16, 100, 'death', 1, 10, 5),
(25,  'M25', 'Desecrating',  'Monsters leave desecrated ground on death', 'DEATH_POOL', 36473, 0, 0, 30, 45, 60, 4, 16, 100, 'death', 1, 10, 5),
(26,  'M26', 'Splitting',    'Elite monsters split into {} lesser copies on death', 'SPLIT_ON_DEATH', 0, 0, 0, 2, 2, 3, 3, 16, 100, 'death', 1, 10, 5),
-- Player (suffixes)
(101, 'P01', 'of Grief',             'Players have {}% reduced healing received', 'PLAYER_AURA', 90001, -1, 0, 15, 30, 45, 2, 16, 100, 'heal', 1, 10, 5),
(102, 'P02', 'of Frailty',           'Players take {}% increased damage', 'PLAYER_AURA', 90002, 1, 0, 8, 15, 22, 3, 16, 100, 'taken', 1, 10, 5),
(103, 'P03', 'of Enfeeblement',      'Players deal {}% less damage', 'PLAYER_AURA', 90003, -1, 0, 8, 15, 22, 3, 16, 100, 'done', 1, 10, 5),
(104, 'P04', 'of Elemental Weakness', 'Players have {} less resistance to all schools', 'PLAYER_AURA', 90004, -1, 0, 50, 100, 150, 4, 16, 100, 'resist', 1, 8, 4),
(106, 'P06', 'of Lethargy',          'Players have {}% reduced movement speed', 'PLAYER_AURA', 90005, -1, 0, 10, 15, 20, 2, 16, 100, 'move_p', 1, 6, 3),
(108, 'P08', 'of Drought',           'Players have {}% reduced mana regeneration', 'PLAYER_AURA', 90006, -1, 0, 20, 35, 50, 4, 16, 100, 'mana', 1, 10, 5),
(111, 'P11', 'of the Throne',        'Players have {}% less dodge and parry chance', 'PLAYER_AURA', 90007, -1, 0, 10, 15, 20, 5, 16, 100, 'avoid', 1, 8, 4),
-- Area
(202, 'A02', 'Blizzard-swept', 'Every 12 seconds a Blizzard falls on a random player', 'GROUND_PERIODIC', 19099, 12000, 0, 25, 35, 50, 4, 16, 100, 'storm', 1, 10, 5),
(203, 'A03', 'Plague-cloud',   'Every 15 seconds a plague cloud forms under a random player', 'GROUND_PERIODIC', 55876, 15000, 0, 25, 35, 50, 4, 16, 100, 'storm', 1, 10, 5),
-- Pack composition
(301, 'C01', 'Teeming',       'Packs are {}% larger', 'PACK_SIZE', 0, 0, 0, 20, 35, 50, 1, 16, 100, '', 1, 15, 7),
(302, 'C02', 'Swarming',      'Packs hold {}% more lesser monsters', 'SWARM', 0, 0, 0, 30, 50, 80, 2, 16, 100, '', 1, 10, 5),
(303, 'C03', 'Elite',         '{}% more monsters are elite', 'ELITE_CHANCE', 0, 0, 0, 15, 25, 40, 4, 16, 100, '', 1, 10, 5),
(304, 'C04', 'Rare-infested', '{}% increased chance of a rare monster', 'RARE_CHANCE', 0, 0, 0, 25, 50, 100, 2, 16, 100, '', 1, 8, 20),
-- Boss
(401, 'B01', 'Colossal',      'The master has {}% more life', 'BOSS_LIFE', 0, 0, 0, 20, 35, 50, 1, 16, 100, 'boss_life', 1, 6, 3),
(402, 'B02', 'Overpowering',  'The master deals {}% more damage', 'BOSS_DAMAGE', 0, 0, 0, 15, 25, 35, 2, 16, 100, 'boss_dmg', 1, 6, 3),
(405, 'B05', 'Summoner',      'The master calls {} servants at 75%, 50% and 25% of its life', 'BOSS_ADDS', 0, 0, 0, 3, 4, 6, 3, 16, 100, 'boss_adds', 1, 8, 4);

-- -----------------------------------------------
-- The player affixes' passive spells (reserved range 90000-90499).
-- Attributes 0xC0 = passive + hidden; duration index 21 = until removed;
-- the amount is set by the module when it applies the aura.
-- -----------------------------------------------
DELETE FROM `spell_dbc` WHERE `ID` BETWEEN 90001 AND 90007 AND `Name_Lang_enUS` LIKE 'Atlas Affix: %';
INSERT INTO `spell_dbc` (
    `ID`, `Attributes`, `CastingTimeIndex`, `DurationIndex`, `RangeIndex`, `EquippedItemClass`,
    `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`, `ImplicitTargetA_1`, `EffectAura_1`, `EffectMiscValue_1`,
    `Effect_2`, `EffectDieSides_2`, `EffectBasePoints_2`, `ImplicitTargetA_2`, `EffectAura_2`, `EffectMiscValue_2`,
    `Name_Lang_enUS`
) VALUES
-- 118 SPELL_AURA_MOD_HEALING_PCT
(90001, 0x000000C0, 1, 21, 1, -1, 6, 1, 0, 1, 118, 0,   0, 0, 0, 0, 0, 0,  'Atlas Affix: of Grief'),
-- 87 SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, all schools
(90002, 0x000000C0, 1, 21, 1, -1, 6, 1, 0, 1, 87, 127,  0, 0, 0, 0, 0, 0,  'Atlas Affix: of Frailty'),
-- 79 SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, all schools
(90003, 0x000000C0, 1, 21, 1, -1, 6, 1, 0, 1, 79, 127,  0, 0, 0, 0, 0, 0,  'Atlas Affix: of Enfeeblement'),
-- 22 SPELL_AURA_MOD_RESISTANCE, all spell schools (126 = holy..arcane)
(90004, 0x000000C0, 1, 21, 1, -1, 6, 1, 0, 1, 22, 126,  0, 0, 0, 0, 0, 0,  'Atlas Affix: of Elemental Weakness'),
-- 33 SPELL_AURA_MOD_DECREASE_SPEED
(90005, 0x000000C0, 1, 21, 1, -1, 6, 1, 0, 1, 33, 0,    0, 0, 0, 0, 0, 0,  'Atlas Affix: of Lethargy'),
-- 110 SPELL_AURA_MOD_POWER_REGEN_PERCENT, mana
(90006, 0x000000C0, 1, 21, 1, -1, 6, 1, 0, 1, 110, 0,   0, 0, 0, 0, 0, 0,  'Atlas Affix: of Drought'),
-- 49 SPELL_AURA_MOD_DODGE_PERCENT and 47 SPELL_AURA_MOD_PARRY_PERCENT
(90007, 0x000000C0, 1, 21, 1, -1, 6, 1, 0, 1, 49, 0,    6, 1, 0, 1, 47, 0, 'Atlas Affix: of the Throne');
