#ifndef XEEN_TOWN_H
#define XEEN_TOWN_H
#include <stdint.h>
#include "xeen/movement.h"
/* The character advancement and temple rules of the 277E5
 * training grounds (kind 5) and temple (kind 4), over a 162h-byte member
 * record (B41E + 162h*n). Pure DS arithmetic; the location's presentation,
 * payments (29281), voices and time charges stay with the session. */

/* 28BCF: the experience the member needs for the next level, counted from
 * the class base word DS:2920[+13h]: below level 12 base << (level-1),
 * from 12 on (level-11) * 1024000 + base << 10. */
uint32_t xeen_level_threshold(const xeen_movement *, const uint8_t *member);
/* 28C76: the member's total experience: +15Ch plus the threshold of the
 * level below (the stored experience is relative to the current level). */
uint32_t xeen_level_experience(const xeen_movement *, const uint8_t *member);
/* 28C2D: 28BCF - 28C76, or 0 below 1 (a signed 32-bit compare). */
uint32_t xeen_experience_needed(const xeen_movement *, const uint8_t *member);
/* 271AE: the town's top level, F64C (Clouds: map 28 10, 30 15, else 20;
 * Darkside: 29 30, 31 50, 37 200, else 100). */
uint8_t xeen_training_top_level(uint8_t world, uint8_t map);
/* 271AE/2549B: the price, level * level * 10 (the permanent level +23h). */
uint32_t xeen_training_cost(const uint8_t *member);
/* 28CFC: 2AB96 asleep (8), paralysed, unconscious, dead, stoned or
 * eradicated (11..15) cannot train; depressed and confused (9, 10) can. */
int xeen_training_refused(const uint8_t *member);
/* 25550..2557B: the experience less 28BCF - (28C76 - XP), the steps
 * between this level's and the last level's thresholds; +23h + 1. */
void xeen_train_level(const xeen_movement *, uint8_t *member);

/* 2730E: the prices into F642 (heal), F63E (donation), F63A (uncurse)
 * with F632 (dead and stoned) and F636 (eradicated), F62E (the level), and
 * the town's bases F650 uncurse, F652 eradicated, F654 dead or stoned,
 * F656 heal and F64E donation (Clouds by map: 28 0/0/0/0 and 10, 30
 * 100/500/50/10 and 25, 49 300/5000/500/100 and 100, 73 200/1000/100/20
 * and 50; another Clouds map keeps the previous bases; Darkside 29, 31, 37
 * and the rest likewise). L = 2C909 (the level with its temporary part):
 * heal = L*10 + F656 when HP is below 2AAC2, plus L*10 per condition
 * 1..12, plus for dead and stoned L*100 + count*50 + F654 each, plus for
 * eradicated L*1000 + count*500 + F652; uncurse = L*20 + F650 when any item
 * row's state has bit 40h or condition 0 is set; donation = F64E, or 0
 * once the visit's 57AD:0000 is set (blessed). */
typedef struct {
    uint32_t heal, donation, uncurse, dead_stoned, eradicated;
} xeen_temple_prices;
void xeen_temple_prices_set(xeen_movement *, const uint8_t *member, int blessed,
                            xeen_temple_prices *);
/* 2692E: the uncurse column, "%uk" of the thousands above 999, else "%u"
 * (the low 16 bits). */
void xeen_temple_amount(char *out, unsigned long size, uint32_t value);
/* 25722..25775 after a paid Heal: the six temporary resistances (138h..
 * 142h, even), +22h, +24h and the seven stat temporaries (15h..21h, odd)
 * cleared, HP = 2AAC2 (with the conditions still set), then every condition
 * but 0 (cursed) cleared. */
void xeen_temple_heal(const xeen_movement *, uint8_t *member);
/* 258E2..2595B after a paid Uncurse: condition 0 and the cursed bit (40h)
 * of every item row cleared. */
void xeen_temple_uncurse(uint8_t *member);
#endif
