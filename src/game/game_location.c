/*
 * The locations (Phase 246, from game_inn.c): 277E5, the common frame of the town services (tavern,
 * training grounds, bank, guild, temple, blacksmith) and the special encounters: the keeper's portrait and
 * per-kind animation (26978), the voices and music, the buttons and windows, each kind's setup, the
 * pyramid, the sign-in, the redraw, the keys and the leave. The add-on glue of training_preview and
 * spell_list_sort is included here. The inn (43E0B) is game_inn.c's.
 */
#include "game_location.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when a location cannot be entered, set up or left; each value is the original address of
 * the step (tools/xeen_replay/stops.json). Most guard a resource file that did not load.
 */
enum {
    PYRAMID_STOP_MOONS_NOT_ALIGNED = 0x33ebe,  /* Darkside alone: "The moons are not aligned..." is not reconstructed */
    PYRAMID_STOP_TRAVEL = 0x33fa0,             /* the other world's map did not load */
    LOCATION_STOP_UNHANDLED_KIND = 0x277e5,    /* a location kind the native does not set up */
    LOCATION_STOP_NO_HOST = 0x277e5,           /* no presentation or no save files to draw and keep the location */
    LOCATION_STOP_SPELL_NAMES = 0x27875,       /* spells.xen */
    LOCATION_STOP_MATERIAL_NAMES = 0x278e5,    /* MAE.xen */
    LOCATION_STOP_TEXTS_FILE = 0x2b658,        /* the service's texts: tavern.bin or spldesc.bin */
    TAVERN_STOP_SPRITES = 0x27b0c,             /* tavern.icn */
    TRAINING_STOP_SPRITES = 0x27991,           /* train.icn */
    BANK_STOP_SPRITES = 0x27c3b,               /* bank.icn */
    BANK_STOP_SECOND_SPRITES = 0x27c49,        /* bank2.icn */
    GUILD_STOP_SPRITES = 0x27b9a,              /* esc.icn */
    TEMPLE_STOP_SPRITES = 0x27a95,             /* esc.icn */
    BLACKSMITH_STOP_SPRITES = 0x27a18,         /* esc.icn */
    LOCATION_STOP_ANIMATION_SPRITES = 0x27dc2, /* one of the location's animation files */
    LOCATION_STOP_WINDOW_TEXT = 0x276e9,       /* the location's window text (27198) could not be formed */
    LOCATION_STOP_RETURN_MAP = 0x280c8         /* the party's map did not reload on leaving */
};
/* ------------------------------ the locations
 * Event operation 17 suspends the interpreter with a location kind (its record's byte 6); the location runs here and
 * resumes the script with the location's result. The tavern's Sign In leaves for the inn through a restart of the
 * exploration loop (F64A = 2): the game is saved, the dispatcher ends, the loop leaves through its exit routine and
 * the restart opens the inn's roster screen when the party has no map (map 0).
 * Original: the dispatcher 277E5 (operation 17: 3245F); Sign In 25FF0; the save 280E2 (29502(1)); 17C03, 17F28,
 * the book 15647, 16159, 43C37 and the roster screen 43E0B. */
#include "../addon/training_preview/trainer.inc" /* add-on training_preview: training_preview_* */
#include "../addon/spell_list_sort/glue.inc"     /* add-on spell_list_sort: spell_list_sort_* */
/* The original calls of the voice driver (187E9) and the CD status (47F3D) in this unit, each by the call's
 * return address: the audio model records which call asked (traces name them). Not stops, not waits. */
enum {
    ENTER_VOICE_CANCEL_CALL = 0x27b76,/* a location's entry silences any voice */
    GREETING_VOICE_CALL = 0x27f0c,   /* the keeper's greeting */
    COME_VOICE_CANCEL_CALL = 0x28023,/* leaving the Darkside blacksmith: the voice stops ... */
    COME_VOICE_CALL = 0x28043,       /* ... and come1.voc plays */
    GOODBYE_VOICE_CANCEL_CALL = 0x27fc5,/* leaving another location: the voice stops ... */
    GOODBYE_VOICE_CALL = 0x27ff6     /* ... and the goodbye plays */
};
/*
 * One step of a location's animation (every kind): the keeper's portrait and the kind's own frames; with
 * REDRAW_BORDER (the callers pass it while the view window is closed) the border's animation counters cleared (the
 * clairvoyance eyes, the Danger Sense, Spot Secret Doors and Levitate frames) and the border redrawn; the next frame
 * and its sound cues; then the speech window (window 40, when open) or the view (window 3) shown.
 * Original: 26978(arg) (the border 26D3C, 26306; the update 26E48: 50BF6).
 */
void location_animate(xeen_game *g, int redraw_border)
{
    uint8_t *ds = g->movement.ds;
    unsigned kind = g->location_kind;
    int dark = ds[XEEN_DS_WORLD] != 0;
    location_keeper_portrait(g, kind, dark);
    location_keeper_animation(g, kind, dark);
    if (redraw_border) {
        xeen_border_frames_clear(ds);
        location_border(g);
    }
    location_frame_cues(g, kind, dark, location_next_frame(g, dark));
    training_preview_view(g); /* add-on training_preview: the open ladder */
    (void)xeen_presentation_update_window(g->presentation, ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)] ? 40 : 3);
}
/* Shows the location's text (DS text buffer 2, built by location_text) framed in the right column, then its buttons;
 * the training_preview add-on's open panel replaces both. Original: 4E38C(0Ah, AC0F). */
void location_window(xeen_game *g)
{
    if (training_preview_panel(g)) {
        return; /* add-on training_preview: the panel */
    }
    menu_window_text(g, XEEN_WINDOW_RIGHT_COLUMN, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    location_buttons(g);
}
/** Stops a location at the unsupported BOUNDARY (a registered stop), first clearing its kind and the tavern's wait and
 * food states. */
xeen_status location_stop(xeen_game *g, uint32_t boundary)
{
    g->location_kind = 0;
    g->tavern_wait = TAVERN_WAIT_NONE;
    g->tavern_food_step = TAVERN_FOOD_NONE;
    return panel_stop(g, boundary);
}
/* Ends a location: its result AX (and MOVED, the party changed place) reaches operation 17, and the interpreter
 * resumes, which ends the script. Original: 3246F (3067 = 1), then 33690. */
xeen_status location_return(xeen_game *g, uint16_t ax, int moved)
{
    g->execution.location_ax = ax;
    g->dispatcher.execution.location_ax = ax;
    g->execution.location_moved = (uint8_t)moved;
    g->dispatcher.execution.location_moved = (uint8_t)moved;
    g->phase = XG_STOPPED;
    drive(g);
    return XEEN_SERVICE_REQUIRED;
}
/*
 * The pyramid (location kind 12, before any location setup) moves the party to the other world. With only Darkside
 * installed the original says "The moons are not aligned..." in window 12, which is not reconstructed (a stop).
 * From Darkside: map 34h leads to Clouds map 31h at (7, 14) facing south, any other map to Clouds map 17h at
 * (8, 10). From Clouds: map 31h leads to Darkside map 34h at (2, 2), any other map to Darkside map 1Dh at (25, 21).
 * The party faces north unless stated; the location returns 0.
 * Original: 277E5 kind 12 (277EE) -> 33EB1 (the next map's side 96E2 = !94, the map load 19E82 across the worlds).
 */
static xeen_status pyramid(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint8_t map, x, y, dir = 0;
    if (!ds[XEEN_DS_CLOUDS_INSTALLED]) {
        return game_stop(g, PYRAMID_STOP_MOONS_NOT_ALIGNED);
    }
    if (ds[XEEN_DS_WORLD]) {
        if (ds[XEEN_DS_PARTY_MAP] == 52u) {
            map = 0x31;
            x = 7;
            y = 14;
            dir = 2;
        } else {
            map = 0x17;
            x = 8;
            y = 10;
        }
    } else if (ds[XEEN_DS_PARTY_MAP] == 49u) {
        map = 0x34;
        x = 2;
        y = 2;
    } else {
        map = 0x1d;
        x = 25;
        y = 21;
    }
    ds[XEEN_DS_MAP_SIDE] = (uint8_t)!ds[XEEN_DS_WORLD];
    if (map_reload(g, map) != XR_OK) {
        return game_stop(g, PYRAMID_STOP_TRAVEL);
    }
    m = &g->movement;
    ds = m->ds;
    ds[XEEN_DS_PARTY_X] = x;
    ds[XEEN_DS_PARTY_Y] = y;
    ds[XEEN_DS_PARTY_DIRECTION] = dir;
    xeen_navigation_set(m, (xeen_navigation){dir, x, y, map});
    return location_return(g, 0, 1);
}
/* Whether location kind KIND has a handler: kind 7 has none and no kind above 13 exists. Original: the handler
 * table CS:2D62 of 277E5 (kind 7's entry 27D6E). */
static int location_kind_handled(unsigned kind)
{
    return kind <= LOCATION_GATEKEEPER_2 && kind != 7u;
}
/*
 * The setup every handled kind shares: the map's sprites are freed (kept for the arena), the kind's animation starts
 * (its frame count from DS:3026, frame 0, at (8, 8)), the location's time charge is 0, the buttons are pushed, the
 * map's state is saved, the spell and material names (spells.xen, MAE.xen) are made resident from the Darkside
 * archive, and the greeting hello1.voc is preloaded. XEEN_OK goes on.
 * Original: 277EE..27956 (4AA3E(98D0), 190ED, the map save 30F34, the voice into F42C).
 */
static xeen_status location_enter(xeen_game *g, unsigned kind)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, *r;
    size_t n = 0;
    uint8_t world;
    uint32_t stop;
    g->location_kind = (uint8_t)kind;
    g->panel_stop = 0;
    if (kind != LOCATION_ARENA) {
        (void)xeen_sprite_free_from(&g->sprites, xeen_read16(m, XEEN_DS_MAP_SPRITE_BASE)); /* 4AA3E(98D0) 277FD */
    }
    /* 2780C..2781D: the signed frame count at 3026[world x 0Bh + kind] (11 kinds per world) */
    xeen_write16(m, XEEN_DS_ANIMATION_FRAME_COUNT,
                 (uint16_t)(int16_t)(int8_t)ds[XEEN_DS_LOCATION_ANIMATION_FRAMES + ds[XEEN_DS_WORLD] * 11u + kind]);
    xeen_write16(m, XEEN_DS_ANIMATION_KIND, (uint16_t)kind);
    xeen_write16(m, XEEN_DS_ANIMATION_FRAME, 0);
    xeen_write16(m, XEEN_DS_LOCATION_TIME_CHARGE, 0);
    g->location_blessed = 0;
    g->location_message = LOCATION_MESSAGE_NONE; /* 2782C: 57AD:0000 = 0 */
    xeen_write16(m, XEEN_DS_ANIMATION_Y, 8);               /* 27835..2783B: 8D2E = 8D30 = 8 */
    xeen_write16(m, XEEN_DS_ANIMATION_X, 8);
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    g->location_mode = ds[XEEN_DS_UI_MODE];
    if ((stop = location_map_save(g)) != 0) {
        return location_stop(g, stop);
    }
    xeen_write16(m, XEEN_DS_MAP_FIELD(ID), 0); /* 27852: C0CC = 0 */
    xeen_write16(m, 0xf47c, 0);                /* 27855: F47C = 0 (then 27858 calls 33CD8, DOS memory: platform) */
    world = ds[XEEN_DS_WORLD];
    xeen_select_archive(m, 1);
    if (!(r = menu_load(g, ds_text(g, XEEN_TEXT_SPELL_NAMES_FILE), &n))) {
        return location_stop(g, LOCATION_STOP_SPELL_NAMES);
    }
    xeen_write16(m, XEEN_DS_SPELLS_FILE_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_SPELLS_FILE, 0);
    (void)xeen_resident_table(m, XEEN_DS_SPELL_NAMES, 0, r, n, XEEN_SPELL_NAMES_LIMIT);
    free(r);
    if (!(r = menu_load(g, ds_text(g, XEEN_TEXT_MATERIAL_NAMES_FILE), &n))) {
        return location_stop(g, LOCATION_STOP_MATERIAL_NAMES);
    }
    xeen_write16(m, XEEN_DS_MAE_FILE_SEGMENT, 0);
    xeen_write16(m, XEEN_DS_MAE_FILE, 0);
    (void)xeen_resident_table(m, XEEN_DS_MATERIAL_NAMES, 0, r, n, XEEN_MATERIAL_NAMES_LIMIT);
    free(r);
    location_voc_load(g, ds_text(g, XEEN_TEXT_LOCATION_HELLO_VOICE)); /* 27956 hello1.voc into F42C (no start here) */
    xeen_select_archive(m, world);
    return XEEN_OK;
}
/* A location's button set: the six button-table far pointers (offsets in segment 4853, in the
 * order x, y, width, height, key, enabled) and the set's sprite handle. */
static void location_install_buttons(xeen_movement *m, const uint16_t tables[6], uint16_t sprites)
{
    button_tables(m, tables[0], tables[1], tables[2], tables[3], tables[4], tables[5]);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, sprites);
}
/*
 * The tavern's setup: tavern.icn becomes the current sprites (pushed on the handle stack; the buttons use it), the UI
 * mode the location's, the text block pointer DOS memory 50D4:1C7E, the tavern's texts (tavern.bin) kept as the
 * location's text, the tavern's wait and food states reset, its buttons installed, the drink count cleared, any
 * voice silenced and the greeting loaded (hello1.voc on Darkside, hello.voc on the Clouds; started with the first
 * screen). Original: 27B08 (190A3, 2B632, 187E9(0, 0)).
 */
static xeen_status tavern_setup(xeen_game *g)
{
    static const uint16_t buttons[6] = {0x1a4c, 0x1a5a, 0x1a60, 0x1a66, 0x1a6c, 0x1a72};
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, *r;
    size_t n = 0;
    uint16_t h = 0;
    if (!handle_push(g, ds_text(g, XEEN_TEXT_TAVERN_SPRITES), &h)) {
        return location_stop(g, TAVERN_STOP_SPRITES);
    }
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_LOCATION;
    xeen_write16(m, XEEN_DS_TEXT_BLOCK_SEGMENT, 0x50d4);
    xeen_write16(m, XEEN_DS_TEXT_BLOCK, 0x1c7e); /* 2B632: the strings in DOS memory */
    if (!(r = menu_load(g, ds_text(g, XEEN_TEXT_TAVERN_TEXTS_FILE), &n))) {
        return location_stop(g, LOCATION_STOP_TEXTS_FILE);
    }
    free(g->guild_desc);
    g->guild_desc = r;
    g->guild_desc_size = n;
    g->tavern_wait = TAVERN_WAIT_NONE;
    g->tavern_food_step = TAVERN_FOOD_NONE;
    location_install_buttons(m, buttons, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    xeen_write16(m, XEEN_DS_TAVERN_DRANK, 0);
    voice_cancel(g, ENTER_VOICE_CANCEL_CALL);                                    /* 27B71 187E9(0, 0) */
    location_voc_load(g, ds_text(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_LOCATION_HELLO_VOICE : XEEN_TEXT_LOCATION_HELLO_VOICE_CLOUDS)); /* Started at 27F07 */
    return XEEN_OK;
}
/*
 * The training grounds' setup: no member trained yet (the six flags cleared), train.icn the current sprites and the
 * buttons' set, no trainee chosen, any voice silenced, the greeting loaded (youtrn1.voc on Darkside, training.voc on
 * the Clouds). Original: 2797D (13FE7, 190A3, 187E9(0, 0)).
 */
static xeen_status training_setup(xeen_game *g)
{
    static const uint16_t buttons[6] = {0x1a3c, 0x1a42, 0x1a44, 0x1a46, 0x1a48, 0x1a4a};
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    memset(ds + XEEN_DS_MEMBER_TRAINED, 0, 6);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_TRAINING_SPRITES), &h)) {
        return location_stop(g, TRAINING_STOP_SPRITES);
    }
    location_install_buttons(m, buttons, h);
    xeen_write16(m, XEEN_DS_TRAINEE, 0);
    training_preview_enter(g); /* add-on training_preview: closed */
    voice_cancel(g, 0);
    location_voc_load(g, ds_text(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_TRAINING_VOICE_DARKSIDE : XEEN_TEXT_TRAINING_VOICE_CLOUDS)); /* Started at 27F07 */
    return XEEN_OK;
}
/*
 * The bank's setup: bank.icn the current sprites and the buttons' set; bank2.icn loaded beside it (not pushed) into DS
 * 98D2, the button sprites of the bank's deposit and withdrawal dialog (game_bank.c); any voice silenced; the shared
 * animation word F47C set to 1; the greeting loaded (bank1.voc on Darkside, banker.voc on the Clouds); the bank's
 * dialog closed. Original: 27C37 (190A3, 18931, 187E9(0, 0)).
 */
static xeen_status bank_setup(xeen_game *g)
{
    static const uint16_t buttons[6] = {0x1a1e, 0x1a26, 0x1a29, 0x1a2c, 0x1a2f, 0x1a32};
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0, h2 = 0;
    if (!handle_push(g, ds_text(g, XEEN_TEXT_BANK_SPRITES), &h)) {
        return location_stop(g, BANK_STOP_SPRITES);
    }
    if (!sprite_load(g, ds_text(g, XEEN_TEXT_BANK_SPRITES_2), &h2)) {
        return location_stop(g, BANK_STOP_SECOND_SPRITES);
    }
    xeen_write16(m, 0x98d2, h2);
    location_install_buttons(m, buttons, h);
    voice_cancel(g, 0);
    xeen_write16(m, 0xf47c, 1);
    location_voc_load(g, ds_text(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_BANK_VOICE_DARKSIDE : XEEN_TEXT_BANK_VOICE_CLOUDS)); /* Started at 27F07 */
    g->bank_mode = 0;
    return XEEN_OK;
}
/*
 * The guild's setup: esc.icn the current sprites and the buttons' set, the UI mode the location's, the text block
 * pointer DOS memory 50D4:1C7E and the spell descriptions (spldesc.bin) kept as the location's text, the global
 * sprites' frame 7 drawn at (232, 74), any voice silenced, the greeting loaded (parrot1.voc on Darkside,
 * guild10.voc on the Clouds), the guild's spell list closed. Original: 27B96 (190A3, 2B632; the offsets at
 * 50D4:165C, the size in 8582; 187E9(0, 0)).
 */
static xeen_status guild_setup(xeen_game *g)
{
    static const uint16_t buttons[6] = {0x1a78, 0x1a84, 0x1a89, 0x1a8e, 0x1a98, 0x1a93};
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    size_t dn = 0;
    if (!handle_push(g, ds_text(g, XEEN_TEXT_LOCATION_ESC_SPRITES), &h)) {
        return location_stop(g, GUILD_STOP_SPRITES);
    }
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_LOCATION;
    xeen_write16(m, XEEN_DS_TEXT_BLOCK_SEGMENT, 0x50d4);
    xeen_write16(m, XEEN_DS_TEXT_BLOCK, 0x1c7e);
    free(g->guild_desc);
    g->guild_desc_size = 0;
    if (!(g->guild_desc = menu_load(g, ds_text(g, XEEN_TEXT_SPELL_DESCRIPTIONS_FILE), &dn))) {
        return location_stop(g, LOCATION_STOP_TEXTS_FILE);
    }
    g->guild_desc_size = dn;
    exit_draw(g, xeen_read16(m, XEEN_DS_GLOBAL_SPRITES), 7, 232, 74, 0);
    location_install_buttons(m, buttons, h);
    voice_cancel(g, 0);
    location_voc_load(g, ds_text(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_GUILD_VOICE_DARKSIDE : XEEN_TEXT_GUILD_VOICE_CLOUDS)); /* Started at 27F07 */
    g->guild_active = 0;
    return XEEN_OK;
}
/*
 * The temple's setup: no donation yet this visit, esc.icn the current sprites and the buttons' set (the guild's
 * tables but its own key table), any voice silenced, the greeting loaded (help2.voc on Darkside, maywe2.voc on the
 * Clouds). Original: 27A8B (190A3, 187E9(0, 0)).
 */
static xeen_status temple_setup(xeen_game *g)
{
    static const uint16_t buttons[6] = {0x1a78, 0x1a84, 0x1a89, 0x1a8e, 0x1aa7, 0x1a93};
    xeen_movement *m = &g->movement;
    uint16_t h = 0;
    xeen_write16(m, XEEN_DS_TEMPLE_DONATIONS, 0);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_LOCATION_ESC_SPRITES), &h)) {
        return location_stop(g, TEMPLE_STOP_SPRITES);
    }
    location_install_buttons(m, buttons, h);
    voice_cancel(g, 0);
    location_voc_load(g, ds_text(g, m->ds[XEEN_DS_WORLD] ? XEEN_TEXT_TEMPLE_VOICE_DARKSIDE : XEEN_TEXT_TEMPLE_VOICE_CLOUDS)); /* Started at 27F07 */
    return XEEN_OK;
}
/*
 * The blacksmith's setup: esc.icn the current sprites and the buttons' set (the guild's tables but its own key table),
 * any voice silenced, the greeting loaded (see2.voc on Darkside, whaddayo.voc on the Clouds).
 * Original: 27A14 (190A3, 187E9(0, 0)).
 */
static xeen_status blacksmith_setup(xeen_game *g)
{
    static const uint16_t buttons[6] = {0x1a78, 0x1a84, 0x1a89, 0x1a8e, 0x1aa2, 0x1a93};
    xeen_movement *m = &g->movement;
    uint16_t h = 0;
    if (!handle_push(g, ds_text(g, XEEN_TEXT_LOCATION_ESC_SPRITES), &h)) {
        return location_stop(g, BLACKSMITH_STOP_SPRITES);
    }
    location_install_buttons(m, buttons, h);
    voice_cancel(g, 0);
    location_voc_load(g, ds_text(g, m->ds[XEEN_DS_WORLD] ? XEEN_TEXT_BLACKSMITH_VOICE_DARKSIDE : XEEN_TEXT_BLACKSMITH_VOICE_CLOUDS)); /* Started at 27F07 */
    return XEEN_OK;
}
/*
 * A town service's first screen: its music (by world and kind), its animation files (%s1.twn, %s2.twn, ...: as many
 * as the world's table gives the kind), the location's text, the first frame and the border, the right column and the
 * location's window, the portraits, the first animation step and the keeper's greeting; then the location's poll.
 * Original: 27D6E..27F0C: the songs DS:1880, the files DS:3018, 26978(1), 187E9 at 27F07.
 */
static xeen_status location_present(xeen_game *g, unsigned kind)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned i, count;
    char name[16];
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_LOCATION;
    location_music(g,
                   ds_text(g,
                           xeen_read16(m, (uint16_t)(XEEN_DS_LOCATION_SONGS + ds[XEEN_DS_WORLD] * 0x1cu + kind * 4u))),
                   0xdf);
    (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_LOCATION_TOWN_FILE),
                   far_text(g, XEEN_DS_LOCATION_STEMS, kind));
    ds_put_text(g, XEEN_DS_FILE_NAME, name);
    count = (unsigned)(int8_t)ds[XEEN_DS_LOCATION_ANIMATION_FILES + ds[XEEN_DS_WORLD] * 7u + kind];
    for (i = 0; i < count; ++i) {
        uint16_t h = 0;
        if (!sprite_load(g, ds_text(g, XEEN_DS_FILE_NAME), &h)) {
            return location_stop(g, LOCATION_STOP_ANIMATION_SPRITES);
        }
        xeen_write16(m, XEEN_DS_ANIMATION_SPRITE(i), h);
        ++ds[0xf5ec]; /* the file number in the name */
    }
    g->location_member = XEEN_PARTY_RECORDS;
    if (!location_text(g)) {
        return location_stop(g, LOCATION_STOP_WINDOW_TEXT);
    }
    xeen_border_frames_clear(ds);
    location_first_frame(g);
    location_border(g);
    panel_window(g, XEEN_WINDOW_RIGHT_COLUMN, 1);
    location_window(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
    portrait_highlight(g, 0); /* the first member highlighted, the party bar shown */
    location_animate(g, 1);
    if (kind == LOCATION_BANK) {
        xeen_write16(m, 0xf47c, 2);
    }
    exit_event(g, "voice");
    voc_start(g, GREETING_VOICE_CALL); /* the keeper's greeting, after the first animation frame */
    g->location_sampled = 0;
    return panel_set(g, LOCATION_POLL, LOCATION_POLL_BOUNDARY);
}
/*
 * Enters an operation 17 location, up to its first key poll: the pyramid before any setup; the shared entry; the
 * special locations' own routines (the arena, the Reaper, the gatekeepers, the sphinx, the guardians); else the town
 * service's own setup, then its first screen.
 * Original: 3245F -> 277E5(kind), up to the first 26E60 poll; the pyramid kind 12; the table CS:2D62: the arena
 * 27CC5 (360C4), the Reaper 27D4C (34FAA), the gatekeepers 27D53 (35992: a pass in F09A..F09E admits the party), the
 * sphinx 27D5A (33FCB), the guardians 27D61 (343F6).
 */
xeen_status location_begin(xeen_game *g)
{
    unsigned kind = g->resident[(uint16_t)(g->execution.state.offset + 6u)];
    xeen_status status;
    if (kind == LOCATION_PYRAMID) {
        return pyramid(g);
    }
    if (!location_kind_handled(kind)) {
        return game_stop(g, LOCATION_STOP_UNHANDLED_KIND);
    }
    if (!g->presentation || !g->save_files) {
        return game_stop(g, LOCATION_STOP_NO_HOST);
    }
    if ((status = location_enter(g, kind)) != XEEN_OK) {
        return status;
    }
    switch (kind) {
        case LOCATION_ARENA:
            return arena_begin(g); /* game_arena.c */
        case LOCATION_REAPER:
            return reaper_begin(g);
        case LOCATION_GATEKEEPER:
        case LOCATION_GATEKEEPER_2:
            return gate_begin(g); /* a pass admits the party */
        case LOCATION_SPHINX:
            return sphinx_begin(g);
        case LOCATION_GUARDIANS:
            return guard_begin(g);
        case LOCATION_TAVERN:
            status = tavern_setup(g);
            break;
        case LOCATION_TRAINING:
            status = training_setup(g);
            break;
        case LOCATION_BANK:
            status = bank_setup(g);
            break;
        case LOCATION_GUILD:
            status = guild_setup(g);
            break;
        case LOCATION_TEMPLE:
            status = temple_setup(g);
            break;
        default:
            status = blacksmith_setup(g);
            break;
    }
    if (status != XEEN_OK) {
        return status;
    }
    return location_present(g, kind);
}
/*
 * Leaving a location (Esc, or the loop's exit), after the right column is restored: it waits (a stage) until the
 * keeper's voice has played; then the portrait unhighlighted, the location's sprites popped from the handle stack, the
 * button set popped and the previous song resumed. At the Sign In (map 0) the game is saved and the result is 2; on
 * any other map the map loads again in place (map_reload) and the visit's time charge (the temple's cures, plus the
 * exit's own 1440 minutes) goes to the clock, the result 0. Then the UI mode the location found, the scene's
 * animation sprites cleared, the event-text and bottom windows closed, the exploration icons and the speech face
 * redrawn, the key cleared, and the interpreter resumes with the result.
 * Original: 28056 onward after Esc or F64A: 18765(0Ah), 33D36 (DOS memory), 42298, 190CF, 18FFF; map 0: 29502(1),
 * AX = 2; else 19E82, F628 += 1440, 30CF3(F628), AX = 0; 9A1C, 8D56..8D65, 28D80, F5FE = 0.
 */
static xeen_status location_end_tail(xeen_game *);
/** Leaves a location: restores the right column and runs the leaving tail. */
static xeen_status location_end(xeen_game *g)
{
    panel_window(g, XEEN_WINDOW_RIGHT_COLUMN, 0);
    return location_end_tail(g);
}
/* The leaving tail (contract above location_end_tail's declaration). */
static xeen_status location_end_tail(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char music[16];
    uint32_t stop;
    uint16_t ax;
    /* 28086 33D36 loops on 187E9(1, 0) (33D4F) until the keeper's
     * voice has played (the voice model; with none at once). */
    if (voice_busy(g, VOICE_END_WAIT_BOUNDARY)) {
        return panel_set(g, LOCATION_END_WAIT, LOCATION_END_WAIT_BOUNDARY);
    }
    portrait_unhighlight(g);
    handle_pop(g);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    memcpy(music, ds + XEEN_DS_PREVIOUS_SONG_NAME, sizeof music);
    music[sizeof music - 1u] = 0;
    location_music(g, music, 0xdf);
    if (ds[XEEN_DS_PARTY_MAP]) {
        xeen_gameplay_services services;
        if (map_reload(g, ds[XEEN_DS_PARTY_MAP]) != XR_OK) {
            return location_stop(g, LOCATION_STOP_RETURN_MAP);
        }
        m = &g->movement;
        ds = m->ds;
        services = session_services(g);
        xeen_write16(m, XEEN_DS_LOCATION_TIME_CHARGE,
                     (uint16_t)(xeen_read16(m, XEEN_DS_LOCATION_TIME_CHARGE) + 0x5a0u));
        if ((stop = xeen_gameplay_clock_add(m, xeen_read16(m, XEEN_DS_LOCATION_TIME_CHARGE),
                                            &services)) != 0) {
            return location_stop(g, stop);
        }
        ax = 0;
    } else {
        if ((stop = save_game(g)) != 0) {
            return location_stop(g, stop); /* 29502(1): 2956A..29686 */
        }
        ++g->saves; /* INT 21h 0Dh: platform */
        ax = 2;
    }
    ds[XEEN_DS_UI_MODE] = g->location_mode;
    memset(ds + XEEN_DS_ANIMATION_SPRITES, 0, 0x10);
    xeen_window_close(m, XEEN_WINDOW_EVENT_TEXT);
    xeen_window_close(m, XEEN_WINDOW_BOTTOM);
    /* 28D96: 4ADB1(0, DS:2A5C), the exploration icons drawn over the restored panel (presentation).
     * With F384 and 9894 set 28DAF also redraws window 28h (
     * speech_face_tail; 8D56 is clear here, so the name and frame only). */
    explore_icons(g);
    speech_face_tail(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_ICONS);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->location_kind = 0;
    g->panel_stage = 0;
    g->execution.location_ax = ax;
    g->dispatcher.execution.location_ax = ax;
    g->phase = XG_STOPPED;
    drive(g);
    return XEEN_SERVICE_REQUIRED;
}
/* The farewell's wait (a stage, at the site location_leave chose) until the farewell voice has played (at once
 * without a voice), then the location ends. Original: the loops on 187E9(1, 0) at 28001 (the tavern) and 28046 (the
 * Darkside blacksmith). */
static xeen_status location_leave_wait(xeen_game *g)
{
    if (voice_busy(g, g->location_wait_site)) {
        return panel_set(g, LOCATION_LEAVE_WAIT, g->location_wait_site);
    }
    return location_end(g);
}
/**
 * Leaves the current location the way its kind does: the temple, the training grounds, the bank, the guild and the
 * Clouds blacksmith end at once; the Darkside blacksmith says come1.voc, and the tavern says goodbye (gdluck1.voc on
 * Darkside, goodbye.voc on the Clouds) and records the party's map as the map record's id; each of those two waits
 * for its voice first. Original: the table CS:2D54 (28056), the tavern 27FA9 (27FBC), the blacksmith 28013.
 */
static xeen_status location_leave(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    if (g->location_kind == LOCATION_TEMPLE || g->location_kind == LOCATION_TRAINING ||
        g->location_kind == LOCATION_BANK || g->location_kind == LOCATION_GUILD) {
        return location_end(g); /* CS:2D54 -> 28056 */
    }
    if (g->location_kind == LOCATION_BLACKSMITH) {
        if (!m->ds[XEEN_DS_WORLD]) {
            return location_end(g);
        }
        voice_cancel(g, COME_VOICE_CANCEL_CALL);
        location_voc_load(g, ds_text(g, XEEN_TEXT_LOCATION_COME_VOICE));
        voc_start(g, COME_VOICE_CALL); /* come1.voc */
        g->location_wait_site = LOCATION_COME_WAIT_BOUNDARY;
        return location_leave_wait(g);
    }
    voice_cancel(g, GOODBYE_VOICE_CANCEL_CALL);
    location_voc_load(g, ds_text(g, m->ds[XEEN_DS_WORLD] ? XEEN_TEXT_GOODBYE_VOICE_DARKSIDE : XEEN_TEXT_GOODBYE_VOICE_CLOUDS));
    voc_start(g, GOODBYE_VOICE_CALL);
    xeen_write16(m, XEEN_DS_MAP_FIELD(ID), m->ds[XEEN_DS_PARTY_MAP]);
    g->location_wait_site = LOCATION_LEAVE_WAIT_BOUNDARY;
    return location_leave_wait(g);
}
/*
 * The tavern's Sign In: the party is placed at the town's exit cell (the table by world, location kind and town: on
 * Darkside the town is (map - 29) / 2, on the Clouds map - 28), facing west on the Clouds and on Darkside map 29
 * (Clouds map 49 at (11, 12)), east on map 31, else south; the town becomes the inn and every member's inn and side;
 * a day (1440 minutes) passes; the party leaves the maps (map 0) and the exploration loop exits with 2, the save flow.
 * Original: 25FF0 (the exits 2E1C, 30CF3(1440), ED99 = 0, F64A = 2).
 */
static xeen_status location_sign_in(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int town;
    unsigned i, at;
    uint32_t stop;
    xeen_gameplay_services services = session_services(g);
    if (ds[XEEN_DS_WORLD]) {
        town = ((int)ds[XEEN_DS_PARTY_MAP] - 0x1d) / 2;
    } else {
        town = (int)ds[XEEN_DS_PARTY_MAP] - 0x1c;
    }
    at = (unsigned)XEEN_LOCATION_EXIT((int)ds[XEEN_DS_WORLD], (int)xeen_read16(m, XEEN_DS_ANIMATION_KIND), town);
    ds[XEEN_DS_PARTY_X] = ds[(uint16_t)(XEEN_DS_LOCATION_EXITS + at)];
    ds[XEEN_DS_PARTY_Y] = ds[(uint16_t)(XEEN_DS_LOCATION_EXITS + 1u + at)];
    if (!ds[XEEN_DS_WORLD] || ds[XEEN_DS_PARTY_MAP] == 29) {
        if (ds[XEEN_DS_PARTY_MAP] == 49) {
            ds[XEEN_DS_PARTY_X] = 0xb;
            ds[XEEN_DS_PARTY_Y] = 0xc;
        }
        ds[XEEN_DS_PARTY_DIRECTION] = XEEN_WEST;
    } else if (ds[XEEN_DS_PARTY_MAP] == 31) {
        ds[XEEN_DS_PARTY_DIRECTION] = XEEN_EAST;
    } else {
        ds[XEEN_DS_PARTY_DIRECTION] = XEEN_SOUTH;
    }
    ds[XEEN_DS_INN_MAP] = ds[XEEN_DS_PARTY_MAP];
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        xeen_write16(m, (uint16_t)(XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * i + XEEN_CHARACTER_INN),
                     ds[XEEN_DS_PARTY_MAP]);
        ds[XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * i + XEEN_CHARACTER_INN_SIDE] = ds[XEEN_DS_MAP_SIDE];
    }
    if ((stop = xeen_gameplay_clock_add(m, 0x5a0, &services)) != 0) {
        return location_stop(g, stop);
    }
    ds[XEEN_DS_PARTY_MAP] = 0;
    xeen_write16(m, XEEN_DS_LOOP_EXIT, 2);
    return XEEN_OK;
}
/* After a key: the location's text rebuilt and shown with its buttons; Esc or the loop's exit leaves, else the location
 * polls again (the frame clock sampled anew). Original: 27F20 (4E38C, 50BF6(0Ah)), then the 26E60 loop. */
xeen_status location_redraw(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_presentation_trace_extra("locend"); /* Evidence: 25464's RETF (2624E) */
    if (!location_text(g)) {
        return location_stop(g, LOCATION_STOP_WINDOW_TEXT);
    }
    location_window(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN);
    if (xeen_read16(m, XEEN_DS_INPUT_CODE) == XEEN_KEY_ESCAPE || xeen_read16(m, XEEN_DS_LOOP_EXIT)) {
        return location_leave(g);
    }
    g->location_sampled = 0;
    return panel_set(g, LOCATION_POLL, LOCATION_POLL_BOUNDARY);
}
/*
 * A location's member key F1..F6 (the tavern 260DD, the blacksmith, the temple, the training grounds 2564B, the
 * guild; each inlines it): the input code (F5FE) becomes the member's number N (F5FE - C9h); when the party has a
 * member N, it becomes the location's member and its portrait is highlighted (2A1F6). Returns 1 then, else 0 (also
 * for any other key). The callers' own stores for a chosen member (the tavern's F626, the training's F62C) follow
 * the highlight, which reads neither.
 */
int location_member_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    unsigned n;
    if (key < XEEN_KEY_F1 || key > XEEN_KEY_F6) {
        return 0;
    }
    n = key - XEEN_KEY_F1;
    xeen_write16(m, XEEN_DS_INPUT_CODE, (uint16_t)n);
    if (m->ds[XEEN_DS_PARTY_COUNT] <= n) {
        return 0;
    }
    g->location_member = XEEN_PARTY_RECORD(n);
    portrait_highlight(g, n); /* 2A1F6 (its frames) */
    return 1;
}
/*
 * A key in a town location, by its kind: the blacksmith (shop_key), the training grounds (train_key), the temple
 * (temple_key) and the guild (guild_loc_key) handle their own; the bank opens its dialog on D(eposit) or W(ithdraw);
 * the tavern's are D(rink), F(ood), R(umour), T(ip), S(ign in), and F1..F6 choose a customer (the drink count
 * cleared). Every other key, and the tavern's after its action, redraws. Original: 25464 (the tavern 25997 with the
 * keys CS:0EB0, the blacksmith 25679, the temple 256D9, the training grounds 2547B), then 27F20.
 */
static xeen_status location_key(xeen_game *g, uint16_t key)
{
    xeen_status status;
    xeen_presentation_trace_extra("locmark %x", (unsigned)key); /* Evidence: 25464's entry */
    if (g->location_kind == LOCATION_BLACKSMITH) {
        return shop_key(g, key);
    }
    if (g->location_kind == LOCATION_TRAINING) {
        return train_key(g, key);
    }
    if (g->location_kind == LOCATION_TEMPLE) {
        return temple_key(g, key);
    }
    if (g->location_kind == LOCATION_GUILD) {
        return guild_loc_key(g, key);
    }
    if (g->location_kind == LOCATION_BANK) {
        if (key == 'D' || key == 'W') {
            return bank_begin(g, key == 'W' ? FUNDS_BANK : FUNDS_PARTY);
        }
        return location_redraw(g);
    }
    switch (key) {
        case 'D':
            return tavern_drink(g);
        case 'F':
            return tavern_food(g);
        case 'R':
            return tavern_rumour(g);
        case 'T':
            return tavern_tip(g);
        case 'S':
            if ((status = location_sign_in(g)) != XEEN_OK) {
                return status;
            }
            break;
        default:
            /* 260DD: a present member becomes the customer, F626 = 0; then 27F20 (no character view). */
            if (location_member_key(g, key)) {
                xeen_write16(&g->movement, XEEN_DS_TAVERN_DRANK, 0);
            }
            break;
    }
    return location_redraw(g);
}
/*
 * The location's loop, by stage: the poll samples the frame clock once, then a key goes to the open message, the
 * tavern's held answer, the bank's dialog or location_key; without one, the clock is read (a stage), and from 3 ticks
 * on the animation steps (the border redrawn while the view window is closed) and the clock is sampled anew. The
 * farewell and end waits, the guild's spell list poll and the inn's stages resume here too.
 * Original: the 26E60 loop: 4EA0D, 189E3 (a key returns), 4EA1C below 3 ticks polls again, else 26978(9877 == 0).
 */
xeen_status location_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    uint16_t key;
    switch (g->panel_stage) {
        case LOCATION_POLL:
            if (!g->location_sampled) {
                status = frame_base(g, bios, user);
                if (status != XEEN_OK) {
                    return status;
                }
                g->location_sampled = 1;
            }
            key = training_preview_poll(g); /* 189E3 (add-on training_preview: its buttons) */
            if (key) {
                return g->location_message ? location_message_closed(g)
                       : g->tavern_wait    ? tavern_held(g)
                                           : /* 18F2F mode 2; 26E60 as a wait */
                               g->bank_mode ? bank_key(g, key)
                                               : location_key(g, key);
            }
            g->dialog_loop_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
            g->panel_stage = LOCATION_TICK;
            g->lifecycle_boundary = LOCATION_TICK_BOUNDARY;
            return XEEN_SERVICE_REQUIRED;
        case LOCATION_TICK:
            if ((status = timer_ticks(g, &g->dialog_loop_elapsed, bios, user)) != XEEN_OK) {
                return status;
            }
            ++g->location_tick_reads;
            g->location_ticks = g->timer_registers.ax; /* The replay's clock */
            if (g->timer_registers.ax >= 3u) {
                location_animate(g, !g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)]);
                g->location_sampled = 0;
            }
            g->panel_stage = LOCATION_POLL;
            g->lifecycle_boundary = LOCATION_POLL_BOUNDARY;
            return XEEN_SERVICE_REQUIRED;
        case LOCATION_LEAVE_WAIT:
            return location_leave_wait(g);
        case LOCATION_END_WAIT:
            return location_end_tail(g);
        case GUILD_POLL:                   /* 3F415's busy 189E3 */
            key = spell_list_sort_poll(g); /* add-on spell_list_sort (off: panel_poll) */
            return key ? guild_key(g, key) : XEEN_SERVICE_REQUIRED;
        default:
            return inn_resume(g, bios, user);
    }
}
