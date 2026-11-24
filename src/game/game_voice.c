/*
 * The CD and voice services (Phase 248, from game.c): the session's calls into the audio models (src/audio/) at the
 * game's BIOS clock: 483C7's CD voice play, 47F3D's drive status and the talking mouth it decides, 47B7D's load
 * hooks around 188BB/188F6/18931/18975, 187E9's voice clock, query and stop, and the VOC kept from 18975 for its
 * 187E9 start.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* 483C7, an NPC's CD voice, at the game's BIOS clock (the play's tick): xeen_native_audio_cd_voice. */
void cd_voice_start(xeen_game *g)
{
    g->audio.cd_now = g->timer.clock;                      /* The play's tick */
    xeen_native_audio_cd_voice(&g->audio, g->movement.ds);
}
/* The CD drive's status as the original's caller at SITE reads it (the speech loop, the face form, a location's
 * frame, the song), at the game's BIOS clock (the last INT 1Ah reading).
 * Original: 47F3D (SITE: 30454, 2FB31, 26D7B, 18ED9). */
int cd_status(xeen_game *g, uint32_t site)
{
    g->audio.cd_now = g->timer.clock;
    return xeen_native_audio_cd_status(&g->audio, site);
}
/* The CD's check before a file load (and its replay after it, in the audio library), at the game's BIOS clock.
 * Original: 47B7D before each load wrapper 188BB, 188F6, 18931, 18975; their FB06 replays (src/audio/audio.c). */
void cd_preload(xeen_game *g)
{
    g->audio.cd_now = g->timer.clock;
    xeen_native_audio_cd_preload(&g->audio, g->movement.ds);
}
/**
 * Tells the CD-audio model that a resource load finished (the 47B7D postload step) at the current
 * timer tick.
 */
void cd_postload(xeen_game *g)
{
    g->audio.cd_now = g->timer.clock;
    xeen_native_audio_cd_postload(&g->audio, g->movement.ds);
}
/**
 * Returns the talking-mouth animation frame of a CD voice line: a random 0..3 while the drive
 * reports the voice playing at SITE, else 0.
 */
uint16_t cd_mouth(xeen_game *g, uint32_t site)
{
    return cd_status(g, site) ? location_random(g, 0, 3) : 0u;
}
/* The voice model's clock: the host's BIOS clock read (when it gives one) for the voice queries and starts. Without
 * the model nothing is read, so the historical replays' timer state and step counts stay as they were.
 * Original: 187E9 through the digital driver's model (src/audio/audio.c). */
void voice_clock(xeen_game *g)
{
    if (!g->audio.voice_model) {
        return;
    }
    if (g->clock_bios) {
        (void)xeen_timer_peek(&g->timer, g->clock_bios, g->clock_user);
    }
    g->audio.voice_now = g->timer.clock;
}
/* Whether the voice still plays, as the original's caller at SITE asks it (always 0 without the voice model).
 * Original: 187E9(1, 0) from SITE (its return address). */
int voice_busy(xeen_game *g, uint32_t site)
{
    if (!g->audio.voice_model) {
        return 0;
    }
    voice_clock(g);
    return xeen_native_audio_voice_query(&g->audio, &g->movement, site);
}
/* 187E9(0, 0): the voice stops (the playback sink too); the exit event kept. */
/** Stops the current voice at SITE (records the event and the voice clock first). */
void voice_cancel(xeen_game *g, uint32_t site)
{
    exit_event(g, "voice stop");
    voice_clock(g);
    xeen_native_audio_voice_stop(game_audio(g), &g->movement, site);
}
/* Keeps a copy of the voice's N bytes B for its later start (voc_start), replacing the last one; without memory no
 * voice is kept. Original: 18975 into F42C, for 187E9. */
void voc_keep(xeen_game *g, const uint8_t *b, size_t n)
{
    uint8_t *copy = bytes_copy(b, n);
    if (!copy) {
        g->voc_kept = 0;
        return;
    }
    free(g->voc_bytes);
    g->voc_bytes = copy;
    g->voc_size = n;
    g->voc_kept = 1;
}
/* Starts the kept voice, as the original's caller at SITE does (both of the driver's start arguments play it);
 * nothing without one. Original: 187E9(F42C, x). */
void voc_start(xeen_game *g, uint32_t site)
{
    if (!g->voc_kept || !g->voc_bytes) {
        return;
    }
    voice_clock(g);
    xeen_native_audio_voice_at(game_audio(g), &g->movement,
                               g->voc_bytes, g->voc_size, site);
}
