#!/usr/bin/env python3
"""Phase 291: the evidenced semantic names of the program data and the derived targets of its far pointers.

The DS offsets that the native code names are the #define constants of include/xeen/ds_texts.h (XEEN_TEXT_*) and
include/xeen/ds_globals.h (XEEN_DS_*), each documented there with its original readers. This module maps every such
constant onto the program-data catalogue (src/data: text entries, far-pointer tables, typed tables, structures,
opaque and zero regions) and derives from that mapping, deterministically:

  - the canonical `name` of a text entry or far-pointer table: the one constant whose value is the entry's first
    byte, lower-cased without its XEEN_TEXT_/XEEN_DS_ prefix (XEEN_TEXT_ARENA_LEVEL_PROMPT -> arena_level_prompt),
    and its `c_name`. A typed table's or structure's id is already its name: its `c_name` is recorded, and a
    constant at its start that is worded differently but names the same table is an evidenced `name_aliases`
    entry (SAME_TABLE below, each reviewed);
  - `interior_names`: constants that point inside an entry (a registered text alias, a member of a structure, an
    element of a typed table, a slot of a pointer table, or a reviewed shared suffix of a text);
  - for each far pointer, its target (`targets`, parallel to `entries`): the entry it points at or into.

Nothing here is guessed from text or byte patterns: a name exists only where a header constant with its reader
evidence names that exact offset. Everything that does not fit (a constant in the middle of an entry that is not
reviewed, two constants at one start, a duplicated name, a reviewed item that no longer matches) is an error, so
the metadata cannot go stale silently. The raw pointer words ("ds:XXXX", "code:XXXX") stay the source of truth;
the targets are derived from them and the current catalogue.

The extractor (extract_program_data.py) writes these fields into the metadata (tools/data/meta, Phase 300; src/data
before); `check` recomputes them from the headers and the committed sources and metadata without XEEN.DAT and fails
on any difference.

Usage: ds_names.py check [--root ROOT] [--data DIR] [--meta DIR]   # the committed metadata is current (no XEEN.DAT)
       ds_names.py report [--root ROOT] [--json FILE]  # the classification of every constant
       ds_names.py self-test [--root ROOT]             # the negative controls
"""
import argparse, copy, json, re, shutil, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_program_data as G                      # noqa: E402

DS_SIZE = 0x8C4C
HEADERS = ('include/xeen/ds_texts.h', 'include/xeen/ds_globals.h')
DEFINE = re.compile(r'^#define\s+(XEEN_(?:DS|TEXT)_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+)u?\b(.*)$')
FIELDS = ('name', 'c_name', 'name_aliases', 'interior_names', 'targets')   # the derived fields (stripped by check)

# Reviewed: hex constants of the two headers that are not DS offsets.
NON_OFFSET = {
    'XEEN_DS_SEGMENT': 'the segment word value 4853h that the native stores for a far pointer into the DS',
    'XEEN_DS_SCRATCH_BYTES': 'the length (2000 bytes) of the scratch area XEEN_DS_SCRATCH at 9C6F',
}
# Reviewed: a constant at a typed table's or structure's first byte, worded differently from its id, that names the
# same table (its header comment cites the id): an evidenced alias of the id.
SAME_TABLE = {
    'XEEN_DS_MISC_POWER_SPELLS': ('misc_power_spell', 'the header: "program data misc_power_spell"'),
    'XEEN_DS_CONDITION_COLOURS': ('condition_colour', 'the header: "condition_colour: by condition 0..16"'),
    'XEEN_DS_LOCATION_EXITS': ('location_exit_cell', 'the header: "program data location_exit_cell"'),
    'XEEN_DS_COMMAND_ICONS': ('command_icon_list', 'the header: "the program data\'s command_icon_list"'),
}
# Reviewed: a constant at a table's first byte that names only its first element or row, not the table.
FIRST_ELEMENT = {
    'XEEN_DS_CD_PLAY_TRACK': ('cd_play', 'the first of its three words (the track), as XEEN_DS_CD_PLAY_START and '
                              'XEEN_DS_CD_PLAY_END name the others (Phase 332)'),
    'XEEN_DS_CLOUDS_ENDING_MARK': ('p0096', 'slot 0 of the three far pointers: its readers 334DE/334E2 and 15F60/15F64 '
                                   'take only slot 0 (the Clouds ending mark t30A2); slots 1 and 2 are the save '
                                   'prefixes darkside_prefix and clouds_prefix (Phase 329)'),
    'XEEN_DS_RACE_RESISTANCE_MAGIC': ('race_creation_values', 'the first of the six five-byte rows (magic, copied '
                                      'to +141h by 3FF1C), as XEEN_DS_RACE_RESISTANCE_FIRE.. name the others'),
}
# Reviewed: a virtual base, a constant below an entry that its reader indexes from (displacement = value - start).
VIRTUAL_BASES = {
    'XEEN_DS_RACE_SP_BONUS_BASE': ('race_sp_bonus', -1, '46500 reads [bx + 298F] with bx = 2 x race + stat, stat 1..2 '
                                   '(Phase 329)'),
    'XEEN_DS_MONSTER_EFFECT_FLAGS': ('monster_effect_flags', -0x10, 'read at + 16 x effect + 2 x step for effects '
                                     '1..15 (effect 0\'s row, inside the text before, is never read)'),
    'XEEN_DS_SPELL_COST_NAME_BASE': ('p27B6', -4, '39BBA indexes [bx + 27B2] with bx = 4 x reason, reason 1..2 '
                                     '(the audit\'s override of 39BB6/39BBA)'),
    'XEEN_DS_TOWN_PORTAL_LOAD_BASE': ('town_portal_maps', -1, '3B0D2 reads 67C3[FB00 x 10 + n] with n 1..5 (the '
                                      'audit\'s override of 3B0D2)'),
}
# Reviewed: a text constant inside a text entry at an offset the extractor did not register as an alias (the
# original reads the entry's tail: ds_text reads to the 00, so the name is the suffix from there).
SUFFIXES = {
    'XEEN_TEXT_DETAILS_FONT_RESET': ('t777E', 'the entry\'s last byte, the normal-font control 01h'),
    'XEEN_TEXT_EMPTY': ('t7CEE', 'the terminator of "cast.icn": an empty string'),
    'XEEN_TEXT_PLURAL': ('t7D0D', 'the tail "s" of the entry'),
    'XEEN_TEXT_ITEMS_LEFT': ('t7E46', 'the tail "{03}l" of the entry'),
}


class NameError_(Exception):
    pass


def canonical(c):
    return re.sub(r'^XEEN_(?:DS|TEXT)_', '', c).lower()


def header_constants(root):
    """Every '#define XEEN_DS_/XEEN_TEXT_ NAME 0xHEX' of the two headers: name, value, header and the comment that
    documents it (the define line's trailing comment, else the block comment directly above its group)."""
    out = []
    for h in HEADERS:
        lines = (Path(root) / h).read_text().split('\n')
        for k, line in enumerate(lines):
            m = DEFINE.match(line)
            if not m:
                continue
            note = ''
            tail = m.group(3).strip()
            if tail.startswith('/*'):
                body, j = tail, k
                while '*/' not in body and j + 1 < len(lines):
                    j += 1
                    body += ' ' + lines[j].strip().lstrip('*').strip()
                note = body
            else:
                j = k - 1
                while j >= 0 and lines[j].startswith('#define'):
                    j -= 1
                if j >= 0 and lines[j].rstrip().endswith('*/'):
                    end = j
                    while j >= 0 and '/*' not in lines[j]:
                        j -= 1
                    note = ' '.join(x.strip().lstrip('*').strip() for x in lines[j:end + 1])
            note = re.sub(r'\s+', ' ', note.replace('/*', '').replace('*/', '')).strip()
            out.append({'c': m.group(1), 'value': int(m.group(2), 16), 'header': h, 'note': note})
    return out


def catalogue(J):
    """The catalogue: (start, end, kind, id, entry) of every entry and region, in address order."""
    spans = []
    for e in J['ds_text.json']['entries']:
        o = int(e['offset'], 16)
        spans.append((o, o + e['length'] + 1, 'text', e['id'], e))
    for t in J['ds_pointers.json']['tables']:
        o = int(t['offset'], 16)
        spans.append((o, o + 4 * len(t['entries']), 'pointers', t['id'], t))
    for t in J['ds_tables.json']['tables']:
        o = int(t['offset'], 16)
        spans.append((o, o + t['count'] * G.TYPES[t['type']][1], 'table', t['id'], t))
    for t in J['ds_structs.json']['structs']:
        o = int(t['offset'], 16)
        spans.append((o, o + t['size'], 'struct', t['id'], t))
    for r in J['program_ds.json']['regions']:
        if r['kind'] in ('bytes', 'zero'):
            o = int(r['at'], 16)
            spans.append((o, o + r['size'], r['kind'], 'b' + r['at'] if r['kind'] == 'bytes' else 'z' + r['at'],
                          r))
    spans.sort(key=lambda s: s[0])
    return spans


def locate(spans, off):
    lo, hi = 0, len(spans)
    while lo < hi:
        mid = (lo + hi) // 2
        if spans[mid][1] <= off:
            lo = mid + 1
        else:
            hi = mid
    s = spans[lo] if lo < len(spans) else None
    return s if s and s[0] <= off < s[1] else None


def interior_kind(s, off):
    """How an offset inside entry s (not its start) is addressed: alias, member, element, slot, or None."""
    start, _, kind, _, e = s
    rel = off - start
    if kind == 'text':
        return 'alias' if any(int(a, 16) == off for a, _ in e.get('aliases', [])) else None
    if kind == 'struct':
        return 'member' if any(int(m['offset'], 16) == off for m in e['members']) else None
    if kind == 'table':
        return 'element' if rel % G.TYPES[e['type']][1] == 0 else None
    if kind == 'pointers':
        return 'slot' if rel % 4 == 0 else None
    return None


def classify(J, consts):
    """Each constant's place: a list of dicts (c, value, header, note, cls, id, rel) and the errors found."""
    spans = catalogue(J)
    by_id = {s[3]: s for s in spans}
    out, errors = [], []
    used = {k: set() for k in ('NON_OFFSET', 'SAME_TABLE', 'FIRST_ELEMENT', 'VIRTUAL_BASES', 'SUFFIXES')}
    for x in consts:
        c, v = x['c'], x['value']
        r = dict(x, cls=None, id=None, rel=0)
        out.append(r)
        if c in NON_OFFSET:
            r['cls'] = 'non-offset'
            used['NON_OFFSET'].add(c)
            continue
        if c in VIRTUAL_BASES:
            tid, disp, _ = VIRTUAL_BASES[c]
            used['VIRTUAL_BASES'].add(c)
            t = by_id.get(tid)
            if t is None or v != t[0] + disp:
                errors.append(f'{c} = {v:04X}: reviewed virtual base of {tid}{disp:+d}, which is no longer there')
            r.update(cls='virtual-base', id=tid, rel=disp)
            continue
        if c.endswith('_BYTES') and v < DS_SIZE:
            errors.append(f'{c} = {v:04X}: a _BYTES constant inside the image must be reviewed (NON_OFFSET)')
        if v >= DS_SIZE:
            r['cls'] = 'bss'
            continue
        s = locate(spans, v)
        if s is None:
            errors.append(f'{c} = {v:04X}: no catalogue entry holds it (the layout has a gap)')
            continue
        r['id'], r['rel'] = s[3], v - s[0]
        if s[2] in ('bytes', 'zero'):
            r['cls'] = 'region'
            continue
        if v == s[0]:
            if c in FIRST_ELEMENT:
                used['FIRST_ELEMENT'].add(c)
                if FIRST_ELEMENT[c][0] != s[3]:
                    errors.append(f'{c}: reviewed first element of {FIRST_ELEMENT[c][0]}, but it starts {s[3]}')
                r['cls'] = 'element'
            elif s[2] == 'struct' and canonical(c) != s[3] and c not in SAME_TABLE:
                r['cls'] = 'member'                   # the first member (a structure's id is its own name)
            else:
                r['cls'] = 'start'
                if c in SAME_TABLE:
                    used['SAME_TABLE'].add(c)
                    if SAME_TABLE[c][0] != s[3]:
                        errors.append(f'{c}: reviewed alias of {SAME_TABLE[c][0]}, but it starts {s[3]}')
            continue
        if c in SUFFIXES:
            used['SUFFIXES'].add(c)
            if SUFFIXES[c][0] != s[3] or s[2] != 'text':
                errors.append(f'{c} = {v:04X}: reviewed suffix of {SUFFIXES[c][0]}, but it is inside {s[3]}')
            r['cls'] = 'suffix'
            continue
        k = interior_kind(s, v)
        if k is None:
            errors.append(f'{c} = {v:04X}: inside {s[3]} at +{v - s[0]:X}, not at an alias, member, element or slot '
                          '(an unexpected middle-of-entry reference: review it or correct the constant)')
            r['cls'] = 'unexpected'
            continue
        r['cls'] = k
    for table, names in (('NON_OFFSET', NON_OFFSET), ('SAME_TABLE', SAME_TABLE), ('FIRST_ELEMENT', FIRST_ELEMENT),
                         ('VIRTUAL_BASES', VIRTUAL_BASES), ('SUFFIXES', SUFFIXES)):
        for c in sorted(set(names) - used[table]):
            errors.append(f'{table} reviews {c}, which the headers no longer define (stale review)')
    return out, errors


def derive(J, root=ROOT):
    """The derived metadata: {entry id: {field: value}} for texts, pointer tables, typed tables and structures, the
    classification and the errors."""
    consts = header_constants(root)
    rows, errors = classify(J, consts)
    spans = catalogue(J)
    by_id = {s[3]: s for s in spans}
    meta = {}
    starts = {}
    for r in rows:
        if r['cls'] == 'start':
            starts.setdefault(r['id'], []).append(r)
    names = {}
    for sid, rs in sorted(starts.items()):
        kind = by_id[sid][2]
        m = meta.setdefault(sid, {})
        if kind in ('text', 'pointers'):
            if len(rs) > 1:
                errors.append(f'{sid}: several constants at its start ({", ".join(r["c"] for r in rs)}): resolve '
                              'which one names it (the entry stays address-named)')
                continue
            m['name'], m['c_name'] = canonical(rs[0]['c']), rs[0]['c']
            names.setdefault(m['name'], []).append(sid)
        else:
            own = [r for r in rs if canonical(r['c']) == sid]
            other = [r for r in rs if canonical(r['c']) != sid]
            if len(own) > 1 or (not own and len(other) > 1):
                errors.append(f'{sid}: several constants name it ({", ".join(r["c"] for r in rs)})')
                continue
            m['c_name'] = (own or other)[0]['c']
            for r in other:
                if r['c'] not in SAME_TABLE:
                    errors.append(f'{r["c"]}: at the start of {sid} but neither its id nor a reviewed alias')
                    continue
                m.setdefault('name_aliases', []).append({'name': canonical(r['c']), 'c_name': r['c']})
            if own and other:
                m['c_name'] = own[0]['c']
    for sid in sorted(by_id):
        if by_id[sid][2] in ('table', 'struct'):
            names.setdefault(sid, []).append(sid)
            for a in meta.get(sid, {}).get('name_aliases', []):
                names.setdefault(a['name'], []).append(sid)
    for n, ids in sorted(names.items()):
        if len(ids) > 1:
            errors.append(f'the name {n} is given to {", ".join(ids)}')
    for r in rows:
        if r['cls'] in ('alias', 'member', 'element', 'slot', 'suffix', 'virtual-base'):
            m = meta.setdefault(r['id'], {})
            m.setdefault('interior_names', []).append(
                {'offset': '%04X' % r['value'], 'kind': r['cls'], 'name': canonical(r['c']), 'c_name': r['c']})
    for m in meta.values():
        if 'interior_names' in m:
            m['interior_names'].sort(key=lambda x: (x['offset'], x['c_name']))
    for t in J['ds_pointers.json']['tables']:
        meta.setdefault(t['id'], {})['targets'] = [target(spans, meta, p) for p in t['entries']]
    return meta, rows, errors


def target(spans, meta, ptr):
    """A far pointer's target, derived from its raw words: 'code' (raw segment 0000h, a code offset), 'bss' (a DS
    offset at or above 8C4C), else '<kind> <id>' with '+<hex>' for an offset inside the entry, then the evidenced
    name at that exact offset when there is one."""
    seg, _, off = ptr.partition(':')
    if seg == 'code':
        return 'code'
    o = int(off, 16)
    if o >= DS_SIZE:
        return 'bss'
    s = locate(spans, o)
    if s is None:
        return 'unknown'
    out = f'{s[2]} {s[3]}' + ('' if o == s[0] else f'+{o - s[0]:X}')
    m = meta.get(s[3], {})
    if o == s[0] and 'name' in m:
        out += ' ' + m['name']
    else:
        n = [x['name'] for x in m.get('interior_names', []) if int(x['offset'], 16) == o and x['kind'] != 'virtual-base']
        if n:
            out += ' ' + n[0]
    return out


def entry_lists(J):
    return {'text': J['ds_text.json']['entries'], 'pointers': J['ds_pointers.json']['tables'],
            'table': J['ds_tables.json']['tables'], 'struct': J['ds_structs.json']['structs']}


def apply(J, root=ROOT):
    """Write the derived fields into the merged sources J (the extractor's step); raises on any error."""
    strip(J)
    meta, _, errors = derive(J, root)
    if errors:
        raise NameError_('\n'.join(errors))
    for kind, lst in entry_lists(J).items():
        for i, e in enumerate(lst):
            m = meta.get(e['id'])
            if not m:
                continue
            keys = list(e)
            at = keys.index('id') + 1
            items = list(e.items())
            new = [(k, m[k]) for k in ('name', 'c_name', 'name_aliases') if k in m]
            items = items[:at] + new + items[at:]
            for k in ('interior_names', 'targets'):
                if k in m:
                    if k == 'targets':                # right after the raw entries it is derived from
                        p = [x[0] for x in items].index('entries') + 1
                        items.insert(p, (k, m[k]))
                    else:
                        items.append((k, m[k]))
            lst[i] = dict(items)
    return J


def strip(J):
    for lst in entry_lists(J).values():
        for e in lst:
            for k in FIELDS:
                e.pop(k, None)


def check(data_dir, root=ROOT, meta_dir=None):
    """The committed metadata equals what the headers and the catalogue derive: a list of differences."""
    J = G.load_sources(data_dir, meta_dir)
    have = {e['id']: {k: e[k] for k in FIELDS if k in e} for lst in entry_lists(J).values() for e in lst}
    K = copy.deepcopy(J)
    strip(K)
    meta, _, errors = derive(K, root)
    for sid in sorted(set(have) | set(meta)):
        got, want = have.get(sid, {}), meta.get(sid, {})
        for k in FIELDS:
            if got.get(k) != want.get(k):
                errors.append(f'{sid}.{k}: committed {json.dumps(got.get(k))[:120]} != derived '
                              f'{json.dumps(want.get(k))[:120]} (regenerate with extract_program_data.py)')
    return errors


def report(root=ROOT, data_dir=None, meta_dir=None):
    J = G.load_sources(data_dir or Path(root) / 'src' / 'data', meta_dir)
    strip(J)
    meta, rows, errors = derive(J, root)
    counts = {}
    for r in rows:
        counts[r['cls']] = counts.get(r['cls'], 0) + 1
    named = {k: sum(1 for e in lst if 'name' in meta.get(e['id'], {}) or 'c_name' in meta.get(e['id'], {}))
             for k, lst in entry_lists(J).items()}
    tk = {}
    for t in J['ds_pointers.json']['tables']:
        for x in meta[t['id']]['targets']:
            k = x.split()[0] + ('-interior' if '+' in x else '')
            tk[k] = tk.get(k, 0) + 1
    total = {k: len(v) for k, v in entry_lists(J).items()}
    return {'constants': len(rows), 'by_class': dict(sorted(counts.items())), 'named_entries': named,
            'entries': total, 'unnamed': {k: total[k] - named[k] for k in total},
            'name_aliases': sum(len(m.get('name_aliases', [])) for m in meta.values()),
            'interior_names': sum(len(m.get('interior_names', [])) for m in meta.values()),
            'pointer_targets': dict(sorted(tk.items())), 'errors': errors,
            'reviewed': {'NON_OFFSET': len(NON_OFFSET), 'SAME_TABLE': len(SAME_TABLE),
                         'FIRST_ELEMENT': len(FIRST_ELEMENT), 'VIRTUAL_BASES': len(VIRTUAL_BASES),
                         'SUFFIXES': len(SUFFIXES)}}


def self_test(root=ROOT):
    """Deliberately broken copies (headers and data in a temporary root): each must fail for its stated reason."""
    ok = True

    def case(what, mutate, expect):
        nonlocal ok
        with tempfile.TemporaryDirectory(prefix='xeen-ds-names-') as tmp:
            t = Path(tmp)
            (t / 'include' / 'xeen').mkdir(parents=True)
            for h in HEADERS:
                shutil.copy(Path(root) / h, t / h)
            shutil.copytree(Path(root) / 'src' / 'data', t / 'data')
            shutil.copytree(Path(root) / 'tools' / 'data' / 'meta', t / 'meta')      # Phase 300
            mutate(t)
            try:
                errs = check(t / 'data', t, t / 'meta')
            except Exception as err:                 # noqa: BLE001  (a broken copy may fail to load)
                errs = [f'{type(err).__name__}: {err}']
            hit = [e for e in errs if re.search(expect, e)]
            good = bool(hit) if expect else not errs
            ok &= good
            print(f'{what:46s} {"FAILS as expected" if expect and good else "PASSES" if good else "WRONG"}: '
                  f'{(hit or errs or ["no error"])[0][:150]}')

    def edit(path, old, new, count=1):
        def f(t):
            p = t / path
            s = p.read_text()
            if old not in s:
                raise SystemExit(f'self-test: {old!r} not in {path}')
            p.write_text(s.replace(old, new, count))
        return f

    def data(fn):
        def f(t):
            J = G.load_sources(t / 'data', t / 'meta')
            fn(J)
            G.write_sources(J, t / 'data', t / 'meta')
        return f

    case('the committed metadata is current', lambda t: None, None)
    case('a renamed text constant (stale name)',
         edit('include/xeen/ds_texts.h', 'XEEN_TEXT_ARENA_LEVEL_PROMPT', 'XEEN_TEXT_ARENA_LEVEL_ASK'),
         r't6712\.name')
    case('a constant moved into the middle of a text',
         edit('include/xeen/ds_texts.h', 'XEEN_TEXT_ARENA_LEVEL_PROMPT 0x6712u', 'XEEN_TEXT_ARENA_LEVEL_PROMPT 0x6714u'),
         r'unexpected middle-of-entry')
    case('two constants at one text start (conflict)',
         edit('include/xeen/ds_texts.h', '#define XEEN_TEXT_ARENA_COUNT_PROMPT 0x6732u',
              '#define XEEN_TEXT_ARENA_COUNT_PROMPT 0x6712u'), r'several constants at its start')
    case('one name for two entries (duplicate)',
         edit('include/xeen/ds_texts.h', 'XEEN_TEXT_ARENA_COUNT_PROMPT 0x6732u', 'XEEN_DS_ARENA_LEVEL_PROMPT 0x6732u'),
         r'the name arena_level_prompt is given to')
    case('a virtual base whose table moved',
         edit('include/xeen/ds_globals.h', 'XEEN_DS_MONSTER_EFFECT_FLAGS 0x49cau', 'XEEN_DS_MONSTER_EFFECT_FLAGS 0x49ccu'),
         r'reviewed virtual base of monster_effect_flags')
    case('a reviewed constant that is gone (stale review)',
         edit('include/xeen/ds_texts.h', 'XEEN_TEXT_PLURAL ', 'XEEN_TEXT_PLURALS '), r'stale review')
    case('an unreviewed name at a table start',
         edit('include/xeen/ds_globals.h', 'XEEN_DS_MISC_POWER_SPELLS 0x28d6u', 'XEEN_DS_MISC_POWERS 0x28d6u'),
         r'neither its id nor a reviewed alias')
    case('a size constant read as an offset',
         edit('include/xeen/ds_globals.h', '#define XEEN_DS_SCRATCH_BYTES 0x7d0u',
              '#define XEEN_DS_SCRATCH_BYTES 0x7d0u\n#define XEEN_DS_LINE_BYTES 0x32u'), r'_BYTES constant')
    case('a hand-edited name in the metadata',
         data(lambda J: next(e for e in J['ds_text.json']['entries'] if e['id'] == 't6712').update(name='x')),
         r't6712\.name: committed "x"')
    case('a hand-edited pointer target',
         data(lambda J: J['ds_pointers.json']['tables'][0]['targets'].__setitem__(0, 'text t0004')),
         r'\.targets: committed')
    case('a pointer word changed under its target',
         data(lambda J: next(t for t in J['ds_pointers.json']['tables'] if t['id'] == 'p17A2')['entries']
              .__setitem__(0, 'ds:3220')), r'p17A2\.targets')
    return ok


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    for n in ('check', 'report', 'self-test'):
        p = sub.add_parser(n)
        p.add_argument('--root', default=str(ROOT))
        if n != 'self-test':
            p.add_argument('--data')
            p.add_argument('--meta', help='the metadata of --data (default tools/data/meta)')
        if n == 'report':
            p.add_argument('--json')
    a = ap.parse_args(argv)
    root = Path(a.root)
    if a.cmd == 'check':
        errs = check(a.data or root / 'src' / 'data', root, a.meta)
        for e in errs:
            print(e)
        print(f'ds_names check: {"PASS" if not errs else "FAIL"} ({len(errs)} problems)')
        return 1 if errs else 0
    if a.cmd == 'report':
        r = report(root, a.data, a.meta)
        s = json.dumps(r, indent=1)
        if a.json:
            Path(a.json).write_text(s + '\n')
        print(s)
        return 1 if r['errors'] else 0
    ok = self_test(root)
    print(f'ds_names self-test: {"PASS" if ok else "FAIL"}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
