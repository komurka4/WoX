#!/usr/bin/env python3
"""Every map of both worlds -> its world-map anchor (outdoor map 1..24, x, y) + a heuristic name.
Sources (priority order): outdoor itself; inbound teleport/fall from an outdoor cell (op 7/31/52);
location-kind tables (native game_cutscene.c: towers kind 8, gates 10/13, guardians 9, sphinx 11,
pyramid kind 12 = cross-world); interior chain (BFS through inbound edges); exit target (interior
op 7 to an outdoor cell); Darkside sky maps 89..112 = map-88 same cell; Clouds fall table (game_fall.c).
Output data/anchors.json and data/anchors.txt (review)."""
import json, sys, re, collections, struct
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import extract as E
D = E.OUT
recs = json.load(open(D / 'records.json')); tel = json.load(open(D / 'teleports.json'))
cells = collections.defaultdict(list)
for r in recs: cells[(r['world'], r['map'], r['x'], r['y'])].append(r)
def cell_texts(w, m, x, y):
    t = E.texts(w, m); out = []
    for r in cells[(w, m, x, y)]:
        for i in E.TEXT_OPS.get(r['op'], ()):
            k = i - 6
            if k < len(r['par']) and r['par'][k] < len(t) and t[r['par'][k]] and t[r['par'][k]] not in out:
                out.append(t[r['par'][k]])
    return out
# location op 17 cells on outdoor maps: (world, map) -> [(x,y,kind)]
loc = collections.defaultdict(list)
for r in recs:
    if r['op'] == 17 and r['par']: loc[(r['world'], r['map'])].append((r['x'], r['y'], r['par'][0]))
# location tables (outdoor map -> destination map), from src/game/game_cutscene.c / game_inn.c
LOC_TABLE = {  # (world, src_map, kind) -> [(dst_world, dst_map)]
    (1, 3, 8): [(1, 0x39)], (1, 0xd, 8): [(1, 0x45)], (1, 0x10, 8): [(1, 0x3d)], (1, 0x17, 8): [(1, 0x41)], (1, 0x1d, 8): [(1, 0x35)],
    (0, 7, 8): [(1, 0x71)], (0, 0xc, 8): [(0, 0x37)], (0, 0xd, 8): [(1, 0x75)], (0, 0xf, 8): [(0, 0x3b)], (0, 0x18, 8): [(0, 0x33)],
    (1, 4, 13): [(1, 0x1d)], (1, 6, 13): [(1, 0x23)], (1, 0x13, 13): [(1, 0x1f)], (1, 0x16, 13): [(1, 0x21)],
    (1, 4, 10): [(1, 0x1d)], (1, 6, 10): [(1, 0x23)], (1, 0x13, 10): [(1, 0x1f)], (1, 0x16, 10): [(1, 0x21)],
    (0, 0xe, 10): [(0, 0x25)], (0, 0x12, 10): [(0, 0x23), (0, 0x24)], (0, 0x17, 10): [(0, 0x21), (0, 0x22)],
    (1, 0xc, 9): [(1, 0x49)], (1, 0xe, 9): [(1, 0x53)], (1, 0x13, 9): [(1, 0x79)], (1, 0x16, 9): [(1, 0x4e)],
    (0, 8, 9): [(0, 0x51)], (0, 0xc, 9): [(0, 0x50)], (0, 0x13, 9): [(1, 0x79)], (0, 0x14, 9): [(0, 0x4f)],
    (1, 2, 11): [(1, 0x7d)], (0, 5, 11): [(0, 0x52)],
    (1, 0x62, 13): [(1, 0x25)], (1, 0x62, 10): [(1, 0x25)],   # Olympus gate on sky map 98 (game_cutscene.c pass table)
}
inbound = collections.defaultdict(list)   # (w, dst) -> [(src_w, src_map, x, y, how)]
for t in tel:
    if t['dst_map'] != t['src_map']:
        inbound[(t['world'], t['dst_map'])].append((t['world'], t['src_map'], t['x'], t['y'], 'op%d' % t['op']))
for (w, m), ls in loc.items():
    for x, y, kind in ls:
        for dw, dm in LOC_TABLE.get((w, m, kind), []):
            inbound[(dw, dm)].append((w, m, x, y, 'kind%d' % kind))
exits = collections.defaultdict(list)
for t in tel:
    if not (1 <= t['src_map'] <= 24) and 1 <= t['dst_map'] <= 24:
        exits[(t['world'], t['src_map'])].append((t['world'], t['dst_map'], t['dst_x'], t['dst_y'], 'exit'))
CLOUDS_FALL = [(24,11,9),(12,6,15),(15,4,12)] + [(23,12,10)]*23 + [(24,11,9)]*4 + [(12,6,15)]*4 + [(15,4,12)]*4 + \
    [(23,12,10)]*2 + [(14,8,3)]*2 + [(23,12,10)]*2 + [(1,8,7)]*2 + [(23,12,10)]*3 + [(12,11,13)] + [(23,12,10)] + \
    [(15,4,12)]*3 + [(23,12,10)]*3 + [(5,12,7)]*3 + [(6,12,3)]
maps = sorted({(r['world'], r['map']) for r in recs} | set(inbound) | set(exits))
memo = {}
def resolve(w, m, depth=0, seen=frozenset()):
    if 1 <= m <= 24: return (w, m, None, None, 'outdoor', 0)
    if (w, m) in memo: return memo[(w, m)]
    cands = []
    for sw, sm, x, y, how in inbound.get((w, m), []):
        if 1 <= sm <= 24: cands.append((sw, sm, x, y, how, 1))
    if w == 1 and 89 <= m <= 112: return (1, m - 88, None, None, 'sky(map-88)', 1)
    for sw, sm, x, y, how in inbound.get((w, m), []):          # entered from a sky map: the cell beneath it
        if sw == 1 and 89 <= sm <= 112: cands.append((1, sm - 88, x, y, 'below sky %d %s' % (sm, how), 1))
    if True:
        for sw, sm, x, y, how in inbound.get((w, m), []):
            if 1 <= sm <= 24 or (sw == 1 and 89 <= sm <= 112): continue
            if (sw, sm) in seen or (sw, sm) == (w, m): continue
            a = resolve(sw, sm, depth + 1, seen | {(w, m)})
            if a: cands.append(a[:4] + ('via %s%d %s' % ('CD'[sw], sm, a[4]), a[5] + 1))
    FALLBACK = ('exit', 'sky(map-88)', 'clouds-fall-table')
    if cands and all(any(f in c[4] for f in FALLBACK) for c in cands) and False: pass
    if not cands and exits.get((w, m)):
        e = exits[(w, m)][0]; cands.append(e[:4] + ('exit', 1))
    if not cands and w == 1 and 89 <= m <= 112: cands.append((1, m - 88, None, None, 'sky(map-88)', 1))
    if not cands and w == 0 and 25 <= m <= 85:
        f = CLOUDS_FALL[m - 25]; cands.append((0, f[0], f[1], f[2], 'clouds-fall-table', 1))
    cands.sort(key=lambda c: (any(f in c[4] for f in ('exit', 'sky(map-88)', 'clouds-fall-table')), c[5]))
    if depth == 0: memo[(w, m)] = cands[0] if cands else None
    return cands[0] if cands else None
mirror = {}
cc = E.cc(1)
for name, w in (('xeenmirr.txt', 0), ('darkmirr.txt', 1)):
    b = E.X.member(cc, name, False)
    for k in range(0, len(b) - 31, 32):
        n = b[k:k + 28].split(b'\0')[0].decode('latin1'); mp, x, y, d = b[k + 28:k + 32]
        mirror.setdefault((w, mp), []).append((n, x, y))
def guess_name(w, m):
    if (w, m) in mirror:
        for n, x, y in mirror[(w, m)]:
            if not (1 <= m <= 24): return n.title()
    for sw, sm, x, y, how in inbound.get((w, m), []):
        for t in cell_texts(sw, sm, x, y):
            g = re.match(r"(?:Enter|Exit|Leave|Climb into|Go into) (?:the )?(.+?)\?", t) or re.match(r"(.+?)\. Enter\?", t)
            if g: return g.group(1)
    return ''
out = {}
for w, m in maps:
    a = resolve(w, m)
    out['%s%d' % ('CD'[w], m)] = dict(world=w, map=m, name=guess_name(w, m),
        anchor=None if not a else dict(world=a[0], map=a[1], x=a[2], y=a[3], how=a[4], hops=a[5]))
json.dump(out, open(D / 'anchors.json', 'w'), indent=1)
with open(D / 'anchors.txt', 'w') as f:
    for k, e in out.items():
        a = e['anchor']
        f.write('%-5s %-32s %s\n' % (k, e['name'][:32], '-' if not a else '%s%d (%s,%s) %s' % ('CD'[a['world']], a['map'], a['x'], a['y'], a['how'])))
print(sum(1 for e in out.values() if not e['anchor']), 'unresolved of', len(out))
