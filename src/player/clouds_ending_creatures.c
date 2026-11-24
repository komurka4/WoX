/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Compose and play the creature parade between royal dialogue and score display. The parade
 * entry points use runtime, the creature table and shared sky banks; legacy playback retains
 * its explicit diagnostic stop guard.
 */
#include "clouds_ending_creatures.h"
#include "player_host.h"
#include "clouds_ending.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned jitter;
    unsigned sprite;
    const char *voice;
} Creature;

static const Creature creatures[73] = {
    {0, 0, "Slime.voc"},     {1, 1, "Bat.voc"},       {0, 2, "noise.voc"},
    {0, 3, "hiss.voc"},      {0, 4, "web.voc"},       {0, 5, "unnh.voc"},
    {0, 6, "Orc.voc"},       {0, 7, "ooh.voc"},       {0, 8, "Skeleton.voc"},
    {0, 9, "laff3d2.voc"},   {0, 10, "spell001.voc"}, {0, 11, "tiger1.voc"},
    {0, 12, "Begger.voc"},   {0, 13, "ribbit.voc"},   {0, 76, "spell002.voc"},
    {1, 23, "insects.voc"},  {0, 16, "snakeman.voc"}, {0, 17, "electric.voc"},
    {0, 80, "whoosh2.voc"},  {0, 19, "Scorpion.voc"}, {0, 20, "Harpy.voc"},
    {1, 83, "boltelec.voc"}, {1, 22, "Sprite.voc"},   {0, 24, "whaa-kee.voc"},
    {0, 25, "gazong1.voc"},  {1, 26, "punchooh.voc"}, {0, 27, "Ghoul.voc"},
    {0, 28, "splash3.voc"},  {0, 29, "yaa.voc"},      {0, 30, "snakeman.voc"},
    {0, 31, "zonga1.voc"},   {0, 32, "ogre.voc"},     {1, 33, "Phantom.voc"},
    {0, 34, "tree.voc"},     {0, 35, "huah.voc"},     {0, 36, "hit1.voc"},
    {0, 37, "monsterb.voc"}, {0, 38, "nyaahh.voc"},   {0, 39, "gargrwl.voc"},
    {0, 40, "bang.voc"},     {1, 42, "photon.voc"},   {0, 43, "bow.voc"},
    {0, 44, "fx7.voc"},      {0, 45, "swipe1.voc"},   {0, 84, "nyaahh.voc"},
    {0, 47, "siktroll.voc"}, {0, 48, "monstera.voc"}, {1, 49, "zombie.voc"},
    {0, 50, "elecbolt.voc"}, {1, 51, "scream.voc"},   {0, 52, "barkl.voc"},
    {0, 53, "fx7.voc"},      {0, 55, "stonegol.voc"}, {0, 56, "fxa.voc"},
    {0, 57, "whip.voc"},     {0, 58, "bong.voc"},     {0, 59, "splash2.voc"},
    {1, 60, "windblow.voc"}, {0, 61, "explode1.voc"}, {0, 62, "beam1.voc"},
    {0, 63, "elecspel.voc"}, {1, 64, "boom1.voc"},    {0, 65, "fire.voc"},
    {1, 66, "Roc.voc"},      {0, 67, "pop4.voc"},     {0, 68, "Dragon.voc"},
    {1, 70, "fx2.voc"},      {0, 71, "blob.voc"},     {0, 72, "gleen.voc"},
    {0, 73, "slimebos.voc"}, {0, 75, "ooh.voc"},      {0, 88, "crash2a.voc"},
    {1, 89, "Xeen.voc"},
};
static const int jitter_x[8] = {-2, -1, 0, 1, 2, 1, 0, -1};
static const int jitter_y[8] = {-2, 0, 2, 0, -1, 0, 2, 0};

/* Load and decode the named speech asset from the borrowed archive into the supplied voice
 * slot. Temporary archive bytes are freed before return; decoded samples belong to the slot
 * and must be released with voice_free. Returns 1 on success or 0 on read or decode failure.
 */
static int clouds_ending_creatures_load_voice(CC *archive, const char *name, Voice *voice)
{
    Blob resource = {0};
    int ok = 0;
    voice_free(voice);
    if (cine_load(archive, name, &resource)) {
        ok = player_voc_decode(&resource, voice);
    }
    free(resource.data);
    return ok;
}

/* Advance the cinematic selector and choose a creature transformation from zero through five.
 * Borrows and updates clock and selector state; returns zero on clock failure as well as a
 * legitimate zero choice, with the failure retained in the cinematic.
 */
static unsigned clouds_ending_creatures_select_power(Cinematic *cinematic,
                                                     uint16_t previous_accumulator)
{
    uint16_t time_word_high;
    uint16_t time_word_low;
    if (cinematic->approximate) {
        if (!cine_dos_time(cinematic, previous_accumulator, &time_word_high, &time_word_low)) {
            return 0;
        }
    } else {
        queen_clock(cinematic->clock_origin, cinematic->now - cinematic->clock_start,
                    &time_word_high, &time_word_low);
    }
    return queen_select_range(&cinematic->selector,
                              cinematic->approximate ? previous_accumulator : 0x2c00,
                              time_word_high, time_word_low, 0, 5);
}

/* Compose one creature-parade frame with the moving sky, mirror, royal figures and optional
 * jitter. Borrows all art and labels and updates caller sky and jitter state. Returns 1 after
 * a successful present or 0 on a draw, retrace, present or global-abort stop.
 */
static int clouds_ending_creatures_show(Cinematic *cinematic, const Blob *back, const Blob *mirror,
                                        const Blob *royal, const Blob *monster, unsigned frame,
                                        int jitter, unsigned *phase, unsigned *sky, unsigned scene,
                                        const char *kind, unsigned serial)
{
    int x = 31;
    int y = 10;
    char label[96];
    if (scene >= 73 || *sky >= 320 ||
        (!strcmp(kind, "idle")     ? frame >= 8
         : !strcmp(kind, "attack") ? frame >= 3
         : !strcmp(kind, "hold")   ? frame != 2
         : !strcmp(kind, "power")  ? frame != 3
                                   : 1)) {
        return 0;
    }
    if (cinematic->approximate) {
        uint32_t site = !strcmp(kind, "idle")     ? 0x16b47
                        : !strcmp(kind, "attack") ? 0x16c64
                        : !strcmp(kind, "hold")   ? 0x16d67
                                                  : 0x16ead;
        cine_coverage_state(cinematic, site, kind, scene * 8 + frame, *sky);
    }
    cine_restore_horizontal(cinematic, *sky);
    *sky = (*sky + 1) % 320;
    *phase = (*phase + 1) % 8;
    if (jitter) {
        x += jitter_x[*phase];
        y += jitter_y[*phase];
    }
    if (!cine_draw_at(cinematic, back, 0, 0, 0, 0) ||
        !cine_draw_at(cinematic, monster, frame, x, y, 0) ||
        !cine_draw_at(cinematic, mirror, 0, 0, 0, 0) ||
        !cine_draw_effect(cinematic, royal, 0, 0, 85, 0x800) ||
        !cine_draw_effect(cinematic, royal, 1, 220, 85, 0x800)) {
        return 0;
    }
    snprintf(label, sizeof(label), "clouds-ending-creature-%02u-%s-%04u", scene, kind, serial);
    /* Ordinary keys, including Escape, are ignored during the parade.
     * Global abort remains available.
     */
    if (!cine_retrace(cinematic) || !cine_present(cinematic, label, label)) {
        return 0;
    }
    if (!clouds_ending_poll(cinematic)) {
        cinematic->aborted = 1;
        return 0;
    }
    return 1;
}

/* Check whether the supplied creature loop tuple belongs to the legacy allowed prefix. Borrows
 * the cinematic and kind label; returns 1 for an allowed tuple or 0 with a diagnostic for a
 * tuple outside the table. This guard does not draw or advance time.
 */
static int clouds_ending_creatures_check_legacy_tuple(Cinematic *cinematic, unsigned scene,
                                                      unsigned frame, unsigned sky,
                                                      const char *kind)
{
    static const unsigned poses[][2] = {
        {30, 0}, {31, 0}, {31, 1}, {32, 1}, {32, 2}, {33, 1}, {33, 2}, {33, 3}, {34, 2}, {34, 3},
        {34, 4}, {35, 2}, {35, 3}, {35, 4}, {35, 5}, {36, 3}, {36, 5}, {36, 6}, {37, 3}, {37, 4},
        {37, 6}, {37, 7}, {38, 4}, {38, 5}, {38, 6}, {39, 4}, {39, 5}, {39, 7}, {40, 5}, {40, 6},
        {41, 5}, {41, 6}, {42, 6}, {42, 7}, {43, 6}, {43, 7}, {44, 7}, {45, 7},
    };
    size_t index;
    if (scene == 0 && !strcmp(kind, "idle")) {
        for (index = 0; index < sizeof(poses) / sizeof(poses[0]); index++) {
            if (sky == poses[index][0] && frame == poses[index][1]) {
                return 1;
            }
        }
    }
    player_log("Clouds ending stops at source16b47 before scene%u/%s/frame%u/sky%u: no whole-image "
            "witness",
            scene, kind, frame, sky);
    cinematic->evidence_stop =
        "16b47: first idle creature tuple lacks an individual original witness";
    cinematic->result = XEEN_FAILED;
    return 0;
}

/* Play up to limit entries of the creature parade, optionally enforcing the legacy tuple
 * table. Borrows the cinematic and archive and updates the caller's sky offset. Frees all
 * local assets; returns 1 on completion or 0 on a failed operation, input stop or rejected
 * tuple.
 */
static int clouds_ending_creatures_creatures_play_checked(Cinematic *cinematic, CC *archive,
                                                          unsigned *sky_offset, unsigned limit,
                                                          int checked)
{
    Blob back = {0};
    Blob mirror = {0};
    Blob royal = {0};
    Blob mon = {0};
    Blob attack = {0};
    Voice voice = {0};
    Voice power = {0};
    unsigned scene;
    unsigned frame;
    unsigned phase = 0;
    unsigned sky = *sky_offset;
    unsigned serial;
    uint16_t elapsed;
    int ok = 0;
    char name[96];

    if (!cine_load(archive, "mirrback.end", &back) || !cine_load(archive, "mirror.end", &mirror) ||
        !cine_load(archive, "kingcord.end", &royal)) {
        goto done;
    }
    for (scene = 0; scene < limit && scene < 73; scene++) {
        const Creature *c = &creatures[scene];
        snprintf(name, sizeof(name), "%03u.mon", c->sprite);
        if (!cine_load(archive, name, &mon)) {
            goto done;
        }
        snprintf(name, sizeof(name), "%03u.att", c->sprite);
        if (!cine_load(archive, name, &attack)) {
            goto done;
        }
        if (!clouds_ending_creatures_load_voice(archive, c->voice, &voice)) {
            goto done;
        }
        serial = 0;
        for (frame = 0; frame < 8; frame++) {
            cine_reset(cinematic);
            do {
                if ((checked && !clouds_ending_creatures_check_legacy_tuple(cinematic, scene, frame,
                                                                            sky, "idle")) ||
                    !clouds_ending_creatures_show(cinematic, &back, &mirror, &royal, &mon, frame,
                                                  c->jitter, &phase, &sky, scene, "idle",
                                                  serial++)) {
                    goto done;
                }
            } while (!cine_elapsed(cinematic));
        }
        for (frame = 0; frame < 3; frame++) {
            if (frame == 2 && (!cinematic->subtitles)) {
                cine_voice(cinematic, &voice, c->voice);
            }
            cine_reset(cinematic);
            do {
                if ((checked && !clouds_ending_creatures_check_legacy_tuple(cinematic, scene, frame,
                                                                            sky, "attack")) ||
                    !clouds_ending_creatures_show(cinematic, &back, &mirror, &royal, &attack, frame,
                                                  c->jitter, &phase, &sky, scene, "attack",
                                                  serial++)) {
                    goto done;
                }
            } while (!cine_elapsed(cinematic));
        }
        cine_reset(cinematic);
        do {
            if ((checked &&
                 !clouds_ending_creatures_check_legacy_tuple(cinematic, scene, 2, sky, "hold")) ||
                !clouds_ending_creatures_show(cinematic, &back, &mirror, &royal, &attack, 2,
                                              c->jitter, &phase, &sky, scene, "hold", serial++)) {
                goto done;
            }
            elapsed = (uint16_t)cine_elapsed(cinematic);
        } while (cinematic->approximate ? (int16_t)elapsed < 15 : elapsed < 15);
        snprintf(
            name, sizeof(name), "pow%u.voc",
            clouds_ending_creatures_select_power(cinematic, (uint16_t)(0x2c00 | (elapsed & 255))));
        if (!clouds_ending_creatures_load_voice(archive, name, &power)) {
            goto done;
        }
        if (!cinematic->subtitles) {
            cine_voice(cinematic, &power, name);
        }
        cine_reset(cinematic);
        do {
            if ((checked &&
                 !clouds_ending_creatures_check_legacy_tuple(cinematic, scene, 3, sky, "power")) ||
                !clouds_ending_creatures_show(cinematic, &back, &mirror, &royal, &attack, 3,
                                              c->jitter, &phase, &sky, scene, "power", serial++)) {
                goto done;
            }
        } while (cine_elapsed_before(cinematic, 7));
        free(mon.data);
        mon.data = NULL;
        free(attack.data);
        attack.data = NULL;
    }
    *sky_offset = sky;
    cine_record(cinematic, "clouds-ending-creatures-boundary-16ed0");
    ok = 1;
done:
    free(back.data);
    free(mirror.data);
    free(royal.data);
    free(mon.data);
    free(attack.data);
    voice_free(&voice);
    voice_free(&power);
    return ok;
}

/* Play the requested number of creature-parade entries without the legacy tuple restriction.
 * Borrows the cinematic and archive and updates the caller's sky offset; score is retained for
 * the include-based test interface. Returns 1 on completion or 0 on a failed operation or
 * input stop.
 */
static int clouds_ending_creatures_creatures_play(Cinematic *cinematic, CC *archive, uint32_t score,
                                                  unsigned *sky_offset, unsigned limit)
{
    (void)score;
    return clouds_ending_creatures_creatures_play_checked(cinematic, archive, sky_offset, limit, 0);
}

/* Play the creature parade using the legacy tuple restriction. Borrows the cinematic and
 * archive and updates the caller's sky offset; the score argument is retained for interface
 * compatibility. Releases temporary assets and returns 1 on completion or 0 on a failed
 * operation, input stop or disallowed tuple.
 */
int clouds_ending_creatures_legacy_prefix(Cinematic *cinematic, CC *archive, uint32_t score,
                                          unsigned *sky_offset)
{
    (void)score;
    return clouds_ending_creatures_creatures_play_checked(cinematic, archive, sky_offset, 73, 1);
}

/* Play the full creature parade against the moving sky and mirror. Borrows the cinematic and
 * archive, retains the score argument for interface compatibility, and updates the caller's
 * sky offset. Frees temporary assets; returns 1 on completion or 0 on failure or input stop.
 */
int clouds_ending_creatures(Cinematic *cinematic, CC *archive, uint32_t score, unsigned *sky_offset)
{
    return clouds_ending_creatures_creatures_play(cinematic, archive, score, sky_offset, 73);
}
