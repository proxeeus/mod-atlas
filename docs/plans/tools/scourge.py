import sys, os, collections, csv, re
sys.path.insert(0, os.path.dirname(__file__))
from maps import D

CT = {r['entry']: r for r in D['creature_template']}
FT = {r['ID']: r for r in D['factiontemplate_dbc']}
SCOURGE_FACTIONS = {20}  # Faction.dbc 20 = Scourge; others found by inspection below
heroic = set()
for t in CT.values():
    for k in ('difficulty_entry_1', 'difficulty_entry_2', 'difficulty_entry_3'):
        if t[k]:
            heroic.add(t[k])
spawnmap = collections.defaultdict(collections.Counter)
for s in D['creature']:
    spawnmap[s['id1']][s['map']] += 1
spells = collections.defaultdict(set)
for r in D['creature_template_spell']:
    if r['Spell']:
        spells[r['CreatureID']].add(r['Spell'])
smart_casts = collections.defaultdict(set)
for r in D['smart_scripts']:
    if r['source_type'] == 0 and r['entryorguid'] > 0 and r['action_type'] == 11:  # SMART_ACTION_CAST
        smart_casts[r['entryorguid']].add(r['action_param1'])
models = collections.defaultdict(list)
for r in D['creature_template_model']:
    models[r['CreatureID']].append(r['CreatureDisplayID'])
texts = collections.Counter(r['CreatureID'] for r in D['creature_text'])

SRC = {533: 'Naxx', 329: 'Strat', 289: 'Scholo', 600: 'DTK', 601: 'AzjN', 619: 'AK', 595: 'CoS', 632: 'FoS',
       658: 'PoS', 668: 'HoR', 631: 'ICC', 575: 'UP', 574: 'UK', 129: 'RFD', 571: 'Northrend', 604: 'Gundrak',
       0: 'EK', 1: 'Kalimdor', 609: 'Ebon Hold'}
BAD_NAME = re.compile(r'trigger|bunny|dummy|credit|invisible|\bstalker\b|\(|test|\[|controller|vehicle|target|'
                      r'kill credit|bombard|teleport|portal|spawner|summoner', re.I)

ARCH = [('ghoul', r'ghoul|ghast|zombie|risen|reanimated (?!frost)|plagued|mindless|deathguard|corpse|rotting'),
        ('geist', r'geist|scavenger|ravenous|carrion|fleshripper|vargul'),
        ('abomination', r'abomination|flesh|stitched|patchwork|horror|bile|construct|golem|eruptor|sludge|ooze|plague beast|atrocity|monstrosity|bloated|gluttonous|mutated'),
        ('skeleton', r'skeleton|skeletal|bone|deathbringer|unholy|wretched|forgotten|frostbone|ymirjar|vrykul|ymirheim|deathwhisper'),
        ('caster', r'necro|cultist|acolyte|adherent|fanatic|initiate|apprentice|disciple|warlock|mage|priest|sorcer|shadowcaster|summoner|shaman|channeler|dark rune|cabalist|binder|darkcaller|deathspeaker|spellcaster|witch|lich|occultist|plaguebringer|ritualist|hexer|frostweaver|soulguard|soulweaver'),
        ('nerubian', r"anub|nerub|crypt fiend|crypt crawler|spider|swarm|webspinner|scarab|skirmisher|darter|watcher|guardian|venom|prime guard|underlord|buzzer|weaver"),
        ('gargoyle', r'gargoyle|wyrm|wyrmkin|drake|gryphon|bat|wing'),
        ('spirit', r"spirit|spectre|specter|banshee|wraith|shade|ghost|wailing|phantom|soul|val'kyr|valkyr|apparition|haunt|wight|shadow"),
        ('death knight', r'death knight|deathknight|deathstalker|dread|knight|champion|vanguard|commander|captain|lieutenant|darkfallen|vampir|san\'layn|deathblade|blood'),
        ('beast', r'hound|wolf|worg|bear|horse|charger|steed|rat|bug|beetle|insect|maggot|leech')]


def arche(name):
    for a, rx in ARCH:
        if re.search(rx, name, re.I):
            return a
    return 'other'


rows = []
for e, t in CT.items():
    if e in heroic or not spawnmap.get(e):
        continue
    ft = FT.get(t['faction'], {})
    fac = ft.get('Faction')
    sm = spawnmap[e]
    srcmaps = [m for m in sm if m in SRC]
    if not srcmaps:
        continue
    is_scourge = fac in SCOURGE_FACTIONS or (t['type'] == 6 and fac not in (0, None))
    if not is_scourge:
        continue
    if t['npcflag'] or (t['unit_flags'] & (0x02000000 | 0x00000100 | 0x00000200)) or (t['flags_extra'] & 0x80):
        continue  # npc, not selectable, immune to pc/npc, trigger
    if t['VehicleId'] or BAD_NAME.search(t['name'] or ''):
        continue
    lvl = t['maxlevel']
    if lvl < 70 and not set(srcmaps) & {329, 289, 129}:
        continue
    risk = 'cpp' if t['ScriptName'] else ('smart' if t['AIName'] == 'SmartAI' else 'plain')
    sp = spells.get(e, set()) | smart_casts.get(e, set())
    rows.append(dict(entry=e, name=t['name'], subname=t['subname'] or '', lvl=lvl, rank=t['rank'], cls=t['unit_class'],
                     type=t['type'], fac=fac, ftid=t['faction'], arche=arche(t['name']), risk=risk,
                     script=t['ScriptName'], nspells=len(sp), nmodels=len(models.get(e, [])),
                     model=(models.get(e) or [0])[0], texts=texts.get(e, 0),
                     maps='/'.join('%s:%d' % (SRC[m], sm[m]) for m in sorted(srcmaps, key=lambda m: -sm[m])),
                     spawns=sum(sm[m] for m in srcmaps)))

rows.sort(key=lambda r: (r['arche'], -r['lvl'], r['name']))
with open(os.path.join(os.path.dirname(__file__), 'scourge.csv'), 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader(); w.writerows(rows)
print(len(rows), 'candidates')
print(collections.Counter(r['arche'] for r in rows))
print(collections.Counter(r['risk'] for r in rows))
print(collections.Counter(r['rank'] for r in rows))
print(collections.Counter(r['fac'] for r in rows).most_common(15))
