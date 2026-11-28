"""Catalogue of client lights (fog, tint) and screen effects for echo atmosphere.

Usage: python3 -I atmosphere.py <dbc dir> <out dir>
Needs Light, LightParams, LightFloatBand, LightIntBand, LightSkybox, ScreenEffect,
Spell, SpellDuration, Map and WorldMapArea .dbc files (3.3.5a). Writes atmosphere_lights.csv
and atmosphere_screen_effects.csv.
"""
import colorsys
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dbc import load, f32  # noqa: E402

src, out = sys.argv[1], sys.argv[2]
D = lambda name: os.path.join(src, name + '.dbc')

maps, mstr = load(D('Map'))
MAP = {r[0]: {'dir': mstr(r[1]), 'type': r[2], 'name': mstr(r[5])} for r in maps}
TYPE = {0: 'world', 1: 'dungeon', 2: 'raid', 3: 'battleground', 4: 'arena'}

lights, _ = load(D('Light'))
params, _ = load(D('LightParams'))
PARAMS = {r[0]: r for r in params}
fbands, _ = load(D('LightFloatBand'))
FBAND = {r[0]: r for r in fbands}
ibands, _ = load(D('LightIntBand'))
IBAND = {r[0]: r for r in ibands}
sky, skystr = load(D('LightSkybox'))
SKY = {r[0]: skystr(r[1]) for r in sky}


def band_values(rec, as_float):
    n = rec[1]
    times = rec[2:2 + 16][:n]
    data = rec[18:18 + 16][:n]
    if as_float:
        data = [f32(v) for v in data]
    return list(zip(times, data))


def at_noon(values):
    # Times are half-minutes (0..2880); take the key at or before noon.
    best = values[0][1] if values else 0
    for t, v in values:
        if t <= 1440:
            best = v
    return best


def fog(params_id):
    """Fog end (yards, at noon and lowest of the day) and fog colour at noon."""
    fe = FBAND.get(params_id * 6 - 5)
    fc = IBAND.get(params_id * 18 - 17 + 7)
    sk = IBAND.get(params_id * 18 - 17 + 2)
    ends = band_values(fe, True) if fe else []
    noon = at_noon(ends) / 36.0 if ends else 0
    low = min(v for _, v in ends) / 36.0 if ends else 0
    colour = at_noon(band_values(fc, False)) if fc else 0
    skytop = at_noon(band_values(sk, False)) if sk else 0
    return noon, low, colour, skytop


def rgb(c):
    return (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF


def tint(c):
    r, g, b = rgb(c)
    h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
    if l < 0.08:
        return 'black'
    if s < 0.15:
        return 'grey' if l < 0.7 else 'white'
    deg = h * 360
    for limit, name in ((15, 'red'), (40, 'orange'), (65, 'yellow'), (160, 'green'), (200, 'teal'),
                        (255, 'blue'), (290, 'violet'), (335, 'magenta'), (361, 'red')):
        if deg < limit:
            return ('dark ' if l < 0.25 else '') + name


# Zone rectangles, to name the zone a world light sits in.
wma, wstr = load(D('WorldMapArea'))
ZONES = [(r[1], wstr(r[3]), f32(r[4]), f32(r[5]), f32(r[6]), f32(r[7])) for r in wma if r[2]]


def zone_of(mapid, wx, wy):
    best = None
    for m, name, left, right, top, bottom in ZONES:
        if m == mapid and bottom <= wx <= top and right <= wy <= left:
            size = (top - bottom) * (left - right)
            if best is None or size < best[0]:
                best = (size, name)
    return best[1] if best else ''


rows = []
for r in lights:
    lid, mapid = r[0], r[1]
    x, y, z = f32(r[2]), f32(r[3]), f32(r[4])
    fall_start, fall_end = f32(r[5]), f32(r[6])
    clear = r[7]
    if clear not in PARAMS:
        continue
    noon, low, colour, skytop = fog(clear)
    p = PARAMS[clear]
    m = MAP.get(mapid, {'name': '?', 'type': -1, 'dir': '?'})
    zone = ''
    if m['type'] == 0 and not (x == 0 and y == 0 and z == 0):
        # World maps: the client's transform (offset 51200/3) gives world X/Y.
        zone = zone_of(mapid, 51200 / 3 - z / 36, 51200 / 3 - x / 36)
    rows.append({
        'light': lid, 'map': mapid, 'map_name': m['name'], 'map_type': TYPE.get(m['type'], '?'), 'zone': zone,
        'global': int(x == 0 and y == 0 and z == 0),  # MapMgr's default-light test 'x': round(x / 36, 1), 'y': round(y / 36, 1), 'z': round(z / 36, 1),
        # The client leaves global lights untransformed (DB/DBClient/Lights.cpp TransformMapCoord):
        # their falloff stays raw and centres on the map origin. Area lights are /36.
        'radius_yd': round(fall_end if (x == 0 and y == 0 and z == 0) else fall_end / 36, 1),
        'anchor_ok': int(x == 0 and y == 0 and z == 0 and fall_end >= 20000), 'params': clear,
        'fog_end_noon_yd': round(noon, 1), 'fog_end_min_yd': round(low, 1),
        'fog_rgb': '#%02X%02X%02X' % rgb(colour), 'fog_tint': tint(colour),
        'sky_rgb': '#%02X%02X%02X' % rgb(skytop), 'skybox': SKY.get(p[2], ''),
        'glow': round(f32(p[3]), 2),
    })

with open(os.path.join(out, 'atmosphere_lights.csv'), 'w', newline='') as fh:
    w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)

# Screen effects and the client spells that carry them (aura 260).
effects, estr = load(D('ScreenEffect'))
spells, sstr = load(D('Spell'))
durs, _ = load(D('SpellDuration'))
DUR = {r[0]: struct.unpack('<i', struct.pack('<I', r[1]))[0] for r in durs}
EFFECT_TYPE = {0: 'none', 1: 'death (grey world)', 2: 'netherworld fog', 3: 'swirling fog / glow'}
# SPELL_ATTR0_PASSIVE, SPELL_ATTR0_DO_NOT_DISPLAY (no aura icon), SPELL_ATTR0_DO_NOT_LOG
ATTR = [(0x40, 'passive'), (0x80, 'no-icon'), (0x100, 'unlogged')]

carriers = {}
for r in spells:
    for j in range(3):
        if r[95 + j] == 260:
            others = [(r[71 + k], r[95 + k]) for k in range(3) if k != j and r[71 + k]]
            flags = [n for bit, n in ATTR if r[4] & bit]
            carriers.setdefault(r[110 + j], []).append({
                'spell': r[0], 'name': sstr(r[136]), 'flags': '+'.join(flags) or '-',
                'other_effects': ' '.join('e%d/a%d' % e for e in others) or '-',
                'duration_ms': DUR.get(r[40], 0),
            })

erows = []
for r in effects:
    sid = r[0]
    signed = [struct.unpack('<i', struct.pack('<I', v))[0] for v in r[3:7]]
    for c in carriers.get(sid, [{}]):
        erows.append({
            'effect': sid, 'name': estr(r[1]), 'type': EFFECT_TYPE.get(r[2], r[2]),
            'param0': '#%08X' % r[3] if r[2] == 3 else signed[0], 'param1': signed[1],
            'param2': signed[2], 'param3': signed[3],
            'light_params': struct.unpack('<i', struct.pack('<I', r[7]))[0],
            'ambience': struct.unpack('<i', struct.pack('<I', r[8]))[0],
            'music': struct.unpack('<i', struct.pack('<I', r[9]))[0],
            'spell': c.get('spell', ''), 'spell_name': c.get('name', ''), 'spell_flags': c.get('flags', ''),
            'spell_other_effects': c.get('other_effects', ''), 'spell_duration_ms': c.get('duration_ms', ''),
        })

with open(os.path.join(out, 'atmosphere_screen_effects.csv'), 'w', newline='') as fh:
    w = csv.DictWriter(fh, fieldnames=list(erows[0].keys()))
    w.writeheader()
    w.writerows(erows)

print(len(rows), 'lights,', len(effects), 'screen effects,', sum(len(v) for v in carriers.values()), 'carrier spells')
