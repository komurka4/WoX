/* Phase 330: the Space interaction gate xeen_slice_interaction_gate (17B63's call of 1C680 and 17B85's test).
 *
 *   units                    labelled synthetic DS states: each neighbour rule of 1C680 (a missing north/south
 *                            neighbour: F492 = 1; a missing east/west one: F492 = F608, 0 and 8000h; the party map
 *                            id 0: F608), cells in the map, past the maps, a cache miss and the native rejections
 *   genuine DOSDIR EXPECTED  post-load DS of the genuine saves (XEENnn.WOX + DARKnn.WOX, those that load): for every facing
 *                            and x, y in -3..34 the gate's status, eligible and the scratch words F48E, F490, F492,
 *                            F494, plus the number of other DS bytes changed (a miss: its status only), folded
 *                            (FNV-1a 64) into a digest per save that must equal
 *                            EXPECTED's (tests/data/phase330_space_gate.txt, from the original 1C680 under Unicorn)
 *   dump DOSDIR OUTDIR       writes those post-load DS images (slotN.bin), the generator's input
 *
 * Original: 1C680 (listing in the Phase 329/330 evidence); 1C82B..1C82E copies the word F608 into F492 for a missing
 * east/west neighbour (and when no axis wrapped and the map id is 0); 1C717 writes 1 for a missing north/south one. */
#include "xeen/game.h"
#include "xeen/navigation.h"
#include "xeen/ds_globals.h"
#include "xeen/map_record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "space gate line %d: %s\n", __LINE__, #c);                                                 \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

static xeen_movement m;
static uint8_t before[65536];

static uint16_t word(uint16_t a)
{
    return xeen_read16(&m, a);
}
/* Runs the gate at (x, y, facing) and returns the status; *ELIGIBLE gets the flag, *OTHER the number of DS bytes
 * written outside F48E..F495 and F5FE. */
static xeen_status gate(int x, int y, unsigned facing, uint16_t *eligible, unsigned *other)
{
    xeen_status s;
    unsigned k;
    m.ds[XEEN_DS_PARTY_X] = (uint8_t)x;
    m.ds[XEEN_DS_PARTY_Y] = (uint8_t)y;
    m.ds[XEEN_DS_PARTY_DIRECTION] = (uint8_t)facing;
    xeen_write16(&m, XEEN_DS_INPUT_CODE, 0x20);
    memcpy(before, m.ds, sizeof before);
    *eligible = 0xffff;
    s = xeen_slice_interaction_gate(&m, eligible);
    *other = 0;
    for (k = 0; k < 65536u; k++) {
        if (m.ds[k] != before[k] && !(k >= 0xf48eu && k < 0xf496u) && !(k >= XEEN_DS_INPUT_CODE && k < 0xf600u)) {
            ++*other;
        }
    }
    return s;
}

/* ---------------------------------------------------------------- synthetic (labelled) */

#define SLOT_IDS XEEN_DS_MAP_SLOT_IDS
static void slot_id(unsigned slot, uint16_t id)
{
    xeen_write16(&m, (uint16_t)(SLOT_IDS + 2u * slot), id);
}
static uint16_t field(unsigned slot, uint16_t f)
{
    return (uint16_t)(XEEN_DS_MAP_DATA + XEEN_MAP_RECORD_BYTES * slot + f);
}
static void scratch(void)
{
    xeen_write16(&m, XEEN_DS_CELL_WALLS, 0x4444);
    xeen_write16(&m, XEEN_DS_CELL_SURFACE, 0x3333);
    xeen_write16(&m, XEEN_DS_CELL_SEEN, 0x5555);
    xeen_write16(&m, XEEN_DS_MAP_SLOT, 7);
}
/* Party map 5 in slot 0 with neighbour north = 6 (slot 1); every other link 0; the other slots hold 7FFFh. */
static void synthetic(uint16_t outdoors)
{
    unsigned i;
    memset(&m, 0, sizeof m);
    for (i = 0; i < 4; i++) {
        xeen_write16(&m, (uint16_t)XEEN_DS_WALL_SHIFT(i, 2u), (uint16_t)(12u - 4u * i)); /* the program data */
    }
    for (i = 0; i < 9; i++) {
        slot_id(i, 0x7fff);
    }
    slot_id(0, 5);
    slot_id(1, 6);
    m.ds[XEEN_DS_PARTY_MAP] = 5;
    xeen_write16(&m, field(0, XEEN_MAP_RECORD_NORTH), 6);
    xeen_write16(&m, XEEN_DS_OUTDOORS, outdoors);
    scratch();
}

static void units(void)
{
    static const uint16_t flags[2] = {0, 0x8000};
    unsigned w, other;
    uint16_t e;
    for (w = 0; w < 2; w++) {
        const uint16_t f608 = flags[w];
        /* A missing east (x 16..31) or west (x < 0) neighbour: F492 = F608, the slot found first kept; F48E and F490
         * not written; dispatcher (8888h indoors, 0Fh outdoors). The Phase 329 native gate wrote 1 here. */
        synthetic(f608);
        CHECK(gate(16, 3, 0, &e, &other) == XEEN_OK && e == 1 && !other);
        CHECK(word(XEEN_DS_CELL_SEEN) == f608 && word(XEEN_DS_MAP_SLOT) == 0);
        CHECK(word(XEEN_DS_CELL_WALLS) == 0x4444 && word(XEEN_DS_CELL_SURFACE) == 0x3333);
        scratch();
        CHECK(gate(-1, 3, 2, &e, &other) == XEEN_OK && e == 1 && !other && word(XEEN_DS_CELL_SEEN) == f608);
        scratch();
        CHECK(gate(31, 0, 1, &e, &other) == XEEN_OK && e == 1 && !other && word(XEEN_DS_CELL_SEEN) == f608);
        /* North present (slot 1), its east missing: the diagonal resolves y first, then F608 again. */
        scratch();
        CHECK(gate(17, 18, 3, &e, &other) == XEEN_OK && e == 1 && word(XEEN_DS_CELL_SEEN) == f608);
        CHECK(word(XEEN_DS_MAP_SLOT) == 1 && word(XEEN_DS_CELL_SURFACE) == 0x3333);
        /* Controls: a missing south (y < 0) neighbour, or north of slot 1, writes 1 whatever F608 (1C717). */
        scratch();
        CHECK(gate(3, -1, 0, &e, &other) == XEEN_OK && e == 1 && !other && word(XEEN_DS_CELL_SEEN) == 1);
        CHECK(word(XEEN_DS_MAP_SLOT) == 0 && word(XEEN_DS_CELL_SURFACE) == 0x3333);
        scratch();
        CHECK(gate(16, -2, 0, &e, &other) == XEEN_OK && e == 1 && word(XEEN_DS_CELL_SEEN) == 1); /* y first */
        scratch();
        CHECK(gate(3, 18, 0, &e, &other) == XEEN_OK && e == 1 && word(XEEN_DS_CELL_SEEN) == 0); /* slot 1, unseen */
        CHECK(word(XEEN_DS_MAP_SLOT) == 1 && word(XEEN_DS_CELL_SURFACE) == 0);
        /* The party map id 0 (a slot holding id 0): no axis wrapped and DX = 0, so 1C82B: F608 (labelled quirk). */
        synthetic(f608);
        slot_id(8, 0);
        m.ds[XEEN_DS_PARTY_MAP] = 0;
        CHECK(gate(3, 3, 0, &e, &other) == XEEN_OK && e == 1 && word(XEEN_DS_CELL_SEEN) == f608);
        CHECK(word(XEEN_DS_MAP_SLOT) == 8 && word(XEEN_DS_CELL_SURFACE) == 0x3333);
        /* Past the maps: F48E = 8888h only. */
        synthetic(f608);
        CHECK(gate(32, 3, 0, &e, &other) == XEEN_OK && e == 1 && !other && word(XEEN_DS_CELL_WALLS) == 0x8888);
        CHECK(word(XEEN_DS_CELL_SEEN) == 0x5555 && word(XEEN_DS_MAP_SLOT) == 7);
        /* A neighbour not in the cache: the native bound. */
        synthetic(f608);
        xeen_write16(&m, field(0, XEEN_MAP_RECORD_EAST), 9);
        CHECK(gate(16, 3, 0, &e, &other) == XEEN_CACHE_MISS && e == 0);
    }
    /* A cell of the map: the seen bit (C128 + slot x 37Ch), the wall by the facing's shift; indoors only wall 0. */
    synthetic(0);
    m.ds[field(0, XEEN_MAP_RECORD_SEEN) + (3u * 16u + 5u) / 8u] = (uint8_t)(0x80u >> ((3u * 16u + 5u) & 7u));
    m.ds[field(0, XEEN_MAP_RECORD_METADATA) + 3u * 16u + 5u] = 0x0b; /* surface 3 */
    xeen_write16(&m, field(0, (uint16_t)(XEEN_MAP_RECORD_TILES + 3u * 32u + 5u * 2u)), 0x1020);
    CHECK(gate(5, 3, 0, &e, &other) == XEEN_OK && e == 0 && !other); /* facing 0: shift 12, wall 1 (a door) */
    CHECK(word(XEEN_DS_CELL_SEEN) == 0x04 && word(XEEN_DS_CELL_SURFACE) == 3 && word(XEEN_DS_CELL_WALLS) == 0x4444);
    CHECK(gate(5, 3, 1, &e, &other) == XEEN_OK && e == 1); /* shift 8: wall 0 */
    CHECK(gate(5, 3, 2, &e, &other) == XEEN_OK && e == 0); /* shift 4: wall 2 */
    CHECK(gate(5, 3, 3, &e, &other) == XEEN_OK && e == 1); /* shift 0: wall 0 */
    xeen_write16(&m, XEEN_DS_OUTDOORS, 0x8000); /* outdoors: every wall reaches the dispatcher */
    CHECK(gate(5, 3, 0, &e, &other) == XEEN_OK && e == 1);
    /* Native rejections: a facing above 3 before any write; a wall-shift word above 15 after the lookup's writes. */
    synthetic(0);
    CHECK(gate(3, 3, 4, &e, &other) == XEEN_BAD_INPUT && e == 0 && !other);
    CHECK(word(XEEN_DS_CELL_SEEN) == 0x5555 && word(XEEN_DS_MAP_SLOT) == 7);
    xeen_write16(&m, (uint16_t)XEEN_DS_WALL_SHIFT(0, 2u), 16);
    CHECK(gate(16, 3, 0, &e, &other) == XEEN_BAD_INPUT && e == 0 && word(XEEN_DS_CELL_SEEN) == 0);
    CHECK(xeen_slice_interaction_gate(NULL, &e) == XEEN_BAD_INPUT && xeen_slice_interaction_gate(&m, NULL) == XEEN_BAD_INPUT);
    puts("space gate units: neighbour rules (F492 = F608 east/west, 1 north/south), map id 0, cells, bounds");
}

/* ---------------------------------------------------------------- genuine post-load states */

#define SAVES 10
static uint8_t *load(const char *dir, const char *name, size_t *n)
{
    char path[1024];
    FILE *f;
    long z;
    uint8_t *b;
    CHECK(snprintf(path, sizeof path, "%s/%s", dir, name) > 0);
    if (!(f = fopen(path, "rb"))) {
        return NULL;
    }
    CHECK(!fseek(f, 0, SEEK_END) && (z = ftell(f)) > 0);
    rewind(f);
    CHECK((b = malloc((size_t)z)) != NULL);
    *n = (size_t)z;
    CHECK(fread(b, 1, *n, f) == *n && !fclose(f));
    return b;
}
/* The post-load DS of save N into m.ds; 0 when the save does not load (its error printed). */
static int loaded(const char *dir, unsigned n)
{
    char xs[16], ds[16], error[128];
    size_t len[4];
    uint8_t *b[4];
    xeen_game *g;
    unsigned i;
    snprintf(xs, sizeof xs, "XEEN%02u.WOX", n);
    snprintf(ds, sizeof ds, "DARK%02u.WOX", n);
    b[0] = load(dir, xs, &len[0]);
    b[1] = load(dir, ds, &len[1]);
    b[2] = load(dir, "XEEN.CC", &len[2]);
    b[3] = load(dir, "DARK.CC", &len[3]);
    if (!b[0] || !b[1] || !b[2] || !b[3]) {
        for (i = 0; i < 4; i++) {
            free(b[i]);
        }
        return 0;
    }
    g = xeen_game_create_saved(b[0], len[0], b[1], len[1], b[2], len[2], b[3], len[3], error, sizeof error);
    if (!g || xeen_game_enter_world(g) != XR_OK) {
        printf("save %u does not load: %s\n", n, g ? "world entry" : error);
        xeen_game_destroy(g);
        for (i = 0; i < 4; i++) {
            free(b[i]);
        }
        return 0;
    }
    memset(&m, 0, sizeof m);
    memcpy(m.ds, xeen_game_ds(g), sizeof m.ds);
    xeen_game_destroy(g);
    for (i = 0; i < 4; i++) {
        free(b[i]);
    }
    return 1;
}
static uint64_t fold(uint64_t h, unsigned v, unsigned bytes)
{
    while (bytes--) {
        h = (h ^ (v & 0xffu)) * 0x100000001b3ull;
        v >>= 8;
    }
    return h;
}
/* The digest of save N's cases (the generator's stream: per case status, eligible, F48E F490 F492 F494, other). */
static uint64_t digest(unsigned *cases, unsigned *east_west)
{
    static uint8_t image[65536];
    uint64_t h = 0xcbf29ce484222325ull;
    unsigned facing, other;
    int x, y;
    memcpy(image, m.ds, sizeof image);
    *cases = *east_west = 0;
    for (facing = 0; facing < 4; facing++) {
        for (x = -3; x < 35; x++) {
            for (y = -3; y < 35; y++) {
                uint16_t e;
                xeen_status s;
                memcpy(m.ds, image, sizeof image);
                s = gate(x, y, facing, &e, &other);
                h = fold(h, s == XEEN_OK ? 0u : s == XEEN_CACHE_MISS ? 1u : 2u, 1);
                if (s == XEEN_OK) { /* the original's search has no bound: a miss is compared by status only */
                    h = fold(h, e, 1);
                    h = fold(h, word(XEEN_DS_CELL_WALLS), 2);
                    h = fold(h, word(XEEN_DS_CELL_SURFACE), 2);
                    h = fold(h, word(XEEN_DS_CELL_SEEN), 2);
                    h = fold(h, word(XEEN_DS_MAP_SLOT), 2);
                    h = fold(h, other, 2);
                }
                *east_west += s == XEEN_OK && word(XEEN_DS_CELL_SEEN) == word(XEEN_DS_OUTDOORS) && (x < 0 || x > 15) &&
                              y >= 0 && y <= 15 && !(x > 31);
                ++*cases;
            }
        }
    }
    memcpy(m.ds, image, sizeof image);
    return h;
}
static void genuine(const char *dir, const char *expected)
{
    FILE *f = fopen(expected, "r");
    char line[256];
    unsigned checked = 0;
    CHECK(f != NULL);
    while (fgets(line, sizeof line, f)) {
        unsigned n, cases, ew, map, my_cases, my_ew;
        unsigned long long want;
        uint64_t got;
        if (line[0] == '#' || sscanf(line, "save %u map %u digest %llx cases %u eastwest %u", &n, &map, &want, &cases,
                                     &ew) != 5) {
            continue;
        }
        CHECK(n >= 1 && n <= SAVES);
        CHECK(loaded(dir, n));
        CHECK(m.ds[XEEN_DS_PARTY_MAP] == map);
        got = digest(&my_cases, &my_ew);
        printf("save %u map %u (F608 %04x): digest %016llx, %u cases, %u east/west missing\n", n, map,
               word(XEEN_DS_OUTDOORS), (unsigned long long)got, my_cases, my_ew);
        CHECK(my_cases == cases && my_ew == ew);
        CHECK(got == want);
        ++checked;
    }
    fclose(f);
    CHECK(checked == 4); /* saves 1, 2, 3 and 5: the others need a map relocation before entry */
    printf("space gate genuine: %u post-load states equal the original 1C680\n", checked);
}
static void dump(const char *dir, const char *out)
{
    unsigned n;
    for (n = 1; n <= SAVES; n++) {
        char path[1024];
        FILE *f;
        if (!loaded(dir, n)) {
            continue;
        }
        CHECK(snprintf(path, sizeof path, "%s/slot%u.bin", out, n) > 0);
        CHECK((f = fopen(path, "wb")) != NULL);
        CHECK(fwrite(m.ds, 1, sizeof m.ds, f) == sizeof m.ds && !fclose(f));
        printf("slot%u map %u F608 %04x\n", n, m.ds[XEEN_DS_PARTY_MAP], word(XEEN_DS_OUTDOORS));
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "units")) {
        units();
    } else if (argc == 4 && !strcmp(argv[1], "genuine")) {
        genuine(argv[2], argv[3]);
    } else if (argc == 4 && !strcmp(argv[1], "dump")) {
        dump(argv[2], argv[3]);
    } else {
        fprintf(stderr, "usage: units | genuine DOSDIR EXPECTED | dump DOSDIR OUTDIR\n");
        return 2;
    }
    return 0;
}
