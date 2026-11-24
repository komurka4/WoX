#!/usr/bin/env python3
"""Phase 184: build-time generator of the native program data (run by CMake).

Phase 300: the sources in DATA_DIR (default src/data/) hold only what rebuilds the image, plus each table's and
structure's native "consumers":

  manifest.json      the image's SHA-256, the RNG seed words and their SHA-256
  texts/*.tsv        one string per line, "OFFSET<TAB>text" (header "offset<TAB>text"); a file covers the
                     address range from the four hex digits its name starts with to the next file's
  pointers.json      far-pointer tables: offset, entries ("ds:XXXX" / "code:XXXX")
  tables.json        typed tables: id, offset, type, values (flat), consumers
  structs.json       record_types (field -> type or [type, count]) and the structures: id, offset, members
                     (name with type+values, record+count, chars or hex), records_csv, consumers
  records/<id>.csv   a structure's record rows in order, one column per field (array fields name[k])
  opaque.json        the opaque ("hex") and zero ("zero": size) regions

Every entry sits at its own offset; sorted, they must tile DS:0000..8C4B exactly. It never reads XEEN.DAT. It
rebuilds the initialized DS image byte for byte, checks the tiling, every text (no embedded 00, valid tokens, inside
its file's range, ascending), the pointers' raw segments, the table types, the structures' members and CSVs, and that
the image's SHA-256 and the seed bytes' SHA-256 equal manifest.json; any failure stops the build.

Everything else (names, evidence, readers, descriptions, shapes, labels) is metadata in tools/data/meta/, written by
the extractor beside src/data and never read by the build. load_sources(data, meta) merges the two into the model
every tool works on (the Phase 291 form: program_ds.json, ds_text.json, ...; derived fields such as ids, lengths,
counts, sizes and offsets recomputed); write_sources(J, data, meta) is its inverse.

Output: a C file with xeen_program_ds_image[], xeen_program_rng_seed[] and the
digest (include/xeen/program_data.h), or the raw image with --image.

Usage: gen_program_data.py [--data DIR] (--c OUT.c | --image OUT.bin)...
"""
import argparse, csv, hashlib, io, json, re, struct, sys
from pathlib import Path

TYPES = {'int8': ('b', 1), 'uint8': ('B', 1), 'int16': ('h', 2), 'uint16': ('H', 2)}
SEGMENTS = {'ds': 0x481D, 'code': 0x0000}
TOKEN = re.compile(r'\{([0-9A-F]{2})\}|([\x20-\x7A\x7C\x7E])')
DS_SIZE = 0x8C4C
META_DIR = Path(__file__).resolve().parent / 'meta'
DATA_FILES = ('manifest.json', 'pointers.json', 'tables.json', 'structs.json', 'opaque.json')
DATA_SUBDIRS = ('texts', 'records')
META_FILES = ('headers.json', 'texts.json', 'pointers.json', 'tables.json', 'structs.json')
PART_NAME = re.compile(r'^([0-9A-F]{4})_[a-z0-9_]+\.tsv$')
HEX4 = re.compile(r'[0-9A-F]{4}')
# The merged model's file names (the Phase 184..291 layout, kept as the in-memory model's keys).
NAMES = ('program_ds.json', 'ds_text.json', 'ds_pointers.json', 'ds_tables.json', 'ds_structs.json',
         'driver_constants.json')


class DataError(Exception):
    pass


def decode_text(s, where):
    out, p = bytearray(), 0
    while p < len(s):
        m = TOKEN.match(s, p)
        if not m:
            raise DataError(f'{where}: invalid text at character {p}: {s[p:p + 8]!r} '
                            '(printable ASCII except braces, or {XX} upper-case hex)')
        out.append(int(m.group(1), 16) if m.group(1) else ord(m.group(2)))
        p = m.end()
    return bytes(out)


def canonical_chars(b):
    return ''.join(chr(c) if 0x20 <= c <= 0x7E and c not in (0x7B, 0x7D) else '{%02X}' % c for c in b)


def pack(typ, values, where):
    if typ not in TYPES:
        raise DataError(f'{where}: unknown type {typ!r}')
    try:
        return struct.pack('<%d%s' % (len(values), TYPES[typ][0]), *values)
    except struct.error as err:
        raise DataError(f'{where}: {err}')


def hex4(v, where):
    if not isinstance(v, str) or not HEX4.fullmatch(v):
        raise DataError(f'{where}: {v!r} is not four upper-case hex digits')
    return int(v, 16)


def read_json(path, what):
    try:
        return json.loads(Path(path).read_text(encoding='ascii'))
    except FileNotFoundError:
        raise DataError(f'missing source: {path}')
    except (json.JSONDecodeError, UnicodeDecodeError) as err:
        raise DataError(f'{what}: {err}')


# ---- record types --------------------------------------------------------------------------------------------

def field_list(rt, name, where):
    """A record type's fields as (name, type, count or None), from structs.json's {field: type | [type, count]}."""
    if name not in rt or not isinstance(rt[name], dict) or not rt[name]:
        raise DataError(f'{where}: unknown record type {name!r}')
    out = []
    for f, t in rt[name].items():
        typ, n = (t[0], t[1]) if isinstance(t, list) and len(t) == 2 else (t, None)
        if typ not in TYPES or (n is not None and (not isinstance(n, int) or n < 1)):
            raise DataError(f'record type {name}: field {f} has a bad type {t!r}')
        out.append((f, typ, n))
    return out


def record_size(fields):
    return sum(TYPES[t][1] * (n or 1) for _, t, n in fields)


def csv_header(fields):
    return [c for f, _, n in fields for c in (['%s[%d]' % (f, k) for k in range(n)] if n else [f])]


# ---- the sources (src/data) ----------------------------------------------------------------------------------

def text_parts(d):
    """The text files in address order: [(relative path, start, end, [(offset, text)])]."""
    files = sorted((Path(d) / 'texts').glob('*.tsv'))
    if not files:
        raise DataError(f'missing source: {Path(d) / "texts"}/*.tsv')
    starts = []
    for f in files:
        m = PART_NAME.match(f.name)
        if not m:
            raise DataError(f'texts/{f.name}: the name must be XXXX_words.tsv (XXXX: its range start, hex)')
        starts.append(int(m.group(1), 16))
    parts = []
    for k, f in enumerate(files):
        lo, hi = starts[k], starts[k + 1] if k + 1 < len(files) else DS_SIZE
        rel = 'texts/' + f.name
        body = f.read_text(encoding='ascii').split('\n')
        if body[0] != 'offset\ttext' or body[-1] != '':
            raise DataError(f'{rel}: the first line must be "offset<TAB>text" and the file must end with a newline')
        rows, last = [], lo - 1
        for n, line in enumerate(body[1:-1], 2):
            off, sep, txt = line.partition('\t')
            if not sep or not HEX4.fullmatch(off):
                raise DataError(f'{rel} line {n}: not "OFFSET<TAB>text"')
            o = int(off, 16)
            if not lo <= o < hi:
                raise DataError(f'{rel} line {n}: {off} is outside its part ({lo:04X}..{hi:04X})')
            if o <= last:
                raise DataError(f'{rel} line {n}: {off} is not in ascending order')
            last = o
            rows.append((off, txt))
        parts.append((rel, lo, hi, rows))
    return parts


def record_rows(d, s, fields):
    """A structure's record rows from its CSV (header = the record type's columns), as nested values."""
    rel = s['records_csv']
    try:
        rows = list(csv.reader(io.StringIO((Path(d) / rel).read_text(encoding='ascii'))))
    except FileNotFoundError:
        raise DataError(f"struct {s['id']}: {rel} is missing")
    want = csv_header(fields)
    if not rows or rows[0] != want:
        raise DataError(f'{rel}: header {rows[:1]} != {[want]}')
    out = []
    for k, row in enumerate(rows[1:], 2):
        if len(row) != len(want):
            raise DataError(f'{rel} line {k}: {len(row)} columns, expected {len(want)}')
        try:
            cells = [int(x) for x in row]
        except ValueError:
            raise DataError(f'{rel} line {k}: a value is not a decimal integer')
        rec, at = [], 0
        for _, _, n in fields:
            rec.append(cells[at:at + n] if n else cells[at])
            at += n or 1
        out.append(rec)
    return out


def load_data(d):
    """The sources of DATA_DIR, checked and with derived layout: {'manifest', 'texts': [(off, text, part)],
    'parts', 'pointers', 'tables', 'structs': {record_types, structs (members with offsets and values)},
    'opaque'}."""
    d = Path(d)
    man = read_json(d / 'manifest.json', 'manifest.json')
    parts = text_parts(d)
    texts = [(off, txt, rel) for rel, _, _, rows in parts for off, txt in rows]
    pointers = read_json(d / 'pointers.json', 'pointers.json')
    tables = read_json(d / 'tables.json', 'tables.json')
    S = read_json(d / 'structs.json', 'structs.json')
    opaque = read_json(d / 'opaque.json', 'opaque.json')
    rt = S.get('record_types', {})
    for s in S.get('structs', []):
        base = hex4(s.get('offset'), f"struct {s.get('id')}")
        rows, used, at = None, 0, base
        for m in s['members']:
            where = f"{s['id']}.{m.get('name')}"
            m['_offset'] = at
            if 'record' in m:
                fields = field_list(rt, m['record'], where)
                if rows is None:
                    if 'records_csv' not in s:
                        raise DataError(f"struct {s['id']}: record members without records_csv")
                    rows = record_rows(d, s, fields)
                if not isinstance(m.get('count'), int) or used + m['count'] > len(rows):
                    raise DataError(f"{where}: {m.get('count')} records, the CSV has {len(rows) - used} left")
                m['_values'] = rows[used:used + m['count']]
                used += m['count']
                at += record_size(fields) * m['count']
            elif 'chars' in m:
                at += len(decode_text(m['chars'], where))
            elif 'hex' in m:
                if not re.fullmatch(r'(?:[0-9A-F]{2})+', m['hex']):
                    raise DataError(f'{where}: raw bytes not upper-case hex')
                at += len(m['hex']) // 2
            elif m.get('type') in TYPES and isinstance(m.get('values'), list):
                at += TYPES[m['type']][1] * len(m['values'])
            else:
                raise DataError(f'{where}: a member is type+values, record+count, chars or hex')
        if rows is not None and used != len(rows):
            raise DataError(f"{s['records_csv']}: {len(rows)} rows, the record members take {used}")
        s['_size'] = at - base
    return {'manifest': man, 'texts': texts, 'parts': parts, 'pointers': pointers, 'tables': tables,
            'structs': S, 'opaque': opaque}


def member_bytes(m, rt, where):
    if 'record' in m:
        fields = field_list(rt, m['record'], where)
        out = bytearray()
        for i, row in enumerate(m['_values']):
            for (f, typ, n), v in zip(fields, row):
                out += pack(typ, v if n else [v], f'{where}[{i}].{f}')
        return bytes(out)
    if 'chars' in m:
        return decode_text(m['chars'], where)
    if 'hex' in m:
        return bytes.fromhex(m['hex'])
    return pack(m['type'], m['values'], where)


def build(data_dir):
    """The image, the seed words and the digest, from DATA_DIR alone."""
    D = load_data(data_dir)
    pieces = []
    for off, txt, rel in D['texts']:
        b = decode_text(txt, f'{rel} {off}')
        if b'\0' in b:
            raise DataError(f'{rel} {off}: embedded 00')
        pieces.append((int(off, 16), b + b'\0', 'text ' + off))
    for t in D['pointers']:
        where = f"pointers {t.get('offset')}"
        b = bytearray()
        for ent in t['entries']:
            seg, _, off = ent.partition(':')
            if seg not in SEGMENTS or not HEX4.fullmatch(off):
                raise DataError(f'{where}: bad far pointer {ent!r}')
            b += struct.pack('<HH', int(off, 16), SEGMENTS[seg])
        pieces.append((hex4(t.get('offset'), where), bytes(b), where))
    for t in D['tables']:
        if not all(isinstance(v, int) and not isinstance(v, bool) for v in t.get('values', [None])):
            raise DataError(f"table {t.get('id')}: values must be a flat list of integers")
        pieces.append((hex4(t.get('offset'), f"table {t.get('id')}"), pack(t['type'], t['values'], t['id']),
                       'table ' + t['id']))
    rt = D['structs'].get('record_types', {})
    for s in D['structs'].get('structs', []):
        b = b''.join(member_bytes(m, rt, f"{s['id']}.{m.get('name')}") for m in s['members'])
        pieces.append((int(s['offset'], 16), b, 'struct ' + s['id']))
    for r in D['opaque']:
        where = f"opaque {r.get('at')}"
        if 'zero' in r:
            b = bytes(r['zero'])
        elif re.fullmatch(r'(?:[0-9A-F]{2})+', r.get('hex', '')):
            b = bytes.fromhex(r['hex'])
        else:
            raise DataError(f'{where}: "zero": size or "hex": upper-case hex')
        pieces.append((hex4(r.get('at'), where), b, where))
    pieces.sort(key=lambda p: p[0])
    image = bytearray()
    for off, b, what in pieces:
        if off != len(image):
            raise DataError(f'{what} at {off:04X}: expected {len(image):04X} (gap or overlap)')
        image += b
    if len(image) != DS_SIZE:
        raise DataError(f'image is {len(image):#x} bytes, the DS needs {DS_SIZE:#x}')
    man = D['manifest']
    digest = hashlib.sha256(image).hexdigest()
    if digest != man.get('ds_sha256'):
        raise DataError(f"image SHA-256 {digest} != recorded {man.get('ds_sha256')}: the data differs from "
                        'the original extraction (an intended change must update the recorded digest)')
    words = man.get('rng_seed_words', [])
    if len(words) != 3 or not all(isinstance(w, str) and HEX4.fullmatch(w) for w in words):
        raise DataError('manifest.json: rng_seed_words must be three four-digit hex words')
    seeds = [int(w, 16) for w in words]
    if hashlib.sha256(struct.pack('<3H', *seeds)).hexdigest() != man.get('rng_seed_sha256'):
        raise DataError('RNG seed SHA-256 differs from the recorded value (an intended change must update the '
                        'recorded digest)')
    return bytes(image), seeds, digest


# ---- the merged model (sources + tools/data/meta) ------------------------------------------------------------

TEXT_KEYS = ('id', 'name', 'c_name', 'offset', 'length', 'text', 'confidence', 'evidence', 'aliases', 'audit',
             'interior_names')
POINTER_KEYS = ('id', 'name', 'c_name', 'offset', 'entries', 'targets', 'native_far_text', 'interior_names')
TABLE_KEYS = ('id', 'c_name', 'name_aliases', 'offset', 'type', 'count', 'byte_order', 'description', 'consumers',
              'audit', 'original_readers', 'decoded', 'shape', 'dims', 'shape_basis', 'values', 'interior_names')
STRUCT_KEYS = ('id', 'c_name', 'name_aliases', 'offset', 'size', 'audit', 'description', 'original_readers',
               'consumers', 'sets', 'key_only_installs', 'members', 'values_csv', 'interior_names')
TEXT_DERIVED = ('id', 'offset', 'length', 'text')
POINTER_DERIVED = ('id', 'offset', 'entries')
TABLE_DERIVED = ('id', 'offset', 'type', 'count', 'byte_order', 'values', 'consumers')
STRUCT_DERIVED = ('id', 'offset', 'size', 'members', 'values_csv', 'consumers')


def ordered(d, keys):
    return {k: d[k] for k in keys if k in d} | {k: v for k, v in d.items() if k not in keys}


def load_meta(meta_dir):
    m = Path(META_DIR if meta_dir is None else meta_dir)
    return {n: read_json(m / n, 'meta/' + n) for n in META_FILES}


def load_sources(data_dir, meta_dir=None):
    """The merged model: the sources of DATA_DIR with the metadata of META_DIR (default tools/data/meta) and the
    derived fields. Metadata of an entry the sources do not hold is ignored (the extractor's --check proves the two
    directories agree)."""
    D = load_data(data_dir)
    M = load_meta(meta_dir)
    H = M['headers.json']
    mt = {e['offset']: e for e in M['texts.json']}
    texts = []
    for off, txt, _ in D['texts']:
        e = {'id': 't' + off, 'offset': off, 'length': len(decode_text(txt, off)), 'text': txt,
             **{k: v for k, v in mt.get(off, {}).items() if k not in TEXT_DERIVED}}
        texts.append(ordered(e, TEXT_KEYS))
    mp = M['pointers.json']
    pointers = [ordered({'id': 'p' + t['offset'], 'offset': t['offset'], 'entries': t['entries'],
                         **{k: v for k, v in mp.get(t['offset'], {}).items() if k not in POINTER_DERIVED}},
                        POINTER_KEYS) for t in D['pointers']]
    mtab = M['tables.json']
    tables = []
    for t in D['tables']:
        e = {'id': t['id'], 'offset': t['offset'], 'type': t['type'], 'count': len(t['values']),
             'byte_order': None if TYPES[t['type']][1] == 1 else 'little-endian', 'values': t['values'],
             'consumers': t.get('consumers', []),
             **{k: v for k, v in mtab.get(t['id'], {}).items() if k not in TABLE_DERIVED}}
        tables.append(ordered(e, TABLE_KEYS))
    ms = M['structs.json']
    S = D['structs']
    rt_meta = ms.get('record_types', {})
    record_types = {}
    for n in S.get('record_types', {}):
        fields = field_list(S['record_types'], n, 'record type ' + n)
        fm, at, out = rt_meta.get(n, {}), 0, []
        for f, typ, cnt in fields:
            e = {'name': f, 'offset': at, 'type': typ}
            if cnt:
                e['count'] = cnt
            e.update(fm.get('fields', {}).get(f, {}))
            out.append(e)
            at += TYPES[typ][1] * (cnt or 1)
        record_types[n] = {'size': at, **({'about': fm['about']} if 'about' in fm else {}), 'fields': out}
    structs = []
    for s in S.get('structs', []):
        sm = ms.get('structs', {}).get(s['id'], {})
        descs = sm.get('member_descriptions', {})
        members = []
        for m in s['members']:
            e = {'name': m['name'], 'offset': '%04X' % m['_offset']}
            if 'record' in m:
                e.update(record=m['record'], count=m['count'])
                if m['name'] in descs:
                    e['description'] = descs[m['name']]
                e['values'] = m['_values']
            else:
                if 'chars' in m:
                    e.update(type='chars', length=len(decode_text(m['chars'], m['name'])), text=m['chars'])
                elif 'hex' in m:
                    e.update(type='bytes', size=len(m['hex']) // 2, hex=m['hex'])
                else:
                    e.update(type=m['type'], count=len(m['values']), values=m['values'])
                if m['name'] in descs:
                    e['description'] = descs[m['name']]
            members.append(e)
        e = {'id': s['id'], 'offset': s['offset'], 'size': s['_size'], 'consumers': s.get('consumers', []),
             'members': members,
             **({'values_csv': s['records_csv']} if 'records_csv' in s else {}),
             **{k: v for k, v in sm.items() if k not in STRUCT_DERIVED and k != 'member_descriptions'}}
        structs.append(ordered(e, STRUCT_KEYS))
    labels = H.get('region_labels', {})
    regions = []
    for off, _, _ in D['texts']:
        regions.append({'at': off, 'kind': 'text', 'id': 't' + off})
    for t in pointers:
        regions.append({'at': t['offset'], 'kind': 'far_pointers', 'id': t['id']})
    for t in tables:
        regions.append({'at': t['offset'], 'kind': 'table', 'id': t['id']})
    for s in structs:
        regions.append({'at': s['offset'], 'kind': 'struct', 'id': s['id']})
    for r in D['opaque']:
        if 'zero' in r:
            regions.append({'at': r['at'], 'kind': 'zero', 'size': r['zero']})
        else:
            b = r['hex']
            regions.append({'at': r['at'], 'kind': 'bytes', 'size': len(b) // 2, **labels.get(r['at'], {}),
                            'hex': [b[i:i + 64] for i in range(0, len(b), 64)]})
    regions.sort(key=lambda r: int(r['at'], 16))
    man = D['manifest']
    head = lambda n: dict(H.get('files', {}).get(n, {}))
    hp = head('program_ds.json')
    layout = ordered({**hp, 'size': DS_SIZE, 'sha256': man['ds_sha256'], 'regions': regions},
                     ('format', 'source', 'extracted_by', 'about', 'size', 'sha256', 'relocation_sites', 'regions'))
    ht = head('ds_text.json')
    pabout = {p['file']: p.get('about') for p in ht.pop('parts', [])}
    parts = [{'file': rel, 'range': ['%04X' % lo, '%04X' % hi], 'entries': len(rows),
              **({'about': pabout[rel]} if pabout.get(rel) else {})} for rel, lo, hi, rows in D['parts']]
    hc = head('driver_constants.json')
    rng = dict(hc.pop('rng_state', {}))
    rng.update(words=man['rng_seed_words'], sha256=man['rng_seed_sha256'])
    rng = ordered(rng, ('segment', 'offsets', 'file_offset', 'byte_order', 'words', 'sha256', 'about'))
    return {
        'program_ds.json': layout,
        'ds_text.json': {**ht, 'parts': parts, 'entries': texts},
        'ds_pointers.json': {**head('ds_pointers.json'), 'tables': pointers},
        'ds_tables.json': {**head('ds_tables.json'), 'tables': tables},
        'ds_structs.json': {**head('ds_structs.json'), 'record_types': record_types, 'structs': structs},
        'driver_constants.json': {**hc, 'rng_state': rng},
    }


# ---- writing ---------------------------------------------------------------------------------------------------

def _list(vals, per, indent):
    """A JSON list of scalars, PER to a line, closing bracket at INDENT."""
    if len(vals) <= per:
        return json.dumps(vals)
    pad = ' ' * (indent + 2)
    rows = [', '.join(json.dumps(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
    return '[\n' + ',\n'.join(pad + r for r in rows) + '\n' + ' ' * indent + ']'


def _block(pairs, indent=1):
    pad = ' ' * (indent + 2)
    return '{\n' + ',\n'.join(f'{pad}{json.dumps(k)}: {v}' for k, v in pairs) + '\n' + ' ' * indent + '}'


def _array(blocks):
    return '[\n ' + ',\n '.join(blocks) + '\n]\n' if blocks else '[]\n'


def flat_values(t):
    """A table's values as a flat list (a shaped model table may hold nested rows)."""
    v = t['values']
    while v and isinstance(v[0], list):
        v = [x for r in v for x in r]
    return v


def source_files(J):
    """The sources of the merged model J: {relative path: text} (manifest, texts, pointers, tables, structs,
    records, opaque)."""
    L, T, P, TB, S, C = (J[n] for n in NAMES)
    out = {}
    rng = C['rng_state']
    out['manifest.json'] = json.dumps({'ds_sha256': L['sha256'], 'rng_seed_words': rng['words'],
                                       'rng_seed_sha256': rng['sha256']}, indent=1) + '\n'
    parts = T.get('parts')
    if not parts:
        raise DataError('ds_text.json: the model needs "parts" (file, range) to write the text files')
    for p in parts:
        lo, hi = (int(x, 16) for x in p['range'])
        rows = ['%s\t%s' % (e['offset'], e['text']) for e in sorted(T['entries'], key=lambda e: int(e['offset'], 16))
                if lo <= int(e['offset'], 16) < hi]
        if any('\t' in r.split('\t', 1)[1] or '\n' in r for r in rows):
            raise DataError(f"{p['file']}: a text holds a raw tab or newline")
        out[p['file']] = '\n'.join(['offset\ttext'] + rows) + '\n'
    out['pointers.json'] = _array([_block([('offset', json.dumps(t['offset'])),
                                           ('entries', _list(t['entries'], 8, 2))]) for t in P['tables']])
    out['tables.json'] = _array([_block([('id', json.dumps(t['id'])), ('offset', json.dumps(t['offset'])),
                                         ('type', json.dumps(t['type'])), ('values', _list(flat_values(t), 16, 2)),
                                         ('consumers', _list(t.get('consumers', []), 1, 2))])
                                 for t in TB['tables']])
    rt = {n: {f['name']: [f['type'], f['count']] if 'count' in f else f['type'] for f in r['fields']}
          for n, r in S.get('record_types', {}).items()}
    blocks = []
    for t in S['structs']:
        members, rows, header = [], [], None
        for m in t['members']:
            if 'record' in m:
                members.append(json.dumps({'name': m['name'], 'record': m['record'], 'count': m['count']}))
                fields = field_list(rt, m['record'], t['id'])
                header = csv_header(fields)
                for row in m['values']:
                    cells = []
                    for (_, _, n), v in zip(fields, row):
                        cells += v if n else [v]
                    rows.append(cells)
            elif m.get('type') == 'chars':
                members.append(json.dumps({'name': m['name'], 'chars': m['text']}))
            elif m.get('type') == 'bytes':
                members.append(json.dumps({'name': m['name'], 'hex': m['hex']}))
            else:
                members.append(json.dumps({'name': m['name'], 'type': m['type'], 'values': m['values']}))
        pairs = [('id', json.dumps(t['id'])), ('offset', json.dumps(t['offset']))]
        if rows:
            rel = t.get('values_csv') or 'records/%s.csv' % t['id']
            pairs.append(('records_csv', json.dumps(rel)))
            buf = io.StringIO()
            w = csv.writer(buf, lineterminator='\n')
            w.writerow(header)
            w.writerows(rows)
            out[rel] = buf.getvalue()
        pairs.append(('members', '[\n    ' + ',\n    '.join(members) + '\n   ]'))
        pairs.append(('consumers', _list(t.get('consumers', []), 1, 2)))
        blocks.append(_block(pairs))
    out['structs.json'] = ('{\n "record_types": ' + json.dumps(rt) + ',\n "structs": [\n ' + ',\n '.join(blocks) +
                           '\n ]\n}\n')
    opaque = []
    for r in L['regions']:
        if r['kind'] == 'zero':
            opaque.append(json.dumps({'at': r['at'], 'zero': r['size']}))
        elif r['kind'] == 'bytes':
            opaque.append(json.dumps({'at': r['at'], 'hex': ''.join(r['hex'])}))
    out['opaque.json'] = _array(opaque)
    return out


def meta_files(J):
    """The metadata of the merged model J: {meta file name: text}."""
    L, T, P, TB, S, C = (J[n] for n in NAMES)
    files = {}
    for n in NAMES:
        h = {k: v for k, v in J[n].items() if k not in ('regions', 'entries', 'tables', 'structs', 'record_types',
                                                        'size', 'sha256', 'parts', 'rng_state')}
        if n == 'ds_text.json':
            h['parts'] = [{'file': p['file'], 'about': p['about']} for p in T.get('parts', []) if p.get('about')]
        if n == 'driver_constants.json':
            h['rng_state'] = {k: v for k, v in C['rng_state'].items() if k not in ('words', 'sha256')}
        files[n] = h
    labels = {r['at']: {k: r[k] for k in r if k.startswith('label')} for r in L['regions']
              if r['kind'] == 'bytes' and any(k.startswith('label') for k in r)}
    out = {'headers.json': dump_json({'files': files, 'region_labels': labels})}
    lines = [json.dumps({'offset': e['offset'], **{k: v for k, v in e.items() if k not in TEXT_DERIVED}})
             for e in T['entries']]
    out['texts.json'] = '[\n ' + ',\n '.join(lines) + '\n]\n'
    out['pointers.json'] = dump_json({t['offset']: {k: v for k, v in t.items() if k not in POINTER_DERIVED}
                                      for t in P['tables'] if any(k not in POINTER_DERIVED for k in t)})
    out['tables.json'] = dump_json({t['id']: {k: v for k, v in t.items() if k not in TABLE_DERIVED}
                                    for t in TB['tables']})
    rtm = {n: {**({'about': r['about']} if 'about' in r else {}),
               'fields': {f['name']: {k: v for k, v in f.items() if k not in ('name', 'offset', 'type', 'count')}
                          for f in r['fields']}} for n, r in S.get('record_types', {}).items()}
    sm = {}
    for t in S['structs']:
        e = {k: v for k, v in t.items() if k not in STRUCT_DERIVED}
        d = {m['name']: m['description'] for m in t['members'] if m.get('description')}
        if d:
            e['member_descriptions'] = d
        sm[t['id']] = e
    out['structs.json'] = dump_json({'record_types': rtm, 'structs': sm})
    return out


def write_sources(J, data_dir, meta_dir=None):
    """Write the merged model J as the sources (DATA_DIR) and the metadata (META_DIR, default tools/data/meta);
    stale generated files are removed. Returns (written source paths, written metadata names)."""
    d, m = Path(data_dir), Path(META_DIR if meta_dir is None else meta_dir)
    src, meta = source_files(J), meta_files(J)
    for root, files in ((d, src), (m, meta)):
        for rel, text in files.items():
            p = root / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text, encoding='ascii')
    for sub in DATA_SUBDIRS:
        for f in (d / sub).glob('*') if (d / sub).is_dir() else []:
            if f'{sub}/{f.name}' not in src:
                f.unlink()
    return sorted(src), sorted(meta)


def generated_files(data_dir, meta_dir=None):
    """The generated files present: ({source path}, {metadata name})."""
    d, m = Path(data_dir), Path(META_DIR if meta_dir is None else meta_dir)
    src = {n for n in DATA_FILES if (d / n).is_file()}
    for sub in DATA_SUBDIRS:
        src |= {f'{sub}/{f.name}' for f in (d / sub).glob('*') if f.is_file()}
    return src, {n for n in META_FILES if (m / n).is_file()}


# ---- shared helpers (the extractor, the tests) -----------------------------------------------------------------

def shape_values(t):
    """Phase 291: a typed table's flat values. A table with "shape" (reader-established dimensions, C order) may hold
    them as nested rows: every row must have its dimension's length; they flatten in order. A flat list of count
    values is also accepted."""
    shape, vals = t.get('shape'), t['values']
    if not shape:
        return vals
    n = 1
    for d in shape:
        if not isinstance(d, int) or d < 1:
            raise DataError(f"{t['id']}: bad shape {shape}")
        n *= d
    if n != t['count']:
        raise DataError(f"{t['id']}: shape {shape} holds {n} values, count {t['count']}")
    if len(vals) == n and all(isinstance(v, int) for v in vals):
        return vals

    def flat(v, dims, where):
        if not isinstance(v, list) or len(v) != dims[0]:
            raise DataError(f"{t['id']}{where}: a row of {len(v) if isinstance(v, list) else 'no'} values for "
                            f"dimension {dims[0]} of shape {shape}")
        if len(dims) == 1:
            if not all(isinstance(x, int) for x in v):
                raise DataError(f"{t['id']}{where}: a value is not an integer")
            return list(v)
        return [x for k, r in enumerate(v) for x in flat(r, dims[1:], f'{where}[{k}]')]
    return flat(vals, shape, '')


def nest_values(vals, shape):
    """The inverse of shape_values: flat values as nested rows of shape (C order)."""
    if len(shape) == 1:
        return list(vals)
    step = len(vals) // shape[0]
    return [nest_values(vals[k * step:(k + 1) * step], shape[1:]) for k in range(shape[0])]


def dump_json(body):
    """Phase 291: the deterministic JSON form: json.dumps(indent=1, ensure_ascii=True), except that a list of integers
    inside a list (a shaped table's row) is written on one line."""
    rows = []

    def mark(x, inner=False):
        if isinstance(x, dict):
            return {k: mark(v) for k, v in x.items()}
        if isinstance(x, list):
            if inner and x and all(isinstance(v, int) and not isinstance(v, bool) for v in x):
                rows.append(json.dumps(x))
                return '@@ROW%d@@' % (len(rows) - 1)
            return [mark(v, True) for v in x]
        return x
    text = json.dumps(mark(body), indent=1, ensure_ascii=True)
    for k, r in enumerate(rows):
        token = '"@@ROW%d@@"' % k
        if text.count(token) != 1:
            raise DataError('dump_json: row placeholder collision')
        text = text.replace(token, r)
    return text + '\n'


def c_source(image, seeds, digest):
    # The comment's wording is kept verbatim (Phase 300): the generated text, digest d05d2010.., is an identity
    # witness of every program-data phase since 184.
    lines = ['/* Generated by tools/data/gen_program_data.py from the JSON in src/data (Phase 184). Do not edit:',
             ' * edit the JSON sources and rebuild. Never generated from XEEN.DAT. */',
             '#include "xeen/program_data.h"', '',
             '/* DS:0000..8C4B, the initialized data segment of the original XEEN.DAT. */',
             'const uint8_t xeen_program_ds_image[XEEN_PROGRAM_DS_SIZE]={']
    for i in range(0, len(image), 24):
        lines.append(','.join(str(c) for c in image[i:i + 24]) + ',')
    lines[-1] = lines[-1].rstrip(',')
    lines += ['};',
              '/* Driver segment 4853 CS:16F8, 16FA, 16FC: the RNG 50525 state at program start. */',
              'const uint16_t xeen_program_rng_seed[3]={%s};' % ','.join('0x%04Xu' % w for w in seeds),
              'const char xeen_program_ds_sha256[65]="%s";' % digest, '']
    return '\n'.join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--data', default=str(Path(__file__).resolve().parents[2] / 'src' / 'data'))
    ap.add_argument('--c', help='write the C source here')
    ap.add_argument('--image', help='write the raw DS image here')
    a = ap.parse_args(argv)
    try:
        image, seeds, digest = build(a.data)
    except (DataError, KeyError, ValueError, TypeError, AttributeError) as err:
        print(f'gen_program_data: {err}', file=sys.stderr)
        return 1
    if a.c:
        src = c_source(image, seeds, digest)
        p = Path(a.c)
        if not p.exists() or p.read_text() != src:          # unchanged output keeps its timestamp
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(src)
    if a.image:
        Path(a.image).write_bytes(image)
    return 0


if __name__ == '__main__':
    sys.exit(main())
