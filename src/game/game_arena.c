#include "game_location.h"
#include "xeen/monster_record.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * The Warzone arena: location kind 6, on Clouds map 55h at cell (7, 6).
 *
 * The Battle Master speaks through the NPC dialogue window (speaker XEEN_DS_WARZONE_TITLE) and asks for numbers with
 * the amount prompt. Each is a native modal whose answer resumes here: g->arena_stage records the question that is
 * open, and drive() hands the dialogue's key to arena_resume instead of the event interpreter. A visit is one of:
 * - a fight is pending and one of its monsters still stands: map text 4, and the visit ends;
 * - a fight is pending and every monster has fallen (the fight was won): every member's Warzone win count goes up by
 *   one; map text 3 formats the first member's count with its ordinal suffix, and the party is sent to Vertigo;
 * - no fight is pending: if any member has 99 or more wins, "What!  You again?..." and the party is sent to Vertigo.
 *   Otherwise map text 0 asks whether to fight. No: map text 1, and the party leaves to map 6, cell (12, 4), facing
 *   west. Yes: the monsters' level (1-10) and their number (1-20) are asked, where 0 ends the visit and a larger
 *   value asks again; map text 2 announces the fight and arena_generate places the opponents.
 * The visit ends (arena_end) with the party facing east at the map's run position and every window closed; leaving
 * (arena_leave) reloads the map's texts, which the dialogue's texts overwrote.
 *
 * Original: the location dispatcher 277E5 runs the shared location setup, calls the handler 360C4 (27CC5) and
 * resumes at 27CCA; 3666C is the handler's common end. The step-by-step notes are in
 * docs/reference/ORIGINAL_NOTES.md (the arena).
 */
enum {
    ARENA_IDLE,
    ARENA_NOT_WON,
    ARENA_WIN,
    ARENA_AGAIN,
    ARENA_ASK,
    ARENA_NO,
    ARENA_LEVEL,
    ARENA_COUNT,
    ARENA_BEGIN
};
/*
 * Where native play stops when the arena cannot go on. Each value is the original address of the step that could not
 * be performed: the stop registry (tools/xeen_replay/stops.json) and every trace report it. The text, record and
 * sprite stops are host guards for resources that are missing or too short; the roll stops mean no random source is
 * attached (the original always has one).
 */
enum {
    ARENA_STOP_NO_QUESTION = 0x360c4,         /* an answer arrived while no arena question was open */
    ARENA_STOP_DIALOGUE = 0x3000d,            /* the Battle Master's dialogue could not be shown */
    ARENA_STOP_STANDING_TEXT = 0x36106,       /* map text 4: monsters still stand */
    ARENA_STOP_VICTORY_TEXT = 0x36149,        /* map text 3: the victory format */
    ARENA_STOP_VICTORY_FORMAT = 0x361ba,      /* the formatted victory text does not fit */
    ARENA_STOP_CHALLENGE_TEXT = 0x36222,      /* map text 0: the question whether to fight */
    ARENA_STOP_REFUSAL_TEXT = 0x3625e,        /* map text 1: the refusal */
    ARENA_STOP_BEGIN_TEXT = 0x36328,          /* map text 2: the fight begins */
    ARENA_STOP_TRAVEL_VERTIGO = 0x361f9,      /* Vertigo did not load */
    ARENA_STOP_TRAVEL_REFUSAL = 0x36285,      /* map 6, where a refusal sends the party, did not load */
    ARENA_STOP_ROLL_OPPONENT = 0x36397,       /* the roll of an opponent type */
    ARENA_STOP_OPPONENT_RECORD = 0x36414,     /* an opponent type outside the monster database */
    ARENA_STOP_OPPONENT_SPRITES = 0x36449,    /* the opponent's %03u.mon did not load */
    ARENA_STOP_OPPONENT_ATTACK = 0x364a7,     /* the opponent's %03u.att did not load */
    ARENA_STOP_ROLL_POSITION = 0x364d0,       /* the roll of a monster's cell */
    ARENA_STOP_ROLL_OPPONENT_SLOT = 0x3653b,  /* the roll of which of the five types a monster is */
    ARENA_STOP_MONSTER_RECORD = 0x36593,      /* a placed monster's type outside the monster database */
    ARENA_STOP_ROLL_FRAME = 0x365ae,          /* the roll of a monster's first sprite frame */
    ARENA_STOP_ROLL_EFFECT_TIMER = 0x3663f    /* the roll of a monster's effect timer */
};
/* The destinations: a winner, or a party the Battle Master turns away, goes to Vertigo; a refusal to fight leaves to
 * map 6 at the arena's door. */
enum {
    ARENA_MAP_VERTIGO = 0x1c,
    ARENA_MAP_REFUSAL = 6,
    ARENA_REFUSAL_X = 12,
    ARENA_REFUSAL_Y = 4
};
/* The questions' limits, the generator's sizes, and the win count at which the Battle Master turns a party away. */
enum {
    ARENA_MAX_LEVEL = 10,
    ARENA_MAX_MONSTERS = 20,
    ARENA_TYPES_PER_LEVEL = 7,
    ARENA_OPPONENT_TYPES = 5,
    ARENA_WINS_TURNED_AWAY = 99
};
/* The NPC dialogue's modes (3000D's second argument): a yes/no question, a message, and a question answered with
 * the amount prompt. */
enum {
    ARENA_ASK_YES_NO = 0,
    ARENA_MESSAGE = 1,
    ARENA_ASK_NUMBER = 2
};
/* The map texts the arena shows (indices into the map's XEEN_DS_EVENT_TEXT_OFFSETS). */
enum {
    ARENA_TEXT_CHALLENGE = 0,
    ARENA_TEXT_REFUSAL = 1,
    ARENA_TEXT_BEGIN = 2,
    ARENA_TEXT_VICTORY = 3,
    ARENA_TEXT_STANDING = 4
};
/* Member I's Warzone wins: award 9 ("%u Warzone Wins", award.bin), a count the awards list (416A2)
 * prints. */
#define WARZONE_WINS(i) \
    (XEEN_PARTY_RECORDS + (i) * XEEN_CHARACTER_BYTES + XEEN_CHARACTER_AWARDS + XEEN_AWARD_WARZONE_WINS)
/* Map text K (XEEN_DS_EVENT_TEXT_OFFSETS into the block at 9C5C:9C5E). */
static const uint8_t *arena_text(xeen_game *g, unsigned k, size_t *room)
{
    uint16_t at = xeen_read16(&g->movement, (uint16_t)(XEEN_DS_EVENT_TEXT_OFFSETS + 2u * k));
    if (!g->resources.texts.bytes || at >= g->resources.texts.size) {
        return NULL;
    }
    *room = g->resources.texts.size - at;
    return g->resources.texts.bytes + at;
}
/* Copies map text K into the 300 bytes at DEST, zero padded (the original's _fstrncpy, 15616); 0 when the text is
 * missing or runs past the end of the file. */
static int arena_copy(xeen_game *g, uint16_t dest, unsigned k)
{
    size_t room = 0, i;
    const uint8_t *t = arena_text(g, k, &room);
    if (!t) {
        return 0;
    }
    for (i = 0; i < 300u && i < room && t[i]; ++i) {
        g->movement.ds[(uint16_t)(dest + i)] = t[i];
    }
    if (i < 300u && i == room) {
        return 0; /* the text runs past the file */
    }
    for (; i < 300u; ++i) {
        g->movement.ds[(uint16_t)(dest + i)] = 0;
    }
    return 1;
}
/** Stops the arena at BOUNDARY (an ARENA_STOP_* site, or a stop forwarded from a callee). */
static xeen_status arena_stop(xeen_game *g, uint32_t boundary)
{
    g->arena_stage = ARENA_IDLE;
    return location_stop(g, boundary);
}
/* Leaves the arena: the map's texts again, the world's archive, the location's buttons popped; the location returns
 * 0 to the event script (MOVED: the party was sent to another map). Original: 27CCA..27D6B, then 3246F. */
static xeen_status arena_leave(xeen_game *g, int moved)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, world = ds[XEEN_DS_WORLD];
    g->arena_stage = ARENA_IDLE;
    xeen_select_archive(m, 0); /* the map's texts are reloaded from the Clouds archive */
    {
        char name[16];
        (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_MAP_TEXTS_FILE),
                       ds[XEEN_DS_PARTY_MAP] > 99u ? 'x' : '0', (unsigned)ds[XEEN_DS_PARTY_MAP]);
        memcpy(ds + XEEN_DS_FILE_NAME, name, strlen(name) + 1u);
    }
    xeen_write16(m, XEEN_DS_LOADED_FILE_SIZE, (uint16_t)g->resources.texts.size);
    xeen_select_archive(m, world);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    g->location_kind = 0;
    return location_return(g, 0, moved);
}
/* Loads MAP for the party (Vertigo, or map 6 after a refusal); the position is kept unless the caller sets one.
 * Original: the map load 19E82(MAP). */
static int arena_travel(xeen_game *g, uint16_t map)
{
    if (map_reload(g, map) != XR_OK) {
        return 0;
    }
    xeen_navigation_set(&g->movement, (xeen_navigation){g->movement.ds[XEEN_DS_PARTY_DIRECTION],
                                                        g->movement.ds[XEEN_DS_PARTY_X],
                                                        g->movement.ds[XEEN_DS_PARTY_Y],
                                                        g->movement.ds[XEEN_DS_PARTY_MAP]});
    return 1;
}
/* The visit's end: the party faces east at the map's run position, and every window closes.
 * Original: 3666C (the run position 2125F, the windows 186C0). */
static xeen_status arena_end(xeen_game *g, int moved)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint32_t stop;
    ds[XEEN_DS_PARTY_DIRECTION] = XEEN_EAST;
    ds[XEEN_DS_PARTY_X] = ds[XEEN_DS_MAP_FIELD(RUN_X)];
    ds[XEEN_DS_PARTY_Y] = ds[XEEN_DS_MAP_FIELD(RUN_Y)];
    xeen_navigation_set(m, (xeen_navigation){ds[XEEN_DS_PARTY_DIRECTION], ds[XEEN_DS_PARTY_X],
                                             ds[XEEN_DS_PARTY_Y], ds[XEEN_DS_PARTY_MAP]});
    if ((stop = windows_close_all(g)) != 0) {
        return arena_stop(g, stop);
    }
    return arena_leave(g, moved);
}
static xeen_status arena_answer(xeen_game *g, uint16_t answer);
/* The face the arena's dialogue shows: 3000D(1Bh, name, text, mode) (3611C push 1Bh). */
#define ARENA_FACE 0x1bu
/* The Battle Master says TEXT (a DS offset) in MODE, the question of STAGE: an answer continues at arena_answer; a
 * wait leaves the dialogue's poll to the game loop. Original: 3000D(1Bh, DS:6426, TEXT, MODE). */
static xeen_status arena_say(xeen_game *g, int stage, uint16_t mode, uint16_t text)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t answer = 0;
    int handled;
    xeen_event_request r = {XEEN_REQUEST_NPC_DIALOGUE, ARENA_FACE, mode, 0, 0};
    g->arena_stage = stage;
    g->arena_text = text;
    g->arena_mode = mode;
    g->panel_stage = 0; /* after a 28ABA: no panel poll is open */
    handled = run_npc_dialogue(g, &r, ds, sizeof m->ds, &answer, ds + xeen_read16(m, XEEN_DS_WARZONE_TITLE),
                               ds + text);
    if (handled < 0) {
        return arena_stop(g, ARENA_STOP_DIALOGUE);
    }
    if (!handled) {
        if (!g->ui.pending) {
            return arena_stop(g, ARENA_STOP_DIALOGUE);
        }
        return input_wait(g, XEEN_REQUEST_NPC_DIALOGUE);
    }
    xeen_presentation_trace_extra("npcend"); /* evidence: 3000D's RETF (305E7) */
    return arena_answer(g, answer);
}
/* drive() while the arena's dialogue is open: the dialogue again, and its answer. Unlike arena_say, an unhandled
 * dialogue here waits for input without testing g->ui.pending (a native session asymmetry; its reason is not
 * established from the original). */
xeen_status arena_resume(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t answer = 0;
    int handled;
    xeen_event_request r = {XEEN_REQUEST_NPC_DIALOGUE, ARENA_FACE, g->arena_mode, 0, 0};
    handled = run_npc_dialogue(g, &r, ds, sizeof m->ds, &answer, ds + xeen_read16(m, XEEN_DS_WARZONE_TITLE),
                               ds + g->arena_text);
    if (handled < 0) {
        return arena_stop(g, ARENA_STOP_DIALOGUE);
    }
    if (!handled) {
        return input_wait(g, XEEN_REQUEST_NPC_DIALOGUE);
    }
    xeen_presentation_trace_extra("npcend"); /* evidence: 3000D's RETF (305E7) */
    return arena_answer(g, answer);
}
/* The amount prompt for the open number question: two digits, 200 pixels wide, in window 11 (the dialogue's text
 * window). Original: 28ABA(2, C8h, 0Bh). */
static xeen_status arena_number(xeen_game *g)
{
    g->amount_window = 11;
    g->amount_width = 200;
    g->amount_max = 2;
    g->amount_owner = AMOUNT_ARENA;
    return amount_begin(g);
}
/* One roll of the original's random range 504DA(LOW, HIGH) with AX as it stands at the call (the generator passes
 * the value it last computed); 0 when no random source is attached. Original: the generator 3638B..3666A. */
static int arena_draw(xeen_game *g, uint16_t ax, uint16_t low, uint16_t high, uint16_t *v)
{
    return g->event_random &&
           xeen_entity_rng_range(g->event_random(g->event_random_user, ax), low, high, v);
}
/*
 * The opponent type 3638B..363FC makes of a drawn monster id (the byte (level - 1) x 7 + 1..7, so 1..70
 * for levels 1..10), in place in 50D4:678C and in this order. The ids are rows of xeen.mon, the Warzone
 * (Clouds map 55h) being a Clouds map:
 * - above 43h (67, the Diamond Golem) less 3: level 10's Fire Dragon (68), Frost Dragon (69) and Devil
 *   (70) become the Lava Golem, Roc and Diamond Golem (65..67), so no arena opponent is above 67;
 * - 3Bh, the Water Dragon (59), becomes the Cloud Golem (60);
 * - 1Ch, the Water Golem (28), becomes the Jouster (29).
 * Why these two are left out is not established: no field of their records sets them apart, and the
 * only original rule that names either, 2A5D1's (the Water Dragon may leave water nowhere in Clouds),
 * is outdoor while map 55h is indoor (Phase 254 report).
 */
static uint8_t arena_opponent(uint8_t drawn)
{
    uint8_t id = drawn;
    if (id > XEEN_CLOUDS_DIAMOND_GOLEM) {
        id = (uint8_t)(id - 3u);
    }
    if (id == XEEN_CLOUDS_WATER_DRAGON) {
        id = XEEN_CLOUDS_CLOUD_GOLEM;
    }
    if (id == XEEN_CLOUDS_WATER_GOLEM) {
        id = XEEN_CLOUDS_JOUSTER;
    }
    return id;
}
/* The generator's pillar rule: 364FE..36532 move a record drawn onto x 5 or 10 with y 8 or 4 to y 5. */
enum { ARENA_PILLAR_Y = 5 };
/**
 * Generates the arena's opponents for LEVEL: five types rolled from the level's seven monsters, their sprite files,
 * then each of the map's monster records placed on the grid with one of the types. Returns 0, or the stop of a step
 * that failed. Every roll's AX argument is the value the original's register holds at that call, so the random draws
 * match it: the level's base, the record count, the x drawn, the y as stored, the HP, the record's offset.
 * Original: 3638B..3666A.
 */
static uint32_t arena_generate(xeen_game *g, unsigned level)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    xeen_sprite_archives a = sprite_archives(g, NULL);
    unsigned i, count = ds[XEEN_DS_MAP_MONSTER_COUNT];
    char name[16];
    memset(g->arena_ids, 0xff, sizeof g->arena_ids); /* no type chosen */
    if (xeen_read16(m, XEEN_DS_ANIMATION_SPRITES)) { /* free the map's animation sprites */
        (void)xeen_sprite_free_from(&g->sprites, xeen_read16(m, XEEN_DS_ANIMATION_SPRITES));
        memset(ds + XEEN_DS_ANIMATION_SPRITES, 0, 16); /* the handle words 8D56..8D65 */
    }
    for (i = 0; i < ARENA_OPPONENT_TYPES; ++i) {
        /* the level's seven monster ids follow LEVEL_BASE */
        uint16_t level_base = (uint16_t)((level - 1u) * ARENA_TYPES_PER_LEVEL), roll = 0;
        uint8_t id;
        size_t row;
        uint16_t h = 0;
        /* 36393: 504DA(1, 7), one of the level's ARENA_TYPES_PER_LEVEL ids (the stop guard keeps its literal) */
        if (!arena_draw(g, level_base, 1, 7, &roll)) {
            return ARENA_STOP_ROLL_OPPONENT;
        }
        id = arena_opponent((uint8_t)(level_base + roll)); /* the sum's low byte */
        g->arena_ids[i] = id;
        row = (size_t)id * XEEN_MONSTER_RECORD_BYTES;
        if (row + XEEN_MONSTER_RECORD_BYTES > g->monster_database_size) {
            return ARENA_STOP_OPPONENT_RECORD;
        }
        (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_ARENA_MONSTER_FILE),
                       (unsigned)g->monster_database[row + XEEN_MONSTER_SPRITE]);
        ds_put_text(g, XEEN_DS_FILE_NAME, name);
        if (xeen_sprite_load(&g->sprites, &a, ds[XEEN_DS_WORLD], name, 0, &h) != XR_OK) {
            return ARENA_STOP_OPPONENT_SPRITES;
        }
        xeen_write16(m, (uint16_t)(XEEN_DS_MONSTER_SPRITES + 2u * i), h);
        (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_ARENA_ATTACK_FILE),
                       (unsigned)g->monster_database[row + XEEN_MONSTER_SPRITE]);
        ds_put_text(g, XEEN_DS_FILE_NAME, name);
        if (xeen_sprite_load(&g->sprites, &a, ds[XEEN_DS_WORLD], name, 0, &h) != XR_OK) {
            return ARENA_STOP_OPPONENT_ATTACK;
        }
        xeen_write16(m, (uint16_t)(XEEN_DS_MONSTER_ATTACK_SPRITES + 2u * i), h);
    }
    for (i = 0; i < count; ++i) {
        uint16_t d = (uint16_t)XEEN_MAP_MONSTER(i), x = 0, y = 0, slot = 0, frame = 0, timer = 0,
                 hp;
        size_t row;
        /* the grid: x 3..11 (364CC), y 2..10 (364E2) */
        if (!arena_draw(g, (uint16_t)count, 3, 11, &x) || !arena_draw(g, x, 2, 10, &y)) {
            return ARENA_STOP_ROLL_POSITION;
        }
        ds[(uint16_t)(d + XEEN_MAP_MONSTER_X)] = (uint8_t)x;
        ds[(uint16_t)(d + XEEN_MAP_MONSTER_Y)] = (uint8_t)y;
        if ((x == 5u || x == 10u) && (y == 8u || y == 4u)) { /* the four cells 364FE tests */
            ds[(uint16_t)(d + XEEN_MAP_MONSTER_Y)] = ARENA_PILLAR_Y;
        }
        if (!arena_draw(g, y, 0, 4, &slot)) {
            return ARENA_STOP_ROLL_OPPONENT_SLOT;
        }
        ds[(uint16_t)(d + XEEN_MAP_MONSTER_SPRITE_SLOT)] = (uint8_t)slot;
        ds[(uint16_t)(d + XEEN_MAP_MONSTER_ID)] = g->arena_ids[slot];
        row = (size_t)ds[(uint16_t)(d + XEEN_MAP_MONSTER_ID)] * XEEN_MONSTER_RECORD_BYTES;
        if (row + XEEN_MONSTER_RECORD_BYTES > g->monster_database_size) {
            return ARENA_STOP_MONSTER_RECORD;
        }
        hp = xeen_get_le16(g->monster_database + row + XEEN_MONSTER_HP); /* 3659C */
        xeen_write16(m, (uint16_t)(d + XEEN_MAP_MONSTER_HP), hp);
        if (!arena_draw(g, hp, 0, 7, &frame)) {
            return ARENA_STOP_ROLL_FRAME;
        }
        ds[(uint16_t)(d + XEEN_MAP_MONSTER_FRAME)] = (uint8_t)frame;
        xeen_write16(m, (uint16_t)(d + XEEN_MAP_MONSTER_EFFECT), g->monster_database[row + XEEN_MONSTER_EFFECT]);
        xeen_write16(m, (uint16_t)(d + XEEN_MAP_MONSTER_EFFECT_CYCLE), g->monster_database[row + XEEN_MONSTER_EFFECT]);
        if (g->monster_database[row + XEEN_MONSTER_EFFECT]) {
            if (!arena_draw(g, (uint16_t)(row & 15u), 0, 7, &timer)) {
                return ARENA_STOP_ROLL_EFFECT_TIMER;
            }
            xeen_write16(m, (uint16_t)(d + XEEN_MAP_MONSTER_EFFECT_STEP), timer);
        }
        ds[(uint16_t)(d + XEEN_MAP_MONSTER_ACTIVE)] = 1;
    }
    return 0;
}
/** Continues the arena after its question was answered with AX. */
static xeen_status arena_answer(xeen_game *g, uint16_t answer)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint32_t stop;
    switch (g->arena_stage) {
        case ARENA_NOT_WON:
            return arena_end(g, 0);
        case ARENA_WIN:
        case ARENA_AGAIN:
            if (!arena_travel(g, ARENA_MAP_VERTIGO)) {
                return arena_stop(g, ARENA_STOP_TRAVEL_VERTIGO);
            }
            return arena_end(g, 1);
        case ARENA_ASK:
            if (!answer) {
                if (!arena_copy(g, XEEN_DS_TEXT_BUFFER, ARENA_TEXT_REFUSAL)) {
                    return arena_stop(g, ARENA_STOP_REFUSAL_TEXT);
                }
                return arena_say(g, ARENA_NO, ARENA_MESSAGE, XEEN_DS_TEXT_BUFFER);
            }
            return arena_say(g, ARENA_LEVEL, ARENA_ASK_NUMBER, XEEN_TEXT_ARENA_LEVEL_PROMPT);
        case ARENA_NO: /* every window closes, then the party leaves to map 6 */
            if ((stop = windows_close_all(g)) != 0) {
                return arena_stop(g, stop);
            }
            if (!arena_travel(g, ARENA_MAP_REFUSAL)) {
                return arena_stop(g, ARENA_STOP_TRAVEL_REFUSAL);
            }
            m = &g->movement;
            ds = m->ds;
            ds[XEEN_DS_PARTY_X] = ARENA_REFUSAL_X;
            ds[XEEN_DS_PARTY_Y] = ARENA_REFUSAL_Y;
            ds[XEEN_DS_PARTY_DIRECTION] = XEEN_WEST;
            xeen_navigation_set(m, (xeen_navigation){XEEN_WEST, ARENA_REFUSAL_X, ARENA_REFUSAL_Y, ds[XEEN_DS_PARTY_MAP]});
            return arena_leave(g, 1);
        case ARENA_LEVEL:
        case ARENA_COUNT:
            return arena_number(g);
        case ARENA_BEGIN:
            ds[XEEN_DS_MAP_MONSTER_COUNT] = (uint8_t)g->arena_count;
            ds[XEEN_DS_PARTY_DIRECTION] = XEEN_EAST;
            if ((stop = arena_generate(g, g->arena_level)) != 0) {
                return arena_stop(g, stop);
            }
            return arena_end(g, 0);
    }
    return arena_stop(g, ARENA_STOP_NO_QUESTION);
}
/* The amount prompt's VALUE for the open number question: 0 ends the visit, a value above the limit asks again.
 * Original: the answer of 28ABA (owner 3). */
xeen_status arena_amount(xeen_game *g, uint32_t value)
{
    uint16_t n = (uint16_t)value;
    if (!n) {
        return arena_end(g, 0);
    }
    if (g->arena_stage == ARENA_LEVEL) {
        if ((int16_t)n > ARENA_MAX_LEVEL) {
            return arena_say(g, ARENA_LEVEL, ARENA_ASK_NUMBER, XEEN_TEXT_ARENA_LEVEL_PROMPT);
        }
        g->arena_level = n;
        return arena_say(g, ARENA_COUNT, ARENA_ASK_NUMBER, XEEN_TEXT_ARENA_COUNT_PROMPT);
    }
    if ((int16_t)n > ARENA_MAX_MONSTERS) {
        return arena_say(g, ARENA_COUNT, ARENA_ASK_NUMBER, XEEN_TEXT_ARENA_COUNT_PROMPT);
    }
    g->arena_count = n;
    if (!arena_copy(g, XEEN_DS_TEXT_BUFFER, ARENA_TEXT_BEGIN)) {
        return arena_stop(g, ARENA_STOP_BEGIN_TEXT);
    }
    return arena_say(g, ARENA_BEGIN, ARENA_MESSAGE, XEEN_DS_TEXT_BUFFER);
}
enum { ARENA_WINS_TENS = 3, ARENA_WINS_UNITS = 4 }; /* the five-digit count's last two digits */
/* The arena's handler, after the location setup: the visit's first question or message (see the file's head).
 * Original: 360C4, called from the location dispatcher 277E5. */
xeen_status arena_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned i;
    xeen_presentation_trace_extra("cutmark arena"); /* Evidence: the handler's entry */
    if (ds[XEEN_DS_MAP_MONSTER_COUNT]) {
        for (i = 0; i < ds[XEEN_DS_MAP_MONSTER_COUNT]; ++i) {
            uint16_t d = (uint16_t)XEEN_MAP_MONSTER(i);
            if (ds[(uint16_t)(d + XEEN_MAP_MONSTER_X)] != XEEN_MAP_MONSTER_REMOVED &&
                ds[(uint16_t)(d + XEEN_MAP_MONSTER_Y)] != XEEN_MAP_MONSTER_REMOVED) {
                if (!arena_copy(g, XEEN_DS_TEXT_BUFFER, ARENA_TEXT_STANDING)) {
                    return arena_stop(g, ARENA_STOP_STANDING_TEXT);
                }
                return arena_say(g, ARENA_NOT_WON, ARENA_MESSAGE, XEEN_DS_TEXT_BUFFER);
            }
        }
        {
            char count[8], text[1000];
            unsigned suffix;
            int k;
            if (!arena_copy(g, XEEN_DS_TEXT_BUFFER_2, ARENA_TEXT_VICTORY)) {
                return arena_stop(g, ARENA_STOP_VICTORY_TEXT);
            }
            for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
                ++ds[(uint16_t)WARZONE_WINS(i)];
            }
            (void)snprintf(count, sizeof count, ds_text(g, XEEN_TEXT_ARENA_WINS_NUMBER), (unsigned)ds[WARZONE_WINS(0)]);
            ds_put_text(g, XEEN_DS_TEXT_BUFFER_0, count);
            /* the ordinal by the count's last digit ("th" for 10..19), read back from the five digits: the tens at
             * offset 3, the units at 4 */
            suffix = ds[XEEN_DS_TEXT_BUFFER_0 + ARENA_WINS_TENS] == '1'
                             ? 0u
                             : (unsigned)(int)(int8_t)(ds[XEEN_DS_TEXT_BUFFER_0 + ARENA_WINS_UNITS] - (unsigned)'0');
            k = snprintf(text, sizeof text, ds_text(g, XEEN_DS_TEXT_BUFFER_2), (int)ds[WARZONE_WINS(0)],
                         far_text(g, XEEN_DS_ORDINAL_SUFFIXES, suffix));
            if (k < 0 || (size_t)k >= sizeof text) {
                return arena_stop(g, ARENA_STOP_VICTORY_FORMAT);
            }
            memcpy(ds + XEEN_DS_TEXT_BUFFER, text, (size_t)k + 1u);
            return arena_say(g, ARENA_WIN, ARENA_MESSAGE, XEEN_DS_TEXT_BUFFER);
        }
    }
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        if (ds[(uint16_t)WARZONE_WINS(i)] >= ARENA_WINS_TURNED_AWAY) {
            return arena_say(g, ARENA_AGAIN, ARENA_MESSAGE, XEEN_TEXT_ARENA_AGAIN);
        }
    }
    if (!arena_copy(g, XEEN_DS_TEXT_BUFFER, ARENA_TEXT_CHALLENGE)) {
        return arena_stop(g, ARENA_STOP_CHALLENGE_TEXT);
    }
    return arena_say(g, ARENA_ASK, ARENA_ASK_YES_NO, XEEN_DS_TEXT_BUFFER);
}
