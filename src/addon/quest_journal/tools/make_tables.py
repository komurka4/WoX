#!/usr/bin/env python3
"""quest_journal generator, last step (developer tool, run deliberately; never a build step: Phase 184).

Run from the repository root (QJ_WORK = a scratch directory outside the repository; facts.json there is the
original inventory written by tools/coverage/quests.py; every step reads the protected originals read-only):
    export QJ_WORK=/tmp/qj_work; mkdir -p $QJ_WORK
    python3 -B tools/coverage/quests.py facts $QJ_WORK/facts.json
    python3 -B src/addon/quest_journal/tools/extract.py
    python3 -B src/addon/quest_journal/tools/anchors.py
    python3 -B src/addon/quest_journal/tools/locations.py
    python3 -B src/addon/quest_journal/tools/make_tables.py
(-B: tools/coverage/__pycache__ holds tracked .pyc files that a plain run rewrites.)

Outputs (committed):
  src/addon/quest_journal/targets.inc       quest/note -> world-map target (world, world cell x/y, kind, label)
  src/addon/quest_journal/descriptions.inc  quest item -> description text(s) + selecting quest bit
  src/addon/world_map/anchors.inc           interior map -> its entrance cell on the outdoor grid
and $QJ_WORK/targets_review.md (every row with its derivation).

Target rules (owner decisions, Phase 247):
  quest: the NPC only: the cell that takes (completes) the quest bit - the NPC you report back to (the
         coordinates the original quest text names are this NPC in 43/43 parsed cases); else the giver.
  note:  coordinates parsed from the note text; else the cell that grants the note when the note is about
         that place (passwords, mines, plaques, runes, lessons, combinations); none for riddles/rhymes/hints.
  interior maps resolve to their entrance on the outdoor grid (anchors.json); the world is the anchor's world
  (Dragon/Darkstone Tower and the Dungeon of Death are Darkside map numbers standing on the Clouds grid)."""
import json, sys, re, collections
import os
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import extract as E
D = str(E.OUT); ADDON = os.path.dirname(HERE); WM = os.path.join(os.path.dirname(ADDON), 'world_map')
q = json.load(open(D + '/quest_locations.json')); n = json.load(open(D + '/note_locations.json'))
anchors = json.load(open(D + '/anchors.json')); recs = json.load(open(D + '/records.json'))
from descriptions import ITEMS
cells = collections.defaultdict(list)
for r in recs: cells[(r['world'], r['map'], r['x'], r['y'])].append(r)
def npc_name(w, mp, x, y):
    t = E.texts(w, mp)
    for r in cells[(w, mp, x, y)]:
        if r['op'] == 5 and r['par'] and r['par'][0] < len(t) and t[r['par'][0]]: return t[r['par'][0]]
    return None
PLACE = {k: e['name'] for k, e in anchors.items() if e.get('name')}
PLACE.update({'C51': "Witch's Tower", 'C52': "Witch's Tower", 'C53': "Witch's Tower", 'C54': "Witch's Tower",
              'C55': 'Tower of High Magic', 'C56': 'Tower of High Magic', 'C57': 'Tower of High Magic', 'C58': 'Tower of High Magic',
              'C59': "Darzog's Tower", 'C60': "Darzog's Tower", 'C61': "Darzog's Tower", 'C62': "Darzog's Tower",
              'C63': 'Castle Burlock', 'C65': 'Castle Burlock', 'C66': 'Castle Burlock', 'C67': 'Castle Basenji',
              'C69': 'Castle Basenji', 'C70': 'Castle Basenji', 'C71': 'Newcastle', 'C72': 'Newcastle', 'C73': 'Newcastle',
              'C74': 'Newcastle', 'C75': 'Castle Xeen', 'C76': 'Castle Xeen', 'C77': 'Castle Xeen', 'C78': 'Castle Xeen',
              'C79': 'Temple of Yak', 'C80': 'Dungeon of Terror', 'C81': 'Golem Dungeon', 'C82': 'Sphinx',
              'C83': 'Sphinx', 'C84': 'Sphinx', 'D30': 'Castleview', 'D29': 'Castleview', 'D31': 'Sandcaster',
              'D32': 'Sandcaster', 'D33': 'Lakeside', 'D34': 'Lakeside', 'D35': 'Necropolis', 'D36': 'Necropolis',
              'D37': 'Olympus', 'D38': 'Olympus', 'D42': 'Castle Kalindra', 'D43': 'Castle Kalindra', 'D44': 'Castle Kalindra',
              'D45': 'Castle Blackfang', 'D46': 'Castle Blackfang', 'D47': 'Castle Blackfang', 'D48': 'Castle Blackfang',
              'D50': 'Castle Alamar', 'D51': 'Castle Alamar', 'D52': 'Castle Alamar', 'D53': "Ellinger's Tower",
              'D54': "Ellinger's Tower", 'D55': "Ellinger's Tower", 'D56': "Ellinger's Tower", 'D57': 'Great Western Tower',
              'D58': 'Great Western Tower', 'D59': 'Great Western Tower', 'D60': 'Great Western Tower',
              'D61': 'Great Southern Tower', 'D62': 'Great Southern Tower', 'D63': 'Great Southern Tower', 'D64': 'Great Southern Tower',
              'D65': 'Great Eastern Tower', 'D66': 'Great Eastern Tower', 'D67': 'Great Eastern Tower', 'D68': 'Great Eastern Tower',
              'D69': 'Great Northern Tower', 'D70': 'Great Northern Tower', 'D71': 'Great Northern Tower', 'D72': 'Great Northern Tower',
              'D73': 'Temple of Bark', 'D74': 'Temple of Bark', 'D75': 'Temple of Bark', 'D76': 'Temple of Bark', 'D77': 'Temple of Bark',
              'D78': 'Dungeon of Lost Souls', 'D79': 'Dungeon of Lost Souls', 'D80': 'Dungeon of Lost Souls',
              'D81': 'Dungeon of Lost Souls', 'D82': 'Dungeon of Lost Souls', 'D83': 'Great Pyramid', 'D84': 'Great Pyramid',
              'D85': 'Great Pyramid', 'D113': 'Darkstone Tower', 'D114': 'Darkstone Tower', 'D115': 'Darkstone Tower',
              'D116': 'Darkstone Tower', 'D117': 'Dragon Tower', 'D118': 'Dragon Tower', 'D119': 'Dragon Tower',
              'D120': 'Dragon Tower', 'D128': 'Dragon Tower', 'D121': 'Dungeon of Death', 'D122': 'Dungeon of Death',
              'D123': 'Dungeon of Death', 'D124': 'Dungeon of Death', 'D125': 'Sphinx', 'D126': 'Sphinx', 'D127': 'Sphinx'})
NOTE_NONE = set(range(49, 61)) | set(range(68, 73)) | {97, 98, 99, 100, 104}   # riddles, rhymes, plaques of hints
NOTE_NONE |= set(range(38, 48))   # owner, Phase 247: the Mine 1..5 note and the nine Deep Mine notes have no place
NOTE_TOWN = {32: (0, 28), 33: (0, 29), 34: (0, 30), 35: (0, 31), 36: (0, 32), 37: (0, 85)}  # the named town itself
def world_cell(w, mp, x, y):
    """(world, wx, wy, how) on the world map, or None."""
    if 1 <= mp <= 24: return (w, (mp - 1) // 4 * 16 + x, (3 - (mp - 1) % 4) * 16 + y, 'outdoor')
    a = anchors.get('%s%d' % ('CD'[w], mp), {}).get('anchor')
    if not a or a['x'] is None: return None
    return (a['world'], (a['map'] - 1) // 4 * 16 + a['x'], (3 - (a['map'] - 1) % 4) * 16 + a['y'], a['how'])
rows = []
for uid, e in q.items():
    k = int(uid.split('-')[1])
    src, kind, label, review = None, 'npc', None, ''
    if e['take']: src = e['take'][0]
    elif e['give']: src = e['give'][0]
    if src:
        label = npc_name(src['world'], src['map'], src['x'], src['y'])
        if not label: label = PLACE.get('%s%d' % ('CD'[src['world']], src['map'])); kind = 'place'
        if not (1 <= src['map'] <= 24): review += ' interior->entrance'
    wc = world_cell(src['world'], src['map'], src['x'], src['y']) if src else None
    if not wc: review += ' NO TARGET'
    rows.append(('quest', k, uid, e['text'], src, wc, kind, label, review))
for uid, e in n.items():
    k = int(uid.split('-')[1]); src, kind, label, review = None, 'none', None, ''
    if k in NOTE_TOWN:
        w, mp = NOTE_TOWN[k]; src = dict(world=w, map=mp, x=0, y=0); kind = 'place'
    elif e['parsed']:
        p = e['parsed'][0]; src = dict(world=p['world'], map=p['map'], x=p['x'], y=p['y']); kind = 'cell'
    elif k in NOTE_NONE: kind = 'none'
    elif e['give']:
        src = e['give'][0]; kind = 'place'; review += ' grant-cell'
    if src:
        label = PLACE.get('%s%d' % ('CD'[src['world']], src['map'])) if not (1 <= src['map'] <= 24) else None
    wc = world_cell(src['world'], src['map'], src['x'], src['y']) if src else None
    if src and not wc: review += ' NO ANCHOR'
    rows.append(('note', k, uid, e['text'], src, wc, kind, label, review))
def cstr(s): return '"' + (s or '').replace('\\', '\\\\').replace('"', '\\"') + '"'
with open(ADDON + '/targets.inc', 'w') as f:
    f.write('/* Generated by src/addon/quest_journal/tools/make_tables.py from the original scripts (do not edit).\n'
            ' * {kind: 0 none, 1 cell, 2 place (entrance of an interior map / a town), 3 npc; world; world cell x;\n'
            ' *  world cell y (from the south, world_map.c party_y convention); label} */\n')
    for what, n_ in (('quest', 56), ('note', 128)):
        f.write('static const qj_target qj_%s_targets[%d] = {\n' % (what, n_))
        for r in [r for r in rows if r[0] == what]:
            kind = {'none': 0, 'cell': 1, 'place': 2, 'npc': 3}[r[6]] if r[5] else 0
            w, x, y = (r[5][0], r[5][1], r[5][2]) if r[5] else (0, 0, 0)
            f.write('    /* %s */ {%d, %d, %d, %d, %s},\n' % (r[2], kind, w, x, y, cstr(r[7])))
        f.write('};\n')
with open(str(E.WORK) + '/targets_review.md', 'w') as f:
    f.write('# Quest / auto-note targets (generated; review)\n\nworld cell = (x, y from the south) on the 96 x 64 grid of world_map.c; '
            'C/D = Clouds/Darkside.\n\n| unit | text | source cell | target (world, x, y, how) | kind | label | review |\n|---|---|---|---|---|---|---|\n')
    for r in rows:
        src = '%s%d(%d,%d)' % ('CD'[r[4]['world']], r[4]['map'], r[4]['x'], r[4]['y']) if r[4] else '—'
        wc = '%s %d,%d %s' % ('CD'[r[5][0]], r[5][1], r[5][2], r[5][3]) if r[5] else '—'
        f.write('| %s | %s | %s | %s | %s | %s | %s |\n' % (r[2], str(r[3])[:70].replace('|', '/'), src, wc, r[6] if r[5] else 'none', r[7] or '', r[8].strip()))
with open(ADDON + '/descriptions.inc', 'w') as f:
    f.write('/* Generated by tools/make_tables.py from tools/descriptions.py (owner-approved texts; do not edit). {quest index (0..55) whose bit selects\n'
            ' * AFTER, -1 one text; BEFORE (or the only text); AFTER}. Index = quest item n (F077 + n). */\n'
            'static const qj_item_text qj_item_texts[85] = {\n')
    for k in range(85):
        e = ITEMS[k]
        if 'text' in e: f.write('    /* %s */ {-1, %s, NULL},\n' % (e['id'], cstr(e['text'])))
        else: f.write('    /* %s */ {%d, %s, %s},\n' % (e['id'], int(e['quest'].split('-')[1]) - 1, cstr(e['before']), cstr(e['after'])))
    f.write('};\n')
qs = [r for r in rows if r[0] == 'quest']; ns = [r for r in rows if r[0] == 'note']
print('quests: %d with target, %d none' % (sum(1 for r in qs if r[5]), sum(1 for r in qs if not r[5])))
print('notes:  %d with target, %d none' % (sum(1 for r in ns if r[5]), sum(1 for r in ns if not r[5])))

# world_map's anchors: every interior map whose entrance resolved (the outdoor maps 1..24 are their own place;
# the Darkside sky maps 89..112 are map - 88 at the same cell: a rule in world_map.c, not a row here).
rows_a = []
for key, e in anchors.items():
    a = e.get('anchor'); w, mp = e['world'], e['map']
    if 1 <= mp <= 24 or not a or a.get('x') is None or (w == 1 and 89 <= mp <= 112): continue
    rows_a.append((w, mp, a['world'], a['map'], a['x'], a['y'], a['how'], e.get('name') or ''))
rows_a.sort()
with open(WM + '/anchors.inc', 'w') as f:
    f.write('/* Generated by src/addon/quest_journal/tools/make_tables.py from the original scripts (do not edit).\n'
            ' * Interior map -> its entrance on the outdoor grid: {world, map, entrance world, outdoor map 1..24,\n'
            ' * x, y} (the cell inside that 16 x 16 map, y from the south); how it was derived in the comment:\n'
            ' * opN a teleport (op 7/31/52) into the map, kindN a location (op 17) of that kind (game_cutscene.c\n'
            ' * tables), via a chain of those, below = under a sky map, clouds-fall-table = where the original\n'
            ' * Clouds fall table (game_fall.c) drops a party falling out of the map. */\n')
    for w, mp, aw, am, x, y, how, name in rows_a:
        f.write('    {%d, %d, %d, %d, %d, %d}, /* %s%d %s: %s */\n' % (w, mp, aw, am, x, y, 'CD'[w], mp, name.replace('*/', ''), how))
print('anchors: %d interior maps' % len(rows_a))
