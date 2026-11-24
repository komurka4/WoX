/*
 * The tavern's services (Phase 248, from game_town.c): 25997's entry, its sayings and held texts, a drink, a tip,
 * a rumour and the food (asked, answered, paid), in the shared 277E5 location loop. The location loop itself is
 * game_location.c's; the location messages and payments are game_town.c's.
 */
#include "game_location.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when the tavern cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json). The entry stops guard texts read from tavern.bin.
 */
enum {
    TAVERN_STOP_DRINK_TEXT = 0x25a44,        /* "Good Stuff" */
    TAVERN_STOP_SHOW_TEXT = 0x25e71,         /* the tavern's message does not fit */
    TAVERN_STOP_TIP_DRINK_TEXT = 0x25ee2,    /* "Have a Drink" */
    TAVERN_STOP_TIP_DRUNK_TEXT = 0x25cee,    /* "You're Drunk" */
    TAVERN_STOP_TIP_ENTRY = 0x25e39,         /* the tip's text */
    TAVERN_STOP_RUMOUR_TOWN = 0x25fad,       /* the map is not a town */
    TAVERN_STOP_RUMOUR_ENTRY = 0x25fe5,
    TAVERN_STOP_FOOD_ASK = 0x25bf6,          /* the food's question could not be shown */
    TAVERN_STOP_FOOD_TOWN = 0x25b88,         /* the map is not a town */
    TAVERN_STOP_FOOD_ENTRY = 0x25bc4
};
/* ------------------------------ the tavern's services
 * The keys: D drink, F food, R rumour, S Sign In, T tip, F1..F6 a member. The town is the map: Clouds 1Ch, 1Eh,
 * 31h; Darkside 1Dh, 1Fh and any other (the shipped taverns).
 * Original: 25997 (CS:0EB0: D 259B7, F 25AEC, R 25F66, S 25FF0, T 25C89, F1..F6 260DD); tavern.bin by 2B632 at 27B08
 * (DOS memory at 50D4:1C7E, the offsets at 50D4:165C). */
/* Copies tavern.bin's entry K (the text after the file's K-th NUL; entry 0 the file's start, as the guild's
 * spldesc.bin: the original never writes entry 0's offset) into DS at DEST, N bytes, the rest of them zero. Returns 0
 * when the file has fewer entries or the text runs past it. Original: 2B632's offsets; 15616(dest, src, n). */
static int tavern_entry(xeen_game *g, unsigned k, uint16_t dest, unsigned n)
{
    uint8_t *ds = g->movement.ds;
    size_t at = nul_offset(g->guild_desc, g->guild_desc_size, k), i;
    if (at == (size_t)-1) {
        return 0;
    }
    /* 15616(dest, src, n): _fstrncpy, the rest of the n bytes zero. */
    for (i = 0; i < n && at + i < g->guild_desc_size && g->guild_desc[at + i]; ++i) {
        ds[(uint16_t)(dest + i)] = g->guild_desc[at + i];
    }
    if (i < n && at + i >= g->guild_desc_size) {
        return 0; /* the text runs past the file */
    }
    for (; i < n; ++i) {
        ds[(uint16_t)(dest + i)] = 0;
    }
    return 1;
}
/*
 * The tavern's town, 0..2: the world's three shipped taverns in the order the services' ladders test them (25AEC,
 * 25C89, 25F66): Clouds maps 1Ch, 1Eh and 31h; Darkside maps 1Dh, 1Fh and any other map. -1: another Clouds map
 * (each service decides: the tip keeps F658, the food and the rumour stop).
 */
static int tavern_town(unsigned world, unsigned map)
{
    if (world) {
        return map == 29u ? 0 : map == 31u ? 1 : 2;
    }
    return map == 28u ? 0 : map == 30u ? 1 : map == 49u ? 2 : -1;
}
/* Writes the tavern's options page ("Tavern Options for" the member, TEXT, the party's gold) into the right column
 * with the location's buttons and presents it. Returns 0 when the page does not fit. Original: 14BBA(AC0F, [2A4C],
 * the member, TEXT, 2689A of the gold), 4E38C(0Ah), 27E8E's buttons, 50BF6(0Ah). */
static int tavern_say(xeen_game *g, uint16_t text)
{
    xeen_movement *m = &g->movement;
    char gold[24], out[1024];
    int n;
    location_amount(g, gold, sizeof gold, xeen_ds_read32(m, XEEN_DS_PARTY_GOLD));
    n = snprintf(out, sizeof out, far_text(g, XEEN_DS_TAVERN_OPTIONS_TEXT, 0),
                 ds_text(g, g->location_member), ds_text(g, text), gold);
    if (n < 0 || (size_t)n >= sizeof out) {
        return 0;
    }
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    location_window(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN);
    return 1;
}
/* Holds what the tavern shows (STAGE: its text, or a tip in the bottom window) until any key: the key code cleared and
 * the location's loop run as the wait (tavern_held ends it). Original: 189BC (F5FE = 0), then 26E60: the location loop
 * (4EA0D, 189E3, 4EA1C and 26978). */
static xeen_status tavern_hold(xeen_game *g, int stage)
{
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    g->tavern_wait = stage;
    g->location_sampled = 0;
    return panel_set(g, LOCATION_POLL, LOCATION_POLL_BOUNDARY);
}
/* The key that ends a tavern wait: the key code cleared, a tip's bottom window closed, the location redrawn (its
 * loop goes on). Original: 189BC (F5FE = 0); 18765(0Ch); the handler's return (26247; 255D4) to 27F20. */
xeen_status tavern_held(xeen_game *g)
{
    int stage = g->tavern_wait;
    g->tavern_wait = TAVERN_WAIT_NONE;
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    if (stage == TAVERN_WAIT_TIP) {
        panel_window(g, XEEN_WINDOW_BOTTOM, 0);
    }
    return location_redraw(g);
}
/* The tavern's Drink (D): refused for a member who cannot act; the party pays 1 gold (too little: the unpaid message);
 * gulp.voc plays, the tavern's drink flag (F626) is set, "Good Stuff" is shown; with a roll of 1..25 on 1..100 the
 * drinker gets one more level of Drunk, the party bar is redrawn and sound 1Ch plays; then the text's wait.
 * Original: 259B7: 28CFC; 29281(0, 1, 0, 0, 2) (291FB's mode-2 message); 187E9(0, 0), gulp.voc, 187E9(F42C, 0);
 * F626 = 1; 504DA(1, 100) (AL: 50BF6's, 0 under the harness's substitution) below 26: +14Ah + 1, 2ABB8(1),
 * 1880E(1Ch). */
xeen_status tavern_drink(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_status status = XEEN_OK;
    if (location_refused(g, &status)) {
        return status;
    }
    if (!town_pay(g, 1, LOCATION_MESSAGE_UNPAID, &status)) {
        return status;
    }
    town_voice(g, XEEN_TEXT_TAVERN_GULP_VOICE);
    xeen_write16(m, XEEN_DS_TAVERN_DRANK, 1);
    if (!tavern_say(g, XEEN_TEXT_TAVERN_GOOD_STUFF)) {
        return location_stop(g, TAVERN_STOP_DRINK_TEXT);
    }
    if (location_random(g, 1, 100) < 26u) {
        ++m->ds[(uint16_t)(g->location_member + XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DRUNK))];
        if (g->presentation) {
            (void)xeen_world_entry_party_bar(m, g->presentation,
                                             1); /* 2ABB8(1): the party bar, 50BF6(21h) */
        }
        (void)sound_effect(g, 28);
    }
    return tavern_hold(g, TAVERN_WAIT_TEXT);
}
/* Shows the tavern's line (the text in A43F, centred under the tavern's format) in the bottom window, opened framed
 * and presented, then holds it for the tip's wait (tavern_hold). Stops when the line does not fit.
 * Original: 25E65: AC0F = DS:51BF ("\x03c\x0b012%s") of A43F, 1871B(0Ch), 4E38C(0Ch), 50BF6(0Ch), the wait,
 * 18765(0Ch). */
static xeen_status tavern_show(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char out[0x200];
    int n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_TAVERN_LINE), ds_text(g, XEEN_DS_TEXT_BUFFER_0));
    if (n < 0 || (size_t)n >= sizeof out) {
        return location_stop(g, TAVERN_STOP_SHOW_TEXT);
    }
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    panel_window(g, XEEN_WINDOW_BOTTOM, 1);
    menu_window_text(g, XEEN_WINDOW_BOTTOM, WINDOW_FRAMED, out);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_BOTTOM);
    return tavern_hold(g, TAVERN_WAIT_TIP);
}
/* The tavern's T (a tip) for the customer: a member who cannot act is refused; without a drink since the last tip
 * or customer, "Have a Drink"; a drunk customer, "You're Drunk"; otherwise one gold is paid, the thanks voice, and
 * the town's next tip from tavern.bin (its first by town; the map's tip counter, saved with the map, counts on
 * modulo 15, 10 on Darkside) shown.
 * Original: 25C89: 28CFC; F626; +14Ah; 29281(0, 1, 0, 0, 2); tipsb.voc / its Darkside one; F658 by town (Clouds 1Ch
 * 1Eh, 1Eh 2Dh, 31h 3Ch, another map unchanged; Darkside 1Dh 1Eh, 1Fh 28h, else 32h); A43F = C107 + F658 (199
 * bytes); C107 (+33Bh of the map record); 25E65. */
xeen_status tavern_tip(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    xeen_status status = XEEN_OK;
    unsigned map = ds[XEEN_DS_PARTY_MAP];
    if (location_refused(g, &status)) {
        return status;
    }
    if (!xeen_read16(m, XEEN_DS_TAVERN_DRANK)) {
        if (!tavern_say(g, XEEN_TEXT_TAVERN_HAVE_A_DRINK)) {
            return location_stop(g, TAVERN_STOP_TIP_DRINK_TEXT);
        }
        return tavern_hold(g, TAVERN_WAIT_TEXT);
    }
    xeen_write16(m, XEEN_DS_TAVERN_DRANK, 0);
    if (ds[(uint16_t)(g->location_member + XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DRUNK))]) {
        if (!tavern_say(g, XEEN_TEXT_TAVERN_DRUNK)) {
            return location_stop(g, TAVERN_STOP_TIP_DRUNK_TEXT);
        }
        return tavern_hold(g, TAVERN_WAIT_TEXT);
    }
    if (!town_pay(g, 1, LOCATION_MESSAGE_UNPAID, &status)) {
        return status;
    }
    town_voice(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_THANKS_VOICE_DARKSIDE : XEEN_TEXT_THANKS_VOICE_CLOUDS);
    {
        /* each town's tavern.bin tips: Clouds 15 from 1Eh, 2Dh, 3Ch; Darkside 10 from 1Eh, 28h, 32h */
        static const uint8_t first_tip[2][3] = {{0x1e, 0x2d, 0x3c}, {0x1e, 0x28, 0x32}};
        int town = tavern_town(ds[XEEN_DS_WORLD], map);
        if (town >= 0) {
            ds[XEEN_DS_TAVERN_FIRST_TIP] = first_tip[ds[XEEN_DS_WORLD] != 0][town];
        }
    }
    if (!tavern_entry(g, (unsigned)ds[XEEN_DS_MAP_FIELD(TAVERN_TIP)] + ds[XEEN_DS_TAVERN_FIRST_TIP],
                      XEEN_DS_TEXT_BUFFER_0, 0xc7)) {
        return location_stop(g, TAVERN_STOP_TIP_ENTRY);
    }
    ds[XEEN_DS_MAP_FIELD(TAVERN_TIP)] =
            (uint8_t)((unsigned)(uint8_t)(ds[XEEN_DS_MAP_FIELD(TAVERN_TIP)] + 1u) % (ds[XEEN_DS_WORLD] ? 10u : 15u));
    return tavern_show(g);
}
/* The tavern's R (a rumour): the town's ten rumours from tavern.bin, the day's (the day modulo 10, the day as a
 * signed word); no payment. Stops for a Clouds map with no tavern town or a missing text.
 * Original: 25F66: [BP-2] by town (Clouds 1Ch 0, 1Eh 10, 31h 20; Darkside 1Dh 0, 1Fh 10, else 20), A43F = text
 * [BP-2] + EFF0 mod 10; 25E65. */
xeen_status tavern_rumour(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int town = tavern_town(ds[XEEN_DS_WORLD], ds[XEEN_DS_PARTY_MAP]);
    unsigned k;
    if (town < 0) {
        return location_stop(g, TAVERN_STOP_RUMOUR_TOWN);
    }
    /* ten rumours a town, one a day */
    k = (unsigned)((int)(10u * (unsigned)town) + (int16_t)xeen_read16(m, XEEN_DS_DAY) % 10);
    if (!tavern_entry(g, k, XEEN_DS_TEXT_BUFFER_0, 0xc7)) {
        return location_stop(g, TAVERN_STOP_RUMOUR_ENTRY);
    }
    return tavern_show(g);
}
static xeen_status tavern_food_answer(xeen_game *, uint16_t);
/* The food's yes/no over the bottom window (its wait animates the location): FIRST opens it; an answer goes on to
 * tavern_food_answer, a pending wait returns to the game loop (drive() resumes here). Stops when it cannot open.
 * Original: 2F2FF(0, 1) (the wait runs 26978(1)). */
static xeen_status tavern_ask(xeen_game *g, int first)
{
    xeen_movement *m = &g->movement;
    uint16_t answer = 0;
    int handled;
    xeen_event_request r = {XEEN_REQUEST_YES_NO, 0, 1, 0, 0};
    if (first) {
        g->tavern_food_step = TAVERN_FOOD_ASKING;
        g->panel_stage = 0;
    }
    handled = event_service(g, &r, m->ds, sizeof m->ds, &answer);
    if (handled < 0 || (!handled && first && !g->ui.pending)) {
        g->tavern_food_step = TAVERN_FOOD_NONE;
        return location_stop(g, TAVERN_STOP_FOOD_ASK);
    }
    if (!handled) {
        return input_wait(g, XEEN_REQUEST_YES_NO);
    }
    return tavern_food_answer(g, answer);
}
/* The tavern's F (food): the party's full food (members x 15, 60 or 120 by town) and its price by town and world
 * (Clouds 10, 50, 100; Darkside 10, 100, 250), then the town's food text in the bottom window (the right
 * column closed) and its yes/no (tavern_ask). Stops for a Clouds map with no tavern town, or a missing text.
 * Original: 25AEC: F624, F65A and [BP-2] by town (Clouds 1Ch / 1Eh / 31h, Darkside 1Dh / 1Fh / else); A827 = text
 * (Clouds 4Bh, Darkside 3Ch) + [BP-2] (300 bytes); 18765(0Ah), 1871B(0Ch), 4E38C(0Ch, A827), 50BF6(0Ch), 2F2FF(0, 1);
 * no 28CFC. */
xeen_status tavern_food(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    /* each town's food: the packs a member holds, the price of filling them */
    static const uint8_t packs[3] = {15, 60, 120};
    static const uint16_t price[2][3] = {{10, 50, 100}, {10, 100, 250}};
    unsigned n = ds[XEEN_DS_PARTY_COUNT];
    int town = tavern_town(ds[XEEN_DS_WORLD], ds[XEEN_DS_PARTY_MAP]);
    if (town < 0) {
        return location_stop(g, TAVERN_STOP_FOOD_TOWN);
    }
    xeen_write16(m, XEEN_DS_TAVERN_FULL_FOOD, (uint16_t)(n * packs[town]));
    xeen_write16(m, XEEN_DS_TAVERN_FOOD_PRICE, price[ds[XEEN_DS_WORLD] != 0][town]);
    if (!tavern_entry(g, (ds[XEEN_DS_WORLD] ? 0x3cu : 0x4bu) + (unsigned)town, XEEN_DS_TEXT_BUFFER, 300)) {
        return location_stop(g, TAVERN_STOP_FOOD_ENTRY);
    }
    panel_window(g, XEEN_WINDOW_RIGHT_COLUMN, 0);
    panel_window(g, XEEN_WINDOW_BOTTOM, 1);
    menu_window_text(g, XEEN_WINDOW_BOTTOM, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER));
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_BOTTOM);
    return tavern_ask(g, 1);
}
/* The food question answered (ANSWER: yes). Yes with less food than the tavern fills to: the party pays the food's
 * price (too little: the unpaid message), its food becomes full and the world's "thanks" voice plays; yes with full
 * packs: "Your food packs are already full!". Then the bottom window closes, the right column opens, the key code is
 * cleared and the location redrawn. Original: 2F2FF answered (window 12 already closed): 29281(0, F65A, 0, 0, 2),
 * EFF6 = F624, the voice, or 18F2F(2); 25C76: 18765(0Ch), 1871B(0Ah), F5FE = 0 (26193), 27F20. */
static xeen_status tavern_food_answer(xeen_game *g, uint16_t answer)
{
    xeen_movement *m = &g->movement;
    xeen_status status = XEEN_OK;
    g->tavern_food_step = TAVERN_FOOD_NONE;
    if (answer) {
        if (xeen_read16(m, XEEN_DS_PARTY_FOOD) < xeen_read16(m, XEEN_DS_TAVERN_FULL_FOOD)) {
            g->tavern_food_step = TAVERN_FOOD_MESSAGE;
            if (!town_pay(g, xeen_read16(m, XEEN_DS_TAVERN_FOOD_PRICE), LOCATION_MESSAGE_UNPAID, &status)) {
                return status;
            }
            g->tavern_food_step = TAVERN_FOOD_NONE;
            xeen_write16(m, XEEN_DS_PARTY_FOOD, xeen_read16(m, XEEN_DS_TAVERN_FULL_FOOD));
            town_voice(g, m->ds[XEEN_DS_WORLD] ? XEEN_TEXT_THANKS_VOICE_DARKSIDE : XEEN_TEXT_THANKS_VOICE_CLOUDS);
        } else {
            g->tavern_food_step = TAVERN_FOOD_MESSAGE;
            return location_message(g, LOCATION_MESSAGE_PLAIN, XEEN_TEXT_TAVERN_FOOD_FULL);
        }
    }
    panel_window(g, XEEN_WINDOW_BOTTOM, 0);
    panel_window(g, XEEN_WINDOW_RIGHT_COLUMN, 1);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    return location_redraw(g);
}
/** Continues the tavern at its question. */
xeen_status tavern_resume(xeen_game *g)
{
    return tavern_ask(g, 0);
}
