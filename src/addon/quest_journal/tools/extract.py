#!/usr/bin/env python3
"""quest_journal generator, step 1 (developer tool; reads the protected originals read-only, never part of a build).
Writes $QJ_WORK/data/{records.json,teleports.json,items_context.txt,...}: the event scripts and teleports."""
import json, sys, collections, re, os
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]            # the repository
WORK = Path(os.environ['QJ_WORK'])                     # a scratch directory outside the repository
sys.path.insert(0, str(ROOT / 'tools/coverage')); sys.path.insert(0, str(ROOT / 'tools/xeen_scenario'))
import quests as Q, xeen_data as X
OUT = WORK / 'data'
OUT.mkdir(parents=True, exist_ok=True)
TEXT_OPS = {1: (6,), 2: (6,), 3: (6,), 4: (6, 7), 5: (6, 7), 21: (6, 7, 8), 39: (6, 7), 41: (6,), 49: (6, 7),
            50: (6,), 53: (6,), 55: (6, 7, 8)}
_cc = {}
def cc(world):
    if world not in _cc: _cc[world] = (ROOT / 'dos' / ('XEEN.CC', 'DARK.CC')[world]).read_bytes()
    return _cc[world]
_tx = {}
def texts(world, mp):
    k = (world, mp)
    if k not in _tx:
        name = 'aaze%c%03u.txt' % ('x' if mp > 99 else '0', mp)
        b = X.member(cc(world), name, False)
        _tx[k] = [Q.clean(t) for t in b.split(b'\0')] if b else []
    return _tx[k]
def decode():
    recs = []
    for world, mp, name, off, payload in Q.scripts():
        rs, _ = Q.records(payload)
        for p, r in rs:
            recs.append(dict(world=world, map=mp, x=r[1], y=r[2], dir=r[3], line=r[4], op=r[5],
                             par=list(r[6:]), name=Q.OPNAME[r[5]] if r[5] < len(Q.OPNAME) else '?'))
    return recs
def describe(r):
    t = texts(r['world'], r['map'])
    s = '%s %s' % (r['name'], ' '.join('%02x' % b for b in r['par']))
    if r['op'] in TEXT_OPS:
        for i in TEXT_OPS[r['op']]:
            k = i - 6
            if k < len(r['par']) and r['par'][k] < len(t):
                s += '\n            [t%d] %s' % (r['par'][k], t[r['par'][k]])
    return s
def main():
    recs = decode()
    json.dump(recs, open(OUT / 'records.json', 'w'))
    cells = collections.defaultdict(list)
    for r in recs: cells[(r['world'], r['map'], r['x'], r['y'])].append(r)
    tel = [dict(world=r['world'], src_map=r['map'], x=r['x'], y=r['y'], dir=r['dir'], dst_map=r['par'][0],
                dst_x=(r['par'][1] if len(r['par']) > 1 else None), dst_y=(r['par'][2] if len(r['par']) > 2 else None), op=r['op'])
           for r in recs if r['op'] in (7, 31, 52) and r['par'] and r['par'][0]]
    json.dump(tel, open(OUT / 'teleports.json', 'w'), indent=0)
    facts = json.load(open(WORK / 'facts.json'))
    byunit = collections.defaultdict(list)
    for r in facts['refs']: byunit[r['unit']].append(r)
    for kind in ('item', 'quest', 'note'):
        with open(OUT / f'{kind}s_context.txt', 'w') as f:
            for uid, u in facts['units'].items():
                if u['kind'] != kind: continue
                f.write('=' * 100 + '\n%s [%s] %s\n' % (uid, u['world'], u['name']))
                seen = set()
                for ref in byunit[uid]:
                    f.write('  %-5s %s op%d %s\n' % (ref['role'], ref['ref'], ref['op'], ref.get('note', '')))
                for ref in byunit[uid]:
                    key = (ref['world'], ref['map'], ref['x'], ref['y'])
                    if key in seen: continue
                    seen.add(key)
                    f.write('  -- cell %s map %d (%d,%d)\n' % ('CD'[key[0]], key[1], key[2], key[3]))
                    for r in sorted(cells[key], key=lambda r: (r['dir'], r['line'])):
                        f.write('     d%d L%02d %s\n' % (r['dir'], r['line'], describe(r)))
main()
