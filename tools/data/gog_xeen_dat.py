#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn the GOG.com release's WORLD/XEEN.DAT into the CD-ROM release's unpacked XEEN.DAT program, for
tools/data/extract_program_data.py.

GOG's `WORLD\\XEEN.DAT` (353,511 bytes, SHA-256 8bf78b48...) is the CD-ROM release's program, already unpacked by
another tool, with four differences (established by comparing it with the CD's file):
  1. the 29,097 zero bytes at the end of the program image are left out (the header asks for more memory instead);
  2. an 11-byte start stub after the data segment restores the PKLITE "PK" signature, plus one relocation for it;
  3. the relocation table is sorted by address (the CD keeps PKLITE's order; the order does not affect the program);
  4. one text is changed for DOSBox: the CD-swap prompt "Please %s World of Xeen CD-ROM Disk %d." at DS:8552 reads
     "Press CTRL+F4 and ENTER to continue... ".
This tool undoes 1, 2 and 4 and writes the relocations in the canonical (sorted) order. The result is the CD
program in canonical form (SHA-256 ef7db634...), which the extractor accepts like the CD's own file.

Usage: tools/data/gog_xeen_dat.py GOG_WORLD_XEEN.DAT OUT
"""
import hashlib
import struct
import sys

GOG_SHA256 = '8bf78b4850d5973cbb063c7dd96b9aaef4e94791d748334187eef74acf528d27'
CANONICAL_SHA256 = 'ef7db634a74059e94ec07bbf6396abcdce7fb7c36c1baa265b170ff7903c1523'

HEADER = bytes.fromhex('4d5a9000ec02a7156c05f603fffff55780000000000000001c000000')  # the CD program's MZ fields
HEADER_SIZE = 0x56C0            # 1388 paragraphs, as in both files
IMAGE_SIZE = 0x57FD0            # the CD program's load image
DS_IMAGE = 0x481D0              # the data segment (481Dh) in the image
STUB = DS_IMAGE + 0x8C4C        # GOG's start stub: right after the initialized data
STUB_RELOCATION = 0x50E25       # its far jump's segment word (linear address in the image)
PROMPT_AT = DS_IMAGE + 0x8552
PROMPT = b'Please %s World of Xeen CD-ROM Disk %d.\x00'   # the CD release's text (40 bytes with its NUL)


def relocations(data):
    count, table = struct.unpack_from('<H', data, 6)[0], struct.unpack_from('<H', data, 0x18)[0]
    return [s * 16 + o for o, s in (struct.unpack_from('<HH', data, table + 4 * i) for i in range(count))]


def canonical(image, linear):
    """The CD program's file from its load image and relocation addresses, relocations sorted."""
    out = bytearray(HEADER)
    for addr in sorted(linear):
        seg = (addr // 0xFFF0) * 0xFFF           # the CD file's segment grouping (FFFh paragraphs)
        out += struct.pack('<HH', addr - seg * 16, seg)
    out += bytes(HEADER_SIZE - len(out))
    return bytes(out) + image


def convert(data):
    if hashlib.sha256(data).hexdigest() != GOG_SHA256:
        raise ValueError('not the GOG.com release\'s WORLD/XEEN.DAT (SHA-256 differs)')
    image = bytearray(data[HEADER_SIZE:])
    image[STUB:] = b''                                    # 2: the stub (the CD has zeros there)
    image[PROMPT_AT:PROMPT_AT + len(PROMPT)] = PROMPT     # 4: the CD's prompt
    image += bytes(IMAGE_SIZE - len(image))               # 1: the zero tail
    linear = [a for a in relocations(data) if a != STUB_RELOCATION]
    out = canonical(bytes(image), linear)                 # 3: canonical relocation order
    if hashlib.sha256(out).hexdigest() != CANONICAL_SHA256:
        raise ValueError('conversion did not produce the expected program (SHA-256 differs)')
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    try:
        out = convert(open(sys.argv[1], 'rb').read())
    except (OSError, ValueError) as err:
        sys.exit(f'{sys.argv[1]}: {err}')
    with open(sys.argv[2], 'wb') as f:
        f.write(out)
    print(f'{sys.argv[1]} -> {sys.argv[2]} (the CD-ROM program, canonical form, SHA-256 verified)')


if __name__ == '__main__':
    main()
