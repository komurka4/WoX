#include "xeen/event_exec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#endif

typedef struct { unsigned copies, reloads, choices, draws; } services;

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int service(void *user, const xeen_event_request *request,
    uint8_t *ds, size_t size, uint16_t *ax)
{
    services *state = user;
    (void)ds; (void)size;
    if (request->address == 0x15616u) { ++state->copies; return 1; }
    if (request->address == 0x4a1b7u) { ++state->reloads; return 1; }
    if (request->address == 0x2f9bau) { ++state->choices; *ax = 0; return 1; }
    if (request->address == 0x504dau && request->argument == 1u &&
        request->second_argument == 10u) {
        ++state->draws; *ax = 7u; return 1;
    }
    return 0;
}

int main(void)
{
    /* Three exact map28 records: current-character SP >= 0, set flag239,
     * then the verified GiveMulti(random 1..10 gold) tuple. */
    static uint8_t records[] = {
        8, 1, 2, 4, 0, 8, 9, 0, 1,
        9, 1, 2, 4, 1, 12, 0, 0, 20, 239,
        15, 1, 2, 4, 2, 20, 100, 10, 0, 0, 0, 0, 0, 0, 0, 0
    };
    static uint8_t truncated[] = { 10, 1, 2, 4, 0, 20, 100, 10, 0, 0, 0 };
    static uint8_t unsupported[] = { 14, 1, 2, 4, 0, 20, 99, 10, 0, 0, 0, 0, 0, 0, 0 };
    static uint8_t cancelled[] = {
        7, 1, 2, 4, 0, 32, 0, 40,
        15, 1, 2, 4, 1, 20, 100, 10, 0, 0, 0, 0, 0, 0, 0, 0
    };
    uint8_t ds[65536] = {0};
    xeen_event_index index;
    xeen_event_execution execution;
    services state = {0, 0, 0, 0};

    ds[0xed97] = 1; ds[0xed98] = 2; ds[0xed96] = 0;
    ds[0xed8c] = 1; ds[0x94] = 0; ds[0x98b4] = 255;
    /* Character 1 SP at record+9E is exactly zero. */
    assert(xeen_events_decode(records, sizeof(records), NULL, 0, &index) == XE_OK);
    assert(xeen_event_execution_begin(&execution, ds, sizeof(ds)) == XEE_DONE);
    assert(xeen_event_execute_interactive(records, sizeof(records), &index,
        ds, sizeof(ds), &execution, 0, service, &state, NULL, NULL) == XEE_BUDGET);
    assert(!state.copies && !state.reloads && !state.choices && !state.draws && !u32(ds + 0x9a16));
    assert(!(ds[0xf01f + 239u / 8u] & (0x80u >> (239u & 7u))));

    assert(xeen_event_execute_interactive(records, sizeof(records), &index,
        ds, sizeof(ds), &execution, 3, service, &state, NULL, NULL) == XEE_EXTERNAL);
    assert(execution.state.boundary == 0x33690u && execution.state.steps == 3u);
    assert(state.copies == 3u && state.reloads == 3u && state.draws == 1u);
    assert(u32(ds + 0x9a16) == 7u);
    assert(ds[0xf01f + 239u / 8u] & (0x80u >> (239u & 7u)));

    memset(ds + 0x9a16, 0, 4);
    ds[0xf01f + 239u / 8u] &= (uint8_t)~(0x80u >> (239u & 7u));
    ds[0xed8c] = 2;
    state.copies = state.reloads = state.choices = state.draws = 0;
    assert(xeen_events_decode(cancelled, sizeof(cancelled), NULL, 0, &index) == XE_OK);
    assert(xeen_event_execution_begin(&execution, ds, sizeof(ds)) == XEE_DONE);
    assert(xeen_event_execute_interactive(cancelled, sizeof(cancelled), &index,
        ds, sizeof(ds), &execution, 2, service, &state, NULL, NULL) == XEE_EXTERNAL);
    assert(execution.state.boundary == 0x33690u && execution.state.steps == 1u);
    assert(state.choices == 1u && !state.draws && !u32(ds + 0x9a16));
    assert(!(ds[0xf01f + 239u / 8u] & (0x80u >> (239u & 7u))));

    state.copies = state.reloads = state.choices = state.draws = 0;
    assert(xeen_events_decode(truncated, sizeof(truncated), NULL, 0, &index) == XE_OK);
    assert(xeen_event_execution_begin(&execution, ds, sizeof(ds)) == XEE_DONE);
    assert(xeen_event_execute_interactive(truncated, sizeof(truncated), &index,
        ds, sizeof(ds), &execution, 1, service, &state, NULL, NULL) == XEE_MALFORMED);
    assert(!state.draws && !u32(ds + 0x9a16));

    assert(xeen_events_decode(unsupported, sizeof(unsupported), NULL, 0, &index) == XE_OK);
    assert(xeen_event_execution_begin(&execution, ds, sizeof(ds)) == XEE_DONE);
    assert(xeen_event_execute_interactive(unsupported, sizeof(unsupported), &index,
        ds, sizeof(ds), &execution, 1, service, &state, NULL, NULL) == XEE_UNSUPPORTED);
    /* Phase61: operation 20 runs; Phase101: mode 99 is not in 2DECE's CS:28D9
     * table, so 2E1F3 calls 2C94C(0, 0, 99, ...), whose give switch (2CFD9)
     * has no native mode 99: an explicit stop there, with no effect. */
    assert(execution.state.boundary == 0x2cfd9u && !state.draws && !u32(ds + 0x9a16));

    puts("Phase26 event semantics, cancellation, budget, malformed and unsupported boundaries passed");
    return 0;
}
