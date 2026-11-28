"""Per-map native creature census for the starter clones. Read-only: uses cache.pkl from maps.py."""
import sys, os, collections, re, subprocess, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from maps import D

SRCDIR = '/Users/ac-playerbots/azerothcore-playerbots/azerothcore-wotlk/src/server/scripts'
MAPS = {600: "Drak'Tharon Keep", 329: 'Stratholme', 36: 'The Deadmines', 602: 'Halls of Lightning',
        33: 'Shadowfang Keep'}
CT = {r['entry']: r for r in D['creature_template']}
FT = {r['ID']: r for r in D['factiontemplate_dbc']}
boss = {r['creditEntry'] for r in D['instance_encounters'] if r['creditType'] == 0}
tspells = collections.defaultdict(set)
for r in D['creature_template_spell']:
    if r['Spell']:
        tspells[r['CreatureID']].add(r['Spell'])
casts = collections.defaultdict(dict)   # entry -> spell -> name from comment
summons = collections.defaultdict(set)
instdata = set()
for r in D['smart_scripts']:
    if r['source_type'] != 0 or r['entryorguid'] <= 0:
        continue
    e = r['entryorguid']
    if r['action_type'] in (11, 85, 86, 134):  # CAST, SELF_CAST, CROSS_CAST, INVOKER_CAST
        m = re.search(r"Cast '([^']+)'", r['comment'] or '')
        casts[e][r['action_param1']] = m.group(1) if m else '?'
    if r['action_type'] in (12, 107):  # SUMMON_CREATURE, SUMMON_CREATURE_GROUP
        summons[e].add(r['action_param1'])
    if r['action_type'] == 34:  # SET_INST_DATA
        instdata.add(e)


def cpp(script):
    if not script:
        return ''
    out = subprocess.run(['grep', '-rlE', r'(CreatureScript\("%s"\)|Register\w*AI\(%s\))' % (script, script), SRCDIR],
                         capture_output=True, text=True).stdout.split()
    if not out:
        return 'missing'
    txt = open(out[0], encoding='utf-8', errors='replace').read()
    if re.search(r'Register\w+CreatureAI\(%s\)' % script, txt) and not re.search(r'RegisterCreatureAI\(%s\)' % script, txt):
        return 'factory'
    i = txt.find('"%s"' % script)
    seg = txt[i:i + 600] if i >= 0 else ''
    if re.search(r'Get\w+AI<', seg):
        return 'factory'
    return 'runs(%s)' % ('uses-instance' if re.search(r'GetInstanceScript|instance->|InstanceScript\s*\*', txt) else 'self')


out = {}
for m, mname in MAPS.items():
    cnt = collections.Counter(); pos = collections.defaultdict(list)
    for s in D['creature']:
        if s['map'] == m:
            cnt[s['id1']] += 1
            pos[s['id1']].append((s['position_x'], s['position_y'], s['position_z']))
    rows = []
    for e, n in cnt.items():
        t = CT.get(e)
        if not t:
            continue
        hostile = not (t['npcflag'] or (t['unit_flags'] & 0x02000300) or (t['flags_extra'] & 0x80))
        sp = {x: '' for x in tspells.get(e, ())}
        sp.update(casts.get(e, {}))
        rows.append(dict(entry=e, name=t['name'], n=n, lvl=t['maxlevel'], rank=t['rank'], cls=t['unit_class'],
                         type=t['type'], family=t['family'], fac=FT.get(t['faction'], {}).get('Faction'),
                         ftid=t['faction'], ai=t['AIName'], script=t['ScriptName'], cpp=cpp(t['ScriptName']),
                         boss=(e in boss or t['rank'] == 3), hostile=hostile, spells=sp,
                         summons=sorted(summons.get(e, ())), instdata=e in instdata,
                         cx=sum(p[0] for p in pos[e]) / n, cy=sum(p[1] for p in pos[e]) / n))
    rows.sort(key=lambda r: (not r['boss'], -r['n']))
    out[m] = rows
    print('=== %d %s' % (m, mname))
    for r in rows:
        flags = ('BOSS ' if r['boss'] else '') + ('' if r['hostile'] else 'NONHOSTILE ') + \
                ('INSTDATA ' if r['instdata'] else '')
        print('%6d %-32s x%-3d L%-2d r%d c%d t%d f%s %s %s %s %s sum=%s | %s' % (
            r['entry'], r['name'][:32], r['n'], r['lvl'], r['rank'], r['cls'], r['type'], r['fac'], r['ai'] or '-',
            r['script'] or '', r['cpp'], flags, r['summons'],
            ', '.join('%s:%s' % (k, v) for k, v in sorted(r['spells'].items()))[:160]))
json.dump({str(k): v for k, v in out.items()}, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'natives.json'), 'w'), indent=1)
