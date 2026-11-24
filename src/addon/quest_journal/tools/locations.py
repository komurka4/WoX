#!/usr/bin/env python3
"""Quest and auto-note locations for the world map (design stage).
For every quest (56) and note (128): the original text, coordinates parsed from it, the script cells that
give / take / test it, the objective cells (items the quest's giver tests or takes), and each site's
world-map anchor (anchors.json). Writes data/quest_locations.json, data/note_locations.json."""
import json, re, sys, collections
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import extract as E
D = str(E.OUT)
facts = json.load(open(E.WORK / 'facts.json')); units = facts['units']
anchors = json.load(open(D + '/anchors.json'))
NAMES = {  # names used by qnotes.bin texts -> (world, map); towns from the entrance texts, rest from anchors.txt review
    (0, 'Vertigo'): 28, (0, 'Nightshadow'): 29, (0, 'Rivercity'): 30, (0, 'Asp'): 31, (0, 'Winterkill'): 32,
    (0, 'Castle Burlock'): 64, (0, 'Newcastle'): 73, (0, 'Warzone'): 85,
    (1, 'Castleview'): 29, (1, 'Sandcaster'): 31, (1, 'Lakeside'): 33, (1, 'Necropolis'): 35, (1, 'Olympus'): 37,
    (1, 'Castle Kalindra'): 41, (1, 'Dragon Tower'): 117}
SECTOR = re.compile(r'\b([A-F])([1-4])(?:,| at)\s*(\d+),\s*(\d+)')
NAMED = re.compile(r"(Vertigo|Nightshadow|Rivercity|Asp|Winterkill|Castle Burlock|Newcastle|Castleview|Sandcaster|"
                   r"Lakeside|Necropolis|Olympus|Castle Kalindra|Dragon Tower)(?: level (\d))?(?:,| at)\s*(\d+),\s*(\d+)")
def sector_map(col, row): return (ord(col) - 65) * 4 + int(row)
def anchor_of(world, mp, x=None, y=None):
    if 1 <= mp <= 24: return dict(world=world, map=mp, x=x, y=y, how='outdoor')
    a = anchors.get('%s%d' % ('CD'[world], mp), {}).get('anchor')
    return a
def parse(world, text):
    out = []
    for c, r, x, y in SECTOR.findall(text or ''):
        out.append(dict(world=world, map=sector_map(c, r), x=int(x), y=int(y), src='text %s%s' % (c, r)))
    for name, lvl, x, y in NAMED.findall(text or ''):
        mp = NAMES.get((world, name))
        if mp:
            if lvl and name == 'Castle Kalindra': mp = 41 + int(lvl) - 1
            out.append(dict(world=world, map=mp, x=int(x), y=int(y), src='text ' + name))
    return out
refs = collections.defaultdict(list)
for r in facts['refs']: refs[r['unit']].append(r)
cell_units = collections.defaultdict(set)
for r in facts['refs']:
    if r['world'] in (0, 1): cell_units[(r['world'], r['map'], r['x'], r['y'])].add((r['unit'], r['role']))
def site(r):
    return dict(world=r['world'], map=r['map'], x=r['x'], y=r['y'], anchor=anchor_of(r['world'], r['map'], r['x'], r['y']))
def build(kind):
    res = {}
    for uid, u in units.items():
        if u['kind'] != kind: continue
        w = 0 if u['world'] == 'C' else 1
        e = dict(id=uid, world=u['world'], text=u['name'], parsed=[], give=[], take=[], objective=[])
        for p in parse(w, str(u['name'])): p['anchor'] = anchor_of(p['world'], p['map'], p['x'], p['y']); e['parsed'].append(p)
        for r in refs[uid]:
            if r['world'] not in (0, 1): continue
            (e['give'] if r['role'] == 'give' else e['take'] if r['role'] == 'take' else []).append(site(r))
        if kind == 'quest':   # objective: items tested/taken in the take cell, given elsewhere
            for t in e['take']:
                for unit, role in cell_units[(t['world'], t['map'], t['x'], t['y'])]:
                    if unit.startswith('item') and role in ('take', 'test'):
                        for r in refs[unit]:
                            if r['role'] == 'give' and r['world'] in (0, 1) and (r['map'], r['x'], r['y']) != (t['map'], t['x'], t['y']):
                                s = site(r); s['item'] = units[unit]['name']; e['objective'].append(s)
        res[uid] = e
    return res
q = build('quest'); n = build('note')
json.dump(q, open(D + '/quest_locations.json', 'w'), indent=1); json.dump(n, open(D + '/note_locations.json', 'w'), indent=1)
def short(s):
    a = s.get('anchor'); return '%s%d(%s,%s)%s' % ('CD'[s['world']], s['map'], s['x'], s['y'],
        '' if not a or a.get('how') == 'outdoor' else '->%s%d(%s,%s)' % ('CD'[a['world']], a['map'], a['x'], a['y']))
with open(D + '/locations.txt', 'w') as f:
    for name, res in (('QUESTS', q), ('NOTES', n)):
        f.write('==== %s\n' % name)
        for uid, e in res.items():
            f.write('%s %s\n   text: %s\n   parsed: %s\n   give: %s\n   take: %s\n   objective: %s\n' % (uid, e['world'], str(e['text'])[:110],
                ' '.join(short(p) for p in e['parsed']), ' '.join(short(s) for s in e['give'][:4]),
                ' '.join(short(s) for s in e['take'][:4]), ' '.join(short(s) + '[' + s['item'][:20] + ']' for s in e['objective'][:5])))
print('quests with parsed coords', sum(1 for e in q.values() if e['parsed']), '/', len(q))
print('notes with parsed coords', sum(1 for e in n.values() if e['parsed']), '/', len(n), '; notes granted somewhere', sum(1 for e in n.values() if e['give']))
