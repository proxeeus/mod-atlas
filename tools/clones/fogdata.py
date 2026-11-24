"""Atlas fogs (fogs.json): ScreenEffect.dbc rows (client only) and the aura-260 spells that carry them, the same
rows for the client's Spell.dbc and the server's spell_dbc. The spell is a copy of a stock screen-effect carrier
(Mist of the Kvaldir, 50203: infinite, can't be cancelled, kept through death) with our ID, name, tooltip and
effect, and removed on map change (also before a logout save), so the fog never outlives the run.
"""
import json
import os
import re

import clonedata
import dbc

SPELL_ID_RANGE = (90400, 90449)
SCREEN_EFFECT_ID_RANGE = (900, 949)
SCREEN_EFFECT_SWIRLING_FOG = 3
AURA_SCREEN_EFFECT = 260
SPELL_EFFECT_APPLY_AURA = 6
AURA_INTERRUPT_FLAG_CHANGE_MAP = 0x00080000

DBC_SCREEN_EFFECT = 'DBFilesClient\\ScreenEffect.dbc'
DBC_SPELL = 'DBFilesClient\\Spell.dbc'

# ScreenEffect.dbc, 10 fields (Wowdecomp2 DB/Rec/ScreenEffectRec.h).
SCREEN_EFFECT_FIELDS = [('ID', 'i'), ('Name', 's'), ('Effect', 'i'), ('Param0', 'u'), ('Param1', 'i'),
                        ('Param2', 'i'), ('Param3', 'i'), ('LightParamsID', 'i'), ('SoundAmbienceID', 'i'),
                        ('ZoneMusicID', 'i')]


def load_config(path=None):
    with open(path or os.path.join(clonedata.HERE, 'fogs.json')) as f:
        cfg = json.load(f)
    spells, effects = set(), set()
    for fog in cfg['fogs']:
        if not SPELL_ID_RANGE[0] <= fog['spell'] <= SPELL_ID_RANGE[1]:
            raise ValueError('fog spell %d outside %d-%d' % (fog['spell'], *SPELL_ID_RANGE))
        if not SCREEN_EFFECT_ID_RANGE[0] <= fog['effect'] <= SCREEN_EFFECT_ID_RANGE[1]:
            raise ValueError('fog %d: screen effect %d outside %d-%d' % (fog['spell'], fog['effect'],
                                                                         *SCREEN_EFFECT_ID_RANGE))
        if fog['spell'] in spells or fog['effect'] in effects:
            raise ValueError('fog %d: duplicate spell or screen effect ID' % fog['spell'])
        if not 1 <= fog['spread'] <= 255 or not 0 <= fog['strength'] <= 100:
            raise ValueError('fog %d: spread 1-255, strength 0-100' % fog['spell'])
        int(fog['colour'], 16)
        spells.add(fog['spell'])
        effects.add(fog['effect'])
    return cfg


def spell_fields():
    """[(column, kind)] of spell_dbc, in Spell.dbc field order (data/sql/base/db_world/spell_dbc.sql)."""
    path = os.path.join(clonedata.REPO, 'data', 'sql', 'base', 'db_world', 'spell_dbc.sql')
    with open(path, encoding='utf8') as f:
        body = re.search(r"CREATE TABLE `spell_dbc` \((.*?)\n\)", f.read(), re.S).group(1)
    fields = []
    for name, decl in re.findall(r"^\s*`([^`]+)`\s+([^,\n]+)", body, re.M):
        decl = decl.lower()
        if decl.startswith(('varchar', 'text', 'char')):
            kind = 's'
        elif decl.startswith(('float', 'double')):
            kind = 'f'
        elif 'unsigned' in decl:
            kind = 'u'
        else:
            kind = 'i'
        fields.append((name, kind))
    if len(fields) != 234:
        raise SystemExit('spell_dbc has %d columns, expected 234' % len(fields))
    return fields


def add_fogs(screen_dbc, spell_dbc, cfg):
    """Append each fog's ScreenEffect and Spell rows. Returns [(fog, effect_row, spell_row)]."""
    fields = spell_fields()
    col = {name: i for i, (name, _) in enumerate(fields)}
    template = spell_dbc.row(cfg['template_spell'])
    if not template:
        raise SystemExit('template spell %d not in Spell.dbc' % cfg['template_spell'])
    if template[col['Effect_1']] != SPELL_EFFECT_APPLY_AURA or template[col['EffectAura_1']] != AURA_SCREEN_EFFECT:
        raise SystemExit('template spell %d does not apply aura 260 in its first effect' % cfg['template_spell'])

    screen_ids, spell_ids = screen_dbc.ids(), spell_dbc.ids()
    added = []
    for fog in cfg['fogs']:
        if fog['effect'] in screen_ids:
            raise SystemExit('ScreenEffect.dbc already has a row %d' % fog['effect'])
        if fog['spell'] in spell_ids:
            raise SystemExit('Spell.dbc already has a row %d (is an old patch-T.MPQ in the order?)' % fog['spell'])
        effect = [fog['effect'], screen_dbc.add_string('Atlas - ' + fog['name']), SCREEN_EFFECT_SWIRLING_FOG,
                  int(fog['colour'], 16), fog['spread'], fog['strength'], 0, -1, 0, 0]
        screen_dbc.rows.append(effect)

        spell = list(template)
        spell[col['ID']] = fog['spell']
        spell[col['EffectMiscValue_1']] = fog['effect']
        spell[col['AuraInterruptFlags']] |= AURA_INTERRUPT_FLAG_CHANGE_MAP
        for name, kind in fields:
            if kind == 's':
                spell[col[name]] = 0
        spell[col['Name_Lang_enUS']] = spell_dbc.add_string(fog['name'])
        spell[col['AuraDescription_Lang_enUS']] = spell_dbc.add_string(fog['aura'])
        spell_dbc.append(spell)
        added.append((fog, effect, spell))
    screen_dbc.rows.sort(key=lambda r: r[0])
    return added


def decode_spell(spell_dbc, row, fields=None):
    return clonedata.decode(spell_dbc, row, fields or spell_fields())
