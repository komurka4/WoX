#ifndef XEEN_ADDON_DAMAGE_NUMBERS_H
#define XEEN_ADDON_DAMAGE_NUMBERS_H
#include <stddef.h>
#include <stdint.h>
/* Owner-approved add-on damage_numbers (Phase 199; NOT original XEEN.DAT
 * behavior, off unless --addon damage_numbers or wox.cfg [addons]
 * damage_numbers = on): damage, healing and misses are shown as numbers
 * over the party portraits and over the monsters, in exploration and in
 * combat. Damage is red with a minus sign, healing green with a plus sign,
 * a miss is the word "Miss" in grey.
 *
 * Two layouts (owner choice, Phase 199):
 *   default  each value pops at the target, then rises and fades; a newer
 *            value on the same target pushes the older ones up (P1/P4 on
 *            the portraits, M1 on the monster's body);
 *   stack    damage_numbers.stack = on (with damage_numbers): one running
 *            total per target and kind (damage, healing, misses) with a hit
 *            count, held while hits keep coming (P2/P5, M2).
 *
 * It is drawn with the engine only: on a copy of the displayed 320x200 page
 * (the game's framebuffer and DS are never written), with the game's font
 * (FNT, normal and reduced) and the game's text colours (the driver table of
 * \fNN codes): damage 32, healing 15, misses 1, their brighter and darker
 * neighbours for the pop and the fade, palette 0 for the outline.
 *
 * The values are the exact amounts (owner choice B): the original code
 * reports them through the one-line hooks below (XEEN_DAMAGE_NUMBERS(...)),
 * each a no-op while the add-on is off. A monster's damage is the amount
 * dealt, overkill included; a party member's is the amount subtracted from
 * the HP; healing is the HP gained.
 *
 * Code: src/addon/damage_numbers/ (notes.c the hooks' queue, overlay.c the
 * layout, animation and drawing). */

/* Nonzero while the add-on is on (the hooks test it). */
extern int xeen_damage_numbers_on;

/* --- hooks, called from the original code (only when on) ---------------- */
/* The HP word of RECORD is about to be written with VALUE: notes the change
 * from its current value (call before the write). */
void xeen_damage_numbers_hp_set(const uint8_t *ds,uint16_t record,uint16_t value);
/* The HP word is about to be restored to VALUE (Rest, the temple, Divine
 * Intervention, give 78: the maximum): only a gain is noted (a member above
 * the maximum loses the excess without a number). */
void xeen_damage_numbers_hp_restore(const uint8_t *ds,uint16_t record,uint16_t value);
/* The same, for a record given by pointer into DS. */
void xeen_damage_numbers_hp_ptr(const uint8_t *ds,const uint8_t *ch,int32_t delta);
void xeen_damage_numbers_hp_ptr_restore(const uint8_t *ds,const uint8_t *ch,uint16_t value);
/* A monster attack missed party member MEMBER (0..5, the record index). */
void xeen_damage_numbers_party_miss(unsigned member);
/* Monster INDEX (E530h + 20 * INDEX) took DELTA (negative: damage, 0: none)
 * or was missed; DS gives its current place on the screen. */
void xeen_damage_numbers_monster(const uint8_t *ds,unsigned index,int32_t delta);
void xeen_damage_numbers_monster_miss(const uint8_t *ds,unsigned index);

#define XEEN_DAMAGE_NUMBERS(call) do{ if(xeen_damage_numbers_on)xeen_damage_numbers_##call; }while(0)

/* The event interpreter's HP writes (SetVar HP, give 8, give 78) report
 * through this pointer instead: it is defined in src/events/event_exec.c, which so
 * references nothing outside itself (historical evidence tools compile it
 * alone into shared libraries). NULL while the add-on is off; RESTORE 1 for
 * a write to the maximum (a gain only), 0 for any other HP write. */
extern void (*xeen_damage_numbers_event_hp)(const uint8_t *ds,uint16_t record,uint16_t value,int restore);
#define XEEN_DAMAGE_NUMBERS_EVENT(ds,record,value,restore) \
    do{ if(xeen_damage_numbers_event_hp)xeen_damage_numbers_event_hp((ds),(record),(value),(restore)); }while(0)

/* --- host side (src/app/explore.c) ------------------------------------------ */
struct xeen_game;
/* Turns the add-on on: STACK selects the stack layout. SPRITE_ARCHIVES is
 * the host's xeen_sprite_archives (borrowed): the font FNT from xeen.cc (or
 * dark.cc), the monsters' sprite bytes for their outlines. 0 when the font
 * is missing (the add-on stays off). */
int xeen_damage_numbers_start(int stack,const void *sprite_archives);
void xeen_damage_numbers_stop(void);
/* The game whose DS and sprite cache the overlay reads (each host pass). */
void xeen_damage_numbers_bind(const struct xeen_game *);
/* The page to show instead of PAGE: PAGE itself while nothing is shown,
 * otherwise a copy with the numbers. NOW_NS is the host clock (0: a fixed
 * 40 ms per call, for headless runs). PALETTE is unused (Phase 270 removed
 * the developer capture that read it; the signature is kept for the hosts). */
const uint8_t *xeen_damage_numbers_page(const uint8_t *page,const uint8_t *palette,uint64_t now_ns);
#endif
