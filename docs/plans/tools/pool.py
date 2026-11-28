import sys, os, csv, collections, re, subprocess
sys.path.insert(0, os.path.dirname(__file__))
from maps import D
HERE = os.path.dirname(__file__)
SRCDIR = '/Users/ac-playerbots/azerothcore-playerbots/azerothcore-wotlk/src/server/scripts'
R = list(csv.DictReader(open(os.path.join(HERE, 'scourge.csv'))))
boss = {r['creditEntry'] for r in D['instance_encounters'] if r['creditType'] == 0}


def cpp_risk(script):
    """Find the C++ script registering this name; flag instance-script dependence."""
    try:
        out = subprocess.run(['grep', '-rln', '"%s"' % script, SRCDIR], capture_output=True, text=True).stdout.split()
    except Exception:
        return '?', ''
    if not out:
        return 'missing', ''
    f = out[0]
    txt = open(f, encoding='utf-8', errors='replace').read()
    # crude: locate the struct/class around the name and look for instance usage in the file section
    i = txt.find('"%s"' % script)
    m = re.search(r'(?:struct|class)\s+(\w+)', txt[max(0, i - 200):i + 200])
    cls = None
    reg = re.search(r'RegisterCreatureAI\((\w+)\)|new\s+(\w+)\(\)', txt[max(0, i - 200):i + 200])
    body = txt
    if reg:
        cls = reg.group(1) or reg.group(2)
        j = txt.find('struct ' + cls)
        if j < 0:
            j = txt.find('class ' + cls)
        if j >= 0:
            k = txt.find('\n};', j)
            body = txt[j:k]
    uses = bool(re.search(r'GetInstanceScript|instance->|InstanceScript\s*\*|me->GetInstanceScript|pInstance', body))
    return ('instance' if uses else 'self'), os.path.relpath(f, SRCDIR)


pool = []
for r in R:
    e = int(r['entry'])
    inst = not r['maps'].startswith(('Northrend', 'EK', 'Kalimdor', 'Ebon Hold'))
    r['boss'] = e in boss or r['rank'] == '3'
    r['named'] = int(r['spawns']) <= 2
    if r['risk'] == 'cpp':
        r['cpp'], r['cppfile'] = cpp_risk(r['script'])
    else:
        r['cpp'], r['cppfile'] = '', ''
    pool.append(r)

with open(os.path.join(HERE, 'pool.csv'), 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(pool[0].keys()))
    w.writeheader(); w.writerows(pool)
print(collections.Counter((r['risk'], r['cpp']) for r in pool))
for r in pool:
    if r['risk'] == 'cpp':
        print(r['entry'], r['name'], r['lvl'], r['rank'], r['maps'], r['script'], r['cpp'], r['cppfile'])
