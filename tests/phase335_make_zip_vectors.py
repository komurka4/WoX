"""Phase 335: test archives for setup_rom_install (tests/test_phase331_setup.c; output: tests/phase335_zip_vectors.h). Content byte i of member k is
TEXT[(i + (i // 37) * k) % len(TEXT)], mirrored by the C test. Prints the C arrays and each member's DEFLATE
block type (0 stored, 1 fixed, 2 dynamic)."""
import io, zipfile, zlib, sys
TEXT = b"Sphinx of black quartz, judge my vow; the five boxing wizards jump quickly. "
def content(k, n):
    return bytes(TEXT[(i + (i // 37) * k) % len(TEXT)] for i in range(n))
SC = "Nuked-SC55-CLAP-ROM-files/Nuked-SC55-Resources/ROMs/SC-55-v1.00/"
def build(members):
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        for name, data, method in members:
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 24, 0, 0, 0))
            info.compress_type = method
            z.writestr(info, data, compresslevel=9 if method == zipfile.ZIP_DEFLATED else None)
    return b.getvalue()
D, S = zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED
sc55 = build([
    (SC, b"", S),
    (SC + "sc55_rom1.bin", content(1, 24), D),        # fixed Huffman
    (SC + "sc55_rom2.bin", content(2, 300), S),        # stored member
    (SC + ".DS_Store", b"\0\0\0\1Bud1", D),            # not taken
    (SC + "sc55_waverom1.bin", content(3, 6000), D),   # dynamic Huffman
    (SC + "sc55_waverom2.bin", content(4, 2500), D),
    (SC + "sc55_waverom3.bin", content(5, 70000), D),  # several blocks
    ("Nuked-SC55-CLAP-ROM-files/Nuked-SC55-Resources/ROMs/SC-55-v1.21/sc55_rom1.bin", content(6, 50), D),
    (SC + "notes.txt", b"other files stay", D),
    (SC + "._sc55_rom1.bin", b"AppleDouble", D),       # a dot file: not taken
    (SC + "sub/sc55_rom9.bin", content(9, 40), D),     # in a subfolder: not taken
])
mt32 = build([
    ("MT32_CONTROL.ROM", content(7, 4000), D),
    ("MT32_PCM.ROM", content(8, 9000), D),
    ("readme.txt", b"not taken", D),
])
mt32_one = build([("MT32_PCM.ROM", content(8, 900), D)])  # one of the two files


def far_copy_stream():
    """A final fixed-Huffman block whose first symbol copies 3 bytes from distance 1, before any output."""
    bits = []
    def put(code, length):  # Huffman codes go most significant bit first
        bits.extend((code >> (length - 1 - i)) & 1 for i in range(length))
    bits += [1, 1, 0]  # BFINAL, BTYPE 01 (least significant bit first)
    put(0b0000001, 7)  # 257: length 3
    put(0b00000, 5)    # distance code 0: distance 1
    put(0b0000000, 7)  # 256: end of block
    out = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        out[i // 8] |= b << (i % 8)
    return bytes(out)


def raw_member_zip(name, stream, size, crc):
    """A one-member archive holding the raw DEFLATE STREAM as NAME (SIZE bytes, CRC)."""
    import struct
    n = name.encode()
    local = struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0, 8, 0, 0, crc, len(stream), size, len(n), 0) + n + stream
    central = struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, 0, 8, 0, 0, crc, len(stream), size, len(n),
                          0, 0, 0, 0, 0, 0) + n
    end = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 1, 1, len(central), len(local), 0)
    return local + central + end


mt32_far = raw_member_zip("MT32_CONTROL.ROM", far_copy_stream(), 3, 0)


def bad_nlen_stream():
    """Phase 337: a final stored block of b"abc" whose NLEN is not the complement of LEN (0000h instead of FFFCh)."""
    return bytes([1, 3, 0, 0, 0]) + b"abc"   # BFINAL 1, BTYPE 00, padded to the byte; LEN 3; NLEN 0


# Phase 337: the right size and CRC-32, so only the NLEN check refuses it as damaged (without the check it inflates
# and fails later, on the member count: the Phase 335 mutant that survived every vector).
mt32_nlen = raw_member_zip("MT32_CONTROL.ROM", bad_nlen_stream(), 3, zlib.crc32(b"abc"))


def types(zbytes):
    out = []
    with zipfile.ZipFile(io.BytesIO(zbytes)) as z:
        for i in z.infolist():
            if i.compress_type == D and i.file_size:
                raw = zbytes[i.header_offset:]
                off = 30 + int.from_bytes(raw[26:28], "little") + int.from_bytes(raw[28:30], "little")
                out.append((i.filename.split("/")[-1], (raw[off] >> 1) & 3))
    return out
def c_array(name, data):
    lines = ["static const unsigned char %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 20):
        lines.append("    " + ",".join("%d" % v for v in data[i:i + 20]) + ",")
    lines.append("};")
    return "\n".join(lines)
if __name__ == "__main__":
    print(types(sc55), types(mt32), len(sc55), len(mt32), file=sys.stderr)
    for name, data in (("sc55_zip", sc55), ("mt32_zip", mt32), ("mt32_one_zip", mt32_one), ("mt32_far_zip", mt32_far),
                       ("mt32_nlen_zip", mt32_nlen)):
        print(c_array(name, data))
