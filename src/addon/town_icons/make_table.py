#!/usr/bin/env python3
"""Add-on town_icons (Phase 207, NOT original behavior): generates table.inc,
the icon cells of the town maps, from the original resources (dos/XEEN.CC and
dos/DARK.CC; never XEEN.DAT). Developer tool, run by hand:

    /tmp/xeen-py/bin/python -B src/addon/town_icons/make_table.py > src/addon/town_icons/table.inc

Sources, per town map (its maze0NNN.evt and maze0NNN.mob in the archive's
bank, the same members the game loads):
- event op 17 (DoTownEvent) kinds 0 bank, 1 blacksmith, 2 guild, 3 tavern,
  4 temple, 5 training; kind 12 (the pyramid) only where a map object stands
  on its cell (Castleview's pyramid; Shangri-La's invisible one is left out,
  owner decision); kind 8 (the Reaper at Ellinger's Tower) never (owner);
- event op 21 type 0 (p6 = 0): the mirror's word prompt;
- map objects with the tent sprites (Clouds 009/105, Darkside 045).
No town script moves (op 11) a tent, mirror or pyramid object (checked here).
The coordinates are the event frame's (a 32x32 town's x, y in 0..31).

Phase 309 (owner-approved additions, NOT original behavior):
- Castleview (Darkside 29) (12,10): a tower icon for Ellinger's Tower (the owner's cell; checked here: event
  op 17 kind 8 and map object sprite 047 stand on it). It replaces Phase 207's "no Reaper icon" decision.
- Skyroad (Darkside maps 89..112, "Skyroad A1".."Skyroad F4" by DARK.CC's dark0NNN.txt/darkxNNN.txt names;
  outdoor 16x16 maps): its tent objects (sprite 045; on map 107, Skyroad E3, only). An outdoor view shows
  the neighbouring maps' cells across the edges (1C680), so each tent is also listed in the coordinates of
  the 8 maps around its own (the dat files' N/E/S/W links; x + 16 east, y + 16 north): e.g. map 103 (D3)
  shows E3's (0,5) at (16,5), as D3's own object file lists it. Map files of maps 100.. are mazexNNN.*."""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/coverage'))
sys.path.insert(0, str(ROOT / 'tools/xeen_scenario'))
import quests as Q          # noqa: E402  (scripts(), records())
import xeen_data as X       # noqa: E402

TOWNS = {(0, 28): 'Vertigo', (0, 29): 'Nightshadow', (0, 30): 'Rivercity', (0, 31): 'Asp',
         (0, 32): 'Winterkill', (0, 49): 'Shangri-La', (0, 73): 'Newcastle', (0, 74): 'Newcastle (lower)',
         (1, 29): 'Castleview', (1, 31): 'Sandcaster', (1, 33): 'Lakeside', (1, 35): 'Necropolis',
         (1, 37): 'Olympus', (1, 43): 'Castleview castle (map 43)'}
KIND = {0: 'TI_BANK', 1: 'TI_BLACKSMITH', 2: 'TI_GUILD', 3: 'TI_TAVERN', 4: 'TI_TEMPLE', 5: 'TI_TRAINING'}
TENT = {0: (9, 105), 1: (45,)}
SKYROAD = range(89, 113)            # Darkside
TOWER = (1, 29, 12, 10)             # Castleview, Ellinger's Tower (owner, Phase 309)


def maze(kind, mp):
    return 'maze%c%03u.%s' % ('x' if mp >= 100 else '0', mp, kind)


def bank(world):
    cc = (ROOT / 'dos' / ('XEEN.CC', 'DARK.CC')[world]).read_bytes()
    d = X.directory(cc)
    start = len(d) * 8 + 2
    total = sum(e[2] for e in d[:world + 5])
    return bytes(b ^ 0x35 for b in cc[start:start + total])


def objects(world, mp):
    d = X.member(bank(world), maze('mob', mp), True)
    sprites, p, out = list(d[:16]), 48, []
    while p + 4 <= len(d) and d[p] != 0xff:
        x, y, i = d[p], d[p + 1], d[p + 2]
        out.append((x, y, sprites[i] if i < 16 else None))
        p += 4
    return out


def links(world, mp):
    """The dat file's N, E, S, W maps (0: none)."""
    import struct
    d = X.member(bank(world), maze('dat', mp), True)
    return struct.unpack_from('<4H', d, 512 + 256 + 2)


def neighbours(world, mp):
    """{map: (dx, dy)} of MP and the 8 maps around it: a cell (x, y) of MP is (x + dx, y + dy) there."""
    n, e, s, w = links(world, mp)
    out = {mp: (0, 0)}
    for m, d in ((n, (0, -16)), (e, (-16, 0)), (s, (0, 16)), (w, (16, 0))):
        if m:
            out[m] = d
    # the corners, through both sides (they must agree)
    for a, b, d in ((n, e, (-16, -16)), (n, w, (16, -16)), (s, e, (-16, 16)), (s, w, (16, 16))):
        via = {links(world, a)[(1, 3)[d[0] > 0]] if a else 0, links(world, b)[(0, 2)[d[1] > 0]] if b else 0} - {0}
        if len(via) > 1:
            raise SystemExit('map %d: the corner %s differs by its two sides: %s' % (mp, d, via))
        if via:
            out[via.pop()] = d
    return out


def skyroad_rows():
    rows = []
    for mp in SKYROAD:
        for x, y, s in objects(1, mp):
            if s in TENT[1] and 0 <= x < 16 and 0 <= y < 16:
                for m, (dx, dy) in neighbours(1, mp).items():
                    rows.append((1, m, x + dx, y + dy, 'TI_TENT_DARKSIDE'))
    return rows


def main():
    rows = []
    moved = {}
    for world, mp, name, off, payload in Q.scripts():
        if (world, mp) not in TOWNS:
            continue
        objs = objects(world, mp)
        cells = {(x, y) for x, y, s in objs}
        recs = Q.records(payload)[0]
        moved[(world, mp)] = {r[6] for _, r in recs if r[5] == 11}
        for _, r in recs:
            x, y, op = r[1], r[2], r[5]
            if op == 17 and r[6] in KIND:
                rows.append((world, mp, x, y, KIND[r[6]]))
            elif op == 17 and r[6] == 12 and (x, y) in cells:
                rows.append((world, mp, x, y, 'TI_PYRAMID'))
            elif op == 21 and r[6] == 0:
                rows.append((world, mp, x, y, 'TI_MIRROR'))
        for k, (x, y, s) in enumerate(objs):
            if s in TENT[world]:
                if k in moved[(world, mp)]:
                    raise SystemExit('a script moves tent object %d of map %d' % (k, mp))
                rows.append((world, mp, x, y, 'TI_TENT_CLOUDS' if world == 0 else 'TI_TENT_DARKSIDE'))
    for world, mp, name, off, payload in Q.scripts():
        if (world, mp) == TOWER[:2]:
            ops = [(r[5], r[6]) for _, r in Q.records(payload)[0] if (r[1], r[2]) == TOWER[2:]]
            if (17, 8) not in ops or (TOWER[2], TOWER[3], 47) not in objects(1, 29):
                raise SystemExit('Castleview (12,10) is not Ellinger\'s Tower (op 17 kind 8, object 047)')
    rows.append(TOWER + ('TI_TOWER',))
    for world, mp, x, y, kind in skyroad_rows():
        TOWNS.setdefault((world, mp), 'Skyroad (map %d)' % mp)
        rows.append((world, mp, x, y, kind))
    rows = sorted(set(rows))
    print('/* Generated by make_table.py from dos/XEEN.CC and dos/DARK.CC (Phase 207 add-on')
    print(' * town_icons, NOT original behavior; Phase 309: Castleview\'s tower, the Skyroad tents): {world (96E2),')
    print(' * map (ED99), x, y, kind}; an outdoor map\'s x, y may lie across its edges (-16..31). */')
    last = None
    for world, mp, x, y, kind in rows:
        if (world, mp) != last:
            print('/* %s: %s map %d */' % (TOWNS[(world, mp)], ('Clouds', 'Darkside')[world], mp))
            last = (world, mp)
        print('{%d,%d,%d,%d,%s},' % (world, mp, x, y, kind))


if __name__ == '__main__':
    main()
