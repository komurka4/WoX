/*
 * The host's evidence writers (Phase 242, from explore.c): the binary snapshots of a run (the CUR images,
 * the event store and the save directory's files, the driver's resident sprites, DS and frame dumps), the
 * evidence record line on stdout with the page audit it appends (XEEN_PAGE_AUDIT), and the refresh capture
 * of the displayed page (XEEN_REFRESH_CAPTURE). Observation only: they read the game through its public
 * API and the presentation, and never change the game, its clock, its RNG or its input queue.
 */
#define _POSIX_C_SOURCE 200809L
#include "host_evidence.h"
#include "host_files.h"
#include "xeen/byte_order.h"
#include "xeen/character_record.h"
#include "xeen/ds_globals.h"
#include "xeen/party.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Both worlds' in-memory CUR images, as PREFIX-TAG-xeen-cur.bin and PREFIX-TAG-dark-cur.bin (a world without one is
 * skipped; the replay harness's save-file adapter reads them by these names). 0 when a write fails. */
static int snapshot_cur_images(const char *prefix, const char *tag, xeen_game *g)
{
    char label[64];
    size_t n = 0;
    const uint8_t *b;
    unsigned w;
    for (w = 0; w < 2u; ++w) {
        if ((b = xeen_game_cur(g, w, &n)) != NULL) {
            snprintf(label, sizeof label, "%.24s-%s-cur", tag, w ? "dark" : "xeen");
            if (!host_write_snapshot(prefix, label, b, n)) {
                return 0;
            }
        }
    }
    return 1;
}
/* Each file of the save directory (its DOS names, at most 512) as TAG-savedir-NAME. 0 when a file cannot be read or
 * written. */
static int snapshot_save_dir(const char *prefix, const char *tag, const char *save_dir)
{
    static char names[512][13];
    unsigned count = host_directory_names(save_dir, names, 512), i;
    for (i = 0; i < count; ++i) {
        uint8_t *f;
        size_t n = 0;
        char path[1024], label[64];
        int ok;
        if (!host_dos_path(save_dir, names[i], path, sizeof path) || !(f = host_read_file(path, &n))) {
            return 0;
        }
        snprintf(label, sizeof label, "%.24s-savedir-%.12s", tag, names[i]);
        ok = host_write_snapshot(prefix, label, f, n);
        free(f);
        if (!ok) {
            return 0;
        }
    }
    return 1;
}
/* Evidence for the control panel's original-byte replay (with an evidence PREFIX; without one nothing): both
 * in-memory CUR images (snapshot_cur_images) and, when FULL (tag "commandN"), the event store (TAG-events) and the
 * save directory's files (snapshot_save_dir), each as PREFIX-TAG-....bin. 0 when a write fails. */
int host_snapshot_saves(const char *prefix, const char *tag, xeen_game *g,
                          const save_directory *d, int full)
{
    char label[64];
    size_t n = 0;
    const uint8_t *b;
    if (!prefix) {
        return 1;
    }
    if (!snapshot_cur_images(prefix, tag, g)) {
        return 0;
    }
    if (!full) {
        return 1;
    }
    if ((b = xeen_game_event_store(g, &n)) != NULL) {
        snprintf(label, sizeof label, "%.24s-events", tag);
        if (!host_write_snapshot(prefix, label, b, n)) {
            return 0;
        }
    }
    return !d->save_dir || snapshot_save_dir(prefix, tag, d->save_dir);
}
/**
 * Writes the N bytes B to the evidence file PREFIX-LABEL.bin when an evidence prefix is set (without
 * one it writes nothing and returns 1). Returns 0 when the path does not fit or the file cannot be
 * written.
 */
int host_write_snapshot(const char *prefix, const char *label, const uint8_t *b, size_t n)
{
    char path[1024];
    FILE *f;
    if (!prefix) {
        return 1;
    }
    {
        int written = snprintf(path, sizeof path, "%s-%s.bin", prefix, label);
        if (written < 0 || (size_t)written >= sizeof path) {
            return 0;
        }
    }
    f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    if (fwrite(b, 1, n, f) != n) {
        fclose(f);
        return 0;
    }
    return fclose(f) == 0;
}
/* Beside a start snapshot LABEL-before, the driver's resident
 * sprites as LABEL-sprites ("XSP1", the last handle, then per handle 1..last
 * its state, archive (0 xeen.cc, 1 dark.cc, 2 cur) and NUL-padded name in
 * 14 bytes): the original-byte replay places those resources behind its
 * handle table, as the EMS pages the driver maps for 4ADB1. */
int host_snapshot_sprites(const char *prefix, const char *before, const xeen_game *g)
{
    const xeen_sprite_cache *c = xeen_game_sprites(g);
    uint8_t b[6 + 16 * XEEN_SPRITE_SLOTS];
    char label[48];
    size_t n = 6, len;
    unsigned h;
    if (!prefix || !c) {
        return 1;
    }
    len = strlen(before);
    /* The label is BEFORE with its "-before" (7 characters) replaced by "-sprites" (8 and the NUL). */
    if (len < 7 || len - 7u + sizeof "-sprites" > sizeof label || strcmp(before + len - 7, "-before")) {
        return 0;
    }
    memcpy(label, before, len - 7);
    strcpy(label + len - 7, "-sprites");
    memcpy(b, "XSP1", 4);
    b[4] = (uint8_t)c->last;
    b[5] = (uint8_t)(c->last >> 8);
    for (h = 1; h <= c->last && h <= XEEN_SPRITE_SLOTS; h++, n += 16) {
        const xeen_sprite *e = &c->slots[h - 1u];
        memset(b + n, 0, 16);
        b[n] = e->state;
        b[n + 1] = e->archive;
        if (e->state == XSP_LOADED) {
            memcpy(b + n + 2, e->name, strlen(e->name) < 14 ? strlen(e->name) : 14);
        }
    }
    return host_write_snapshot(prefix, label, b, n);
}
/* Party hit points in roster order (character +156, signed). */
#define HP(i)                                                                           \
    ((int)(int16_t)xeen_get_le16(ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * (i) + \
                                 XEEN_CHARACTER_HP))
/* The audit file: opened at the first record (whatever it shows) when XEEN_PAGE_AUDIT names one; NULL while the audit
 * is off or after a failed write ended it. */
static FILE *page_audit_file;
static FILE *page_audit_open(void)
{
    static int opened;
    if (!opened) {
        const char *path = getenv("XEEN_PAGE_AUDIT");
        opened = 1;
        if (path) {
            page_audit_file = fopen(path, "wb");
        }
    }
    return page_audit_file;
}
/* Writes N bytes B as the next part of the record; a short write closes the file and ends the audit (returns 0). */
static int page_audit_write(const void *b, size_t n)
{
    if (fwrite(b, 1, n, page_audit_file) != n) {
        fclose(page_audit_file);
        page_audit_file = NULL;
        return 0;
    }
    return 1;
}
enum { PAGE_AUDIT_HEAD = 4 + 48 + 2 + 12 + 2 + 18 };
/* The record's head: "PGA1", the tag (at most 47 bytes, NUL-padded to 48), the displayed page, the Who-will flag,
 * the cursor's x, y, width, height and old x, y, the pending window, windows 28/39's x, y, width, height and their
 * valid bytes (little-endian words). */
static void page_audit_head(uint8_t head[PAGE_AUDIT_HEAD], const xeen_presentation *p, const char *tag, int who_pass)
{
    const uint16_t cursor[6] = {p->cursor_x, p->cursor_y, p->cursor_width, p->cursor_height, p->old_x, p->old_y};
    unsigned i;
    memset(head, 0, PAGE_AUDIT_HEAD);
    memcpy(head, "PGA1", 4);
    for (i = 0; i < 47u && tag[i]; ++i) {
        head[4 + i] = (uint8_t)tag[i];
    }
    head[52] = p->displayed_page;
    /* 1 at the Who-will poll: that pass presents only its prompt after its scene, so the scene's pixels stay
     * unpresented there, in the original as natively. Original: 2F9BA's 2FB8D, 50BF6(3) after 1B503(0). */
    head[53] = (uint8_t)(who_pass != 0);
    for (i = 0; i < 6u; ++i) {
        xeen_put_le16(head + 54 + 2 * i, cursor[i]);
    }
    xeen_put_le16(head + 66, p->dirty);
    for (i = 0; i < 8u; ++i) {
        xeen_put_le16(head + 68 + 2 * i, p->runtime_windows[i / 4u][i % 4u]);
    }
    head[84] = p->runtime_window_valid[0];
    head[85] = p->runtime_window_valid[1];
}
/* Marks in MASK (320 x 200) the cursor's opaque pixels drawn at (X0, Y0), clipped to the screen and to the opacity
 * bytes there are. */
static void mark_cursor(const xeen_presentation *p, unsigned x0, unsigned y0, uint8_t *mask)
{
    unsigned x, y;
    for (y = 0; y < p->cursor_height; ++y) {
        for (x = 0; x < p->cursor_width; ++x) {
            if (x0 + x < 320u && y0 + y < 200u && (size_t)y * p->cursor_width + x < p->cursor_size &&
                p->cursor_opacity[y * p->cursor_width + x]) {
                mask[(y0 + y) * 320u + x0 + x] = 1;
            }
        }
    }
}
/* The record's mask: 1 where an opaque cursor pixel lies at the cursor's position or its old one (none without an
 * opacity map). */
static const uint8_t *cursor_mask(const xeen_presentation *p)
{
    static uint8_t mask[XEEN_PRESENT_PIXELS];
    memset(mask, 0, sizeof mask);
    if (p->cursor_opacity) {
        mark_cursor(p, p->cursor_x, p->cursor_y, mask);
        mark_cursor(p, p->old_x, p->old_y, mask);
    }
    return mask;
}
/* evidence (display pages; presentation only, off by default): with
 * XEEN_PAGE_AUDIT=FILE every recorded boundary appends both display pages,
 * the work framebuffer, the displayed page, the cursor rectangles and the
 * palette ("PGA1", tag[48], u8 displayed, u8 Who-will pass, u16 cursor x,y,w,h, old x,y, pending, windows 28/39 x,y,w,h, valid[2],
 * palette[768], page0, page1, framebuffer, mask: 1 where an opaque cursor
 * pixel lies at either cursor position). A window closed on the work
 * buffer but left on a page shows as a page/framebuffer difference. */
static void page_audit(const xeen_presentation *p, const char *tag, int who_pass)
{
    uint8_t head[PAGE_AUDIT_HEAD];
    if (!page_audit_open() || !p || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS) {
        return;
    }
    page_audit_head(head, p, tag, who_pass);
    if (page_audit_write(head, sizeof head) && page_audit_write(p->palette, 768) &&
        page_audit_write(p->pages[0], XEEN_PRESENT_PIXELS) && page_audit_write(p->pages[1], XEEN_PRESENT_PIXELS) &&
        page_audit_write(p->framebuffer, XEEN_PRESENT_PIXELS) &&
        page_audit_write(cursor_mask(p), XEEN_PRESENT_PIXELS)) {
        fflush(page_audit_file);
    }
}
/* Phase 272: the state-summary records are a developer/test control (--diagnostic-state), off by default. */
static int diagnostic_state;
void host_set_diagnostic_state(int on)
{
    diagnostic_state = on != 0;
}
int host_diagnostic_state(void)
{
    return diagnostic_state;
}
/**
 * Prints one evidence record (tag, position, gold, RNG calls, audio and presentation counters) of
 * the current game state.
 */
void host_print_record(xeen_game *g, xeen_presentation *p, const char *tag, unsigned rng_calls)
{
    xeen_game_view v = xeen_game_inspect(g);
    if (!diagnostic_state) {
        page_audit(p, tag, v.lifecycle_boundary == XEEN_GAME_WHO_WILL_POLL);
        return;
    }
    const xeen_native_audio *audio = xeen_game_audio(g);
    const uint8_t *ds = xeen_game_ds(g);
    uint32_t gold = xeen_get_le32(ds + XEEN_DS_PARTY_GOLD), gems = xeen_get_le32(ds + XEEN_DS_PARTY_GEMS);
    unsigned flag239 = (unsigned)xeen_ds_get_bit(ds, XEEN_DS_GAME_FLAGS, 239u);
    printf("%s accepted=%lu map=%u world=%u pos=%u,%u facing=%u minute=%u scenes=%lu renderer=%llu "
           "idle=%lu boundary=%05x navigation=%d event=%d combat=%d attackers=%u,%u,%u monster=%u "
           "actor=%u combatcmd=%lu actions=%lu hit=%u damage=%u draws=%u monsteractions=%u "
           "targets=%u,%u monsterhits=%u,%u scores=%u,%u defenses=%u,%u monsterdamage=%u,%u "
           "playeractor=%u playerattacks=%u playerscore=%u playerthreshold=%u deaths=%u rewards=%u "
           "cleanup=%u tracecomplete=%u sfx=%lu/%u gold=%lu gems=%lu flag239=%u rng=%u rounds=%u "
           "scenes=%u blocks=%u hp=%d/%d/%d/%d/%d/%d shots=%lu volley=%u/%u/%u/%u volleyend=%05x\n",
           tag, v.accepted_commands, v.position.map, v.position.world, v.position.x, v.position.y,
           v.position.direction, v.clock, v.scene_returns, (unsigned long long)p->scene_submissions,
           v.idle_scenes, (unsigned)v.lifecycle_boundary, (int)v.navigation, (int)v.event_status,
           (int)v.combat_phase, v.combat_attackers[0], v.combat_attackers[1], v.combat_attackers[2],
           v.combat_monster_id, v.combat_active_actor, v.combat_commands, v.combat_actions,
           v.combat_last_hit, v.combat_last_damage, v.combat_rng_draws, v.combat_monster_actions,
           v.combat_monster_targets[0], v.combat_monster_targets[1], v.combat_monster_hits[0],
           v.combat_monster_hits[1], v.combat_monster_attack_scores[0],
           v.combat_monster_attack_scores[1], v.combat_monster_defenses[0],
           v.combat_monster_defenses[1], v.combat_monster_damage[0], v.combat_monster_damage[1],
           v.combat_last_player_actor, v.combat_player_attacks, v.combat_last_attack_score,
           v.combat_last_attack_threshold, v.combat_monster_deaths, v.combat_rewards_applied,
           v.combat_cleanup_complete, v.combat_rng_trace_complete, audio ? audio->effects : 0ul,
           audio ? audio->last_effect : 0u, (unsigned long)gold, (unsigned long)gems, flag239,
           rng_calls, v.combat_rounds, v.combat_scenes, v.combat_blocks, HP(0), HP(1), HP(2), HP(3),
           HP(4), HP(5), v.shots, v.volley_targets, v.volley_hits, v.volley_misses, v.volley_saves,
           (unsigned)v.volley_end);
    if (v.combat_rng_draws) {
        unsigned i, stored = v.combat_rng_draws < XEEN_COMBAT_RNG_CAPACITY
                                     ? v.combat_rng_draws
                                     : XEEN_COMBAT_RNG_CAPACITY;
        printf("combat-rng-sequence=");
        for (i = 0; i < stored; ++i) {
            printf("%s%u-%u:%u/%u", i ? "," : "", v.combat_rng_low[i], v.combat_rng_high[i],
                   v.combat_rng_result[i], v.combat_rng_raw[i]);
        }
        printf(" complete=%u total=%u\n", v.combat_rng_trace_complete, v.combat_rng_draws);
    }
    fflush(stdout);
    page_audit(p, tag, v.lifecycle_boundary == XEEN_GAME_WHO_WILL_POLL);
}
/* A page-dump series (the refresh capture, the shown-page capture): its directory (none: off), the file name stem,
 * the dumps written so far (at most PAGE_DUMP_LIMIT) and the last one, so that an unchanged page is not written
 * again. */
#define PAGE_DUMP_LIMIT 2000u
#define DAC_BYTES 768u /* 256 colours x R, G, B */
typedef struct {
    const char *dir;
    const char *stem;
    unsigned count;
    uint8_t last[XEEN_PRESENT_PIXELS + DAC_BYTES];
} page_dump;
static page_dump refresh_capture = {NULL, "refresh", 0, {0}};
static page_dump shown_capture = {NULL, "shown", 0, {0}};
/* Writes PAGE (XEEN_PRESENT_PIXELS bytes) and PALETTE (DAC_BYTES) as DIR/STEM-NNNNSUFFIX.bin and .pal unless D is
 * off, full or the pair equals the last one written; a file that cannot be written is skipped. */
static void page_dump_write(page_dump *d, const uint8_t *page, const uint8_t *palette, const char *suffix)
{
    uint8_t now[XEEN_PRESENT_PIXELS + DAC_BYTES];
    char name[1200];
    FILE *f;
    if (!d->dir) {
        return;
    }
    memcpy(now, page, XEEN_PRESENT_PIXELS);
    memcpy(now + XEEN_PRESENT_PIXELS, palette, DAC_BYTES);
    if (d->count >= PAGE_DUMP_LIMIT || (d->count && !memcmp(now, d->last, sizeof now))) {
        return;
    }
    memcpy(d->last, now, sizeof now);
    snprintf(name, sizeof name, "%s/%s-%04u%s.bin", d->dir, d->stem, d->count, suffix);
    if ((f = fopen(name, "wb"))) {
        (void)fwrite(now, 1, XEEN_PRESENT_PIXELS, f);
        fclose(f);
    }
    snprintf(name, sizeof name, "%s/%s-%04u%s.pal", d->dir, d->stem, d->count, suffix);
    if ((f = fopen(name, "wb"))) {
        (void)fwrite(now + XEEN_PRESENT_PIXELS, 1, DAC_BYTES, f);
        fclose(f);
    }
    ++d->count;
}
void host_refresh_capture_set(const char *dir)
{
    refresh_capture.dir = dir;
}
/**
 * Writes the displayed page and its palette to the refresh-dump directory, named by BOUNDARY (a
 * diagnostic); nothing while no directory is set.
 */
void host_refresh_dump(xeen_presentation *p, uint32_t boundary)
{
    char suffix[16];
    snprintf(suffix, sizeof suffix, "-%05x", (unsigned)boundary);
    page_dump_write(&refresh_capture, p->pages[p->displayed_page], xeen_presentation_dac(p), suffix);
}
void host_shown_capture_set(const char *dir)
{
    shown_capture.dir = dir;
}
int host_shown_capture_on(void)
{
    return shown_capture.dir != NULL;
}
const uint8_t *host_shown_page(const uint8_t *game_page, const uint8_t *shown, const uint8_t *palette)
{
    if (shown_capture.dir) {
        page_dump_write(&shown_capture, shown, palette, memcmp(game_page, shown, XEEN_PRESENT_PIXELS) ? "-overlay" : "");
    }
    return shown;
}
