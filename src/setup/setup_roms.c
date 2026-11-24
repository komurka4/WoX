/* Phase 335: the setup tool's ROM downloads (setup_model.h), owner request: which archive a music card's ROMs come
 * from, and their extraction from that ZIP archive (stored or deflated members; RFC 1951 inflate below). The download
 * itself is the window's (WinHTTP on Windows, curl or wget on Linux). Host tooling, not original behaviour. */
#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif
#include "setup_model.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define SETUP_SEPARATOR '\\'
#else
#define SETUP_SEPARATOR '/'
#endif

/* ------------------------------------------------------------------ the two sets */
typedef struct {
    const char *name, *url;
    const char *folder;    /* the members' folder inside the archive ("" for its top level) */
    const char *extension; /* the members taken from it (any case) */
    unsigned files;        /* how many the archive must hold there */
} rom_source;

/* The owner's archives (Phase 335). The SC-55 one is Nuked-SC55's mk1-v1.00 set (standard_romsets.cpp names its
 * hashes; wox accepts any one complete MK1 set); its folder also holds a .DS_Store, which is not taken. */
static const rom_source sources[] = {
        [SETUP_ROMS_MT32] = {"MT-32",
                             "https://archive.org/download/mame-versioned-roland-mt-32-and-cm-32l-rom-files/"
                             "MT-32_v1.07_legacy_ROM_files.zip",
                             "", ".ROM", 2u},
        [SETUP_ROMS_SC55] = {"SC-55",
                             "https://dn710106.ca.archive.org/0/items/nuked-sc-55-clap-rom-files/"
                             "Nuked-SC55-CLAP-ROM-files.zip",
                             "Nuked-SC55-CLAP-ROM-files/Nuked-SC55-Resources/ROMs/SC-55-v1.00/", ".bin", 5u},
};

/* The largest member taken (the SC-55 wave ROMs are 1 MiB each). */
#define ROM_FILE_MAX (8u << 20)

setup_rom_set setup_music_rom_set(setup_music m)
{
    switch (m) {
        case SETUP_MUSIC_MT32: return SETUP_ROMS_MT32;
        case SETUP_MUSIC_CANVAS:
        case SETUP_MUSIC_WAVE:
        case SETUP_MUSIC_TAP10: return SETUP_ROMS_SC55;
        default: return SETUP_ROMS_NONE;
    }
}
const char *setup_rom_set_url(setup_rom_set s)
{
    return s == SETUP_ROMS_MT32 || s == SETUP_ROMS_SC55 ? sources[s].url : NULL;
}
const char *setup_rom_set_name(setup_rom_set s)
{
    return s == SETUP_ROMS_MT32 || s == SETUP_ROMS_SC55 ? sources[s].name : "";
}
void setup_rom_set_dir(const setup_settings *st, setup_rom_set s, const char *base, char *out, size_t n)
{
    setup_resolve(base, st->dir[s == SETUP_ROMS_MT32 ? SETUP_DIR_MT32 : SETUP_DIR_SC55], out, n);
}

/* ------------------------------------------------------------------ inflate (RFC 1951) */
typedef struct {
    const unsigned char *in;
    size_t in_n, pos;
    unsigned long bits;
    unsigned count;
    unsigned char *out;
    size_t out_n, out_pos;
} inflater;

/* A canonical Huffman code: how many codes of each length, and the symbols in code order. */
typedef struct {
    unsigned short count[16], symbol[288];
} huffman;

/** The next N (0..16) bits, least significant first, into *V; 0 at the end of the input. */
static int need(inflater *z, unsigned n, unsigned *v)
{
    while (z->count < n) {
        if (z->pos >= z->in_n) {
            return 0;
        }
        z->bits |= (unsigned long)z->in[z->pos++] << z->count;
        z->count += 8u;
    }
    *v = (unsigned)(z->bits & ((1ul << n) - 1ul));
    z->bits >>= n;
    z->count -= n;
    return 1;
}
/** H from the code lengths of N symbols; 0 when the lengths are over-subscribed. */
static int build(huffman *h, const unsigned char *lengths, unsigned n)
{
    unsigned short offset[16];
    int left = 1;
    memset(h->count, 0, sizeof h->count);
    for (unsigned i = 0; i < n; ++i) {
        ++h->count[lengths[i]];
    }
    h->count[0] = 0;
    for (unsigned len = 1; len < 16u; ++len) {
        left = left * 2 - h->count[len];
        if (left < 0) {
            return 0;
        }
    }
    offset[1] = 0;
    for (unsigned len = 1; len < 15u; ++len) {
        offset[len + 1u] = (unsigned short)(offset[len] + h->count[len]);
    }
    for (unsigned i = 0; i < n; ++i) {
        if (lengths[i]) {
            h->symbol[offset[lengths[i]]++] = (unsigned short)i;
        }
    }
    return 1;
}
/** The next symbol of code H into *SYM; 0 on a code H does not have or at the end of the input. */
static int decode(inflater *z, const huffman *h, unsigned *sym)
{
    int code = 0, first = 0, index = 0;
    for (unsigned len = 1; len < 16u; ++len) {
        unsigned bit;
        int count = h->count[len];
        if (!need(z, 1, &bit)) {
            return 0;
        }
        code |= (int)bit;
        if (code - count < first) {
            *sym = h->symbol[index + (code - first)];
            return 1;
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return 0;
}
/** A block's literals and copies (codes LIT and DIST) until its end symbol. */
static int codes(inflater *z, const huffman *lit, const huffman *dist)
{
    static const unsigned short len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const unsigned char len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const unsigned short dist_base[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                                                 33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                                                 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const unsigned char dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        unsigned sym, extra, d;
        size_t len, distance;
        if (!decode(z, lit, &sym)) {
            return 0;
        }
        if (sym < 256u) {
            if (z->out_pos >= z->out_n) {
                return 0;
            }
            z->out[z->out_pos++] = (unsigned char)sym;
            continue;
        }
        if (sym == 256u) {
            return 1;
        }
        sym -= 257u;
        if (sym >= 29u || !need(z, len_extra[sym], &extra)) {
            return 0;
        }
        len = (size_t)len_base[sym] + extra;
        if (!decode(z, dist, &d) || d >= 30u || !need(z, dist_extra[d], &extra)) {
            return 0;
        }
        distance = (size_t)dist_base[d] + extra;
        if (distance > z->out_pos || len > z->out_n - z->out_pos) {
            return 0;
        }
        for (; len; --len, ++z->out_pos) {
            z->out[z->out_pos] = z->out[z->out_pos - distance];
        }
    }
}
/** A stored block: the rest of the current byte is dropped, then LEN, its complement and LEN bytes. */
static int stored(inflater *z)
{
    unsigned len;
    z->bits = 0;
    z->count = 0; /* need() keeps fewer than 8 bits: only the current byte's rest */
    if (z->in_n - z->pos < 4u) {
        return 0;
    }
    len = z->in[z->pos] | (unsigned)z->in[z->pos + 1u] << 8;
    if ((len ^ 0xFFFFu) != (z->in[z->pos + 2u] | (unsigned)z->in[z->pos + 3u] << 8)) {
        return 0;
    }
    z->pos += 4u;
    if (len > z->in_n - z->pos || len > z->out_n - z->out_pos) {
        return 0;
    }
    memcpy(z->out + z->out_pos, z->in + z->pos, len);
    z->pos += len;
    z->out_pos += len;
    return 1;
}
static int fixed(inflater *z)
{
    static huffman lit, dist;
    static int built;
    if (!built) {
        unsigned char l[288];
        unsigned i = 0;
        for (; i < 288u; ++i) {
            l[i] = i < 144u ? 8 : i < 256u ? 9 : i < 280u ? 7 : 8; /* RFC 1951 3.2.6 */
        }
        (void)build(&lit, l, 288);
        memset(l, 5, 30);
        (void)build(&dist, l, 30);
        built = 1;
    }
    return codes(z, &lit, &dist);
}
static int dynamic(inflater *z)
{
    static const unsigned char order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    unsigned char lengths[320];
    unsigned nlit, ndist, ncode, v;
    huffman lit, dist;
    if (!need(z, 5, &nlit) || !need(z, 5, &ndist) || !need(z, 4, &ncode)) {
        return 0;
    }
    nlit += 257u;
    ndist += 1u;
    ncode += 4u;
    if (nlit > 286u || ndist > 30u) {
        return 0;
    }
    memset(lengths, 0, sizeof lengths);
    for (unsigned i = 0; i < ncode; ++i) {
        if (!need(z, 3, &v)) {
            return 0;
        }
        lengths[order[i]] = (unsigned char)v;
    }
    if (!build(&lit, lengths, 19)) {
        return 0;
    }
    for (unsigned i = 0; i < nlit + ndist;) {
        unsigned sym, repeat, value = 0;
        if (!decode(z, &lit, &sym)) {
            return 0;
        }
        if (sym < 16u) {
            lengths[i++] = (unsigned char)sym;
            continue;
        }
        if (sym == 16u) {
            if (i == 0 || !need(z, 2, &repeat)) {
                return 0;
            }
            value = lengths[i - 1u];
            repeat += 3u;
        } else if (!need(z, sym == 17u ? 3u : 7u, &repeat)) {
            return 0;
        } else {
            repeat += sym == 17u ? 3u : 11u;
        }
        if (repeat > nlit + ndist - i) {
            return 0;
        }
        for (; repeat; --repeat) {
            lengths[i++] = (unsigned char)value;
        }
    }
    if (!lengths[256] || !build(&lit, lengths, nlit) || !build(&dist, lengths + nlit, ndist)) {
        return 0;
    }
    return codes(z, &lit, &dist);
}
/** Inflates IN (IN_N bytes) into exactly OUT_N bytes at OUT; 0 on damaged data or another size. */
static int inflate(const unsigned char *in, size_t in_n, unsigned char *out, size_t out_n)
{
    inflater z = {in, in_n, 0, 0, 0, out, out_n, 0};
    unsigned last, type;
    do {
        int ok;
        if (!need(&z, 1, &last) || !need(&z, 2, &type)) {
            return 0;
        }
        ok = type == 0u ? stored(&z) : type == 1u ? fixed(&z) : type == 2u ? dynamic(&z) : 0;
        if (!ok) {
            return 0;
        }
    } while (!last);
    return z.out_pos == out_n;
}
static unsigned long crc32_of(const unsigned char *p, size_t n)
{
    static unsigned long table[256];
    unsigned long c = 0xFFFFFFFFul;
    if (!table[1]) {
        for (unsigned long i = 0; i < 256u; ++i) {
            unsigned long t = i;
            for (int k = 0; k < 8; ++k) {
                t = t & 1u ? 0xEDB88320ul ^ (t >> 1) : t >> 1;
            }
            table[i] = t;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFul;
}

/* ------------------------------------------------------------------ the ZIP archive */
static unsigned get16(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8;
}
static unsigned long get32(const unsigned char *p)
{
    return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}
static int failed(char *err, size_t n, const char *why)
{
    if (err && n) {
        snprintf(err, n, "%s", why);
    }
    return 0;
}
/** Whether NAME ends in EXT, ignoring case. */
static int has_extension(const char *name, const char *ext)
{
    size_t a = strlen(name), b = strlen(ext);
    if (a <= b) {
        return 0;
    }
    for (size_t i = 0; i < b; ++i) {
        if (tolower((unsigned char)name[a - b + i]) != tolower((unsigned char)ext[i])) {
            return 0;
        }
    }
    return 1;
}
/** The file name of member NAME when SRC takes it (directly in SRC's folder, of its extension), else NULL. */
static const char *taken(const rom_source *src, const char *name)
{
    size_t len = strlen(src->folder);
    const char *rest = name + len;
    if (strncmp(name, src->folder, len) != 0 || !*rest || strchr(rest, '/') || strchr(rest, '\\') ||
        rest[0] == '.' || !has_extension(rest, src->extension)) {
        return NULL;
    }
    return rest;
}
typedef struct {
    char name[64];
    unsigned char *data;
    size_t size;
} rom_file;

/** The data of the member whose central-directory entry is at E into F (F->name already set). */
static int read_member(const unsigned char *zip, size_t n, const unsigned char *e, rom_file *f, char *err, size_t en)
{
    unsigned flags = get16(e + 8), method = get16(e + 10);
    unsigned long crc = get32(e + 16), packed = get32(e + 20), size = get32(e + 24), local = get32(e + 42);
    const unsigned char *data;
    size_t skip;
    if (flags & 1u) {
        return failed(err, en, "the archive is encrypted");
    }
    if (method != 0u && method != 8u) {
        return failed(err, en, "the archive uses an unsupported compression method");
    }
    if (size > ROM_FILE_MAX || n < 30u || local > n - 30u || get32(zip + local) != 0x04034B50ul) {
        return failed(err, en, "the archive is damaged");
    }
    skip = 30u + (size_t)get16(zip + local + 26) + get16(zip + local + 28);
    if (skip > n - local || packed > n - local - skip) {
        return failed(err, en, "the archive is damaged");
    }
    data = zip + local + skip;
    f->size = size;
    f->data = malloc(size ? size : 1u);
    if (!f->data) {
        return failed(err, en, "out of memory");
    }
    if (method == 0u) {
        if (packed != size) {
            return failed(err, en, "the archive is damaged");
        }
        memcpy(f->data, data, size);
    } else if (!inflate(data, packed, f->data, size)) {
        return failed(err, en, "the archive is damaged");
    }
    if (crc32_of(f->data, size) != crc) {
        return failed(err, en, "the archive is damaged (CRC mismatch)");
    }
    return 1;
}
/** Every file SRC takes from the archive into FILES (at most MAX); their count, or -1 with the reason in ERR. */
static int read_files(const rom_source *src, const unsigned char *zip, size_t n, rom_file *files, unsigned max,
                      char *err, size_t en)
{
    const unsigned char *end = NULL, *e;
    unsigned entries, found = 0;
    unsigned long dir_size, dir_at;
    for (size_t back = 22u; back <= n && back <= 22u + 65535u; ++back) {
        if (get32(zip + n - back) == 0x06054B50ul) {
            end = zip + n - back;
            break;
        }
    }
    if (!end) {
        failed(err, en, "the download is not a ZIP archive");
        return -1;
    }
    entries = get16(end + 10);
    dir_size = get32(end + 12);
    dir_at = get32(end + 16);
    if (dir_at > n || dir_size > n - dir_at) {
        failed(err, en, "the archive is damaged");
        return -1;
    }
    for (size_t at = 0, i = 0; i < entries; ++i) {
        char name[512];
        size_t name_len, length;
        const char *file;
        e = zip + dir_at + at;
        if (dir_size - at < 46u || get32(e) != 0x02014B50ul) {
            failed(err, en, "the archive is damaged");
            return -1;
        }
        name_len = get16(e + 28);
        length = 46u + name_len + get16(e + 30) + get16(e + 32); /* the entry with its name, extra field, comment */
        if (dir_size - at < length) {
            failed(err, en, "the archive is damaged");
            return -1;
        }
        at += length;
        if (name_len < sizeof name) {
            memcpy(name, e + 46, name_len);
            name[name_len] = 0;
            if ((file = taken(src, name)) != NULL) {
                if (found == max || strlen(file) >= sizeof files[0].name) {
                    failed(err, en, "the archive holds other ROM files than expected");
                    return -1;
                }
                snprintf(files[found].name, sizeof files[found].name, "%s", file);
                if (!read_member(zip, n, e, &files[found], err, en)) {
                    return -1; /* the caller frees every entry */
                }
                ++found;
            }
        }
    }
    return (int)found;
}
/** Writes F into DIR (beside it as NAME.part first); a file already there is kept. 1 on success. */
static int write_file(const char *dir, const rom_file *f)
{
    char path[SETUP_PATH_MAX + 80], part[SETUP_PATH_MAX + 88];
    FILE *out;
    int ok;
    snprintf(path, sizeof path, "%s%c%s", dir, SETUP_SEPARATOR, f->name);
    snprintf(part, sizeof part, "%s.part", path);
    if ((out = fopen(path, "rb")) != NULL) {
        fclose(out);
        return 1;
    }
    if ((out = fopen(part, "wb")) == NULL) {
        return 0;
    }
    ok = fwrite(f->data, 1, f->size, out) == f->size;
    ok = fclose(out) == 0 && ok;
    if (!ok || rename(part, path) != 0) {
        remove(part);
        return 0;
    }
    return 1;
}
int setup_rom_install(setup_rom_set s, const unsigned char *zip, size_t n, const char *dir, char *err, size_t en)
{
    rom_file files[8];
    const rom_source *src;
    int found, ok = 1;
    if (s != SETUP_ROMS_MT32 && s != SETUP_ROMS_SC55) {
        return failed(err, en, "this music needs no ROMs");
    }
    src = &sources[s];
    memset(files, 0, sizeof files);
    found = read_files(src, zip, n, files, 8u, err, en);
    if (found < 0) {
        ok = 0;
    } else if ((unsigned)found != src->files) {
        ok = 0;
        if (err && en) {
            snprintf(err, en, "the archive does not hold the %u %s ROM files expected (found %d)", src->files,
                     src->name, found);
        }
    } else if (!setup_make_dirs("", dir)) {
        ok = 0;
        if (err && en) {
            snprintf(err, en, "the folder %s could not be created", dir);
        }
    } else {
        for (int i = 0; i < found && ok; ++i) {
            if (!write_file(dir, &files[i])) {
                ok = 0;
                if (err && en) {
                    snprintf(err, en, "%s could not be written in %s", files[i].name, dir);
                }
            }
        }
    }
    for (unsigned i = 0; i < 8u; ++i) {
        free(files[i].data); /* also the entries read before a failure */
    }
    return ok;
}
