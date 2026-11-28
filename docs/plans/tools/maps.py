import sys, collections, json, pickle, os
sys.path.insert(0, os.path.dirname(__file__))
from sqlparse import load
B = '/Users/ac-playerbots/azerothcore-playerbots/azerothcore-wotlk/data/sql/base/db_world/'
CACHE = os.path.join(os.path.dirname(__file__), 'cache.pkl')

if os.path.exists(CACHE):
    D = pickle.load(open(CACHE, 'rb'))
else:
    D = {}
    for t in ['instance_template', 'areatrigger_teleport', 'instance_encounters', 'creature_template',
              'creature', 'creature_template_model', 'factiontemplate_dbc', 'faction_dbc', 'spell_area',
              'creature_template_spell', 'smart_scripts', 'vehicle_template_accessory', 'creature_addon',
              'creature_template_addon', 'transports', 'creature_text']:
        D[t] = load(B + t + '.sql')[1]
        print(t, len(D[t]), file=sys.stderr)
    pickle.dump(D, open(CACHE, 'wb'))
