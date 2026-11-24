/*
 * The menu session's construction and hand-off (Phase 248, from game_menu.c): the program start before the
 * 1660E main menu (the archives, the program image, the resident checks, the outer loop) that
 * xeen_game_create_menu runs; the host bindings of a menu session (its RNG, the add-ons, the save files, the menu
 * input); and the hand-off at the 19E82 map load of the loaded game (xeen_game_take_session,
 * xeen_game_session_free, xeen_game_create_loaded). The menu's own stages are game_menu.c's; the general
 * construction is game_lifecycle.c's.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The session RNG of a program-start menu game (the 18441 save id,
 * a new game's 41D20); a loaded session binds it again with its monsters. */
int xeen_game_bind_random(xeen_game *g, xeen_entity_random_fn random, void *user)
{
    if (!g || !random || g->lifecycle_boundary != MENU_ENTRY_BOUNDARY || g->acted) {
        return 0;
    }
    g->event_random = random;
    g->event_random_user = user;
    return 1;
}
void xeen_game_bind_addons(xeen_game *g, xeen_addons *addons)
{
    if (g) {
        g->addons = addons;
    }
}
int xeen_game_bind_save_files(xeen_game *g, const xeen_save_files *files)
{
    if (!g || !files || !files->list || !files->read) {
        return 0;
    }
    g->save_files = files;
    return 1;
}
/* Moves both worlds' CUR images from FROM to TO (TO's are not freed: it holds none), leaving FROM without any. */
static void cur_images_move(xeen_owned_bytes to[2], xeen_owned_bytes from[2])
{
    for (unsigned w = 0; w < 2u; ++w) {
        to[w] = from[w];
        from[w] = (xeen_owned_bytes){0};
    }
}
void xeen_game_session_free(xeen_game_session *s)
{
    if (!s) {
        return;
    }
    free(s->cur[0].bytes);
    free(s->cur[1].bytes);
    s->cur[0] = s->cur[1] = (xeen_owned_bytes){0};
    free(s->f42c);
    s->f42c = NULL;
    s->f42c_known = 0;
}
int xeen_game_take_session(xeen_game *g, xeen_game_session *s)
{
    if (!g || !s || !g->program_data || !g->session_ready || g->lifecycle_boundary != MENU_LOAD_BOUNDARY ||
        !g->cur[0].bytes || !g->cur[1].bytes) {
        return 0;
    }
    s->f42c_known = g->f42c_known;
    s->f42c = NULL;
    if (s->f42c_known && !(s->f42c = malloc(s->f42c_known))) {
        return 0;
    }
    if (s->f42c) {
        memcpy(s->f42c, g->f42c, s->f42c_known);
    }
    memcpy(s->ds, g->movement.ds, sizeof s->ds);
    cur_images_move(s->cur, g->cur);
    s->slot = g->movement.ds[XEEN_DS_GAME_SLOT];
    g->session_ready = 0;
    g->menu_stage = 0;
    (void)original_exit(g, MENU_LOAD_BOUNDARY);
    return 1;
}
/* A session construction that failed: the game G (NULL when there is none yet) destroyed and ERROR set to WHY unless
 * WHY is NULL (a stage that wrote its own message). Returns NULL. */
static xeen_game *startup_failure(xeen_game *g, const char *why, char *error, size_t error_size)
{
    xeen_game_destroy(g);
    if (why && error && error_size) {
        (void)snprintf(error, error_size, "%s", why);
    }
    return NULL;
}
/* Where a loaded session starts: the world of the session's DS (94: Darkside when nonzero) and the party's position,
 * facing and map there. */
static xeen_game_start loaded_start(const xeen_game_session *s)
{
    return (xeen_game_start){(uint8_t)(s->ds[XEEN_DS_WORLD] ? 1u : 0u), s->ds[XEEN_DS_PARTY_X], s->ds[XEEN_DS_PARTY_Y],
                             s->ds[XEEN_DS_PARTY_DIRECTION], s->ds[XEEN_DS_PARTY_MAP]};
}
/* What the new session G takes over from the loaded session S once its map is loaded: both worlds' CUR images (S is
 * left without), the F42C bytes when they fit, and the state of a game in play (program data, lifecycle, restored
 * party, loaded session). */
static void adopt_loaded_session(xeen_game *g, xeen_game_session *s)
{
    cur_images_move(g->cur, s->cur);
    if (s->f42c && s->f42c_known <= sizeof g->f42c) {
        memcpy(g->f42c, s->f42c, s->f42c_known);
        g->f42c_known = s->f42c_known;
    }
    g->program_data = 1;
    g->lifecycle = 1;
    g->restored = 1;
    g->loaded_session = 1;
}
/* Creates the session of a game loaded from the menu, at its map load: the map startup of the selected world's state
 * file writes over the carried DS (create_seeded); the party, header and runtime bytes already hold what the menu's
 * load wrote. The Darkside archive is required, the Clouds one optional (bound as the other world's when present).
 * Returns NULL (ERROR the reason) for an invalid loaded session or a refused start.
 * Original: 19E82 (after 19953 / 43DA5 / 1791F). */
xeen_game *xeen_game_create_loaded(xeen_game_session *s, const uint8_t *cloud_cc, size_t cloud_size,
                                   const uint8_t *dark_cc, size_t dark_size, char *error,
                                   size_t error_size)
{
    xeen_game *g;
    xeen_game_start start;
    const uint8_t *world_cc, *other_cc;
    size_t world_size, other_size;
    if (!s || !s->cur[0].bytes || !s->cur[1].bytes || !dark_cc) {
        return startup_failure(NULL, "invalid loaded session", error, error_size);
    }
    start = loaded_start(s);
    if (start.x > 31 || start.y > 31) {
        return startup_failure(NULL, "saved coordinates require unsupported map relocation before entry", error,
                               error_size);
    }
    world_cc = start.world ? dark_cc : cloud_cc;
    world_size = start.world ? dark_size : cloud_size;
    other_cc = start.world ? cloud_cc : dark_cc;
    other_size = start.world ? cloud_size : dark_size;
    if (!world_cc) {
        return startup_failure(NULL, "selected world archive unavailable", error, error_size);
    }
    g = create_seeded(s->cur[start.world].bytes, s->cur[start.world].size, world_cc, world_size, start, NULL, NULL,
                      s->ds, -1, error, error_size);
    if (!g) {
        return NULL;
    }
    if (other_cc && !xeen_game_bind_other_archive(g, other_cc, other_size)) {
        return startup_failure(g, "other-world archive rejected", error, error_size);
    }
    adopt_loaded_session(g, s);
    if (error && error_size) {
        error[0] = 0;
    }
    return g;
}
/* The menu session's archives: the Darkside CC (required) and the Clouds CC (optional: DS 95).
 * Returns NULL, or the reason it failed. */
static const char *startup_archives(xeen_game *g, const uint8_t *cloud_cc, size_t cloud_size,
                                    const uint8_t *dark_cc, size_t dark_size)
{
    if (!dark_cc || xeen_archive_open(&g->assets, dark_cc, dark_size) != XR_OK) {
        return "invalid archive";
    }
    if (cloud_cc) {
        if (xeen_archive_open(&g->other, cloud_cc, cloud_size) != XR_OK) {
            return "invalid archive";
        }
        g->other_bound = 1;
    }
    return NULL;
}
/* The program's first state: the movement, input and event services bound; the program data loaded; the
 * configuration's 29 bytes (XEEN.CFG, optional; the rest zero); the play clock's base from the DOS clock (0
 * without one); Clouds installed when both CCs are present; the sprite cache initialized. Returns NULL, or why the
 * start is refused.
 * Original: 15CDB..15D08: 89B9 = 0 (15CDF); 47C1F (8D38); 15CF3 19C29 time() into 98B0; DS 95 from 15D08
 * 19AEB("xeen.cc"); F606 = 0 (4A2C2 accepted). */
static const char *startup_program(xeen_game *g, int both_cc, const uint8_t *config,
                                   size_t config_size, const xeen_dos_clock *clock)
{
    xeen_movement *m = &g->movement;
    g->world = 1;
    xeen_movement_init(m, movement_service, g);
    xeen_input_poll_init(&g->input_poll, XEEN_DS_SEGMENT);
    xeen_event_ui_init(&g->ui, ui_present, ui_poll, g);
    if (!xeen_game_load_program_data(g)) {
        return "program data disagrees with the native startup state";
    }
    xeen_write16(m, 0x89b9, 0);
    memset(m->ds + XEEN_DS_CONFIG, 0, XEEN_CONFIG_READ_BYTES);
    if (config) {
        memcpy(m->ds + XEEN_DS_CONFIG, config,
               config_size < XEEN_CONFIG_READ_BYTES ? config_size : XEEN_CONFIG_READ_BYTES);
    }
    if (clock) {
        uint32_t now = xeen_dos_time(m, clock);
        xeen_write16(m, XEEN_DS_PLAY_CLOCK_START, (uint16_t)now);
        xeen_write16(m, XEEN_DS_PLAY_CLOCK_START + 2u, (uint16_t)(now >> 16));
    }
    m->ds[XEEN_DS_CLOUDS_INSTALLED] = (uint8_t)both_cc;
    xeen_write16(m, 0xf606, 0);
    xeen_sprite_cache_init(&g->sprites);
    return NULL;
}
/* The start-up comparison's flag: the configuration's bytes from 8D42 (at most 8) against the Clouds ending's mark
 * (the string XEEN_DS_CLOUDS_ENDING_MARK points at), case-insensitively (a..z folded to upper case): the difference of the first pair that differs, 0 when
 * they agree up to 8D42's NUL or the eighth character. Original: 14DA8. */
static uint8_t startup_compare(const xeen_movement *m)
{
    const uint8_t *a = m->ds + XEEN_DS_CONFIG_CLOUDS_ENDING, *b = m->ds + xeen_read16(m, XEEN_DS_CLOUDS_ENDING_MARK);
    unsigned k, ca = 0, cb = 0;
    for (k = 0; k < 8u; ++k) {
        ca = a[k];
        cb = b[k];
        if (!ca) {
            break;
        }
        if (ca >= 'a' && ca <= 'z') {
            ca -= 0x20u;
        }
        if (cb >= 'a' && cb <= 'z') {
            cb -= 0x20u;
        }
        if (ca != cb) {
            break;
        }
        ca = cb = 0;
    }
    return (uint8_t)(ca - cb);
}
/* The resident handles and start-up flags: the first seven sprite handles are the resident buffers' (their DS words
 * written in the original's order), the two driver segment words 0, the start-up comparison flag, music, sound and
 * the key buttons' feedback on; then mouse.icn loaded as the mouse's sprites (the sprite cache is live from here).
 * Returns NULL, or why the start is refused. Driver segments and memory checks are the platform's.
 * Original: 15D9B..15E0F: 9C4E, 8C90..8C86; F42E and F430 (specsnd, promus); 8C80 = 14DA8; ED9B, ED9A, 8C76; F460. */
static const char *startup_residents(xeen_game *g)
{
    static const uint16_t buffers[7] = {0x9c4e, 0x8c90, 0x8c8e, 0x8c8c, 0x8c8a, 0x8c88, XEEN_DS_RESIDENT_SPRITE_SLOT};
    xeen_movement *m = &g->movement;
    xeen_sprite_archives a;
    unsigned i;
    uint16_t h = 0;
    if (xeen_sprite_reserve_startup(&g->sprites, &h) != XR_OK) {
        return "startup resident handles";
    }
    for (i = 0; i < 7u; ++i) {
        xeen_write16(m, buffers[i], (uint16_t)(h + i));
    }
    xeen_write16(m, 0xf42e, 0);
    xeen_write16(m, 0xf430, 0);
    m->ds[XEEN_DS_CLOUDS_ENDING_UNMARKED] = startup_compare(m);
    m->ds[XEEN_DS_MUSIC_ON] = 1;
    m->ds[XEEN_DS_SOUND_ON] = 1;
    m->ds[XEEN_DS_KEY_BUTTON_FEEDBACK] = 1;
    a = sprite_archives(g, NULL);
    if (xeen_sprite_load(&g->sprites, &a, m->ds[XEEN_DS_WORLD], ds_text(g, XEEN_TEXT_MOUSE_SPRITES), 0, &h) != XR_OK) {
        return "mouse.icn unavailable";
    }
    xeen_write16(m, XEEN_DS_MOUSE_SPRITES, h);
    g->world_entered = 1; /* the sprite cache is live */
    return NULL;
}
/* The outer loop's start: the Darkside archive selected, the start-up song (newbrigh.m) loaded, the overloaded
 * F642/F644 word pair cleared; the session then stops at the main menu's entry (exit stage EXIT_AT_MENU), with the
 * lifecycle, the restored party and continuous play enabled. Returns NULL, or why the start is refused.
 * Original: 15F9C (F42A = newbrigh.m, F644:F642 = 0); then 1660E. */
static const char *startup_outer_loop(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_select_archive(m, 1);
    if (!menu_load_word(g, XEEN_DS_SONG_SEGMENT, ds_text(g, XEEN_TEXT_STARTUP_SONG))) {
        return "newbrigh.m unavailable";
    }
    xeen_write16(m, 0xf644, 0);
    xeen_write16(m, 0xf642, 0);
    g->restored = 1;
    g->lifecycle = 1;
    g->continuous = 1;
    g->phase = XG_STOPPED;
    g->exit_stage = EXIT_AT_MENU;
    g->lifecycle_boundary = MENU_ENTRY_BOUNDARY;
    g->command_status = XGC_ORIGINAL_EXIT;
    return NULL;
}
/* Program start, by stage, for both CCs: the archives, the program data and its services, the resident handles, the
 * outer loop's start; the session then waits at the main menu. The first stage that fails names the reason in ERROR
 * and the session is destroyed. Original: 15CDB..15F9C and the 15F9C outer loop to 1660E (DS 95 set). */
xeen_game *xeen_game_create_menu(const uint8_t *cloud_cc, size_t cloud_size, const uint8_t *dark_cc,
                                 size_t dark_size, const uint8_t *config, size_t config_size,
                                 const xeen_dos_clock *clock, char *error, size_t error_size)
{
    xeen_game *g = calloc(1, sizeof *g);
    const char *why = !g ? "allocation failed"
                         : startup_archives(g, cloud_cc, cloud_size, dark_cc, dark_size);
    if (!why) {
        why = startup_program(g, cloud_cc != NULL, config, config_size, clock);
    }
    if (!why) {
        why = startup_residents(g);
    }
    if (!why) {
        why = startup_outer_loop(g);
    }
    if (why) {
        return startup_failure(g, why, error, error_size);
    }
    if (error && error_size) {
        error[0] = 0;
    }
    return g;
}
/* The 1978C CUR image of a world in a loaded session (XEEN.CUR 0, DARK.CUR 1),
 * else NULL. In the menu and at its 19E82 handoff the in-memory CURs are the
 * menu's (a defeated session's live state archive is not). */
const uint8_t *xeen_game_cur(const xeen_game *g, unsigned world, size_t *size)
{
    if (!g || world > 1u || !g->cur[world].bytes) {
        return NULL;
    }
    if (world == g->world && g->state.bytes && !g->menu_stage && !g->session_ready) {
        if (size) {
            *size = g->state.size;
        }
        return g->state.bytes;
    }
    if (size) {
        *size = g->cur[world].size;
    }
    return g->cur[world].bytes;
}
/* A program-start menu game has no world entry: the frontend provider is
 * bound here (copied; its user stays borrowed until destroy). */
int xeen_game_bind_menu_input(xeen_game *g, const xeen_input_provider *provider)
{
    if (!g || !provider || !provider->keyboard || !provider->mouse ||
        g->lifecycle_boundary != MENU_ENTRY_BOUNDARY || g->acted) {
        return 0;
    }
    g->input_provider = *provider;
    return 1;
}
