#include "xeen/movement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static xeen_movement state;
static uint8_t expected[65536];
static unsigned cases;
static struct {
    unsigned calls;
    int handled, mutate;
} service_log;

/* Compare the whole state at service entry, so the ordering assertion covers
 * both intended writes and absence of unrelated writes before dispatch. */
static int recording_service(void *user, xeen_movement *m, xeen_service kind,
                             uint16_t a, uint16_t b, uint16_t *result)
{
    CHECK(user == &service_log);
    CHECK(kind == XEEN_96C0 && a == 0 && b == 0);
    CHECK(service_log.calls++ == 0);
    CHECK(memcmp(m->ds, expected, sizeof(expected)) == 0);
    CHECK(m->ds[0x96c0] == 0);
    if (service_log.mutate) {
        m->ds[0x4321] ^= 0x5a;
        expected[0x4321] ^= 0x5a;
        /* The helper must not overwrite the callback's countdown mutation. */
        m->ds[0x96c0] = 0x7b;
        expected[0x96c0] = 0x7b;
    }
    *result = 0xbeef; /* The scene slice does not consume the service's AX. */
    return service_log.handled;
}

static void prepare(void)
{
    unsigned i;
    xeen_movement_init(&state, recording_service, &service_log);
    for (i = 0; i < sizeof(state.ds); ++i)
        state.ds[i] = (uint8_t)((i * 37u + (i >> 8) + 19u) & 255u);
    memset(&service_log, 0, sizeof(service_log));
    service_log.handled = 1;
    state.ds[0x96c0] = 0;
    state.ds[0x9a1c] = 1;
    state.ds[0x97f6] = 0;
    state.ds[0x96c4] = 1;
    /* These belong to the enclosing scene, not this slice. */
    state.ds[0x9877] = 1;
    state.status = XEEN_CACHE_MISS;
}

static unsigned compute_expected(void)
{
    unsigned phase, expires = 0;
    memcpy(expected, state.ds, sizeof(expected));
    phase = ((unsigned)expected[0xf5e0] + 1u) & 255u;
    expected[0xf5e0] = (uint8_t)(phase % 4u);
    if (expected[0xf5e0] == 0)
        expected[0xf5e3] ^= 0x80;
    if (expected[0x96c0] != 0 &&
        (expected[0x9a1c] == 1 || expected[0x9a1c] == 2) &&
        expected[0x97f6] == 0 && expected[0x96c4] != 0) {
        --expected[0x96c0];
        expires = expected[0x96c0] == 0;
    }
    return expires;
}

static void run_case(xeen_status status, unsigned calls)
{
    CHECK(xeen_scene_prefix(&state) == status);
    CHECK(state.status == status);
    CHECK(service_log.calls == calls);
    CHECK(memcmp(state.ds, expected, sizeof(expected)) == 0);
    ++cases;
}

static void phases_and_words(void)
{
    static const uint16_t masks[] = {0, 1, 0x7fff, 0x8000, 0x8001, 0xffff};
    unsigned world, phase, mask, word;
    for (world = 0; world < 2; ++world)
        for (phase = 0; phase < 256; ++phase)
            for (mask = 0; mask < sizeof(masks) / sizeof(masks[0]); ++mask) {
                prepare();
                state.ds[0x94] = (uint8_t)world;
                state.ds[0xf5e0] = (uint8_t)phase;
                xeen_write16(&state, 0xf5e2, masks[mask]);
                CHECK(compute_expected() == 0);
                run_case(XEEN_OK, 0);
            }
    /* Exhaust every initial word, checking both the toggle and non-toggle
     * branches; alternate world without making it an input to the oracle. */
    for (word = 0; word < 65536u; ++word)
        for (phase = 2; phase < 4; ++phase) {
            prepare();
            state.ds[0x94] = (uint8_t)(word & 1u);
            state.ds[0xf5e0] = (uint8_t)phase;
            xeen_write16(&state, 0xf5e2, (uint16_t)word);
            CHECK(compute_expected() == 0);
            run_case(XEEN_OK, 0);
        }
}

static void countdown_matrix(void)
{
    static const uint8_t counts[] = {0, 1, 2, 3, 0xff};
    static const uint8_t modes[] = {0, 1, 2, 5, 9};
    unsigned w, c, mode, blocked, enabled, expires;
    for (w = 0; w < 2; ++w)
        for (c = 0; c < 5; ++c)
            for (mode = 0; mode < 5; ++mode)
                for (blocked = 0; blocked < 2; ++blocked)
                    for (enabled = 0; enabled < 2; ++enabled) {
                        prepare();
                        state.ds[0x94] = (uint8_t)w;
                        state.ds[0xf5e0] = 3;
                        xeen_write16(&state, 0xf5e2, 0x9234);
                        state.ds[0x96c0] = counts[c];
                        state.ds[0x9a1c] = modes[mode];
                        state.ds[0x97f6] = blocked ? 0x80 : 0;
                        state.ds[0x96c4] = enabled ? 0xff : 0;
                        service_log.mutate = 1;
                        expires = compute_expected();
                        run_case(XEEN_OK, expires);
                    }
}

static void service_failures(void)
{
    unsigned absent, phase;
    for (absent = 0; absent < 2; ++absent)
        for (phase = 0; phase < 4; ++phase) {
            prepare();
            state.ds[0xf5e0] = (uint8_t)phase;
            state.ds[0x96c0] = 1;
            service_log.handled = 0;
            service_log.mutate = !absent;
            if (absent)
                state.service = NULL;
            CHECK(compute_expected() == 1);
            run_case(XEEN_SERVICE_REQUIRED, absent ? 0u : 1u);
        }
}

static void read_fixture(const char *path, uint8_t *bytes)
{
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    CHECK(fread(bytes, 1, 65536, f) == 65536);
    CHECK(fgetc(f) == EOF && !ferror(f));
    CHECK(fclose(f) == 0);
}

int main(int argc, char **argv)
{
    CHECK(argc == 1 || argc == 3);
    if (argc == 1) {
        phases_and_words();
        countdown_matrix();
        service_failures();
    }
    if (argc == 3) {
        prepare();
        read_fixture(argv[1], state.ds);
        read_fixture(argv[2], expected);
        /* Captured prefix fixtures must not require unobserved expiration. */
        run_case(XEEN_OK, 0);
    }
    printf("scene prefix: %u cases passed%s\n", cases,
           argc == 3 ? " (including full DS fixture)" : "");
    return 0;
}
