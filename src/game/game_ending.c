#include "game_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Where native play stops when the hand-off to the movie player cannot be prepared; the value is the original
 * address of the step (tools/xeen_replay/stops.json). */
enum {
    HANDOFF_STOP_PROGRAM_NAME = 0x16241      /* the program's name does not fit */
};

/* The XEEN.DAT side of the endings and of the program's exit to
 * XEEN.EXE (xeen_dat_evidence/phase127/PREFLIGHT.md section 2): the session
 * ends at 123A1 with the hand-off record (xeen_game_handoff); the host then
 * plays XEEN.EXE's movies in process (src/player) and starts a new session. */

/* The ending's score into *SCORE: the members' total experience summed (32 bits), divided (signed) by the party's size
 * and by 10,000, times 100,000; plus the play time (the clock now, less the session's start, plus the saved seconds)
 * as hours x 100 plus minutes (16-bit products: the original's IMUL and IDIV), that sum 16-bit and sign-extended
 * (33618 ADD, CWD). Returns -1 when the host has no clock, else 0. Original: 28C76, 10752, 1065A; 19C3D. */
static int ending_score(xeen_game *g, const uint8_t *ds, uint32_t *score)
{
    xeen_movement *m = &g->movement;
    xeen_dos_clock now;
    uint32_t sum = 0, t;
    uint16_t hours, part, sum16;
    int32_t add;
    unsigned i;
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        sum += xeen_level_experience(m, ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * i);
    }
    *score = (uint32_t)((int32_t)sum / (int32_t)ds[XEEN_DS_PARTY_COUNT]);
    *score = (uint32_t)((int32_t)*score / 10000);
    *score *= 100000u;
    if (!g->save_files->clock(g->save_files->user, &now)) {
        return -1;
    }
    t = xeen_dos_time(m, &now) - xeen_ds_read32(m, XEEN_DS_PLAY_CLOCK_START) + xeen_ds_read32(m, XEEN_DS_PLAY_SECONDS);
    part = (uint16_t)((int32_t)t % 3600);
    hours = (uint16_t)((int32_t)t / 3600);
    sum16 = (uint16_t)((uint16_t)(hours * 100u) + (uint16_t)(int16_t)((int16_t)part / 60));
    add = (int16_t)sum16; /* 33618 ADD, CWD */
    *score += (uint32_t)add;
    return 0;
}
/* The endings' shared tail (operations 30, 57 and 58 after their own parts, and operation 21's ending replays): the
 * configuration written to XEEN.CFG (its result not tested), the game saved without its window, sound and music
 * on; the score (ending_score), kept in F642/F644; then "XEENCOMMAND=<name>,<score>,0" built for
 * XEEN.EXE (the name from the DS word NAME_WORD: WORLDEND, ENDGAME2 or ENDGAME). Returns 0, or -1: a failed save
 * (ending_stop), a malformed name, no member, no clock, or a command format other than the original's.
 * Original: 33518: 14033/15574/13168 (30 bytes of 8D38), 29502(1), 50AB1(CFh) (33556: the music driver's fade, called
 * directly with no F430/ED9B test; Phase 285A: sent to the sink, so the song fades during the hand-off's screen fade
 * and voice wait until 1617E's 187A5(0)), ED9A = ED9B = 1; 28C76,
 * 10752, 1065A; 19C3D; F644:F642; 14BBA("XEENCOMMAND=%s,%010lu,%d", the name, S, CBW(BP-52) = 0) into AC0F, 145D4
 * putenv; 62D4 / 62D8 / 62DC. */
int ending_tail(xeen_game *g, uint16_t name_word, uint8_t *ds)
{
    xeen_movement *m = &g->movement;
    uint16_t off = xeen_read16(m, name_word);
    uint32_t score, stop;
    static const char format[] = "XEENCOMMAND=%s,%010lu,%d"; /* DS:6398 */
    size_t k = 0;
    char text[96];
    int n;
    while (k < 16u && ds[(uint16_t)(off + k)]) {
        ++k;
    }
    if (k == 16u || !ds[XEEN_DS_PARTY_COUNT] || !g->save_files || !g->save_files->clock ||
        strcmp(ds_text(g, XEEN_TEXT_ENDING_COMMAND), format)) {
        return -1;
    }
    g->handoff_cfg = g->save_files->write &&
                     g->save_files->write(g->save_files->user, "XEEN.CFG", ds + XEEN_DS_CONFIG, XEEN_CONFIG_BYTES);
    if ((stop = save_game(g)) != 0) {
        g->ending_stop = stop;
        return -1;
    } /* 29502(1): 2956A.. */
    ++g->saves;                 /* INT 21h 0Dh: platform */
    exit_event(g, "sound CFh");
    if (g->native_audio_enabled) {
        xeen_native_audio_music_driver(&g->audio, 0xcfu); /* 33556: 50AB1(CFh), ungated */
    }
    ds[XEEN_DS_SOUND_ON] = 1;
    ds[XEEN_DS_MUSIC_ON] = 1;
    if (ending_score(g, ds, &score) != 0) {
        return -1;
    }
    xeen_ds_write32(m, 0xf642, score);
    n = snprintf(text, sizeof text, format, (const char *)ds + off, (unsigned long)score, 0);
    if (n < 0 || (size_t)n >= sizeof text) {
        return -1;
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, text, (size_t)n + 1u);
    memcpy(g->handoff_command, text, (size_t)n + 1u);
    return 0;
}
/* The hand-off to the movie player XEEN.EXE (loop exit 1: an ending's tail, or the main menu's Other Options): the
 * screen faded, the voices' and the map texts' memory let go, a playing voice waited for (the session stops until it
 * ends), the file block, the music, the drivers' segments and every sprite let go, and the program's name kept for
 * the host; the session ends at the hand-off boundary (the host plays the movie in process). Stops when the name
 * does not fit.
 * Original: 1617E (16169's AX, F606, is 1: an ending's tail, or the main menu's
 * Other Options buttons through 16FAB): 4A75A(4) the fade; 18693 frees F386,
 * F388, the BP-2 copies of 9C6C and 9C68 (the DS words stay) and 9C60;
 * 187E9(1,0) until 0 (F42E 0: at once); 18693(F42C); 4FD02 (the timer:
 * platform); 187A5(0); 18827 (the digital driver: platform); 18693 of F430,
 * F42A, F42E; 4E231 (the mouse); 4AA3E(1); 4A4F8(1) (the video mode:
 * platform); then 16241: 123A1("xeen.exe", 0, 0), the XEEN.EXE hand-off. */
xeen_status handoff_teardown(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_presentation *p = g->presentation;
    const char *program = ds_text(g, XEEN_TEXT_MOVIE_PLAYER);
    if (g->exit_stage != EXIT_VOICE_WAIT) {
        if (p) {
            xeen_presentation_fade(p, 0, 4); /* 16180 4A75A(4); the fade (was a counter) */
        }
        xeen_write16(m, XEEN_DS_MISS_VOICE_SEGMENT, 0);
        xeen_write16(m, XEEN_DS_POW_VOICE_SEGMENT, 0);
        xeen_write16(m, XEEN_DS_MAP_TEXTS_SEGMENT, 0);
        exit_event(g, "voice");
    }
    /* 161D2 loops on 187E9(1, 0) until the voice has played. */
    if (voice_busy(g, HANDOFF_VOICE_WAIT_BOUNDARY)) {
        g->exit_stage = EXIT_VOICE_WAIT;
        g->phase = XG_STOPPED;
        g->lifecycle_boundary = HANDOFF_VOICE_WAIT_BOUNDARY;
        return XEEN_SERVICE_REQUIRED;
    }
    xeen_write16(m, XEEN_DS_FILE_BUFFER_SEGMENT, 0);
    exit_music_off(g);
    xeen_write16(m, 0xf430, 0); /* 18693(F430): the word cleared as freed (F430, F42A, F42E in that order) */
    xeen_write16(m, XEEN_DS_SONG_SEGMENT, 0);
    xeen_write16(m, 0xf42e, 0);
    (void)xeen_sprite_free_from(&g->sprites, 1);
    if (strlen(program) >= sizeof g->handoff_program) {
        return game_stop(g, HANDOFF_STOP_PROGRAM_NAME);
    }
    memcpy(g->handoff_program, program, strlen(program) + 1u);
    g->exit_stage = EXIT_FINISHED;
    return original_exit(g, XEEN_EXE_HANDOFF_BOUNDARY);
}
