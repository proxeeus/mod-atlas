import sys, os, collections, statistics, json
sys.path.insert(0, os.path.dirname(__file__))
from maps import D

NAMES = {30:'Alterac Valley',33:'Shadowfang Keep',34:'The Stockade',36:'The Deadmines',43:'Wailing Caverns',
 47:'Razorfen Kraul',48:'Blackfathom Deeps',70:'Uldaman',90:'Gnomeregan',109:'Sunken Temple',129:'Razorfen Downs',
 189:'Scarlet Monastery',209:"Zul'Farrak",229:'Blackrock Spire',230:'Blackrock Depths',249:"Onyxia's Lair",
 269:'Black Morass',289:'Scholomance',309:"Zul'Gurub",329:'Stratholme',349:'Maraudon',389:'Ragefire Chasm',
 409:'Molten Core',469:'Blackwing Lair',489:'Warsong Gulch',509:"Ruins of Ahn'Qiraj",529:'Arathi Basin',
 531:"Temple of Ahn'Qiraj",532:'Karazhan',533:'Naxxramas',534:'Hyjal Summit',540:'Shattered Halls',
 542:'Blood Furnace',543:'Hellfire Ramparts',544:"Magtheridon's Lair",545:'Steamvault',546:'Underbog',
 547:'Slave Pens',548:'Serpentshrine Cavern',550:'Tempest Keep (The Eye)',552:'Arcatraz',553:'Botanica',
 554:'Mechanar',555:'Shadow Labyrinth',556:'Sethekk Halls',557:'Mana-Tombs',558:'Auchenai Crypts',
 559:'Nagrand Arena',560:'Old Hillsbrad Foothills',562:"Blade's Edge Arena",564:'Black Temple',565:"Gruul's Lair",
 566:'Eye of the Storm',568:"Zul'Aman",572:'Ruins of Lordaeron',574:'Utgarde Keep',575:'Utgarde Pinnacle',
 576:'The Nexus',578:'The Oculus',580:'Sunwell Plateau',585:"Magisters' Terrace",595:'Culling of Stratholme',
 599:'Halls of Stone',600:"Drak'Tharon Keep",601:'Azjol-Nerub',602:'Halls of Lightning',603:'Ulduar',
 604:'Gundrak',607:'Strand of the Ancients',608:'Violet Hold',615:'Obsidian Sanctum',616:'Eye of Eternity',
 617:'Dalaran Sewers',618:'Ring of Valor',619:"Ahn'kahet",624:'Vault of Archavon',628:'Isle of Conquest',
 631:'Icecrown Citadel',632:'Forge of Souls',649:'Trial of the Crusader',650:'Trial of the Champion',
 658:'Pit of Saron',668:'Halls of Reflection',724:'Ruby Sanctum',35:'Stormwind Vault',44:'Old SM',169:'Emerald Dream',
 13:'Testing',25:'Scott Test',37:'Azshara Crater',42:'Collin Test'}

CT = {r['entry']: r for r in D['creature_template']}
boss_entries = {r['creditEntry'] for r in D['instance_encounters'] if r['creditType'] == 0}
ent = collections.defaultdict(list)
for r in D['areatrigger_teleport']:
    ent[r['target_map']].append(r)
spawns = collections.defaultdict(list)
for r in D['creature']:
    spawns[r['map']].append(r)

out = []
for it in sorted(D['instance_template'], key=lambda r: r['map']):
    m = it['map']
    S = spawns.get(m, [])
    xs = [s['position_x'] for s in S]; ys = [s['position_y'] for s in S]; zs = [s['position_z'] for s in S]
    lv = []; elites = 0; bosses = set(); veh = 0; cpp = set(); smart = 0; types = collections.Counter(); zones = collections.Counter()
    hostile = 0
    for s in S:
        t = CT.get(s['id1'])
        if not t: continue
        if t['npcflag'] or (t['unit_flags'] & 0x02000000) or (t['flags_extra'] & 0x80):  # npc / not selectable / trigger
            continue
        hostile += 1
        lv.append(t['maxlevel'])
        if t['rank'] in (1, 2): elites += 1
        if t['rank'] == 3 or t['entry'] in boss_entries: bosses.add(t['entry'])
        if t['VehicleId']: veh += 1
        if t['ScriptName']: cpp.add(t['entry'])
        if t['AIName'] == 'SmartAI': smart += 1
        types[t['type']] += 1
        zones[s['zoneId']] += 1
    e = ent.get(m, [{}])[0]
    out.append(dict(map=m, name=NAMES.get(m, '?'), script=it['script'], spawns=hostile, distinct=len({s['id1'] for s in S}),
        elites=elites, bosses=len(bosses), boss_names=sorted({CT[b]['name'] for b in bosses})[:12],
        lvl=(min(lv), int(statistics.median(lv)), max(lv)) if lv else None,
        span=(round(max(xs)-min(xs)), round(max(ys)-min(ys)), round(max(zs)-min(zs))) if S else None,
        vehicles=veh, cpp_entries=len(cpp), smart=smart, types=dict(types.most_common(4)), zone=zones.most_common(1),
        entrance=(e.get('Name'), e.get('target_position_x'), e.get('target_position_y'), e.get('target_position_z'), e.get('target_orientation'))))
json.dump(out, open(os.path.join(os.path.dirname(__file__), 'mapstats.json'), 'w'), indent=1)
for o in out:
    print(o['map'], o['name'], '| sp', o['spawns'], 'el', o['elites'], 'boss', o['bosses'], 'lvl', o['lvl'], 'span', o['span'],
          'veh', o['vehicles'], 'cpp', o['cpp_entries'], 'smart', o['smart'], 'zone', o['zone'], '| ent', o['entrance'][0])
