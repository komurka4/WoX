/*
 * Phase 273: the monster update's condition recovery (recover_conditions, src/world/monster_update.c) at its exact
 * boundary. After the moves, every map monster with a condition other than 0 or Dragon Sleep (13) draws
 * random(1, type + 50) and loses the condition when the draw is at most its type. The cases below put the draw
 * one below, at and one above the type for small, middle and the largest types, in both worlds, and check every
 * DS byte: only the cleared condition bytes may differ from the same update run without conditions.
 *
 * The expectations are the original's: xeen_dat_evidence/phase273/a1 runs 2B5E9 -> 38ADB -> 504DA as original
 * bytes in Unicorn (only the raw generator 50525 scripted, as the native random callback is) over these cases
 * and the --grid cases, and compares them with this program's --grid output.
 * Original: 2B5E9 (the condition, record +7; the type, +6), 38ADB (504DA(1, type + 32h) <= type, signed),
 * 504DA (raw & 7FFFh, IDIV by the width, + low).
 *
 * usage: xeen_phase273_monster_recovery_tests            the built-in cases
 *        xeen_phase273_monster_recovery_tests --grid     one case a line on stdin:
 *            WORLD N TYPE COND ... ; RAW ...   ->   COND' ... ; AX ... ; DRAWS
 */
#include "xeen/monster_update.h"
#include "xeen/map_objects.h"
#include "xeen/ds_globals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                                                     \
    do {                                                                                                             \
        if (!(c)) {                                                                                                  \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                                  \
            return 1;                                                                                                \
        }                                                                                                            \
    } while (0)

enum { MAX_RECORDS = 8, MAX_DRAWS = 8 };

struct script {
    uint16_t raw[MAX_DRAWS];
    uint16_t ax[MAX_DRAWS];
    unsigned draws;
};

static uint16_t scripted(void *user, uint16_t ax)
{
    struct script *s = user;
    uint16_t raw = s->draws < MAX_DRAWS ? s->raw[s->draws] : 0u;
    if (s->draws < MAX_DRAWS) {
        s->ax[s->draws] = ax;
    }
    ++s->draws;
    return raw;
}

struct recovery_case {
    uint8_t world;
    unsigned count;
    uint8_t type[MAX_RECORDS];
    uint8_t condition[MAX_RECORDS];
    uint16_t raw[MAX_DRAWS];
};

/* The Phase 72 start state (tests/test_input_release.c): monsters enabled, party at 14, 4; every record off the
 * map (80h, 80h), so neither the cell count nor the sweep meets it and only the recovery acts on it. */
static void start(xeen_movement *m, const struct recovery_case *c, int with_conditions)
{
    unsigned i;
    memset(m, 0, sizeof *m);
    m->ds[0x96c4] = 1;
    m->ds[0x96c0] = 3;
    m->ds[0xed97] = 14;
    m->ds[0xed98] = 4;
    m->ds[0x9a1c] = 1;
    m->ds[XEEN_DS_WORLD] = c->world;
    m->ds[XEEN_DS_MAP_MONSTER_COUNT] = (uint8_t)c->count;
    for (i = 0; i < c->count; ++i) {
        uint16_t record = (uint16_t)XEEN_MAP_MONSTER(i);
        m->ds[record + XEEN_MAP_MONSTER_X] = 0x80;
        m->ds[record + XEEN_MAP_MONSTER_Y] = 0x80;
        m->ds[record + XEEN_MAP_MONSTER_ID] = c->type[i];
        m->ds[record + XEEN_MAP_MONSTER_CONDITION] = with_conditions ? c->condition[i] : 0u;
    }
}

static int is_condition_byte(unsigned offset, unsigned count)
{
    return offset >= XEEN_DS_MAP_MONSTERS && offset < XEEN_DS_MAP_MONSTERS + XEEN_MAP_MONSTER_BYTES * count &&
           (offset - XEEN_DS_MAP_MONSTERS) % XEEN_MAP_MONSTER_BYTES == XEEN_MAP_MONSTER_CONDITION;
}

/* Runs the update over C; OUT receives each record's condition afterwards. Fails when a byte other than a
 * condition differs from the same update without conditions (the recovery's only effect is the clear). */
static int run(const struct recovery_case *c, uint8_t *out, struct script *s, xeen_movement *with,
               xeen_movement *without)
{
    uint32_t boundary = 1;
    unsigned i;
    memset(s, 0, sizeof *s);
    memcpy(s->raw, c->raw, sizeof s->raw);
    start(with, c, 1);
    CHECK(xeen_input_release_96c0_database_rng(with, NULL, 0, scripted, s, &boundary) == XEEN_OK && !boundary);
    start(without, c, 0);
    CHECK(xeen_input_release_96c0_database_rng(without, NULL, 0, NULL, NULL, &boundary) == XEEN_OK && !boundary);
    for (i = 0; i < sizeof with->ds; ++i) {
        CHECK(is_condition_byte(i, c->count) || with->ds[i] == without->ds[i]);
    }
    for (i = 0; i < c->count; ++i) {
        out[i] = with->ds[XEEN_MAP_MONSTER(i) + XEEN_MAP_MONSTER_CONDITION];
    }
    return 0;
}

/* The raw value whose draw random(1, type + 50) is ROLL: 504DA takes (raw & 7FFFh) mod (type + 50) + 1. */
static uint16_t raw_for(unsigned roll)
{
    return (uint16_t)(roll - 1u);
}

static int grid(xeen_movement *with, xeen_movement *without)
{
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        struct recovery_case c;
        struct script s;
        uint8_t out[MAX_RECORDS];
        unsigned world, i, n = 0;
        char *p = line;
        int used;
        memset(&c, 0, sizeof c);
        if (sscanf(p, "%u %u%n", &world, &c.count, &used) != 2 || c.count > MAX_RECORDS) {
            return 2;
        }
        c.world = (uint8_t)world;
        p += used;
        for (i = 0; i < c.count; ++i) {
            unsigned type, condition;
            if (sscanf(p, "%u %u%n", &type, &condition, &used) != 2) {
                return 2;
            }
            c.type[i] = (uint8_t)type;
            c.condition[i] = (uint8_t)condition;
            p += used;
        }
        p = strchr(p, ';');
        if (!p) {
            return 2;
        }
        ++p;
        for (;;) {
            unsigned raw;
            if (n >= MAX_DRAWS || sscanf(p, "%u%n", &raw, &used) != 1) {
                break;
            }
            c.raw[n++] = (uint16_t)raw;
            p += used;
        }
        if (run(&c, out, &s, with, without)) {
            return 1;
        }
        for (i = 0; i < c.count; ++i) {
            printf("%s%u", i ? " " : "", out[i]);
        }
        printf(" ;");
        for (i = 0; i < s.draws && i < MAX_DRAWS; ++i) {
            printf(" %u", s.ax[i]);
        }
        printf(" ; %u\n", s.draws);
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const uint8_t types[] = {1, 2, 35, 59, 100, 204, 205, 254, 255};
    xeen_movement *with = malloc(sizeof *with), *without = malloc(sizeof *without);
    unsigned t, w, checked = 0;
    CHECK(with && without);
    if (argc == 2 && !strcmp(argv[1], "--grid")) {
        int rc = grid(with, without);
        free(with);
        free(without);
        return rc;
    }
    /* Below, at and above the type: cleared, cleared, kept. In both worlds (the recovery does not read the world),
     * for the largest type too (its bound 305 does not wrap at a byte: the draw's AX is 305). */
    for (w = 0; w < 2u; ++w) {
        for (t = 0; t < sizeof types; ++t) {
            int d;
            for (d = -1; d <= 1; ++d) {
                struct recovery_case c;
                struct script s;
                uint8_t out[MAX_RECORDS];
                if (types[t] + d < 1) {
                    continue; /* the smallest draw is 1 */
                }
                memset(&c, 0, sizeof c);
                c.world = (uint8_t)w;
                c.count = 1;
                c.type[0] = types[t];
                c.condition[0] = 7;
                c.raw[0] = raw_for((unsigned)(types[t] + d));
                CHECK(run(&c, out, &s, with, without) == 0);
                CHECK(s.draws == 1u && s.ax[0] == (uint16_t)(types[t] + 50u));
                CHECK(out[0] == (d <= 0 ? 0u : 7u));
                ++checked;
            }
        }
    }
    {
        /* Type 0: the smallest draw (1) is above it, so it never recovers; the raw's top bit is ignored
         * (8000h draws 1 like 0). */
        struct recovery_case c = {0, 2, {0, 0}, {5, 5}, {0, 0x8000u}};
        struct script s;
        uint8_t out[MAX_RECORDS];
        CHECK(run(&c, out, &s, with, without) == 0);
        CHECK(s.draws == 2u && s.ax[0] == 50u && s.ax[1] == 50u && out[0] == 5u && out[1] == 5u);
        /* The top bit with type 35: 8000h + 34 draws 35 (at the type: cleared), 8000h + 35 draws 36 (kept). */
        struct recovery_case top = {1, 2, {35, 35}, {1, 255}, {0x8000u + 34u, 0x8000u + 35u}};
        CHECK(run(&top, out, &s, with, without) == 0);
        CHECK(s.draws == 2u && out[0] == 0u && out[1] == 255u);
        /* Order and skips: records in index order; 0 and Dragon Sleep (13) draw nothing; 12 and 14 draw. */
        struct recovery_case mix = {0, 5, {35, 35, 35, 35, 35}, {0, 13, 12, 14, 7}, {35, 34, 0}};
        CHECK(run(&mix, out, &s, with, without) == 0);
        CHECK(s.draws == 3u && out[0] == 0u && out[1] == 13u && out[2] == 12u && out[3] == 0u && out[4] == 0u);
        checked += 4;
    }
    printf("monster condition recovery: %u boundary cases passed\n", checked);
    free(with);
    free(without);
    return 0;
}
