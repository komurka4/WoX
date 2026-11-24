#!/usr/bin/env python3
"""Phase 338: check the version resources of the Windows executables.

Reads each executable's PE resource directory itself (no Windows tool): its VS_VERSIONINFO (RT_VERSION 16): the
fixed file information's signature, FILEVERSION and PRODUCTVERSION (the numeric tuple, a fourth component 0 when the
version has three) and the StringFileInfo's FileVersion and ProductVersion strings; and that the icon (RT_GROUP_ICON
14 with its RT_ICON 3 images) and the application manifest (RT_MANIFEST 24, setup.exe only) are still there.

Usage: check_versions.py DIR [--wox VERSION] [--setup VERSION] [--json OUT]
  DIR       a folder holding wox.exe and/or setup.exe (tools/windows/package.sh's BUILD/dist or the build directory)
  --wox     the expected version of wox.exe   (default: DIR/../version/wox/xeen_wox_version.h when present)
  --setup   the expected version of setup.exe (default: DIR/../version/setup/xeen_setup_version.h when present)
Exits 1 when an executable named or found differs; prints one line per check.
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

RT_ICON, RT_GROUP_ICON, RT_VERSION, RT_MANIFEST = 3, 14, 16, 24


def sections(data):
    """The PE's sections as (rva, virtual size, file offset, raw size) and its resource directory RVA and size."""
    if data[:2] != b'MZ':
        raise ValueError('not an MZ executable')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('no PE signature')
    count = struct.unpack_from('<H', data, pe + 6)[0]
    opt_size = struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', data, opt)[0]
    dirs = opt + (112 if magic == 0x20b else 96)
    res_rva, res_size = struct.unpack_from('<II', data, dirs + 2 * 8)
    table = opt + opt_size
    out = []
    for i in range(count):
        vsize, rva, raw_size, raw = struct.unpack_from('<IIII', data, table + i * 40 + 8)
        out.append((rva, max(vsize, raw_size), raw, raw_size))
    return out, res_rva, res_size


def offset(secs, rva):
    for start, size, raw, _ in secs:
        if start <= rva < start + size:
            return raw + rva - start
    raise ValueError(f'RVA {rva:#x} outside the sections')


def resources(data):
    """{(type, name, language): bytes} of the resource tree (numeric ids; a named entry keeps its string)."""
    secs, res_rva, _ = sections(data)
    if not res_rva:
        return {}
    base = offset(secs, res_rva)

    def entries(at):
        named, ids = struct.unpack_from('<HH', data, base + at + 12)
        for i in range(named + ids):
            name, target = struct.unpack_from('<II', data, base + at + 16 + i * 8)
            if name & 0x80000000:
                n = base + (name & 0x7fffffff)
                length = struct.unpack_from('<H', data, n)[0]
                name = data[n + 2:n + 2 + 2 * length].decode('utf-16-le')
            yield name, target

    out = {}
    for rtype, t in entries(0):
        for rname, n in entries(t & 0x7fffffff):
            for lang, leaf in entries(n & 0x7fffffff):
                rva, size = struct.unpack_from('<II', data, base + leaf)
                at = offset(secs, rva)
                out[(rtype, rname, lang)] = data[at:at + size]
    return out


def version_node(blob, at=0):
    """One VS_VERSIONINFO-style node at AT: (key, value, children, end)."""
    length, value_length, kind = struct.unpack_from('<HHH', blob, at)
    end = at + length
    p = at + 6
    key_end = p
    while blob[key_end:key_end + 2] != b'\0\0':
        key_end += 2
    key = blob[p:key_end].decode('utf-16-le')
    p = (key_end + 2 + 3) & ~3
    if kind == 1:  # text: VALUE_LENGTH counts WCHARs (with the terminator)
        value = blob[p:p + 2 * value_length].decode('utf-16-le').rstrip('\0')
        p += 2 * value_length
    else:
        value = blob[p:p + value_length]
        p += value_length
    children = []
    p = (p + 3) & ~3
    while p < end:
        child = version_node(blob, p)
        children.append(child[:3])
        p = (child[3] + 3) & ~3
    return key, value, children, end


def version_info(blob):
    """The fixed tuple (file, product) and the StringFileInfo strings of a VS_VERSIONINFO resource."""
    key, fixed, children, _ = version_node(blob)
    if key != 'VS_VERSION_INFO':
        raise ValueError(f'version resource key {key!r}')
    sig, _, fms, fls, pms, pls = struct.unpack_from('<IIIIII', fixed, 0)
    if sig != 0xFEEF04BD:
        raise ValueError(f'VS_FIXEDFILEINFO signature {sig:#x}')

    def tuple4(ms, ls):
        return (ms >> 16, ms & 0xffff, ls >> 16, ls & 0xffff)

    strings = {}
    for ckey, _, tables in children:
        if ckey == 'StringFileInfo':
            for _, _, values in tables:
                for name, value, _ in values:
                    strings[name] = value
    return tuple4(fms, fls), tuple4(pms, pls), strings


def expected_tuple(version):
    parts = [int(p) for p in version.split('.')]
    return tuple(parts + [0] * (4 - len(parts)))


def check(exe, version, need_manifest, report):
    ok = True

    def line(cond, what):
        nonlocal ok
        print(('ok   ' if cond else 'FAIL ') + f'{exe.name}: {what}')
        ok = ok and cond

    res = resources(exe.read_bytes())
    versions = [v for k, v in res.items() if k[0] == RT_VERSION]
    line(len(versions) == 1, f'one version resource ({len(versions)})')
    if versions:
        file_t, product_t, strings = version_info(versions[0])
        want = expected_tuple(version)
        line(file_t == want, f'FILEVERSION {",".join(map(str, file_t))} == {",".join(map(str, want))}')
        line(product_t == want, f'PRODUCTVERSION {",".join(map(str, product_t))} == {",".join(map(str, want))}')
        line(strings.get('FileVersion') == version, f'FileVersion "{strings.get("FileVersion")}" == "{version}"')
        line(strings.get('ProductVersion') == version,
             f'ProductVersion "{strings.get("ProductVersion")}" == "{version}"')
        report[exe.name] = {'file_version': list(file_t), 'product_version': list(product_t), 'strings': strings}
    groups = [k for k in res if k[0] == RT_GROUP_ICON]
    icons = [k for k in res if k[0] == RT_ICON]
    line(bool(groups) and bool(icons), f'icon kept ({len(groups)} group(s), {len(icons)} image(s))')
    manifests = [k for k in res if k[0] == RT_MANIFEST]
    if need_manifest:
        line(len(manifests) == 1, f'manifest kept ({len(manifests)})')
    report.setdefault(exe.name, {}).update({'icon_groups': len(groups), 'icon_images': len(icons),
                                            'manifests': len(manifests),
                                            'resource_types': sorted({str(k[0]) for k in res})})
    return ok


def generated(folder, name):
    header = folder.parent / 'version' / name / f'xeen_{name}_version.h'
    if not header.exists():
        header = folder / 'version' / name / f'xeen_{name}_version.h'
    if header.exists():
        m = re.search(r'#define XEEN_\w+_VERSION "([^"]*)"', header.read_text())
        return m[1] if m else None
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dir', type=Path)
    ap.add_argument('--wox')
    ap.add_argument('--setup')
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    ok, report, seen = True, {}, 0
    for name, version, manifest in (('wox', a.wox, False), ('setup', a.setup, True)):
        exe = a.dir / f'{name}.exe'
        version = version or generated(a.dir, name)
        if not exe.exists():
            if version and (a.wox if name == 'wox' else a.setup):
                print(f'FAIL {exe.name}: missing')
                ok = False
            continue
        if not version:
            print(f'FAIL {exe.name}: no expected version (pass --{name})')
            ok = False
            continue
        seen += 1
        ok = check(exe, version, manifest, report) and ok
    if a.json:
        a.json.write_text(json.dumps(report, indent=1, sort_keys=True) + '\n')
    if not seen:
        print('FAIL no executable checked')
        ok = False
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
