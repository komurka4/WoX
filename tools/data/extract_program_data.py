#!/usr/bin/env python3
"""Phase 184: one-time extraction of XEEN.DAT's initialized program data into src/data/.

DEVELOPER TOOL. It reads the protected original only from the path given on the
command line and is never run by the build, the tests, an install or the game.
The build reads src/data only (tools/data/gen_program_data.py). Phase 300: it writes
the sources (src/data: manifest, texts/*.tsv, pointers, tables, structs, records/*.csv,
opaque) and, beside them, the metadata (--meta, default tools/data/meta: names,
evidence, readers, descriptions, shapes, labels) through gen_program_data.write_sources.

What is extracted (analysis address = seg*16 + off, image at 10000h; file offset =
analysis - A940h; the MZ image starts at file 56C0h):
  - the initialized data segment DS:0000..8C4B (file 4D890h..564DBh, 35,916 bytes),
    byte for byte, as raw file bytes (the native has always used the unrelocated
    image: 695 MZ relocation sites fall inside it; their raw segment words are
    0000h (code) or 481Dh (DS));
  - the driver's gameplay RNG state, segment 4853 CS:16F8/16FA/16FC (three
    little-endian words; 50525 is their only reader and writer; file 3F2E8h).

The DS bytes are partitioned into regions, claimed in this order:
  1. far pointers: every relocation site's segment word with the offset word
     before it (4 bytes); consecutive ones form a table;
  2. curated typed tables (TABLES below: each has a native consumer);
  3. text: NUL-terminated runs that start at a reference (a far pointer to DS,
     a native ds_text/far_text constant in src/, or an immediate operand of the
     original code by linear sweep) and pass a text test; then runs that follow
     a text entry directly in a pool ("pool-adjacent");
  4. zero runs of 16 bytes or more;
  5. everything else: opaque bytes (lossless hex).
The classification affects readability only; reconstruction is byte-exact either way.

Usage: extract_program_data.py --xeen-dat PATH [--out src/data] [--meta tools/data/meta] [--src src]
       extract_program_data.py --xeen-dat PATH --audit tools/data/audit/ds_audit.json --check [--out src/data] [--meta DIR]

--check (Phase 261) extracts into a temporary directory and compares every generated file (Phase 300: the sources'
JSON, texts/*.tsv and records/*.csv, and the metadata files; README.md is not generated) with --out and --meta, byte
for byte: exit 0 when they are identical, 1 with the differing, missing and extra files listed (metadata files as
meta/NAME). It writes nothing under --out or --meta, so a
refactoring checkpoint can prove that src/data is exactly what the original and the current src/ produce (the
extractor reads the XEEN_TEXT_*/XEEN_DS_* names in src/ to classify text).
"""
import argparse, hashlib, json, re, struct, sys
sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parent))
import ds_structs_p186 as P186                    # noqa: E402  (Phase 186 layouts; values from the original)
from pathlib import Path

XEEN_DAT_SHA256 = '88704c229ada806289dc4a513f77c88e1966fb8e3cf1d23652c748a7a7cd1687'
XEEN_DAT_SIZE = 382608
# The same program with its relocation table sorted by address (tools/data/gog_xeen_dat.py writes this form from the
# GOG.com release's WORLD/XEEN.DAT); the order of the relocations does not affect the program or the extraction.
XEEN_DAT_CANONICAL_SHA256 = 'ef7db634a74059e94ec07bbf6396abcdce7fb7c36c1baa265b170ff7903c1523'
DS_FILE = 0x4D890
DS_SIZE = 0x8C4C
DS_SEGMENT_RAW = 0x481D
IMAGE_FILE = 0x56C0
CODE_LINEAR = (0x10000, 0x581D0)
RNG_FILE = 0x48530 - 0xA940 + 0x16F8

# Curated typed tables: (offset, name, type, count, description, consumers)
TABLES = [
    (0x064E, 'movement_offset_x', 'int8', 192,
     'x delta per view/probe slot, 48 per direction (N, E, S, W); DS:0655 is slot 7 (the cell ahead of Jump)',
     ['src/world/movement.c xeen_movement_init/72', 'src/game/game_cast.c cast_jump 0x655+0x30*dir']),
    (0x070E, 'movement_offset_y', 'int8', 192,
     'y delta per view/probe slot, 48 per direction; DS:0715 is slot 7',
     ['src/world/movement.c xeen_movement_init/73', 'src/game/game_cast.c cast_jump 0x715+0x30*dir']),
    (0x07CE, 'movement_wall_shift', 'uint16', 192,
     'wall nibble shift per view/probe slot (low 5 bits used)', ['src/world/movement.c 90']),
    (0x279E, 'direction_wall_shift', 'uint16', 4,
     'wall nibble shift of the cell ahead per facing (N, E, S, W)', ['src/world/monster_update.c can_move', 'src/game/game_cast.c cast_jump']),
    (0x67C4, 'town_portal_maps', 'int8', 10,
     'Town Portal destination map numbers, 5 per world (Clouds, then Darkside), formatted by DS:69F3',
     ['src/game/game_cast_travel.c cast_portal']),
]
# Phase 185 (the DS reference audit, tools/data/audit/ds_audit.json since Phase 291): tables promoted from opaque bytes, each
# with a proven original reader (static: an indexed read whose displacement is the base, in code reached by
# recursive descent; dynamic: the harness observer ds-access) and its index range from the reader.
# (offset, name, type, count, description, native consumers, original readers)
AUDIT_TABLES = [
    (0x17B2, 'portrait_x', 'uint8', 12,
     'party bar portrait x per slot: 17B2[slot] (exploration bar), 17B8[slot] (the second row of six)',
     ['src/world/world_entry.c bar', 'src/game/game_exit.c exit_draw'], ['18C28 [bx+17B2]', '2AD03 [bx+17B8]']),
    (0x187A, 'style_song_group', 'uint8', 6,
     'song group per map style 0..5 (higher styles read the next table\'s bytes)',
     ['src/graphics/sprites.c load_indoor_sprites'], ['1AD82 [bx+187A]', '1ADB1 [bx+187A]']),
    (0x18B8, 'class_base_hp', 'uint8', 10,
     'hit points per level by class 0..9 (maximum HP 2AAC2)', ['src/combat/combat.c 2AAC2', 'src/world/world_entry.c'],
     ['2AAD1 [bx+18B8]']),
    (0x2780, 'monster_pow_icon', 'uint8', 7,
     'projectile icon number (pow%u.icn) by monster ranged type (+1Dh, 0..6); 12 = none',
     ['src/combat/projectile.c 4128F'], ['412D1 [bx+2780]']),
    (0x2787, 'monster_pow_cue', 'uint8', 7,
     'sound cue of the projectile by monster ranged type (+1Dh, 0..6)', ['src/combat/projectile.c 4128F'],
     ['41340 [bx+2787]']),
    (0x2920, 'class_experience_base', 'uint16', 10,
     'experience base per class 0..9 (doubled per level from level 2; 2920[class] << (level-2))',
     ['src/characters/town.c'], ['28C13 [bx+2920]', '28CCD [bx+2920]']),
    (0x2990, 'race_sp_bonus', 'int8', 10,
     'spell point bonus per level by race 0..4 and casting stat (Intellect, Personality): 298F[2 x race + stat], '
     'stat 1..2 (maximum spell points 46470; Phase 329)', ['src/combat/combat.c xeen_combat_max_sp'],
     ['46500 [bx+298F]']),
    (0x299A, 'race_hp_bonus', 'int8', 5,
     'hit point bonus per level by race 0..4 (maximum HP 2AAC2)', ['src/combat/combat.c 2AAC2', 'src/world/world_entry.c'],
     ['2AB09 [bx+299A]']),
    (0x2D80, 'stat_window_x', 'uint8', 20,
     'x of window 1Ch (the character view\'s attribute pop-up) per item n', ['src/game/game_details.c 50B45'],
     ['42914 [bx+2D80]']),
    (0x2D94, 'stat_window_y', 'uint8', 20,
     'y of window 1Ch per item n', ['src/game/game_details.c 50B45'], ['4291D [bx+2D94]']),
    (0x2DA8, 'attribute_bonus_threshold', 'uint16', 24,
     'attribute value thresholds: 2C92E returns 2DD8[i] for the first 2DA8[i] above the value (65535 ends the scan)',
     ['src/combat/combat.c xeen_combat_stat_bonus', 'src/world/world_entry.c', 'src/game/game_details.c'], ['2C93B [bx+2DA8]']),
    (0x2DD8, 'attribute_bonus', 'int8', 24,
     'the bonus for each threshold of attribute_bonus_threshold', ['src/combat/combat.c xeen_combat_stat_bonus'],
     ['2C944 [si+2DD8]']),
    (0x2DF0, 'condition_portrait_frame', 'uint8', 17,
     'party bar portrait frame by worst condition 0..16 (16 = good); frames above 4 use the other sprite',
     ['src/world/world_entry.c bar'], ['2AC21 [bx+2DF0]']),
    (0x3018, 'location_animation_files', 'int8', 14,
     'number of %s1.twn animation files loaded at a location entry, world*7 + kind',
     ['src/game/game_location.c 27D6E'], ['27DE5 [bx+3018]']),
    (0x3026, 'location_animation_frames', 'int8', 22,
     'the location animation\'s frame count (8D36) at entry, world*11 + kind', ['src/game/game_location.c 27806'],
     ['27818 [bx+3026]']),
    (0x67CE, 'town_portal_darkside_maps', 'int8', 5,
     'Town Portal destination maps on Darkside: 19E82(67C3[FB00*10 + n]) with n = 1..5 reads the bytes after '
     'town_portal_maps (the outdoor areas outside the towns); Clouds reads 67C4..67C8', ['src/game/game_cast_travel.c portal_amount'],
     ['3B0D2 [bx+67C3] (dynamic: travel-portal-castleview)']),
    (0x8A0A, 'rtl_month_days', 'int8', 12,
     'Borland RTL days per month (January..December), read by the time conversion 13341', ['src/ui/main_menu.c 13341'],
     ['13371 [bx+8A0A]', '13385 [bx+8A0A]']),
    (0x8A16, 'rtl_month_start_day', 'int16', 12,
     'Borland RTL day of the year at each month\'s start (non-leap)', [], ['13569 [bx+8A16]']),
    # Phase 259 (pass 11: native tables whose values are a DS run): two more RTL date tables inside b8BCD, each
    # with its readers and a native copy.
    (0x8BE6, 'rtl_dos_month_days', 'int8', 12,
     'Borland RTL days per month (January..December) of the DOS date conversion (cbw after each read), a second '
     'copy beside rtl_month_days', ['src/ui/main_menu.c xeen_dos_time 14DEC'],
     ['14E68 [si+8BE6]', '150DB [bx+8BE6]', '150F8 [bx+8BE6]']),
    (0x8BF2, 'rtl_month_cumulative_days', 'uint16', 13,
     'Borland RTL __isDST 15308: the day of the year before each month (non-leap), 0..365, compared unsigned',
     ['src/ui/main_menu.c dos_dst 15308'],
     ['15336 [bx+8BF2]', '15344 [bx+8BF2]', '15389 [bx+8BF2]', '15380 [bx+8BF0]']),
]
TYPES = {'int8': ('b', 1), 'uint8': ('B', 1), 'int16': ('h', 2), 'uint16': ('H', 2)}


def relocations(dat):
    n = struct.unpack_from('<H', dat, 6)[0]
    at = struct.unpack_from('<H', dat, 0x18)[0]
    ds0 = DS_FILE - IMAGE_FILE
    out = []
    for i in range(n):
        off, seg = struct.unpack_from('<HH', dat, at + 4 * i)
        x = seg * 16 + off - ds0
        if 0 <= x < DS_SIZE:
            out.append(x)
    return sorted(out)


def ds_names(src):
    """XEEN_DS_* names of include/xeen/ds_globals.h and XEEN_TEXT_* names of include/xeen/ds_texts.h (Phase 260)
    (#define NAME 0xNNNNu), so a call that passes a named DS offset counts as the literal it names (Phase 254:
    Phase 252/253 named many far_text()/ds_text() bases)."""
    names = {}
    for header in ('ds_globals.h', 'ds_texts.h'):
        h = Path(src).resolve().parent / 'include' / 'xeen' / header
        if h.exists():
            names.update({n: int(v, 16) for n, v in re.findall(
                r'^#define\s+(XEEN_(?:DS|TEXT)_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+)u?\b', h.read_text(), re.M)})
    return names


# Phase 280: native functions that read a DS text by passing their second argument to ds_text() unchanged, so a
# call with a constant there is a native ds_text read of that text: name -> (its defining file, the parameter).
# native_refs() verifies each definition before it counts the calls (a wrapper that stops passing the parameter to
# ds_text, or moves, ends the extraction), so the list cannot outlive what the code does.
TEXT_WRAPPERS = {
    'panel_list': ('game/game_panel.c', 'pattern'),     # the save-file listing: ds_text(g, pattern) is its pattern
    # Phase 312: the map load's wait box (Phase 310): ds_text(g, text) is its text. The new game's literal call
    # (game_newgame.c, "Please Wait..." 5856) is a native read; reload_wait_open's call passes a variable (not counted).
    'wait_box_present': ('game/game.c', 'text'),
}


def wrapper_checked(src, name, file, param):
    """Raises SystemExit unless SRC/FILE defines NAME(xeen_game *g, ... PARAM) and its body calls ds_text(g, PARAM)."""
    p = Path(src) / file
    t = p.read_text(errors='replace') if p.exists() else ''
    m = re.search(r'^\w[^\n;]*\b' + name + r'\(\s*xeen_game\s*\*\s*g\s*,\s*[^,()]*\b' + param + r'\s*\)\s*\{', t, re.M)
    body = t[m.end():t.find('\n}', m.end())] if m else ''
    if not re.search(r'\bds_text\(\s*g\s*,\s*' + param + r'\s*\)', body):
        raise SystemExit(f'declared text wrapper {name} ({file}) does not pass {param} to ds_text(g, {param})')


def native_refs(src):
    texts, tables = set(), set()
    names = ds_names(src)
    arg = r',\s*(0x[0-9a-fA-F]+|XEEN_(?:DS|TEXT)_[A-Z0-9_]+)\s*[,)]'
    for name, (file, param) in TEXT_WRAPPERS.items():
        wrapper_checked(src, name, file, param)
    readers = r'\b(?:' + '|'.join(['ds_text'] + sorted(TEXT_WRAPPERS)) + r')\('

    def value(v):
        return int(v, 16) if v.startswith('0x') else names.get(v)
    for p in sorted(Path(src).rglob('*')):
        if p.suffix not in ('.c', '.inc', '.h'):
            continue
        t = p.read_text(errors='replace')
        texts.update(x for x in map(value, re.findall(readers + r'[^,()]+' + arg, t)) if x is not None)
        # Phase 259: items.c's far_string() is the same reader (entry K of the table: its offset word)
        tables.update(x for x in map(value, re.findall(r'\b(?:far_text|far_string)\([^,()]+' + arg, t))
                      if x is not None)
    return texts, tables


def code_immediates(dat):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    md.skipdata = True
    lo, hi = (a - 0xA940 for a in CODE_LINEAR)
    imms = set()
    num = re.compile(r'^(0x[0-9a-f]+|\d+)$')
    for i in md.disasm(dat[lo:hi], 0):
        if i.mnemonic not in ('mov', 'push', 'lea', 'cmp', 'add', 'sub'):
            continue
        last = i.op_str.rsplit(', ', 1)[-1]
        if num.match(last):
            imms.add(int(last, 0) & 0xFFFF)
    return imms


def is_text(b):
    if not b:
        return False
    printable = sum(0x20 <= c <= 0x7E for c in b)
    letters = sum(chr(c).isalpha() for c in b if c < 0x80)
    return printable / len(b) >= 0.6 and letters >= 1 and all(c < 0x80 for c in b)


TEXT_CONTROLS = set(range(0x01, 0x05)) | set(range(0x09, 0x0E))


def weak_text(b):
    """A run referenced only by a code immediate: at least 3 bytes, 80% printable, two
    letters, and no byte outside printable ASCII and the text engine's control codes
    (01..04, 09..0D)."""
    printable = sum(0x20 <= c <= 0x7E for c in b)
    letters = sum(chr(c).isalpha() for c in b if c < 0x80)
    return len(b) >= 3 and printable >= 0.8 * len(b) and letters >= 2 and \
        all(0x20 <= c <= 0x7E or c in TEXT_CONTROLS for c in b)


def tokens(b):
    out = []
    for c in b:
        out.append(chr(c) if 0x20 <= c <= 0x7E and c not in (0x7B, 0x7D) else '{%02X}' % c)
    return ''.join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--xeen-dat', required=True, help='explicit path of the protected original (read only)')
    ap.add_argument('--out', default=str(Path(__file__).resolve().parents[2] / 'src' / 'data'))
    ap.add_argument('--meta', default=str(Path(__file__).resolve().parent / 'meta'),
                    help='Phase 300: the metadata directory written beside --out (names, evidence, readers, '
                         'descriptions, shapes, labels; never read by the build)')
    ap.add_argument('--src', default=str(Path(__file__).resolve().parents[2] / 'src'))
    ap.add_argument('--audit', help='Phase 185 ledger (tools/data/audit/ds_audit.json): candidate text it proves '
                    'referenced becomes confidence "referenced" with its original evidence')
    ap.add_argument('--phase184', action='store_true', default=False,
                    help='the Phase 184 classification (without the Phase 185 promotions)')
    ap.add_argument('--phase185', action='store_true', default=False,
                    help='the Phase 185 classification (without the Phase 186 tables and structures)')
    a = ap.parse_args(argv)
    proven, semantic = {}, {}
    if a.audit:
        for r in json.loads(Path(a.audit).read_text())['records']:
            if r['kind'] == 'opaque' and r.get('semantic'):        # Phase 291: the ledger's reader-context names
                semantic[r['id']] = r['semantic']
            if r['kind'] == 'text' and r['classification'] == 'TEXT_REFERENCED':
                srcs = {x['source'] for x in r['evidence']['original_static'] + r['evidence']['original_dynamic']
                        if x['strength'] in ('DIRECT_PROVEN', 'INDIRECT_PROVEN', 'RANGE_PROVEN')}
                proven[r['id']] = sorted(srcs)
    dat = Path(a.xeen_dat).read_bytes()
    if len(dat) != XEEN_DAT_SIZE or hashlib.sha256(dat).hexdigest() not in (XEEN_DAT_SHA256, XEEN_DAT_CANONICAL_SHA256):
        sys.exit('not the protected unpacked XEEN.DAT (size/SHA-256 differ): run tools/data/unpack_xeen_dat.sh first')
    ds = dat[DS_FILE:DS_FILE + DS_SIZE]
    tail = dat[DS_FILE + DS_SIZE:]
    assert not any(tail), 'DS bytes past 8C4C in the file are not all zero'
    claimed = [None] * DS_SIZE

    def claim(lo, hi, what):
        for k in range(lo, hi):
            if claimed[k] is not None:
                raise SystemExit(f'overlap at DS:{k:04X}: {what} vs {claimed[k]}')
            claimed[k] = what

    # 1. far pointers
    rel = relocations(dat)
    relset = set(rel)
    ptrs = []
    for r in rel:
        if r < 2 or (r - 2) in relset:
            raise SystemExit(f'relocation DS:{r:04X} has no offset word before it')
        off, seg = struct.unpack_from('<HH', ds, r - 2)
        if seg not in (0, DS_SEGMENT_RAW):
            raise SystemExit(f'relocation DS:{r:04X} raw segment {seg:04X}')
        ptrs.append((r - 2, off, seg))
    tables_fp = []
    for at, off, seg in ptrs:
        if tables_fp and tables_fp[-1]['_end'] == at and \
                not (at in P186.FAR_TABLE_STARTS and not (a.phase184 or a.phase185)):
            t = tables_fp[-1]
        else:
            t = {'_end': at, 'offset': at, 'entries': []}
            tables_fp.append(t)
        t['entries'].append('%s:%04X' % ('ds' if seg == DS_SEGMENT_RAW else 'code', off))
        t['_end'] = at + 4
    for t in tables_fp:
        claim(t['offset'], t['_end'], 'far pointer table %04X' % t['offset'])

    # 2. typed tables
    typed = []
    for row in TABLES + (AUDIT_TABLES if a.phase184 is False else []):
        off, name, typ, count, desc, cons = row[:6]
        code, w = TYPES[typ]
        claim(off, off + w * count, name)
        vals = list(struct.unpack_from('<%d%s' % (count, code), ds, off))
        t = {'id': name, 'offset': '%04X' % off, 'type': typ, 'count': count,
             'byte_order': 'little-endian' if w > 1 else None, 'description': desc,
             'consumers': cons, 'values': vals}
        if len(row) > 6:
            t = {**{k: v for k, v in t.items() if k != 'values'}, 'audit': 'P185', 'original_readers': row[6],
                 'values': vals}
        typed.append(t)
    p186 = not (a.phase184 or a.phase185)
    struct_list = P186.structs(ds) if p186 else []
    for off, name, typ, count, desc, cons, readers in (P186.P186_TABLES if p186 else []):
        code, w = TYPES[typ]
        claim(off, off + w * count, name)
        typed.append({'id': name, 'offset': '%04X' % off, 'type': typ, 'count': count,
                      'byte_order': 'little-endian' if w > 1 else None, 'description': desc, 'consumers': cons,
                      'audit': 'P186', 'original_readers': readers, 'decoded': name not in P186.UNDECODED_TABLES,
                      'values': list(struct.unpack_from('<%d%s' % (count, code), ds, off))})
    for st in struct_list:
        claim(int(st['offset'], 16), int(st['offset'], 16) + st['size'], 'struct:' + st['id'])
    typed.sort(key=lambda t: int(t['offset'], 16))
    if p186:                                         # Phase 291: reader-established shapes, values as nested rows
        import gen_program_data as G
        for t in typed:
            if t['id'] in P186.TABLE_SHAPES:
                shape, dims, basis = P186.TABLE_SHAPES[t['id']]
                vals = t.pop('values')
                t.update({'shape': shape, 'dims': dims, 'shape_basis': basis, 'values': G.nest_values(vals, shape)})
                if G.shape_values(t) != vals:
                    raise SystemExit(f"shape of {t['id']}: the rows do not flatten to the values")
        missing = set(P186.TABLE_SHAPES) - {t['id'] for t in typed}
        if missing:
            raise SystemExit(f'TABLE_SHAPES names unknown tables: {sorted(missing)}')

    # 3. text
    nat_text, nat_tables = native_refs(a.src)
    imms = code_immediates(dat)
    evidence = {}

    def add(t, why):
        if 0 <= t < DS_SIZE:
            evidence.setdefault(t, set()).add(why)
    for _, off, seg in ptrs:
        if seg == DS_SEGMENT_RAW:
            add(off, 'far-pointer')
    for t in nat_text:
        add(t, 'native-ds_text')
    for t in imms:
        add(t, 'code-immediate')
    texts = {}
    for t in sorted(evidence):
        if claimed[t] is not None:
            continue
        e = t
        while e < DS_SIZE and ds[e] and claimed[e] is None:
            e += 1
        if e >= DS_SIZE or ds[e] or claimed[e] is not None:
            continue
        body = ds[t:e]
        strong = evidence[t] & {'far-pointer', 'native-ds_text'}
        reader = t in P186.READER_TEXT and not (a.phase184 or a.phase185)    # Phase 295: an original reader's text
        if not (is_text(body) or (not body and strong) or reader or
                (t in P186.FORCED_TEXT and not (a.phase184 or a.phase185))):
            continue
        if not strong and not reader and not weak_text(body):     # code-immediate only: a stricter test
            continue
        hit = next((s for s, x in texts.items() if x['_end'] == e), None)
        if hit is not None and hit < t:
            texts[hit]['aliases'].append(['%04X' % t, sorted(evidence[t])])
            continue
        texts[t] = {'_end': e, 'aliases': [], 'evidence': sorted(evidence[t])}
    for t in sorted(texts):
        claim(t, texts[t]['_end'] + 1, 'text %04X' % t)
    changed = True
    while changed:                                   # pool-adjacent runs
        changed = False
        for t in sorted(texts):
            n = texts[t]['_end'] + 1
            if n >= DS_SIZE or n in texts or claimed[n] is not None or not ds[n]:
                continue
            e = n
            while e < DS_SIZE and ds[e] and claimed[e] is None:
                e += 1
            if e < DS_SIZE and not ds[e] and claimed[e] is None and len(ds[n:e]) >= 2 and is_text(ds[n:e]):
                texts[n] = {'_end': e, 'aliases': [], 'evidence': ['pool-adjacent']}
                claim(n, e + 1, 'text %04X' % n)
                changed = True
    # 4/5. zero and opaque bytes
    regions = []
    k = 0
    while k < DS_SIZE:
        if claimed[k] is not None:
            what = claimed[k]
            j = k
            while j < DS_SIZE and claimed[j] == what:
                j += 1
            kind = 'text' if what.startswith('text') else 'far_pointers' if what.startswith('far') else \
                'struct' if what.startswith('struct:') else 'table'
            rid = ('t%04X' % k) if kind == 'text' else ('p%04X' % k) if kind == 'far_pointers' else \
                what[7:] if kind == 'struct' else what
            regions.append({'at': '%04X' % k, 'kind': kind, 'id': rid})
            k = j
            continue
        j = k
        while j < DS_SIZE and claimed[j] is None:
            j += 1
        run = ds[k:j]

        def opaque(p, q):
            chunk = run[p:q]
            r = {'at': '%04X' % (k + p), 'kind': 'bytes', 'size': len(chunk)}
            sem = semantic.get('b%04X' % (k + p)) if not (a.phase184 or a.phase185) else None
            if sem:                                  # Phase 291: a label only where the ledger proves the readers' role
                r.update({'label': sem['name'], 'label_basis': sem['basis'], 'label_confidence': sem['confidence']})
            r['hex'] = [chunk[i:i + 32].hex().upper() for i in range(0, len(chunk), 32)]
            regions.append(r)
        p = 0
        for z in re.finditer(rb'\x00{16,}', run):    # zero runs of 16 or more
            if z.start() > p:
                opaque(p, z.start())
            regions.append({'at': '%04X' % (k + z.start()), 'kind': 'zero', 'size': z.end() - z.start()})
            p = z.end()
        if p < len(run):
            opaque(p, len(run))
        k = j
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    text_entries = []
    for t in sorted(texts):
        x = texts[t]
        body = ds[t:x['_end']]
        strong = bool(set(x['evidence']) & {'far-pointer', 'native-ds_text'})
        e = {'id': 't%04X' % t, 'offset': '%04X' % t, 'length': len(body), 'text': tokens(body),
             'confidence': 'referenced' if strong else 'candidate',
             'evidence': x['evidence'], 'aliases': x['aliases']}
        # Phase 185: proven by the audit. Phase 280: also when native evidence joined later (a ds_text read through a
        # TEXT_WRAPPERS call): the audit's original evidence and marker stay, so its Phase 184 target keeps its record.
        if e['id'] in proven:
            e['confidence'] = 'referenced'
            e['evidence'] = sorted(set(x['evidence']) | set(proven[e['id']]))
            e['audit'] = 'P186' if e['id'] in P186.TEXT_PROMOTIONS and not (a.phase184 or a.phase185) else 'P185'
        if p186 and e['id'] in P186.TEXT_PROMOTIONS and 'audit' not in e:     # Phase 186: audited although
            e['audit'] = 'P186'                                                # already referenced by a far pointer
            e['evidence'] = sorted(set(e['evidence']) | set(proven.get(e['id'], [])))
        if p186 and t in P186.READER_TEXT:                   # Phase 295: promoted by its original reader
            e['audit'] = 'P295'
        text_entries.append(e)
    fp_entries = [{'id': 'p%04X' % t['offset'], 'offset': '%04X' % t['offset'], 'entries': t['entries'],
                   'native_far_text': ['%04X' % x for x in sorted(nat_tables) if t['offset'] <= x < t['_end']]}
                  for t in tables_fp]
    starts = {t['offset'] + 4 * i for t in tables_fp for i in range(len(t['entries']))}
    stray = sorted(x for x in nat_tables if x not in starts)
    if stray:
        raise SystemExit('native far_text tables not on a far pointer: ' + ' '.join('%04X' % x for x in stray))
    seeds = list(struct.unpack_from('<3H', dat, RNG_FILE))
    head = {'source': 'XEEN.DAT sha256 ' + XEEN_DAT_SHA256, 'extracted_by': 'tools/data/extract_program_data.py'}
    files = {
        'program_ds.json': {
            'format': 'xeen-program-ds-1', **head,
            'about': 'Layout of the initialized data segment DS:0000..8C4B, in address order. Each region is '
                     'one of: text (ds_text.json), far_pointers (ds_pointers.json), table (ds_tables.json), '
                     + ('struct (ds_structs.json), ' if p186 else '') +
                     'zero (size bytes of 00) or bytes (opaque, lossless hex). DS:8C4C and above is BSS: zero '
                     'at program load (the file stores 29,108 zero bytes to FDCB; nothing above).' + (
                     ' Phase 291: this index is generated by the extractor and checked by the generator (the regions '
                     'tile the segment without a gap or an overlap); a bytes region\'s optional "label" is the audit '
                     'ledger\'s reader-context name (tools/data/audit/ds_audit.json "semantic", "label_basis"): the '
                     'readers\' role, the bytes stay opaque.' if p186 else ''),
            'size': DS_SIZE, 'sha256': hashlib.sha256(ds).hexdigest(),
            'relocation_sites': {'count': len(rel), 'note': 'MZ relocation sites inside DS (the segment word of each '
                                 'far pointer in ds_pointers.json). The image keeps the raw, unrelocated words '
                                 '(0000 = code, 481D = DS), as the native has always consumed them.'},
            'regions': regions},
        'ds_text.json': {
            'format': 'xeen-program-text-1', **head,
            'about': 'NUL-terminated DS text. "text" is the exact byte sequence: printable ASCII 20h..7Eh '
                     'except { and } is literal; every other byte (control codes, { }, codepage bytes) is {XX} '
                     'in upper-case hex. "length" excludes the terminating 00, which is implied. "aliases" are '
                     'references into the middle of the entry. Evidence: far-pointer (a DS far pointer targets '
                     'it), native-ds_text (a ds_text() constant in src/), code-immediate (an immediate operand '
                     'of the original code, linear sweep), pool-adjacent (directly follows a text entry). '
                     'confidence: referenced (a far pointer or a native consumer) or candidate (a code immediate '
                     'or pool position only: the bytes are text-like, their use is not proven).',
            'entries': text_entries},
        'ds_pointers.json': {
            'format': 'xeen-program-pointers-1', **head,
            'about': 'Far pointers (offset word, then segment word: 4 bytes, little-endian). "ds:XXXX" stores '
                     'offset XXXX with raw segment 481D (the DS); "code:XXXX" stores offset XXXX with raw segment '
                     '0000 (the first code segment, relocated by DOS at load). native_far_text: the offsets in the '
                     'table that src/ passes to far_text() as a table base.',
            'tables': fp_entries},
        'ds_tables.json': {
            'format': 'xeen-program-tables-1', **head,
            'about': 'Typed DS tables with a native consumer; values are decimal in the stated type.' + (
                     ' Phase 291: a table with "shape" (dimensions established from its original reader\'s index '
                     'arithmetic, "shape_basis"; "dims" names each index) holds its values as nested rows in C order '
                     '(the first dimension has the largest stride); product(shape) = count. Other tables are flat.'
                     if p186 else ''),
            'tables': typed},
        'ds_structs.json': {
            'format': 'xeen-program-structs-1', **head,
            'about': 'Phase 186: DS structures decoded from the original\'s readers. A structure\'s members tile it '
                     'in address order: a typed array (type, count, values), a record array (record: a type in '
                     'record_types, count, values: one row per record, one value per field, a list for an array '
                     'field), chars (the exact bytes in the ds_text.json token form, {00} allowed) or bytes (raw '
                     'upper-case hex). Field and member descriptions say what the original readers do; '
                     '"decoded": false marks a field whose meaning is not established. Layouts are defined in '
                     'tools/data/ds_structs_p186.py; the values are the original\'s bytes.',
            'record_types': P186.RECORD_TYPES if p186 else {},
            'structs': struct_list},
        'driver_constants.json': {
            'format': 'xeen-driver-constants-1', **head,
            'rng_state': {'segment': '4853', 'offsets': ['16F8', '16FA', '16FC'], 'file_offset': '%05X' % RNG_FILE,
                          'byte_order': 'little-endian', 'words': ['%04X' % w for w in seeds],
                          'sha256': hashlib.sha256(dat[RNG_FILE:RNG_FILE + 6]).hexdigest(),
                          'about': 'Initial state of the driver RNG 50525 (CS:16F8 XOR DX, 16FA ADC CX, 16FC SBB '
                                   'AX, then updated in place); initialized executable bytes, the same at every '
                                   'program start.'}},
    }
    if p186:                                         # Phase 291: evidenced names, interior names, pointer targets
        import ds_names as NM
        try:
            NM.apply(files, Path(a.src).resolve().parent)
        except NM.NameError_ as err:
            raise SystemExit(f'ds_names: {err}')
        files['ds_text.json']['about'] += NAMES_ABOUT
        files['ds_pointers.json']['about'] += NAMES_ABOUT + TARGETS_ABOUT
        files['ds_tables.json']['about'] += NAMES_ABOUT
        files['ds_structs.json']['about'] += NAMES_ABOUT
    import gen_program_data as G
    if p186:                                         # Phase 300: src/data holds the sources, --meta the metadata
        entries = files['ds_text.json']['entries']
        files['ds_text.json']['format'] = 'xeen-program-text-2'     # the merged model's form (parts + entries)
        files['ds_text.json']['parts'] = []
        covered = 0
        for lo, hi, name, about in P186.TEXT_PARTS:
            n = sum(lo <= int(e['offset'], 16) < hi for e in entries)
            covered += n
            files['ds_text.json']['parts'].append({'file': name, 'range': ['%04X' % lo, '%04X' % hi],
                                                   'entries': n, 'about': about})
        if covered != len(entries):
            raise SystemExit('TEXT_PARTS do not cover every text entry')
        for t in files['ds_structs.json']['structs']:
            if any('record' in m for m in t['members']):
                t['values_csv'] = 'records/%s.csv' % t['id']
        try:
            G.write_sources(files, out, a.meta)
        except G.DataError as err:
            raise SystemExit(f'write_sources: {err}')
    else:                                            # --phase184 / --phase185: the historical monolithic files
        for name, body in files.items():
            (out / name).write_text(G.dump_json(body))
    kinds = {}
    for r in regions:
        size = {'text': None}.get(r['kind'])
        kinds[r['kind']] = kinds.get(r['kind'], 0) + 1
    print(json.dumps({'regions': kinds, 'text_entries': len(text_entries), 'far_pointer_tables': len(fp_entries),
                      'far_pointers': len(ptrs), 'rng': ['%04X' % w for w in seeds]}))


NAMES_ABOUT = (' Phase 291 (tools/data/ds_names.py, derived, never edited by hand): "name" is the evidenced semantic name '
               'of a text entry or far-pointer table, from the one include/xeen/ds_texts.h or ds_globals.h constant at its '
               'first byte ("c_name", lower-cased without its prefix; the header documents its readers); a typed table\'s '
               'or structure\'s id is its name, "c_name" its constant and "name_aliases" a reviewed constant worded '
               'differently; "interior_names" are constants inside the entry (kind alias, suffix, member, element, slot '
               'or virtual-base, with their offset). An entry without a name has no evidenced name yet.')
TARGETS_ABOUT = (' "targets" (parallel to "entries", derived from the raw words, which stay the source of truth): "code", '
                 '"bss" (a DS offset at or above 8C4C) or "<kind> <id>" with "+<hex>" for an offset inside that entry, '
                 'then the evidenced name at that exact offset when there is one.')
def stale_consumers(out):
    """The native consumers (each generated JSON's 'consumers' lists) that name a src/ file which does not exist
    (Phase 265: four .inc fragments had become .c units; a renamed or moved source must be followed here)."""
    root = Path(__file__).resolve().parents[2]
    bad = []
    def walk(x, where):
        if isinstance(x, dict):
            for k, v in x.items():
                if k == 'consumers' and isinstance(v, list):
                    for c in v:
                        for p in re.findall(r'\bsrc/[\w./*-]+', str(c)):
                            p = p.rstrip('.,;:')
                            if not (root / p).is_file():
                                bad.append(f'{where}: {p} ({c})')
                else:
                    walk(v, where)
        elif isinstance(x, list):
            for v in x:
                walk(v, where)
    for f in sorted(Path(out).glob('*.json')):
        walk(json.loads(f.read_text()), f.name)
    return bad


def check(argv):
    """--check: extract into a temporary directory and compare it with --out and --meta (see the module
    documentation)."""
    import tempfile
    import gen_program_data as G
    argv = [x for x in argv if x != '--check']
    dirs = {'--out': Path(__file__).resolve().parents[2] / 'src' / 'data', '--meta': G.META_DIR}
    for opt in dirs:
        if opt in argv:
            k = argv.index(opt)
            dirs[opt] = Path(argv[k + 1])
            del argv[k:k + 2]
    out, meta = dirs['--out'], dirs['--meta']
    with tempfile.TemporaryDirectory(prefix='xeen-extract-check-') as tmp:
        import contextlib, io
        t_out, t_meta = Path(tmp) / 'data', Path(tmp) / 'meta'
        with contextlib.redirect_stdout(io.StringIO()):
            main(argv + ['--out', str(t_out), '--meta', str(t_meta)])
        (n_src, n_meta), (o_src, o_meta) = G.generated_files(t_out, t_meta), G.generated_files(out, meta)
        differ, missing, extra = [], [], []
        for label, new, old, nd, od in (('', n_src, o_src, t_out, out), ('meta/', n_meta, o_meta, t_meta, meta)):
            differ += [label + f for f in sorted(new & old) if (nd / f).read_bytes() != (od / f).read_bytes()]
            missing += [label + f for f in sorted(new - old)]
            extra += [label + f for f in sorted(old - new)]
        total = len(n_src) + len(n_meta)
    stale = stale_consumers(out)
    for kind, fs in (('differs', differ), (f'missing from {out} / {meta}', missing), ('not generated', extra),
                     ('a consumer names a missing source', stale)):
        for f in fs:
            print(f'{kind}: {f}')
    ok = not (differ or missing or extra or stale)
    print(f'extraction check: {"PASS" if ok else "FAIL"} ({total} generated files'
          f'{"" if ok else f"; {len(differ)} differ, {len(missing)} missing, {len(extra)} extra"})')
    return 0 if ok else 1


if __name__ == '__main__':
    if '--check' in sys.argv[1:]:
        sys.exit(check(sys.argv[1:]))
    main()
