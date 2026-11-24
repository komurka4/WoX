/*
 * The session's archive and DS-text accessors (Phase 248, from game_menu.c, where the main menu's loads
 * first needed them): a DS string, the CC that DS 94 selects (or a given world's), the 507F0 extraction (XOR
 * 35h), the 18975 load (DS 8582 = its size) and its DS-word form, and the 188F6 raw 320x200 picture. Every
 * session unit reads its resources through them; a map's resources are game_resources.c's.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char *ds_text(const xeen_game *g, uint16_t offset)
{
    return (const char *)g->movement.ds + offset;
}
/* Whether C ends a printf conversion specification (the conversions the program data's formats use). */
static int conversion_letter(char c)
{
    return c && strchr("diouxXcs", c) != NULL;
}
const char *ds_format(const xeen_game *g, uint16_t offset, const char *conversions)
{
    const char *f = ds_text(g, offset);
    size_t room = sizeof g->movement.ds - offset, i = 0, k = 0;
    char got[32];
    while (i < room && f[i]) {
        if (f[i] != '%') {
            ++i;
        } else if (i + 1u < room && f[i + 1u] == '%') {
            i += 2u;
        } else {
            do { /* the '%', then its flags, width and length up to the conversion */
                if (k + 2u > sizeof got) {
                    return NULL;
                }
                got[k++] = f[i++];
            } while (i < room && f[i] && !conversion_letter(f[i]));
            if (i >= room || !conversion_letter(f[i])) {
                return NULL;
            }
            got[k++] = f[i++];
        }
    }
    if (i >= room) {
        return NULL; /* no NUL before the end of DS */
    }
    got[k] = 0;
    return strcmp(got, conversions) ? NULL : f;
}
void ds_put_text(xeen_game *g, uint16_t offset, const char *text)
{
    memcpy(g->movement.ds + offset, text, strlen(text) + 1u);
}
/* The archive of world W (0 XEEN.CC, 1 DARK.CC): the bound one when W is the game's world, the other world's
 * when it is bound, else NULL. */
static const xeen_archive *world_cc(const xeen_game *g, unsigned w)
{
    return w == g->world ? &g->assets : (g->other_bound ? &g->other : NULL);
}
/**
 * Returns the archive of the world in DS 94: the bound one when it is the game's world, the other
 * world's when it is bound, else NULL.
 */
const xeen_archive *selected_cc(const xeen_game *g)
{
    unsigned world = g->movement.ds[XEEN_DS_WORLD];
    return world_cc(g, world);
}
/* Looks NAME up in archive A and extracts it (XOR 35h) into a new block, its size in *SIZE. Returns NULL when A is
 * NULL, NAME is absent, or the memory or the extraction fails. Original: 507F0. */
static uint8_t *archive_extract(const xeen_archive *a, const char *name, size_t *size)
{
    xeen_resource_info i;
    uint8_t *b;
    size_t n = 0;
    if (!a || xeen_archive_find_name(a, name, &i) != XR_OK) {
        return NULL;
    }
    b = malloc(i.size ? i.size : 1u);
    if (!b) {
        return NULL;
    }
    if (xeen_archive_extract(a, XR_BANK_ASSETS_XOR35, &i, b, i.size, &n) != XR_OK) {
        free(b);
        return NULL;
    }
    *size = n;
    return b;
}
/* A resource of world W's CC (0 XEEN.CC, 1 DARK.CC), whatever DS 94 selects. */
uint8_t *cc_extract(xeen_game *g, unsigned w, const char *name, size_t *size)
{
    return archive_extract(world_cc(g, w), name, size);
}
/* The member NAME of the selected world's archive, extracted (XOR 35h) into a new buffer (*SIZE bytes); NULL when it is
 * missing. Writes no DS. Original: 507F0's lookup; 188F6 / 188BB / 4A85C write no DS. */
uint8_t *menu_extract(xeen_game *g, const char *name, size_t *size)
{
    return archive_extract(selected_cc(g), name, size);
}
/* Loads the file NAME (with the CD's preload check and replay) and returns its bytes for the caller, its size in
 * *SIZE and in the loaded-file size word; NULL when missing. The native keeps no DOS-memory copy (a segment word the
 * caller stores stays 0, as exit_voc's). Original: 18975 -> 4A877 (8582 at 4A894). */
uint8_t *menu_load(xeen_game *g, const char *name, size_t *size)
{
    uint8_t *b;
    cd_preload(g);
    b = menu_extract(g, name, size);
    if (b) {
        xeen_write16(&g->movement, XEEN_DS_LOADED_FILE_SIZE, (uint16_t)*size);
        cd_postload(g);
    }
    return b;
}
/*
 * Loads the selected CC's file NAME through menu_load (the CD's preload check and replay, the loaded-file size
 * word), where the original's 18975 call keeps its buffer in DOS memory and stores the segment in DS word WORD. The
 * native keeps no DOS copy: a song (WORD XEEN_DS_SONG_SEGMENT) is first kept for the playback sink
 * (keep_song_copy), the bytes are freed and WORD is written 0. Returns 1, or 0 when NAME is missing (WORD then
 * unwritten).
 */
int menu_load_word(xeen_game *g, uint16_t word, const char *name)
{
    size_t n = 0;
    uint8_t *b = menu_load(g, name, &n);
    if (!b) {
        return 0;
    }
    if (word == XEEN_DS_SONG_SEGMENT) {
        keep_song_copy(g, b, n); /* the playback sink's copy */
    }
    free(b);
    xeen_write16(&g->movement, word, 0);
    return 1;
}
/* Draws the 320 x 200 raw picture NAME over the whole framebuffer (with the CD's preload check and replay). Returns 0
 * when it is missing or of another size. Original: 188F6. */
int menu_raw(xeen_game *g, const char *name)
{
    size_t n = 0;
    uint8_t *b;
    int ok;
    cd_preload(g);
    b = menu_extract(g, name, &n);
    ok = b && n == XEEN_PRESENT_PIXELS;
    if (ok) {
        memcpy(g->presentation->framebuffer, b, XEEN_PRESENT_PIXELS);
        cd_postload(g);
    }
    free(b);
    return ok;
}
