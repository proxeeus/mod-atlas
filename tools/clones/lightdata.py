"""A clone map's own light (clones.json "looks"): a global Light.dbc row for the map, its LightParams and their
LightIntBand / LightFloatBand rows, copied from its source map's global light and turned into a thick fog whose
colour is the sky's. Client only (the server has no lights). A thick fog needs a fog end under 27.8 yd (fog mode 1
turns longer ones into a haze over the draw distance, DayNight::CalcColors). Layout per Wowdecomp2:
  - a map's global light: the Light row of that map at (0, 0, 0) with no falloff; m_lightParamsID[8] (normal,
    underwater, then shared sets);
  - bands: LightIntBand id = band + 18 * params - 17 (0 diffuse, 1 ambient, 2-6 sky top..smog, 7 fog, ...),
    LightFloatBand id = band + 6 * params - 5 (0 fog end, 1 fog start fraction, ...) (DayNight::LoadLightingBands);
  - a band row: ID, count, time[16], value[16]; fog end in 1/36 yd (the Black Morass's 4000 is about 111 yd).
"""
import struct

import clonedata
import dbc

DBCS = ('Light', 'LightParams', 'LightIntBand', 'LightFloatBand')
LIGHT_ID_RANGE = (2600, 2649)
PARAMS_ID_RANGE = (950, 999)
SKY_AND_FOG_BANDS = range(2, 8)


def _f32bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def _constant(row, value):
    """A band row holding one value all day."""
    out = list(row)
    out[1] = 1
    out[2:18] = [0] * 16
    out[18:34] = [value] + [0] * 15
    return out


def sources(client, dbc_dir, patch_name='patch-T.MPQ'):
    """{name: (Dbc, where)} of the four light DBCs as the client sees them without our patch."""
    out = {}
    for name in DBCS:
        raw, where = clonedata.effective_raw(client, dbc_dir, name + '.dbc', patch_name)
        out[name] = (dbc.Dbc.parse(raw), where)
    return out


def global_light(L, map_id):
    """The Light row of a map at (0, 0, 0): the light it uses where no local light reaches."""
    for r in L.rows:
        if r[1] == map_id and r[2] == 0 and r[3] == 0 and r[4] == 0:
            return r
    return None


def add_light(tables, map_id, source_map, light, params, look):
    """Give `map_id` its own global light: a copy of `source_map`'s global light (its normal and underwater params
    and their bands) turned into `look` (fog_end_yd, fog_start, fog_colour; the sky takes the fog's colour, no
    skybox model). `tables` is {name: Dbc}; returns {name: [added ids]}."""
    if not 10.0 <= look['fog_end_yd'] < 27.7:
        raise SystemExit('look fog_end_yd must be 10-27.7: from 27.8 on the client thins it into a haze')
    if not LIGHT_ID_RANGE[0] <= light <= LIGHT_ID_RANGE[1] or not PARAMS_ID_RANGE[0] <= params < PARAMS_ID_RANGE[1]:
        raise SystemExit('light ids outside %s / %s' % (LIGHT_ID_RANGE, PARAMS_ID_RANGE))
    L, P, I, F = (tables[n] for n in DBCS)
    if light in L.ids() or params in P.ids() or params + 1 in P.ids():
        raise SystemExit('Light.dbc / LightParams.dbc already have our ids (is an old patch-T.MPQ in the order?)')
    if any(r[1] == map_id for r in L.rows):
        raise SystemExit('Light.dbc already has a light for map %d' % map_id)
    # A map without a global light uses the client's default one, Light 1 (DayNight::LightLoad): copy that.
    src_light = global_light(L, source_map) or L.row(1)
    template, under = src_light[7], src_light[8] or src_light[7]

    fog = int(look['fog_colour'], 16)
    added = {n: [] for n in DBCS}
    for new, src in ((params, template), (params + 1, under)):
        row = list(P.row(src))
        row[0] = new
        if new == params:
            row[2] = 0                                    # no skybox model: the sky is the fog's colour
        P.rows.append(row)
        added['LightParams'].append(new)
        for b in range(18):
            r = list(I.row(b + 18 * src - 17))
            r[0] = b + 18 * new - 17
            if new == params and b in SKY_AND_FOG_BANDS:
                r = _constant(r, fog)
            I.rows.append(r)
            added['LightIntBand'].append(r[0])
        for b in range(6):
            r = list(F.row(b + 6 * src - 5))
            r[0] = b + 6 * new - 5
            if new == params and b == 0:
                r = _constant(r, _f32bits(look['fog_end_yd'] * 36.0))
            elif new == params and b == 1:
                r = _constant(r, _f32bits(look['fog_start']))
            F.rows.append(r)
            added['LightFloatBand'].append(r[0])
    shared = list(src_light)[9:15]                        # the source light's shared sets (storm, death, ...)
    L.rows.append([light, map_id, 0, 0, 0, 0, 0, params, params + 1] + shared)
    added['Light'].append(light)
    for t in (L, P, I, F):
        t.rows.sort(key=lambda r: r[0])
    return added


def check(archive, stock, added):
    """Each light DBC in the patch = the stock rows unchanged plus ours."""
    for name in DBCS:
        new = dbc.Dbc.parse(archive.read('DBFilesClient\\%s.dbc' % name))
        old = stock[name]
        old_rows = {r[0]: r for r in old.rows}
        new_rows = {r[0]: r for r in new.rows}
        if set(new_rows) - set(old_rows) != set(added[name]) or set(old_rows) - set(new_rows):
            raise SystemExit('%s.dbc: unexpected row set change' % name)
        if any(new_rows[i] != r for i, r in old_rows.items()):
            raise SystemExit('%s.dbc: a stock row changed' % name)
