#include "xeen/party.h"
#include "xeen/ds_globals.h"
#include "xeen/character_record.h"
#include "xeen/gameplay_tick.h"
#include "xeen/input_poll.h" /* XEEN_KEY_SPACE */
#include "xeen/combat.h"
#include "xeen/monster_update.h"
#include "xeen/items.h"
#include "xeen/byte_order.h"
/*
 * Where the game clock stops when time cannot pass natively; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    TIME_STOP_NO_STATE = 0x30cf3,             /* no movement state to advance */
    TIME_STOP_NO_RANDOM = 0x30d41,            /* the day's change needs a random source */
    TIME_STOP_DAY_CHANGE = 0x30d41,           /* the day's change stopped */
    TIME_STOP_UNFED_DAWN = 0x30db3,           /* dawn found the party unfed (each member weaker): the caller's message */
    /* Phase 284 (owner-approved registration): the tick's stops, each the value of its public XEEN_TICK_* boundary
     * (XEEN_TICK_CONDITION_LOOP, _CONFUSION_ROLL, _PARALYSIS_ROLL, _SKY_RELOAD; CTests phase51 and phase52 pin them) */
    TIME_STOP_CONDITION_LOOP = 0x2fcab,       /* an 8-hour boundary's poison and disease draws need a random source */
    TIME_STOP_CONFUSION_ROLL = 0x2ff6a,       /* a confused member's recovery draw needs a random source */
    TIME_STOP_PARALYSIS_ROLL = 0x2ffb8,       /* a paralysed member's recovery draw needs a random source */
    TIME_STOP_SKY_RELOAD = 0x30004,           /* night began or ended and no sky service reloaded the sky */
    TIME_STOP_MONSTER_UPDATE = 0x2a701,       /* the countdown ran out and the monsters must move: the non-native
                                               * path does not run their update (XEEN_TICK_MONSTER_UPDATE) */
    TIME_STOP_LOOP_EXIT = 0x17f28             /* the command is refused: the loop is to end (XEEN_TICK_LOOP_EXIT) */
};
/* The clock's optional session services, each NULL when absent: the random generator FN (the conditions' draws and
 * the wares' regeneration need it; without it those steps stop), the sky's reload SKY and the party bar's redraw
 * BAR. Original: the draws of 2FCAB, 2FF59 and 41D20; 2A0FB; 2FFE1's bar. */
typedef struct {
    xeen_entity_random_fn fn;
    void *user;
    int (*sky)(void *, xeen_movement *);
    void *sky_user;
    int session;
    void (*bar)(void *);
    void *bar_user;
} tick_rng;
#define HAS_RNG(g) ((g) && (g)->fn)
/* The RNG alone, outside a session (no sky reload, no bar). */
static tick_rng rng_only(xeen_entity_random_fn fn, void *user)
{
    tick_rng g = {.fn = fn, .user = user, .sky = NULL, .sky_user = NULL, .session = 0, .bar = NULL, .bar_user = NULL};
    return g;
}

/* One random(1, HIGH) draw, passed the caller's AL (pre-time-service evidence only). Original: 504DA. */
static uint16_t draw(const tick_rng *g, uint16_t high, unsigned al)
{
    uint16_t raw = g->fn(g->user, (uint16_t)(al & 255u)), v = 0;
    (void)xeen_entity_rng_range(raw, 1u, high, &v);
    return v;
}
/* One random(LOW, HIGH) draw for the wares' regeneration. Original: 504DA for 305E8 through 41D20. */
static uint16_t ware_draw(void *user, uint16_t low, uint16_t high, uint16_t al)
{
    const tick_rng *g = user;
    uint16_t raw = g->fn(g->user, (uint16_t)(al & 255u)), v = 0;
    (void)xeen_entity_rng_range(raw, low, high, &v);
    return v;
}
/* The bank's interest on the dword at AT (the bank's gold or gems): 1 % of it, the unsigned 32-bit quotient,
 * added. Original: 30DD2 (F012, F016; 10752 with CX = 1). */
static void bank_interest(xeen_movement *m, uint16_t at)
{
    uint32_t v = xeen_ds_read32(m, at);
    xeen_ds_write32(m, at, v + v / 100u);
}
/* 29168: the morning reset of temporary values. Per member the six
 * temporary resistances (138h..142h, even), AC (22h), level (24h) and the
 * seven stat temporaries (15h..21h, odd); then the party resistance words
 * EFFC..F002, EFF8 and the spell bytes ED9E, EDA0..EDA6. */
static void reset_temps(xeen_movement *m)
{
    static const uint16_t words[5] = {XEEN_DS_PROTECTION_POISON, XEEN_DS_PROTECTION_COLD,
                                      XEEN_DS_PROTECTION_ELECTRICITY, XEEN_DS_PROTECTION_FIRE,
                                      XEEN_DS_PARTY_LIGHT};
    static const uint16_t bytes[8] = {XEEN_DS_EFFECT_LEVITATE,     XEEN_DS_EFFECT_WALK_ON_WATER,
                                      XEEN_DS_EFFECT_WIZARD_EYE,   XEEN_DS_EFFECT_CLAIRVOYANCE,
                                      XEEN_DS_EFFECT_HEROISM,      XEEN_DS_EFFECT_HOLY_BONUS,
                                      XEEN_DS_EFFECT_POWER_SHIELD, XEEN_DS_EFFECT_BLESS};
    unsigned i, k;
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        uint8_t *ch = m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES;
        for (k = 0; k < 6u; ++k) {
            ch[XEEN_CHARACTER_RESISTANCE_TEMPORARY(k)] = 0;
        }
        ch[XEEN_CHARACTER_AC_BONUS] = ch[XEEN_CHARACTER_LEVEL_BONUS] = 0;
        for (k = 0; k < 7u; ++k) {
            ch[XEEN_CHARACTER_STAT_TEMPORARY(k)] = 0;
        }
    }
    for (k = 0; k < 5u; ++k) {
        xeen_write16(m, words[k], 0);
    }
    for (k = 0; k < 8u; ++k) {
        m->ds[bytes[k]] = 0;
    }
}
void xeen_gameplay_reset_temps(xeen_movement *m)
{
    if (m) {
        reset_temps(m);
    }
}
/* The 37A02 save of member CH against TYPE: the threshold, then one random(1, high) draw (AL its low byte), won
 * when the draw, compared signed, is at most the threshold. */
static int save_won(const xeen_movement *m, const uint8_t *ch, unsigned type, const tick_rng *g)
{
    uint16_t high, threshold = xeen_combat_save_threshold(m, ch, type, &high);
    return (int16_t)draw(g, high, high & 255u) <= (int16_t)threshold;
}
/* 2FD72 (poisoned, save type 3) and 2FDC0 (diseased, type 4): a 1 on
 * random(1,10) and a won 37A02 save clear the counter, anything else
 * doubles it with SHL (80h becomes 0). Returns the AL the next draw sees:
 * the 504DA result, or the 37A02 return. */
static unsigned cure_or_double(const xeen_movement *m, const uint8_t *ch, uint8_t *counter,
                               unsigned type, const tick_rng *g, unsigned al)
{
    uint16_t roll = draw(g, 10u, al);
    if (roll == 1u) {
        if (save_won(m, ch, type, g)) {
            *counter = 0;
            return 1u;
        }
        al = 0u;
    } else {
        al = roll & 255u;
    }
    *counter = (uint8_t)(*counter << 1);
    return al;
}

/* One member's 480-minute bucket (CH, its conditions at C), in the order bucket_loop's contract lists. */
static void bucket_member(xeen_movement *m, uint8_t *ch, const tick_rng *g, uint8_t *dead)
{
    uint8_t *c = ch + XEEN_CHARACTER_CONDITIONS;
    unsigned k, al;
    if (!c[XEEN_CONDITION_DEAD] && !c[XEEN_CONDITION_STONE] && !c[XEEN_CONDITION_ERADICATED]) {
        for (k = 0; k < XEEN_STAT_COUNT; ++k) {
            if (xeen_combat_stat(m, ch, k, 0) < 1u) {
                c[XEEN_CONDITION_DEAD] = 1;
                *dead = 1;
            }
        }
    }
    if (c[XEEN_CONDITION_HEART_BROKEN] && ++c[XEEN_CONDITION_HEART_BROKEN] > 10u) {
        c[XEEN_CONDITION_HEART_BROKEN] = 0;
        c[XEEN_CONDITION_DEPRESSED] = 1;
    }
    c[XEEN_CONDITION_CONFUSED] = c[XEEN_CONDITION_ASLEEP] = c[XEEN_CONDITION_PARALYZED] = c[XEEN_CONDITION_WEAK] = 0;
    al = 0u; /* 2FD41: the first draw sees AL = 0 */
    if (c[XEEN_CONDITION_POISONED]) {
        al = cure_or_double(m, ch, c + XEEN_CONDITION_POISONED, 3u, g, al);
    }
    if (c[XEEN_CONDITION_DISEASED]) {
        (void)cure_or_double(m, ch, c + XEEN_CONDITION_DISEASED, 4u, g, al);
    }
    if (c[XEEN_CONDITION_INSANE]) {
        ++c[XEEN_CONDITION_INSANE];
    }
    for (k = XEEN_CONDITION_DEAD; k <= XEEN_CONDITION_ERADICATED; ++k) {
        if (c[k] && !++c[k]) {
            c[k] = 0xffu;
        }
    }
    if (c[XEEN_CONDITION_IN_LOVE] && ++c[XEEN_CONDITION_IN_LOVE] > 10u) {
        c[XEEN_CONDITION_IN_LOVE] = 0;
        c[XEEN_CONDITION_HEART_BROKEN] = 1;
    }
    c[XEEN_CONDITION_WEAK] = c[XEEN_CONDITION_DRUNK];
    c[XEEN_CONDITION_DRUNK] = 0;
    if (c[XEEN_CONDITION_DEPRESSED]) {
        c[XEEN_CONDITION_DEPRESSED] = (uint8_t)((c[XEEN_CONDITION_DEPRESSED] + 1u) % 4u);
    }
}
/*2FCAB..2FF3F: the 480-minute bucket loop over the active party, one member
 * at a time (the conditions are ch+143h+index). Without a session RNG a poisoned
 * or diseased member stops at 2FCAB before any write. Per member, in order:
 * - a member neither dead, stone nor eradicated with a stat below 1 dies, and [BP-2] is set (2FCF6: *dead = 1,
 *   read at 2FFD9 for 2FFE1's 2ABB8); it is then aged in the same pass like any other dead member;
 * - Heart Broken counts up and past 10 gives way to Depressed;
 * - Confused, Asleep, Paralyzed and Weak end;
 * - Poisoned, then Diseased, is cured by a 1 on random(1, 10) and a won save, otherwise doubled (cure_or_double);
 * - Insane counts up; Dead, Stone and Eradicated count up, saturating at FFh;
 * - In Love counts up and past 10 gives way to Heart Broken;
 * - Drunk becomes Weak (Weak takes Drunk's count, Drunk ends);
 * - Depressed counts up modulo 4 (from 1 it ends at the third increment). */
static int bucket_loop(xeen_movement *m, const tick_rng *g, uint8_t *dead)
{
    unsigned i;
    if (!HAS_RNG(g)) {
        for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
            const uint8_t *c = m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES +
                               XEEN_CHARACTER_CONDITIONS;
            if (c[XEEN_CONDITION_POISONED] || c[XEEN_CONDITION_DISEASED]) {
                return 0;
            }
        }
    }
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        bucket_member(m, m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES, g, dead);
    }
    return 1;
}
/* 30CF3's wrap: past 1440 minutes the day advances (a day past 99, unsigned below 8000h, wraps to 0 and the year
 * advances). */
static void day_wrap(xeen_movement *m)
{
    while (xeen_read16(m, XEEN_DS_MINUTE_OF_DAY) >= 1440u) {
        uint16_t day = (uint16_t)(xeen_read16(m, XEEN_DS_DAY) + 1u);
        xeen_write16(m, XEEN_DS_MINUTE_OF_DAY,
                     (uint16_t)(xeen_read16(m, XEEN_DS_MINUTE_OF_DAY) - 1440u));
        xeen_write16(m, XEEN_DS_DAY, day);
        if (day < 32768u && day >= 100u) {
            xeen_write16(m, XEEN_DS_DAY, (uint16_t)(day - 100u));
            xeen_write16(m, XEEN_DS_YEAR, (uint16_t)(xeen_read16(m, XEEN_DS_YEAR) + 1u));
        }
    }
}
/* An unfed party's dawn: every member's Weak condition rises by one, up to FFh. */
static void weaken_party(xeen_movement *m)
{
    unsigned i;
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        uint8_t *weak = m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES +
                        XEEN_CHARACTER_CONDITION(XEEN_CONDITION_WEAK);
        if (*weak < 0xffu) {
            ++*weak;
        }
    }
}
/*
 * The morning step: the first clock charge at or after XEEN_NIGHT_ENDS (05:00) of a new day, while the day's dawn
 * is pending (DAWN_PENDING, set by the day change). During an event script or a town location's own loop the dawn
 * only clears the flag. Otherwise the temporary values are reset (reset_temps). A party with PARTY_FED set, or one
 * asleep in Rest, then has PARTY_FED cleared and the dawn ends; an awake party without it grows weaker
 * (weaken_party) and the step stops at the unfed dawn's stop with the dawn still pending: the caller shows "The
 * Party needs rest!" and clears it. Returns 0 otherwise.
 * Original: 30D56 (9842 the pending dawn, F01E the fed flag, modes 9 and 17 untouched, 29168 the reset, the weak
 * condition ch+145h, 30DB3 the message's call).
 */
static uint32_t dawn(xeen_movement *m)
{
    if (!m->ds[XEEN_DS_DAWN_PENDING] || xeen_read16(m, XEEN_DS_MINUTE_OF_DAY) < XEEN_NIGHT_ENDS) {
        return 0;
    }
    if (m->ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_EVENT && m->ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_LOCATION) {
        reset_temps(m);
        if (!m->ds[XEEN_DS_PARTY_FED] && m->ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_REST) {
            weaken_party(m);
            return TIME_STOP_UNFED_DAWN;
        }
        m->ds[XEEN_DS_PARTY_FED] = 0;
    }
    m->ds[XEEN_DS_DAWN_PENDING] = 0;
    return 0;
}
/**
 * Advances the game clock by AMOUNT minutes (the 2FC7E path).
 *
 * Runs the party's condition loop when the clock crosses a 480-minute bucket, wraps the day at 1440
 * minutes and stops at the 30CF3 tail, whose effects time_tail applies.
 */
static xeen_gameplay_tick clock_amount(xeen_movement *m, xeen_gameplay_tick r, uint16_t amount,
                                       const tick_rng *g)
{
    uint16_t clock;
    /*2FC7E: within a480-minute bucket the first party loop is skipped;
     * empty-party paths skip it even when the bucket changes. No party created. */
    r.amount = amount;
    clock = xeen_read16(m, XEEN_DS_MINUTE_OF_DAY);
    if (m->ds[XEEN_DS_PARTY_COUNT] && (uint16_t)(clock + amount) / 480u != clock / 480u &&
        !bucket_loop(m, g, &r.dead)) {
        r.boundary = TIME_STOP_CONDITION_LOOP;
        return r;
    }
    /*30CF3 arithmetic is already recovered. Stop BEFORE its external tail;
     * no CLOCK_TAIL callback is acknowledged. */
    xeen_write16(m, XEEN_DS_MINUTE_OF_DAY, (uint16_t)(clock + amount));
    day_wrap(m);
    r.boundary = XEEN_TICK_CLOCK_TAIL;
    return r;
}

/* A command's clock charge: 10 minutes outdoors, 1 indoors. A defeated party's key (the extra poll after a fatal
 * event) charges nothing: no time passes, the monsters do not move, the countdown is kept, and the command goes on
 * at its own next step (a step's surface step, say). Original: 28178 (the amount), 2817D (F60A: no 2FC7E, no 28195
 * 2A701, 96C0 kept; 17D50 -> 17264). */
static xeen_gameplay_tick clock_begin(xeen_movement *m, xeen_gameplay_tick r, const tick_rng *g)
{
    if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
        r.status = XT_EXTERNAL;
        r.boundary = XEEN_TICK_POST_ACTION;
        return r;
    }
    return clock_amount(m, r, xeen_read16(m, XEEN_DS_OUTDOORS) ? 10u : 1u, g);
}

/**
 * Starts one exploration command tick: refuses the command while the F60A or F64A gates are set and
 * otherwise performs the command's navigation step.
 */
static xeen_gameplay_tick tick_begin(xeen_movement *m, uint16_t command, const tick_rng *g)
{
    xeen_gameplay_tick r = {XSN_INVALID, XT_INPUT_ERROR, 0, 0, 0};
    if (!m) {
        return r;
    }
    /* The bounded slices refuse a command once either gate is set. In a
     * session the 17B09 loop has already decided: with a key available it
     * dispatches it even after a join scene set F60A (17B24), and the
     * command's 17F07 tail exits at 17F28. */
    if ((m->ds[XEEN_DS_PARTY_DEFEATED] && !(g && g->session)) || xeen_read16(m, XEEN_DS_LOOP_EXIT)) {
        r.status = XT_EXTERNAL;
        r.boundary = TIME_STOP_LOOP_EXIT;
        return r;
    }
    r.navigation = xeen_slice_navigate(m, command);
    if (r.navigation == XSN_INVALID) {
        return r;
    }
    r.status = XT_EXTERNAL;
    if (r.navigation == XSN_BOUNDARY) {
        r.boundary = XEEN_TICK_OFF_MAP;
        return r;
    }
    if (r.navigation == XSN_SERVICE_REQUIRED) {
        r.boundary = XEEN_TICK_MOVE_SERVICE;
        return r;
    }
    if (r.navigation == XSN_BLOCKED) {
        r.boundary = XEEN_TICK_MOVE_REJECTED;
        return r;
    }
    if (r.navigation == XSN_TURNED) {
        r.boundary = XEEN_TICK_POST_ACTION;
        return r;
    }
    return clock_begin(m, r, g);
}
xeen_gameplay_tick xeen_gameplay_tick_begin(xeen_movement *m, uint16_t command)
{
    return tick_begin(m, command, NULL);
}

/*
 * 30D27..30D51, a charge's day change (the tick's time_tail and 30CF3 on its own): on a new day ending in 1, or
 * after more than 1440 minutes (30D27 uses signed IDIV and signed amount comparison), 30D41: the blacksmith wares
 * 41D20, then the bank interest 30DD2; a new day sets the dawn flag (30D51). Returns 0, or 30D41 when the wares need
 * the RNG G lacks (then nothing is written).
 */
static uint32_t day_change(xeen_movement *m, uint16_t day, uint16_t old_day, uint16_t amount, const tick_rng *g)
{
    if (((xeen_sign16(day) % 10) == 1 || xeen_sign16(amount) > 1440) &&
        day != old_day) {
        if (!HAS_RNG(g)) {
            return TIME_STOP_NO_RANDOM;
        }
        xeen_item_regenerate_wares(m, ware_draw, (void *)g);
        bank_interest(m, XEEN_DS_BANK_GOLD);
        bank_interest(m, XEEN_DS_BANK_GEMS);
    }
    if (day != old_day) {
        m->ds[XEEN_DS_DAWN_PENDING] = 1;
    }
    return 0;
}
/* The clock's tail after the day's change: the dawn, the timed conditions and the night test; on success it ends
 * where the charge returns to its command. Original: 30D27 onward in 2FC7E (2FF59, 2FFE7; the 28195 return). */
static xeen_gameplay_tick charge_tail(xeen_movement *, xeen_gameplay_tick, const tick_rng *);
/**
 * Applies the day-change tail of the clock (30D27 and 30D41).
 *
 * Uses the original's signed division and comparisons: on a new day ending in 1, or after more than
 * 1440 minutes, the blacksmith wares are regenerated (41D20) and the bank interest is paid (30DD2).
 * Stops at the boundary of the first service it cannot run without an RNG.
 */
static xeen_gameplay_tick time_tail(xeen_movement *m, xeen_gameplay_tick r, uint16_t old_day,
                                    const tick_rng *g)
{
    uint16_t day;
    if (r.boundary != XEEN_TICK_CLOCK_TAIL) {
        return r;
    }
    day = xeen_read16(m, XEEN_DS_DAY);
    if (day_change(m, day, old_day, r.amount, g)) {
        r.boundary = TIME_STOP_DAY_CHANGE;
        return r;
    }
    /*30D56: the first charge at or after 05:00 of a new day. Outside modes
     * 9 and 17, 29168 resets the temporaries; a rested party (F01E) only
     * clears the flag; otherwise, unless asleep (mode 5), weak (ch+145h)
     * rises below FFh and 18F2F shows "The Party needs rest!" (DS 629B),
     * which stays an explicit stop after those writes. 2ABB8(1) redraws
     * the portraits (presentation). */
    if (dawn(m)) {
        r.boundary = TIME_STOP_UNFED_DAWN;
        return r;
    }
    return charge_tail(m, r, g);
}

/*2FF55 onward, after 30CF3 returns: the 2FF59 timed conditions and the
 * 2FFE7 night test. Success stops at the 28195 return of the 2FC7E call. */
static xeen_gameplay_tick charge_tail(xeen_movement *m, xeen_gameplay_tick r, const tick_rng *g)
{
    unsigned i;
    uint16_t minutes = xeen_read16(m, XEEN_DS_MINUTE_OF_DAY), night;
    /* 2FF59, every time charge: confused (ch+14D) tries random(1,3) == 1 and
     * then a type-0 37A02 save, which clears it, else it decrements; paralysed
     * (ch+14E) decrements on random(1,5) == 1. Each member's draws enter with
     * AL = the party size (ED8C, loaded at 2FFD0); the paralysis draw sees the
     * confusion roll or the 37A02 return. Without a session RNG a nonzero
     * condition stops here before its draw. */
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        uint8_t *ch = m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES,
                *c = ch + XEEN_CHARACTER_CONDITIONS;
        unsigned al = m->ds[XEEN_DS_PARTY_COUNT];
        if (c[XEEN_CONDITION_CONFUSED]) {
            uint16_t roll;
            if (!HAS_RNG(g)) {
                r.boundary = TIME_STOP_CONFUSION_ROLL;
                return r;
            }
            roll = draw(g, 3u, al);
            al = roll & 255u;
            if (roll == 1u) {
                if (save_won(m, ch, 0u, g)) {
                    c[XEEN_CONDITION_CONFUSED] = 0;
                    al = 1u;
                } else {
                    --c[XEEN_CONDITION_CONFUSED];
                    al = 0u;
                }
            }
        }
        if (c[XEEN_CONDITION_PARALYZED]) {
            if (!HAS_RNG(g)) {
                r.boundary = TIME_STOP_PARALYSIS_ROLL;
                return r;
            }
            if (draw(g, 5u, al) == 1u) {
                --c[XEEN_CONDITION_PARALYZED];
            }
        }
    }
    /*2FFD9: [BP-2] set by the 2FCAB loop: 2FFE1 2ABB8(1) redraws the party
     * bar (Rest's first bar with an aging death). */
    if (r.dead) {
        if (g && g->bar) {
            g->bar(g->bar_user);
        }
        r.dead = 2;
    }
    /*2FFE7: 30004 calls 2A0FB when the night state differs from 8C92; the
     * sky service commits 8C92 and reloads the F45C sky. Without it, or when
     * the authentic sprite cannot be loaded, the charge stops at 30004. */
    night = (minutes < XEEN_NIGHT_ENDS || minutes >= XEEN_NIGHT_BEGINS) ? 1u : 0u;
    if (night != xeen_read16(m, XEEN_DS_NIGHT) && (!g || !g->sky || !g->sky(g->sky_user, m))) {
        r.boundary = TIME_STOP_SKY_RELOAD;
        return r;
    }
    r.boundary = XEEN_TICK_CHARGE_DONE;
    return r;
}

static xeen_gameplay_tick after_charge(xeen_movement *, xeen_gameplay_tick, int, const uint8_t *,
                                       size_t, const tick_rng *);
/*
 * Runs one command tick (COMMAND), or with WAIT a clock wait, then the time tail and the round charge's return
 * (after_charge). NATIVE (a connected session) enables the native continuations: a step off the map keeps its
 * out-of-map byte coordinate (the step direction from the facing and COMMAND, xeen_step_direction) and goes on
 * through the clock to the outdoor boundary 17334 instead of stopping at 17D4B (a facing above 3 is XT_INPUT_ERROR),
 * and after_charge runs the monster update instead of stopping there.
 */
static xeen_gameplay_tick begin_extended(xeen_movement *m, uint16_t command, int native, int wait,
                                         const uint8_t *database, size_t database_size,
                                         const tick_rng *g)
{
    uint16_t old_day = m ? xeen_read16(m, XEEN_DS_DAY) : 0;
    xeen_gameplay_tick r =
            wait ? clock_begin(m, (xeen_gameplay_tick){XSN_INVALID, XT_EXTERNAL, 0, 0, 0}, g)
                 : tick_begin(m, command, g);
    /* 17D4B is after the original byte INC/DEC. The older bounded adapter
     * deliberately stopped before committing an out-of-map coordinate. A
     * connected native session retains that byte and continues through the
     * ordinary clock/post-action path to outdoor boundary call 17334. */
    if (native && r.navigation == XSN_BOUNDARY && r.boundary == XEEN_TICK_OFF_MAP) {
        int dx = 0, dy = 0;
        if (!xeen_direction_step(xeen_step_direction(m->ds[XEEN_DS_PARTY_DIRECTION], command), &dx, &dy)) {
            r.status = XT_INPUT_ERROR;
            return r;
        }
        m->ds[XEEN_DS_PARTY_X] = (uint8_t)(m->ds[XEEN_DS_PARTY_X] + dx);
        m->ds[XEEN_DS_PARTY_Y] = (uint8_t)(m->ds[XEEN_DS_PARTY_Y] + dy);
        r = clock_begin(m, r, g);
    }
    r = time_tail(m, r, old_day, g);
    return after_charge(m, r, native, database, database_size, g);
}
/*28195 after the 2FC7E call returns. */
static xeen_gameplay_tick after_charge(xeen_movement *m, xeen_gameplay_tick r, int native,
                                       const uint8_t *database, size_t database_size,
                                       const tick_rng *g)
{
    if (r.boundary != XEEN_TICK_CHARGE_DONE) {
        return r;
    }
    /*28195: original condition-state service precedes the state=3 write. */
    if (m->ds[XEEN_DS_MONSTER_COUNTDOWN]) {
        uint32_t boundary = 0;
        if (!native) {
            r.boundary = TIME_STOP_MONSTER_UPDATE;
            return r;
        }
        xeen_status status =
                database ? xeen_input_release_96c0_database_rng(
                                   m, database, database_size, HAS_RNG(g) ? g->fn : NULL,
                                   HAS_RNG(g) ? g->user : NULL, &boundary)
                         : xeen_input_release_96c0_extended(m, &boundary);
        if (status != XEEN_OK) {
            r.boundary = boundary;
            return r;
        }
    }
    m->ds[XEEN_DS_MONSTER_COUNTDOWN] = 3;
    r.boundary = XEEN_TICK_POST_ACTION;
    return r;
}

xeen_gameplay_tick xeen_gameplay_tick_begin_extended(xeen_movement *m, uint16_t command)
{
    return begin_extended(m, command, 0, 0, NULL, 0, NULL);
}
void xeen_gameplay_tick_strafe_tail(xeen_movement *m, uint16_t command, const xeen_gameplay_tick *t)
{
    if (!m || !t || (command != XEEN_MOVE_STRAFE_LEFT && command != XEEN_MOVE_STRAFE_RIGHT) ||
        t->boundary != XEEN_TICK_POST_ACTION ||
        (t->navigation != XSN_MOVED && t->navigation != XSN_BOUNDARY)) {
        return;
    }
    xeen_navigation_turn_tail(m); /* 17CCD after the strafe's 28178 */
}
xeen_gameplay_tick xeen_gameplay_tick_begin_native(xeen_movement *m, uint16_t command)
{
    return begin_extended(m, command, 1, 0, NULL, 0, NULL);
}
xeen_gameplay_tick xeen_gameplay_tick_begin_database(xeen_movement *m, uint16_t command,
                                                     const uint8_t *database, size_t database_size)
{
    return begin_extended(m, command, 1, 0, database, database_size, NULL);
}
xeen_gameplay_tick xeen_gameplay_tick_begin_database_rng(xeen_movement *m, uint16_t command,
                                                         const uint8_t *database,
                                                         size_t database_size,
                                                         xeen_entity_random_fn random, void *user)
{
    tick_rng g = rng_only(random, user);
    return begin_extended(m, command, 1, 0, database, database_size, random ? &g : NULL);
}

/*
 * The Space wait (28178 without a command, the caller's 17DA8): a defeated party's wait charges no time and goes
 * straight on to the post-action; otherwise the time charge and its tails run with the monster database and the
 * session's services G gives (either may be NULL). The command is irrelevant to a wait; Space's is passed.
 */
static xeen_gameplay_tick wait_tick(xeen_movement *m, const uint8_t *database, size_t database_size,
                                    const tick_rng *g)
{
    if (!m) {
        return (xeen_gameplay_tick){XSN_INVALID, XT_INPUT_ERROR, 0, 0, 0};
    }
    if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
        return (xeen_gameplay_tick){XSN_INVALID, XT_EXTERNAL, XEEN_TICK_POST_ACTION, 0, 0};
    }
    return begin_extended(m, XEEN_KEY_SPACE, 1, 1, database, database_size, g);
}
xeen_gameplay_tick xeen_gameplay_tick_wait_native(xeen_movement *m)
{
    return wait_tick(m, NULL, 0, NULL);
}
xeen_gameplay_tick xeen_gameplay_tick_wait_database(xeen_movement *m, const uint8_t *database,
                                                    size_t database_size)
{
    return wait_tick(m, database, database_size, NULL);
}
xeen_gameplay_tick xeen_gameplay_tick_wait_database_rng(xeen_movement *m, const uint8_t *database,
                                                        size_t database_size,
                                                        xeen_entity_random_fn random, void *user)
{
    tick_rng g = rng_only(random, user);
    return wait_tick(m, database, database_size, random ? &g : NULL);
}

/**
 * Advances the clock by AMOUNT minutes through clock_amount and time_tail, as an event's or a
 * service's time charge does; returns the resulting tick status.
 */
static xeen_gameplay_tick time_conditions(xeen_movement *m, uint16_t amount, const tick_rng *g)
{
    xeen_gameplay_tick r = {XSN_INVALID, XT_EXTERNAL, 0, 0, 0};
    uint16_t old_day;
    if (!m) {
        r.status = XT_INPUT_ERROR;
        return r;
    }
    old_day = xeen_read16(m, XEEN_DS_DAY);
    r = clock_amount(m, r, amount, g);
    if (r.boundary != XEEN_TICK_CLOCK_TAIL) {
        return r;
    }
    r = time_tail(m, r, old_day, g);
    if (r.boundary == XEEN_TICK_CHARGE_DONE) {
        r.boundary = 0;
        r.status = XT_INTERNAL;
    }
    return r;
}
xeen_gameplay_tick xeen_gameplay_time_conditions(xeen_movement *m, uint16_t amount)
{
    return time_conditions(m, amount, NULL);
}
xeen_gameplay_tick xeen_gameplay_time_conditions_rng(xeen_movement *m, uint16_t amount,
                                                     xeen_entity_random_fn random, void *user)
{
    tick_rng g = rng_only(random, user);
    return time_conditions(m, amount, random ? &g : NULL);
}

/* Session services: the RNG, the 2A0FB sky reload and the bar redraw hook, as a session (.session = 1). */
static tick_rng services(const xeen_gameplay_services *s)
{
    tick_rng g = {.fn = NULL, .user = NULL, .sky = NULL, .sky_user = NULL, .session = 1, .bar = NULL, .bar_user = NULL};
    if (s) {
        g.fn = s->random;
        g.user = s->random_user;
        g.sky = s->sky;
        g.sky_user = s->sky_user;
        g.bar = s->bar;
        g.bar_user = s->bar_user;
    }
    return g;
}
xeen_gameplay_tick xeen_gameplay_tick_begin_services(xeen_movement *m, uint16_t command,
                                                     const uint8_t *database, size_t database_size,
                                                     const xeen_gameplay_services *s)
{
    tick_rng g = services(s);
    return begin_extended(m, command, 1, 0, database, database_size, &g);
}
xeen_gameplay_tick xeen_gameplay_tick_wait_services(xeen_movement *m, const uint8_t *database,
                                                    size_t database_size,
                                                    const xeen_gameplay_services *s)
{
    tick_rng g = services(s);
    return wait_tick(m, database, database_size, &g);
}
xeen_gameplay_tick xeen_gameplay_time_conditions_services(xeen_movement *m, uint16_t amount,
                                                          const xeen_gameplay_services *s)
{
    tick_rng g = services(s);
    return time_conditions(m, amount, &g);
}
/* After the 18F2F "needs rest" scroll of a tick stopped at 30DB3: 30DC2
 * (2ABB8 portraits, presentation), 30DCA 9842 = 0, then the rest of the
 * 2FC7E call and the 28195 tail exactly as an unsuspended tick. */
xeen_gameplay_tick xeen_gameplay_tick_resume_rest(xeen_movement *m, xeen_gameplay_tick suspended,
                                                  const uint8_t *database, size_t database_size,
                                                  const xeen_gameplay_services *s)
{
    tick_rng g = services(s);
    if (!m || suspended.boundary != XEEN_TICK_UNFED_DAWN) {
        suspended.status = XT_INPUT_ERROR;
        return suspended;
    }
    m->ds[XEEN_DS_DAWN_PENDING] = 0;
    return after_charge(m, charge_tail(m, suspended, &g), 1, database, database_size, &g);
}

xeen_gameplay_tick xeen_gameplay_time_conditions_resume_rest(xeen_movement *m,
                                                             xeen_gameplay_tick suspended,
                                                             xeen_entity_random_fn random,
                                                             void *user)
{
    tick_rng g = rng_only(random, user);
    if (!m || suspended.boundary != XEEN_TICK_UNFED_DAWN) {
        suspended.status = XT_INPUT_ERROR;
        return suspended;
    }
    m->ds[XEEN_DS_DAWN_PENDING] = 0;
    suspended = charge_tail(m, suspended, random ? &g : NULL);
    if (suspended.boundary == XEEN_TICK_CHARGE_DONE) {
        suspended.boundary = 0;
        suspended.status = XT_INTERNAL;
    }
    return suspended;
}

/* 30CF3 on its own: the clock_amount arithmetic and time_tail's 30D27..30DD1
 * part, then the return (the 2FF55 tail belongs to 2FC7E). */
uint32_t xeen_gameplay_clock_add(xeen_movement *m, uint16_t amount, const xeen_gameplay_services *s)
{
    tick_rng g = services(s);
    uint16_t old_day, day;
    if (!m) {
        return TIME_STOP_NO_STATE;
    }
    old_day = xeen_read16(m, XEEN_DS_DAY);
    xeen_write16(m, XEEN_DS_MINUTE_OF_DAY,
                 (uint16_t)(xeen_read16(m, XEEN_DS_MINUTE_OF_DAY) + amount));
    day_wrap(m);
    day = xeen_read16(m, XEEN_DS_DAY);
    if (day_change(m, day, old_day, amount, &g)) {
        return TIME_STOP_DAY_CHANGE;
    }
    return dawn(m);
}
