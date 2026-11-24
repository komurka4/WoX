/*
 * A location's presentation parts (Phase 248, from game_location.c): the 277E5 loop's buttons and border, the VOC
 * and music loads, the map save, the amount and texts, the keeper's portrait and its animation by location kind
 * (guild, bank, temple, training, tavern), the next frame and its cues. location_animate and location_window,
 * which compose them each frame (with the training_preview add-on's hooks), stay in game_location.c with the
 * location's flow.
 */
#include "game_location.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The original calls of the voice driver (187E9) and the CD status (47F3D) in this unit, each by the call's
 * return address: the audio model records which call asked (traces name them). Not stops, not waits. */
enum {
    SMITH_VOICE_QUERY_CALL = 0x2698e, /* the blacksmith's portrait animates while the voice plays */
    GUILD_VOICE_QUERY_CALL = 0x26b00, /* the guild keeper (Darkside) */
    BANK_VOICE_QUERY_CALL = 0x26b67,  /* the banker */
    BANK_DARK_VOICE_QUERY_CALL = 0x26bb2,/* the banker's Darkside frame */
    TEMPLE_VOICE_QUERY_CALL = 0x26c21,/* the temple keeper */
    TRAINING_VOICE_QUERY_CALL = 0x26c5c,/* the trainer */
    TAVERN_VOICE_QUERY_CALL = 0x26d0a /* the barkeeper (Darkside) */
};
/* Where native play stops when a location cannot save the map's state; the value is the original address of the
 * step (tools/xeen_replay/stops.json). */
enum {
    LOCATION_STOP_MAP_SAVE = 0x30f34         /* the map's live state into the active world's archive */
};
/* The buttons of a set (its x words at XS, y bytes at YS, enabled bytes at ON) with sprite H: frame 2i at
 * (x[i], y[i]) for each enabled button, the first LIMIT at most, until the FFFFh terminator (presentation). */
void buttons_draw(xeen_game *g, uint16_t xs, uint16_t ys, uint16_t on, uint16_t h, unsigned limit)
{
    xeen_movement *m = &g->movement;
    const uint8_t *ds = m->ds;
    unsigned i;
    for (i = 0; i < limit && xeen_read16(m, (uint16_t)(xs + 2u * i)) != 0xffffu; ++i) {
        if (ds[(uint16_t)(on + i)]) {
            exit_draw(g, h, (uint16_t)(2u * i), (int16_t)xeen_read16(m, (uint16_t)(xs + 2u * i)),
                      (int16_t)ds[(uint16_t)(ys + i)], 0);
        }
    }
}
/* Draws the live button set: for every enabled button up to the table's FFFFh end, frame 2i of the buttons' sprites
 * at the button's position (the tables of 98AC, 98A8 and 9898, the sprites of 9896).
 * Original: 27E8E's 4AC92 loop. */
void location_buttons(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    buttons_draw(g, xeen_read16(m, XEEN_DS_BUTTON_X_TABLE), xeen_read16(m, XEEN_DS_BUTTON_Y_TABLE),
                 xeen_read16(m, XEEN_DS_BUTTON_ENABLED_TABLE), xeen_read16(m, XEEN_DS_BUTTON_SPRITES), 32u);
}
/* Draws the interface border (xeen_interface_border's draw list, its text and frame). Original: 26306. */
void location_border(xeen_game *g)
{
    xeen_border_ops ops;
    if (xeen_interface_border(&g->movement, &ops) != XEEN_OK) {
        return;
    }
    (void)xeen_presentation_draw_sprites(g->presentation, ops.draws, ops.count);
    if (ops.has_text) {
        (void)xeen_presentation_text(g->presentation, ops.text_window, ops.text);
    }
    if (ops.has_frame) {
        (void)xeen_presentation_frame(g->presentation, ops.frame_window);
    }
}
/* Loads the voice NAME into the file block without starting it (the cutscenes start it later): the CD's preload
 * check, the file, its size as the loaded file's, the block's bytes kept (f42c_load) and the voice kept for
 * voc_start, the CD's replay; a missing file keeps no voice. The block's DOS memory is not modelled: its word stays.
 * Original: 18975 into F42C (its 47B7D check and FB06 replay; 4A877 sets 8582). */
void location_voc_load(xeen_game *g, const char *name)
{
    size_t n = 0;
    uint8_t *b;
    cd_preload(g);
    b = menu_extract(g, name, &n);
    if (b) {
        xeen_write16(&g->movement, XEEN_DS_LOADED_FILE_SIZE, (uint16_t)n);
        f42c_load(g, b, n);
        voc_keep(g, b, n);
        cd_postload(g);
    }
    else {
        g->voc_kept = 0;
    }
    free(b);
    exit_event(g, name);
}
/* Loads the voice NAME (location_voc_load) and starts it at once. Original: 18975 into F42C, then 187E9(F42C, 1). */
void location_voc(xeen_game *g, const char *name)
{
    location_voc_load(g, name);
    voc_start(g, 0); /* Sink; model */
}
/* A location's song NAME: the current song's name becomes the previous one, NAME (at most 14 characters) the current,
 * and the request with FLAG is recorded for the music driver (record_music: its pending song, or the start).
 * Original: 4224F(name, flag): 9757 = 9766 (14CB6), 9766 = name, 50AB1(flag), the pending byte 1658 or 18ED0. */
void location_music(xeen_game *g, const char *name, uint16_t flag)
{
    uint8_t *ds = g->movement.ds;
    size_t n = strlen(name), k = 0;
    while (k < 14u && ds[XEEN_DS_SONG_NAME + k]) {
        ++k;
    }
    memmove(ds + XEEN_DS_PREVIOUS_SONG_NAME, ds + XEEN_DS_SONG_NAME, k);
    ds[XEEN_DS_PREVIOUS_SONG_NAME + k] = 0;
    if (n > 14u) {
        n = 14u;
    }
    memcpy(ds + XEEN_DS_SONG_NAME, name, n);
    ds[XEEN_DS_SONG_NAME + n] = 0;
    record_music(g, name, flag);
}
/* Saves the current map's state into the active world's in-memory state file (xeen_save_map_live) before a location
 * changes the map; nothing for no map or Clouds map 85. The two member names it would write are formatted into the
 * file-name buffer as the original does (.evt, then .mob). Returns 0, or the stop when the state cannot be saved.
 * Original: 30F34 (C0CC; 30F3D, 30F50; the names 30F7F, 30FB6). */
uint32_t location_map_save(xeen_game *g)
{
    uint8_t *active;
    size_t an = 0;
    char error[160];
    xeen_archive cur;
    xeen_movement *m = &g->movement;
    uint16_t maze = xeen_read16(m, XEEN_DS_MAP_FIELD(ID));
    if (xeen_map_save_skipped(maze, m->ds[XEEN_DS_WORLD])) {
        return 0; /* 30F3D, 30F50: nothing */
    }
    {
        /* 30F7F, 30FB6: the member names formatted into F5E8 (".evt", then
         * ".mob"; 30E49 formats its ".dat" names elsewhere). */
        char name[16];
        uint16_t formats[2] = {XEEN_TEXT_RELOAD_EVENTS_FILE, XEEN_TEXT_MAP_MONSTERS_FILE};
        unsigned i;
        for (i = 0; i < 2u; ++i) {
            (void)snprintf(name, sizeof name, ds_text(g, formats[i]), maze > 99u ? 'x' : '0',
                           (unsigned)maze);
            ds_put_text(g, XEEN_DS_FILE_NAME, name);
        }
    }
    if (!(active = cur_copy(g, g->world, &an))) {
        return LOCATION_STOP_MAP_SAVE;
    }
    if (!xeen_save_map_live(active, an, g->movement.ds, sizeof g->movement.ds, g->backing,
                            g->resources.record_bytes, error, sizeof error) ||
        xeen_archive_open(&cur, active, an) != XR_OK) {
        free(active);
        return LOCATION_STOP_MAP_SAVE;
    }
    state_adopt(g, active, an, cur);
    return 0;
}
/* A gold amount as the locations show it: the number, or above 999,999 the millions with " mil" (G's DS text).
 * Original: 2689A (268BF the DS:521C format). */
void location_amount(const xeen_game *g, char *out, size_t n, uint32_t v)
{
    if (v > 999999u) {
        (void)snprintf(out, n, ds_text(g, XEEN_TEXT_AMOUNT_MILLIONS), (unsigned long)(v / 1000000u));
    } else {
        (void)snprintf(out, n, "%lu", (unsigned long)v);
    }
}
/*
 * Builds a location's text window into the second text buffer: the training grounds', the temple's, the bank's and
 * the guild's own texts; the blacksmith's "Store Options for" with the customer's name and the party's gold; the
 * tavern's options text with the name, its options and the gold. Returns 0 when a text does not fit.
 * Original: 27198 kind 3 (276BD): the [2A4C] text, the 54A7 options; kind 1 (272EC): the 53AC text.
 */
int location_text(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char gold[24], out[1024];
    int n;
    uint32_t v = xeen_ds_read32(m, XEEN_DS_PARTY_GOLD);
    if (g->location_kind == LOCATION_TRAINING || g->location_kind == LOCATION_TEMPLE ||
        g->location_kind == LOCATION_BANK) {
        if (!(g->location_kind == LOCATION_TRAINING ? train_text(g, out, sizeof out)
              : g->location_kind == LOCATION_TEMPLE ? temple_text(g, out, sizeof out)
                                                    : bank_text(g, out, sizeof out))) {
            return 0;
        }
        ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
        return 1;
    }
    if (g->location_kind == LOCATION_GUILD) {
        if (!guild_text(g, out, sizeof out)) {
            return 0;
        }
        ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
        return 1;
    }
    location_amount(g, gold, sizeof gold, v);
    if (g->location_kind == LOCATION_BLACKSMITH) {
        n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_STORE_OPTIONS), ds_text(g, g->location_member), gold);
    } else {
        n = snprintf(out, sizeof out, far_text(g, XEEN_DS_TAVERN_OPTIONS_TEXT, 0),
                     ds_text(g, g->location_member), ds_text(g, XEEN_TEXT_TAVERN_OPTIONS), gold);
    }
    if (n < 0 || (size_t)n >= sizeof out) {
        return 0;
    }
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    return 1;
}
/* One draw LO..HI of a location animation, its AX 0 as at every such call of the original (LO without a random
 * source). Original: 504DA. */
uint16_t location_random(xeen_game *g, uint16_t lo, uint16_t hi)
{
    uint16_t v = lo;
    if (g->event_random) {
        (void)xeen_entity_rng_range(g->event_random(g->event_random_user, 0), lo, hi, &v);
    }
    return v;
}
/* One draw LO..HI with AL as the call's AX: what the original's register holds there (the voice query's result,
 * the world byte, or a draw's 0, per the instruction before each call). Original: 504DA in 26978's busy branches. */
static uint16_t location_random_al(xeen_game *g, uint16_t lo, uint16_t hi, uint16_t al)
{
    uint16_t v = lo;
    if (g->event_random) {
        (void)xeen_entity_rng_range(g->event_random(g->event_random_user, al), lo, hi, &v);
    }
    return v;
}
/* Draws the location's portrait: frame F mod 8 of the animation sprite F / 8, F the animation frame, at the
 * animation's position. Original: 8D56[8D32/8] frame 8D32 % 8 at (8D30, 8D2E). */
static void location_portrait(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint16_t f = xeen_read16(m, XEEN_DS_ANIMATION_FRAME);
    exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(f / 8u)), (uint16_t)(f % 8u),
              (int16_t)xeen_read16(m, XEEN_DS_ANIMATION_X),
              (int16_t)xeen_read16(m, XEEN_DS_ANIMATION_Y), 0);
}
/* A location's first frame at its entry: the keeper's portrait, and for the Darkside bank a random frame 0Dh..12h of
 * its fifth animation sprite at (8, 30). Original: 27E15..27E71 (4AC92(8D5E, 504DA(0Dh, 12h), 8, 1Eh)). */
void location_first_frame(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    location_portrait(g);
    if (g->location_kind == LOCATION_BANK && m->ds[XEEN_DS_WORLD]) {
        exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(4)), location_random(g, 0xd, 0x12), 8,
                  30, 0);
    }
}
/*
 * The keeper's portrait for this frame. The blacksmith: while its voice plays, on Darkside the portrait, frame 9 or
 * 10 of the third animation sprite at (34, 33) and a random frame 3..8 of it at (34, 54); on Clouds only a random
 * frame 5..7 of the second sprite at (8, 8); without a voice, the portrait (and on Darkside the frame at (34, 33)).
 * Every other kind draws the portrait when its sprite exists, except the Darkside training grounds (keeper_training
 * draws theirs).
 * Original: 26978's first part (26989..26AEF): 269A2, 26A23, 26A84's 4AC92(8D5A, 9 + (504DA(0, 0Bh) != 1), 22h,
 * 21h), 26A9E.
 */
void location_keeper_portrait(xeen_game *g, unsigned kind, int dark)
{
    xeen_movement *m = &g->movement;
    if (kind == LOCATION_BLACKSMITH) {
        if (voice_busy(g, SMITH_VOICE_QUERY_CALL)) {
            if (dark) {
                location_portrait(g);
                exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(2)),
                          (uint16_t)(9u + (location_random(g, 0, 0xb) != 1u)), 34, 33, 0);
                exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(2)),
                          (uint16_t)(location_random(g, 0, 5) + 3u), 34, 54, 0);
            } else {
                exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(1)),
                          location_random_al(g, 5, 7, 1), 8, 8, 0); /* 26A23: AL 187E9's 1 */
            }
        } else {
            location_portrait(g);
            if (dark) {
                exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(2)),
                          (uint16_t)(9u + (location_random(g, 0, 0xb) != 1u)), 34, 33, 0);
            }
        }
    } else if (!(dark && kind == LOCATION_TRAINING) &&
               xeen_read16(
                       m, XEEN_DS_ANIMATION_SPRITE(xeen_read16(m, XEEN_DS_ANIMATION_FRAME) / 8u))) {
        location_portrait(g);
    }
}
/* The guild's keeper while its voice plays, on Darkside only: with the frame toggle (F47C) clear a random frame
 * 0..3 at (161, 48); set, the toggle flipped and drawn as the frame at (8, 106). Original: 26AF0. */
static void keeper_guild(xeen_game *g, int dark)
{
    xeen_movement *m = &g->movement;
    if (voice_busy(g, GUILD_VOICE_QUERY_CALL) && dark) {
        if (!xeen_read16(m, 0xf47c)) {
            exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(6)),
                      location_random_al(g, 0, 3, 1), 161, 48, 0);
        } else {
            xeen_write16(m, 0xf47c, (uint16_t)(xeen_read16(m, 0xf47c) ^ 1u));
            exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(6)), xeen_read16(m, 0xf47c), 8,
                      106, 0);
        }
    }
}
/* The bank's keeper while its voice plays (or Darkside's counter F47C runs): on Clouds a random frame 7..0Bh at
 * (8, 8); on Darkside, with the voice or the counter at 1, a random frame 0Dh..12h at (8, 30), else with the counter
 * above 1 frame 13 - counter, the counter then counting up and back to 0 past 13.
 * Original: 26B54 (Clouds 26B8B; Darkside 26BAD). */
static void keeper_bank(xeen_game *g, int dark)
{
    xeen_movement *m = &g->movement;
    int q = voice_busy(g, BANK_VOICE_QUERY_CALL);
    if (!q && !(dark && xeen_read16(m, 0xf47c))) {
        return;
    }
    if (!dark) {
        exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(2)),
                  location_random_al(g, 7, 0xb, 0), 8, 8, 0); /* 26B8B: AL DS 94 */
    } else {
        int q2 = voice_busy(g, BANK_DARK_VOICE_QUERY_CALL);
        int16_t c = (int16_t)xeen_read16(m, 0xf47c);
        if (q2 || c == 1) {
            exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(4)),
                      location_random_al(g, 0xd, 0x12, (uint16_t)q2), 8, 30,
                      0); /* AL: 26BAD's result */
        } else if (c > 1) {
            xeen_write16(m, 0xf47c, (uint16_t)(c + 1));
            exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(4)), (uint16_t)(13 - c), 8,
                      30, 0);
            if (c + 1 > 13) {
                xeen_write16(m, 0xf47c, 0);
            }
        }
    }
}
/* The temple's keeper while its voice plays: a random frame 2..4 at (8, 8). Original: 26C11. */
static void keeper_temple(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    if (voice_busy(g, TEMPLE_VOICE_QUERY_CALL)) {
        exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(3)),
                  location_random_al(g, 2, 4, 1), 8, 8, 0);
    }
}
/* The training grounds' keeper while its voice plays: on Clouds a random frame 8..0Ch at (8, 8); on Darkside the
 * counter F47C + 1 as the first animation sprite's frame (modulo 8) and a random frame 0..5 at (61, 74). Without a
 * voice, the Darkside portrait. Original: 26C49 (26CCB). */
static void keeper_training(xeen_game *g, int dark)
{
    xeen_movement *m = &g->movement;
    if (voice_busy(g, TRAINING_VOICE_QUERY_CALL)) {
        if (!dark) {
            exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(1)),
                      location_random_al(g, 8, 0xc, 0), 8, 8, 0); /* AL DS 94 */
        } else {
            int16_t c = (int16_t)(xeen_read16(m, 0xf47c) + 1u);
            xeen_write16(m, 0xf47c, (uint16_t)c);
            exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITES), (uint16_t)(int16_t)(c % 8),
                      8, 8, 0);
            exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(5)), location_random(g, 0, 5),
                      61, 74, 0);
        }
    } else if (dark) {
        location_portrait(g);
    }
}
/* The tavern's keeper while its voice plays, on Darkside only: a random frame 0..7 at (153, 49). Original: 26CFA. */
static void keeper_tavern(xeen_game *g, int dark)
{
    xeen_movement *m = &g->movement;
    if (voice_busy(g, TAVERN_VOICE_QUERY_CALL) && dark) {
        exit_draw(g, xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(4)),
                  location_random_al(g, 0, 7, 1), 153, 49, 0); /* AL 187E9's 1 */
    }
}
/*
 * The keeper's own animation for its kind (the guild, the bank, the temple, the training grounds, the tavern), drawn
 * over the portrait; the blacksmith's is in its portrait. Without a voice playing only Darkside's counters run (the
 * bank's countdown frames and the training grounds' portrait); the voice branches are kept as the original's.
 * Original: 26978's per-kind animation (26AF0..26D3B; the guild 26AF7, the bank 26B78 / 26B82, the temple 26C2B,
 * the training grounds 26C63 / 26CCB, the tavern 26D18).
 */
void location_keeper_animation(xeen_game *g, unsigned kind, int dark)
{
    switch (kind) {
    case LOCATION_GUILD: /* 26AF0 */
        keeper_guild(g, dark);
        break;
    case LOCATION_BANK: /* 26B54 */
        keeper_bank(g, dark);
        break;
    case LOCATION_TEMPLE: /* 26C11 */
        keeper_temple(g);
        break;
    case LOCATION_TRAINING: /* 26C49 */
        keeper_training(g, dark);
        break;
    case LOCATION_TAVERN: /* 26CFA */
        keeper_tavern(g, dark);
        break;
    default:
        break;
    }
}
/*
 * Advances the location's animation and returns its new frame. With the view's or the speech window open the keeper
 * talks instead (talk_mouth: the mouth toggle and the CD status's mouth); otherwise the frame counts on modulo the
 * frame count, compared signed (the native's 0 for a zero count: the original's table has none). On Clouds a frame
 * wrapping to 0 restarts the guild's and the temple's loops at their restart frames (at most one applies).
 * Original: 26D57..26DDE: 8D32 = (8D32 + 1) IDIV 8D36; windows 9877 / 9894; DS:3026; 26DAE.
 */
uint16_t location_next_frame(xeen_game *g, int dark)
{
    xeen_movement *m = &g->movement;
    uint16_t f;
    if (m->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)] || m->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        talk_mouth(g);
    } else {
        int16_t n = (int16_t)xeen_read16(m, XEEN_DS_ANIMATION_FRAME_COUNT);
        xeen_write16(
                m, XEEN_DS_ANIMATION_FRAME,
                (uint16_t)(n ? (int16_t)(xeen_read16(m, XEEN_DS_ANIMATION_FRAME) + 1u) % n : 0));
    }
    f = xeen_read16(m, XEEN_DS_ANIMATION_FRAME);
    if (!dark && !f) { /* 26DAE: the first restart leaves f nonzero, so at most one applies */
        uint16_t count = xeen_read16(m, XEEN_DS_ANIMATION_FRAME_COUNT);
        if (count == CLOUDS_GUILD_FRAMES) {
            xeen_write16(m, XEEN_DS_ANIMATION_FRAME, f = CLOUDS_GUILD_RESTART);
        } else if (count == CLOUDS_TEMPLE_FRAMES) {
            xeen_write16(m, XEEN_DS_ANIMATION_FRAME, f = CLOUDS_TEMPLE_RESTART);
        }
    }
    return f;
}
/*
 * Frame F's sound cues: the blacksmith's anvil on frames 3 and 9 (4 and 13 on Darkside); the Darkside training
 * grounds' frame 23 loads and starts the spit voice.
 * Original: 26DDF..26E47: 1880E(2Dh); spit1.voc (DS:522F), 26E28.
 */
void location_frame_cues(xeen_game *g, unsigned kind, int dark, uint16_t f)
{
    if (kind == LOCATION_BLACKSMITH && (dark ? (f == 4u || f == 13u) : (f == 3u || f == 9u))) {
        (void)sound_effect(g, 45);
    }
    if (kind == LOCATION_TRAINING && dark && f == 23u) {
        location_voc(g, ds_text(g, XEEN_TEXT_LOCATION_SPIT_VOICE));
        exit_event(g, "voice");
    }
}
