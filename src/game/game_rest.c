/*
 * Exploration Rest (Phase 246, from game.c): 17E91 -> 28E35's refusals and confirmation, 28EBE's sleep, the
 * ten 28EF4 steps (one 28178 charge each), 28F2B's finish and the dream (scene1.raw, dreams2.voc), the
 * wake, and the 1B503 scenes and frame waits between them (xeen_game_resume_rest_wait).
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The original calls of the voice driver (187E9) and the CD status (47F3D) in this unit, each by the call's
 * return address: the audio model records which call asked (traces name them). Not stops, not waits. */
enum {
    DREAM_VOICE_CALL = 0x28fb2,/* the dream's voice */
    LAUGH_VOICE_CALL = 0x28ff4/* laff1.voc */
};
/*
 * Where native play stops when Rest or its dream cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    REST_STOP_NO_HOST = 0x28e35,             /* combat's monsters, sprites, random source or presentation missing */
    REST_STOP_CELL_METADATA = 0x1c844,       /* the party's cell's metadata */
    REST_STOP_DANGER_SCROLL = 0x28e69,       /* "Too dangerous to rest here!" could not be shown */
    REST_STOP_CONFIRM = 0x28e9f,             /* "Some Chars may die. Rest anyway?" could not be shown */
    REST_STOP_DREAM_SCREEN = 0x28f45,        /* the dream's screen (a full-screen picture) could not be read */
    REST_STOP_DREAM_NO_MEMORY = 0x28f88,
    REST_STOP_DREAM_VOICE = 0x28fe1,         /* no voice kept for the dream */
    REST_STOP_WAIT_FRAME = 0x2903d,
    REST_STOP_DONE_TEXT = 0x29149,           /* the rest's end message does not fit */
    REST_STOP_DONE_SCROLL = 0x29157          /* the message could not be shown */
};
/* One of Rest's scene passes (after a step, and the last one after the wake): no frame mark before it, and its frame
 * wait returns to rest_after_scene with STAGE (1 a step, 2 the last). Original: 1B503(1) at 28EFA and 29126, entered
 * at its own 4EA35; the 1C1B3 frame wait. */
xeen_status rest_scene(xeen_game *g, int stage)
{
    g->rest_stage = stage;
    return scene_wait(g, 1, XEEN_TIMER_SCENE_MARK);
}
/* One of the ten sleeping steps: a time charge, in which every monster in the 7x7 window moves toward the sleeping
 * party and queued shots may be delivered (a hit wakes the party), then a scene pass, whose contact test can start a
 * fight. Original: 28EF4: 28178 (28195's 2A701 in mode 5; 4128F, the 4152D test), 1B503(1) (contact in modes 1 and
 * 5: 38F5F). */
static xeen_status rest_step(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_gameplay_services services = session_services(g);
    xeen_gameplay_tick tick = xeen_gameplay_tick_wait_services(m, g->monster_database,
                                                               g->monster_database_size, &services);
    tick = ranged_tick(g, tick);
    if (tick.boundary != XEEN_TICK_POST_ACTION || tick.status == XT_INPUT_ERROR) {
        return game_stop(g, tick.boundary);
    }
    return rest_scene(g, 1);
}
static xeen_status rest_wake(xeen_game *);
/*
 * The dream (one rest in twenty): the screen is saved and fades out, the dream's picture (scene1.raw from intro.cc)
 * is shown and fades in, seven ticks pass, its voice (dreams2.voc) plays to its end, then Lord Xeen's laugh
 * (laff1.voc, from dark.cc in both worlds) plays to its end, seven more ticks pass, and the saved screen comes back
 * between two fades (xeen_game_resume_rest_wait). Without a voice model each voice reports itself finished at once
 * (as the historical harness's silent driver). Without intro.cc the dream is the explicit stop 28F45, after the
 * rest's time charge.
 * Original: 28F45..2906A: 186B2 saves the screen (8C90), 4A75A(4); 17FFE selects intro.cc, 188F6 scene1.raw,
 * 50BF6(0); 18975(dreams2.voc) into F42C; 4A74F(4); 4EA0D, 4EA1C until 7; 187E9(F42C, 1) and the 28FB5 loop on
 * 187E9(1, 0); 17FFE(dark.cc) (DS 2980), 18975(laff1.voc), 187E9(F42C, 1), the 28FF7 loop; 4EA0D, 4EA1C until 7.
 */
/* Loads one of the dream's voices (B, N bytes; NULL: none) as the kept voice. Original: 18975 (47B7D, the bytes into
 * F42C, 8582 = the size by 4A894, the FB06 replay). */
static void dream_voc(xeen_game *g, const uint8_t *b, size_t n)
{
    cd_preload(g);
    xeen_write16(&g->movement, XEEN_DS_FILE_BUFFER_SEGMENT, 0);
    if (b) {
        xeen_write16(&g->movement, XEEN_DS_LOADED_FILE_SIZE, (uint16_t)n);
        f42c_load(g, b, n);
        voc_keep(g, b, n);
        cd_postload(g);
    } else {
        g->voc_kept = 0;
    }
}
/**
 * The dream's start: the screen saved, the fade out, the picture shown, the first voice loaded, the fade in; then
 * the first seven-tick wait (REST_DREAM_BOUNDARY). Without intro.cc, its picture or a frame buffer it is an
 * explicit stop at the dream's screen (it was never skipped); a voice that cannot be read stops too.
 */
static xeen_status rest_dream(xeen_game *g)
{
    const char *name = ds_text(g, XEEN_TEXT_REST_DREAM_SCREEN), *voice = ds_text(g, XEEN_TEXT_REST_DREAM_VOICE);
    xeen_presentation *p = g->presentation;
    xeen_resource_info info;
    size_t n = 0;
    uint8_t *b = NULL;
    if (!g->intro_bound || !p || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS ||
        xeen_archive_find_named(&g->intro, name, strlen(name), &info) != XR_OK ||
        info.size != XEEN_PRESENT_PIXELS) {
        return game_stop(g, REST_STOP_DREAM_SCREEN);
    }
    memcpy(g->dream_screen, p->framebuffer, XEEN_PRESENT_PIXELS);
    /* The screen fades out before the picture is loaded and shown (at black); it fades in after the voice is loaded.
     * Original: 28F4F 4A75A(4), 28F78 50BF6(0), 28F92 4A74F(4) after 18975. */
    xeen_presentation_fade(p, 0, 4);
    xeen_presentation_mouse_off(p); /* 28F57 4E231 */
    cd_preload(g);
    if (xeen_archive_extract(&g->intro, XR_BANK_ASSETS_XOR35, &info, p->framebuffer,
                             XEEN_PRESENT_PIXELS, &n) != XR_OK ||
        n != XEEN_PRESENT_PIXELS || xeen_presentation_update_window(p, XEEN_WINDOW_SCREEN) != XEEN_OK) {
        memcpy(p->framebuffer, g->dream_screen, XEEN_PRESENT_PIXELS);
        return game_stop(g, REST_STOP_DREAM_SCREEN);
    }
    cd_postload(g);
    if (xeen_archive_find_named(&g->intro, voice, strlen(voice), &info) == XR_OK &&
        (b = malloc(info.size ? info.size : 1u)) &&
        xeen_archive_extract(&g->intro, XR_BANK_ASSETS_XOR35, &info, b, info.size, &n) == XR_OK) {
        dream_voc(g, b, n);
    } else {
        free(b);
        return game_stop(g, REST_STOP_DREAM_NO_MEMORY);
    } /* 18975: 505CF never returns */
    free(b);
    xeen_presentation_fade(p, 1, 4); /* 28F92 4A74F(4) */
    g->rest_elapsed = (xeen_timer_elapsed_state){0, 0};
    return service_wait(g, REST_DREAM_BOUNDARY);
}
/* After the first wait: the dream's voice, then the laugh, each waited for while it plays (the boundary stays at its
 * wait for the host's next clock). XEEN_SERVICE_REQUIRED while a voice plays, XEEN_OK when both have played, else
 * the explicit stop's status. Original: 28FA7..29005 (the waits 28FBE and 29000 on 187E9's return). */
static xeen_status dream_voices(xeen_game *g)
{
    if (g->lifecycle_boundary == REST_DREAM_BOUNDARY) {
        voc_start(g, DREAM_VOICE_CALL);
        g->lifecycle_boundary = REST_DREAM_VOICE_BOUNDARY;
    }
    if (g->lifecycle_boundary == REST_DREAM_VOICE_BOUNDARY) {
        size_t n = 0;
        uint8_t *b;
        if (voice_busy(g, REST_DREAM_VOICE_BOUNDARY)) {
            return XEEN_SERVICE_REQUIRED;
        }
        b = cc_extract(g, 1, ds_text(g, XEEN_TEXT_REST_DREAM_LAUGH), &n);
        dream_voc(g, b, n);
        free(b);
        if (!g->voc_kept) {
            return game_stop(g, REST_STOP_DREAM_VOICE); /* 18975: 505CF */
        }
        voc_start(g, LAUGH_VOICE_CALL);
        g->lifecycle_boundary = REST_LAUGH_VOICE_BOUNDARY;
    }
    if (voice_busy(g, REST_LAUGH_VOICE_BOUNDARY)) {
        return XEEN_SERVICE_REQUIRED;
    }
    g->rest_elapsed = (xeen_timer_elapsed_state){0, 0};
    g->lifecycle_boundary = REST_DREAM_END_BOUNDARY;
    return XEEN_OK;
}
/* The dream's waits, resumed by the host: the voices' waits (dream_voices), or a seven-tick wait (compared signed)
 * after the first frame mark; the first seven ticks lead to the voices, the last ones to the dream's end. */
xeen_status xeen_game_resume_rest_wait(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    xeen_presentation *p;
    if (!g || g->phase != XG_STOPPED || !g->presentation || !bios ||
        (g->lifecycle_boundary != REST_DREAM_BOUNDARY && g->lifecycle_boundary != REST_DREAM_END_BOUNDARY &&
         g->lifecycle_boundary != REST_DREAM_VOICE_BOUNDARY && g->lifecycle_boundary != REST_LAUGH_VOICE_BOUNDARY)) {
        return XEEN_BAD_INPUT;
    }
    if (g->lifecycle_boundary == REST_DREAM_VOICE_BOUNDARY || g->lifecycle_boundary == REST_LAUGH_VOICE_BOUNDARY) {
        status = dream_voices(g);
        return status == XEEN_OK ? XEEN_SERVICE_REQUIRED : status;
    }
    if (!g->rest_elapsed.entry) {
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        g->rest_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
    }
    if ((status = timer_ticks(g, &g->rest_elapsed, bios, user)) != XEEN_OK) {
        return status;
    }
    if ((int16_t)g->timer_registers.ax < 7) { /* JL: signed */
        g->rest_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
        return XEEN_SERVICE_REQUIRED;
    }
    g->rest_elapsed = (xeen_timer_elapsed_state){0, 0};
    if (g->lifecycle_boundary == REST_DREAM_BOUNDARY) {
        status = dream_voices(g); /* busy: 28FBE or 29000 */
        return status == XEEN_OK ? XEEN_SERVICE_REQUIRED : status;
    }
    /* The dream ends: a fade out, the cursor, the screen as it was before the dream, the world archive again, a
     * fade in. Original: 29016 (4A75A(4), 4E06B(F460, 0), 4E99F(8C90), 50BF6(0), 17FFE, 4A74F(4)). */
    p = g->presentation;
    xeen_presentation_fade(p, 0, 4); /* 29018 4A75A(4); the fade */
    (void)xeen_presentation_select_cursor(p, xeen_read16(&g->movement, XEEN_DS_MOUSE_SPRITES), 0);
    memcpy(p->framebuffer, g->dream_screen, XEEN_PRESENT_PIXELS);
    if (xeen_presentation_update_window(p, XEEN_WINDOW_SCREEN) != XEEN_OK) {
        return game_stop(g, REST_STOP_WAIT_FRAME);
    }
    xeen_presentation_fade(p, 1, 4); /* 29065 4A74F(4) after 17FFE */
    return rest_wake(g);
}
/* The rest's time: AMOUNT minutes with the conditions' loop (380 outdoors and 470 indoors after the ten steps' own
 * charges, 480 under Super Shelter: eight hours in all). Then one rest in twenty (random 1..20 == 1) dreams. The
 * draw is entered with the time routine's return: the night flag, or after a change of the sky the sky's handle.
 * Original: 28F2B: 2FC7E(amount) (Super Shelter 3AF2B); its AL: 2FFE7's night value, or after 2A0FB the 18931 result
 * (F45C). */
static xeen_status rest_finish(xeen_game *g, uint16_t amount)
{
    xeen_movement *m = &g->movement;
    uint16_t roll = 0, night, al;
    xeen_gameplay_services services = session_services(g);
    uint16_t old_night = xeen_read16(m, XEEN_DS_NIGHT);
    xeen_gameplay_tick tick = xeen_gameplay_time_conditions_services(m, amount, &services);
    if (tick.status != XT_INTERNAL) {
        return game_stop(g, tick.boundary);
    }
    night = (xeen_read16(m, XEEN_DS_MINUTE_OF_DAY) < XEEN_NIGHT_ENDS ||
             xeen_read16(m, XEEN_DS_MINUTE_OF_DAY) >= XEEN_NIGHT_BEGINS)
                    ? 1u
                    : 0u;
    al = night != old_night ? xeen_read16(m, 0xf45c) : night;
    (void)xeen_entity_rng_range(g->event_random(g->event_random_user, (uint16_t)(al & 255u)), 1u,
                                20u, &roll);
    return roll == 1u ? rest_dream(g) : rest_wake(g);
}
/* The wake: the temporaries reset, and every member wakes. While food lasts the party counts as fed, and each member
 * who is not dead, stoned or eradicated eats one food, which also ends the party's Heroism, Holy Bonus, Power Shield
 * and Bless (cleared at each meal), ends the member's unconsciousness and restores full HP and SP; a
 * member reached with no food left makes the party starving (no healing). The bar is redrawn, the UI mode restored,
 * and the step's surface tail runs (rest_wake_after_surface), then the last scene.
 * Original: 2906B: 29168; the 2907C loop (+14Bh; EFF6, F01E; 13..15; EDA3..EDA6, +14Fh; 2AAC2, 46470); 2ABB8(1);
 * 9A1C; 17264; 1B503(1). */
static xeen_status rest_wake(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned i;
    xeen_caller_result surface;
    xeen_gameplay_reset_temps(m);
    g->rest_food = 0;
    g->rest_starving = 0;
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        uint8_t *ch = ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES, worst;
        ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ASLEEP)] = 0;
        if (!xeen_read16(m, XEEN_DS_PARTY_FOOD)) {
            g->rest_starving = 1;
            continue;
        }
        ds[XEEN_DS_PARTY_FED] = 1;
        worst = xeen_character_worst_condition(ch);
        if (xeen_condition_is_deathlike(worst)) {
            continue;
        }
        xeen_write16(m, XEEN_DS_PARTY_FOOD, (uint16_t)(xeen_read16(m, XEEN_DS_PARTY_FOOD) - 1u));
        ++g->rest_food;
        ds[XEEN_DS_EFFECT_HEROISM] = ds[XEEN_DS_EFFECT_HOLY_BONUS] =
                ds[XEEN_DS_EFFECT_POWER_SHIELD] = ds[XEEN_DS_EFFECT_BLESS] = 0;
        ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_UNCONSCIOUS)] = 0;
        XEEN_DAMAGE_NUMBERS(hp_restore(m->ds,
                                       XEEN_PARTY_RECORD(i),
                                       xeen_combat_max_hp(m, ch))); /* add-on hook */
        xeen_write16(m,
                     (uint16_t)(XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES + XEEN_CHARACTER_HP),
                     xeen_combat_max_hp(m, ch));
        xeen_write16(m,
                     (uint16_t)(XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES + XEEN_CHARACTER_SP),
                     xeen_combat_max_sp(m, ch));
    }
    if (g->presentation) {
        (void)xeen_world_entry_party_bar(m, g->presentation,
                                         1); /* 29111 2ABB8(1), faces and HP gems */
    }
    ds[XEEN_DS_UI_MODE] = g->rest_mode;
    surface = xeen_post_action_surface_native(m);
    surface = desert_surface(g, surface);
    if (surface.boundary == XEEN_TICK_UNFED_DAWN &&
        desert_scroll(g, DESERT_REST_WAKE,
                      (xeen_gameplay_tick){g->navigation, XT_EXTERNAL, XEEN_TICK_UNFED_DAWN, 0, 0})) {
        return XEEN_SERVICE_REQUIRED;
    }
    return rest_wake_after_surface(g, surface);
}
/* The wake's surface tail has returned: a defeat, a damaging cell's scene or a fall continue as below, else the last
 * scene pass. Original: 17264 returned to 29124; 1B503(1). */
xeen_status rest_wake_after_surface(xeen_game *g, xeen_caller_result surface)
{
    if (surface.boundary == XEEN_SURFACE_DEFEAT_SCENE) {
        g->shelter = 0;
        return surface_scene(g, 2, SURFACE_REST_WAKE);
    } /* a party defeated during the rest */
    /* Waking on a damaging cell (possible only under Super Shelter, which lets the party rest there): the cell's
     * damage scene after the members' healing, then back to Rest's own end (the last scene and "Rest complete"),
     * without the defeat test the movement's tail has.
     * Original: the wake's 2911F 17264 (9A1C 0Ch: 28E5C skips the F488 refusal); 1733D flipped F5DF (post_action),
     * 17344 1B503(1), 2E68D(SI 100, 972E, 0); 17375 -> 17386 RETF to 29124 (no F60A test there). */
    if (surface.boundary == XEEN_SURFACE_DAMAGE_SCENE) {
        return surface_scene(g, 1, SURFACE_REST_WAKE);
    }
    /* A fall at the wake (Levitate ended by the rest on a sky cell) returns to Rest's own end as well, not to the
     * movement's tail. Original: 1731B's 173A0 in Rest's 2911F 17264; RETF to 29124, not the 17D50 tail. */
    if (surface.boundary == XEEN_LOOP_FALL && g->movement.fall_caller == SURFACE_MOVEMENT) {
        g->movement.fall_caller = SURFACE_REST_WAKE;
    }
    if (surface.status != XEEN_OK) {
        return game_stop(g, surface.boundary);
    }
    return rest_scene(g, 2);
}
/* A Rest scene pass has returned. After a step: a fight that ended in the exploration mode ends the rest with the
 * UI mode restored and nothing else (the members stay asleep, unfed and unhealed); otherwise the
 * next step, or the rest's time after the tenth. After the last scene: "8 hours pass. Rest complete." with the
 * healing or starving line and the food eaten, in a message scroll.
 * Original: 28F00; the 28F16 charge; 2912C: 14BBA into A827, 18F2F(A827, 1). */
xeen_status rest_after_scene(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int stage = g->rest_stage, length;
    char text[1016];
    g->rest_stage = 0;
    if (stage == 1) {
        if (ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_EXPLORATION) {
            ds[XEEN_DS_UI_MODE] = g->rest_mode;
            return command_tail(g);
        }
        if (++g->rest_step < 10u) {
            return rest_step(g);
        }
        return rest_finish(g, xeen_read16(m, XEEN_DS_OUTDOORS) ? 380u : 470u);
    }
    length = snprintf((char *)ds + XEEN_DS_TEXT_BUFFER, XEEN_TEXT_BUFFER_BYTES, ds_text(g, XEEN_TEXT_REST_DONE),
                      ds_text(g, g->rest_starving ? XEEN_TEXT_REST_STARVING : XEEN_TEXT_REST_RESTORED),
                      (int)g->rest_food);
    if (length < 0 || length >= 1000) {
        return game_stop(g, REST_STOP_DONE_TEXT);
    }
    (void)snprintf(text, sizeof text, ds_text(g, XEEN_DS_MESSAGE_FORMAT), (const char *)ds + XEEN_DS_TEXT_BUFFER);
    if (!open_scroll_message(g, SCROLL_REST_DONE, text)) {
        return game_stop(g, REST_STOP_DONE_SCROLL);
    }
    return XEEN_SERVICE_REQUIRED;
}
/* The party goes to sleep: every member asleep, the bar redrawn, the UI mode kept and set to Rest's; then the ten
 * steps, or under Super Shelter the eight hours at once. Reached directly or after "yes" to the risk question, which
 * does not repeat the checks. Original: 28EBE (+14Bh, 2ABB8(1), 9A1C = 5; 2FC7E(480) in mode 0Ch); 2937A. */
xeen_status rest_sleep(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned i;
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        ds[XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES +
           XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ASLEEP)] = 1;
    }
    if (g->presentation) {
        (void)xeen_world_entry_party_bar(m, g->presentation,
                                         1); /* 28ED7 2ABB8(1), the asleep faces */
    }
    g->rest_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_REST;
    if (g->rest_mode == XEEN_UI_MODE_SHELTER) {
        return rest_finish(g, 480u);
    }
    g->rest_step = 0;
    return rest_step(g);
}
/* R, Rest (and Super Shelter's rest): on a cell or map where resting is forbidden, unless under Super Shelter,
 * "Too dangerous to rest here!" and nothing else. If any member (in any condition, the dead included) has a stat
 * at 0, the party is asked once "Some Chars may die. Rest anyway?" ("no" changes nothing). Then the party sleeps
 * (rest_sleep). Original: 17E91 -> 28E35; 1C844 (F488), C0D6 bit 4000h, mode 0Ch; 18F2F (DS 589C); 2BD94 for stats
 * 0..6; 2937A(DS:58B8, 0); 29164. */
xeen_status rest(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned i, k;
    if (!play_hosted(g)) {
        return game_stop(g, REST_STOP_NO_HOST);
    }
    if (!g->shelter) {
        xeen_write16(m, XEEN_DS_INPUT_CODE,
                     0x52u); /* the R key of 17E91 (28E35 does not write it) */
    }
    if (xeen_metadata(m) != XEEN_OK) {
        return game_stop(g, REST_STOP_CELL_METADATA);
    }
    if ((xeen_read16(m, XEEN_DS_CELL_NO_REST) || (xeen_read16(m, XEEN_DS_MAP_RESTRICTIONS) & XEEN_MAP_NO_REST)) &&
        ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_SHELTER) {
        if (!open_scroll_message(g, SCROLL_REST_DANGER, "\003c\v010\t000Too dangerous to rest here!")) {
            return game_stop(g, REST_STOP_DANGER_SCROLL);
        }
        return XEEN_SERVICE_REQUIRED;
    }
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        for (k = 0; k < 7u; ++k) {
            if (xeen_combat_stat(m, ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES, k, 0) <
                1u) {
                return open_confirm(g, CONFIRM_REST, XEEN_TEXT_REST_CONFIRM) ? XEEN_SERVICE_REQUIRED
                                                   : game_stop(g, REST_STOP_CONFIRM);
            }
        }
    }
    return rest_sleep(g);
}
