#ifndef XEEN_SPRITES_H
#define XEEN_SPRITES_H
#include "xeen/resources.h"
#include "xeen/entities.h"
/* Native sprite cache with the original driver's observable handle semantics
 * (4A8A5 load, 4AAF6 first-free slot, 4AA3E free_from), not EMS emulation.
 * Handle = slot+1; `last` mirrors driver CS:03D0 (last allocated handle).
 * Each resident handle has a resource identity: archive, name, allocation
 * size (driver CS:09D2) and sprite frame count (decoded header word 0). */
#define XEEN_SPRITE_SLOTS 256u
enum {
    XSP_FREE,
    XSP_RESERVED,
    XSP_LOADED
};
enum {
    XSP_XEEN_CC,
    XSP_DARK_CC,
    XSP_CUR
};
typedef struct {
    uint8_t state, archive;
    char name[16];
    uint16_t size, frames, resource_size;
    /* The frame table as 4ADB1/4AC92 read it (frame*4+2 first
     * cell, frame*4+4 second cell, drawn when non-zero; the frame is not
     * checked against the count). `table_frames` frames have their second
     * word inside the resource (at most 256); bit f of `second` is set when
     * that word is non-zero. */
    uint16_t table_frames;
    uint8_t second[32];
} xeen_sprite;
/* pre/post (optional, set by the game after init): the loader
 * wrapper's checks around each load (18931's 47B7D before it, its FB06
 * replay after a successful one), so every load through the cache makes them. */
typedef struct {
    xeen_sprite slots[XEEN_SPRITE_SLOTS];
    uint16_t last;
    void (*pre)(void *);
    void (*post)(void *);
    void *user;
} xeen_sprite_cache;
/* Borrowed archives. cc[0] = xeen.cc, cc[1] = dark.cc (either may be NULL;
 * a load from a missing archive fails). cur = current game archive, used for
 * names beginning "maze" (any case), as the driver's CUR index. */
typedef struct {
    const xeen_archive *cc[2];
    const xeen_archive *cur;
} xeen_sprite_archives;

void xeen_sprite_cache_init(xeen_sprite_cache *);
/* 4AA3E: no effect when handle > last; otherwise last = handle-1 and every
 * slot from handle upward is released. Handle 0 is rejected (returns 0). */
int xeen_sprite_free_from(xeen_sprite_cache *, uint16_t handle);
/* 15D9B..15E0F: the seven program-start buffers 029.obj, back.raw, 46k.buf
 * and four 34k.buf, reserved with their resource sizes. A reload into one
 * (the 8C86 buffer 7 for pow*.icn) fills it when the resource fits, as the
 * driver overwrites the buffer. Returns the first handle in *first. */
xeen_resource_status xeen_sprite_reserve_startup(xeen_sprite_cache *, uint16_t *first);
/* 18931/4A8A5. reload==0 allocates the first free slot; otherwise the named
 * resource replaces handle `reload` keeping its allocation size and `last`
 * (rejected if larger than the allocation, which would overrun in DOS).
 * The CC is selected by `current_cc` (DS byte 94). Missing resource or full
 * table rejects without change (the original exits fatally). */
xeen_resource_status xeen_sprite_load(xeen_sprite_cache *, const xeen_sprite_archives *,
                                      uint8_t current_cc, const char *name, uint16_t reload,
                                      uint16_t *handle);
/* 4AC92's own tests before a single draw (4ACAB..4AD07): 0 when
 * the driver draws or skips (handle or frame FFFFh, handle 0, a reserved
 * slot whose sprite is not decoded), 1 when the handle is above the last
 * one (4ACF1), 2 when the frame is not below the sprite's word 0 (4AD07);
 * the driver then exits fatally (4AD19 -> 505E3). Scale FFFFh is the
 * caller's test. */
int xeen_sprite_driver_check(const xeen_sprite_cache *, uint16_t handle, uint16_t frame);
/* Loaded entry or NULL (free, reserved or out of range). */
const xeen_sprite *xeen_sprite_get(const xeen_sprite_cache *, uint16_t handle);
/* The number of 4AEB8 calls the driver makes for one draw of
 * (handle, frame): 1, or 2 when the frame's second cell word is non-zero;
 * 0 when the word is not known (no resident sprite, or a frame whose table
 * entry lies past the resource: the driver reads other memory there). */
unsigned xeen_sprite_cells(const xeen_sprite_cache *, uint16_t handle, uint16_t frame);

/* 191DE archive selection: B95==0 forces 1; n==0/1 sets DS byte 94. */
void xeen_select_archive(xeen_movement *, uint16_t n);

/* Resident handles at world entry: startup 1..7 reserved, mouse.icn (F460);
 * 43C37 globals F458,F456,8C7E,973A,9736,9734,9738,98CE and inn.icn (8DA2),
 * released on inn exit; 1791F faces (2A040, 9806+2i), dse.fac (97FC), main.icn
 * (28D6E, 98D4) and 98D0 = 98D4+1. Only these handle words are written; the
 * remaining effects of those routines are not part of this stage. Atomic. */
xeen_resource_status xeen_sprites_enter_world(xeen_sprite_cache *, const xeen_sprite_archives *,
                                              xeen_movement *);

/* Music request issued by map load (4224F): DS 9757 <- 9766, 9766 <- name are
 * written natively; the callback receives the platform part (driver 50AB1 and
 * the pending-music flag). */
typedef void (*xeen_music_fn)(void *, const char *name, uint16_t flag);
/* Map-load sprite allocation 1A7A6..1B0AC plus the sky reload 2A0FB, for
 * indoor (F608==0, 1AD29) and outdoor (F608!=0, 1ABC0) maps. mob_header =
 * the first48 bytes of maze%c%03u.mob (object, monster and wall-item ids).
 * Darkside draws random(0..6) for music through `random` (incoming AX = DS
 * byte 94, as at 1ADCD/1ABF5). A check-only pass runs first: any missing
 * resource or unsupported input rejects before any DS write, RNG draw or
 * music request. Indoor styles C106>5, outdoor wall/surface type bytes >15
 * and monster sprite ids >= 94 (stack-table aliasing in the original) reject.
 * Not included: the 19CC3 music-side check and the conditional 18765(9). */
xeen_resource_status xeen_sprites_map_load(xeen_sprite_cache *, const xeen_sprite_archives *,
                                           xeen_movement *, const uint8_t mob_header[48],
                                           const uint8_t *database, size_t database_size,
                                           xeen_entity_random_fn random, void *random_user,
                                           xeen_music_fn music, void *music_user);
/* Original 2A0FB clock-boundary sky refresh. The clock-derived state is
 * committed only when the authentic replacement sprite can be reloaded into
 * the resident F45C handle. */
xeen_resource_status xeen_sprites_reload_sky(xeen_sprite_cache *, const xeen_sprite_archives *,
                                             xeen_movement *);
#endif
