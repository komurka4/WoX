#include "xeen/party.h"
#include "xeen/character_record.h"
#include "xeen/ds_globals.h"
#include "xeen/interface.h"
#include "xeen/ds_texts.h"
#include <stdio.h>
#include <string.h>
#define B(a) (m->ds[(uint16_t)(a)])
#define W(a) xeen_read16(m, (uint16_t)(a))

int xeen_party_skill(const xeen_movement *m, uint16_t skill)
{
    unsigned member, count = 0;
    for (member = 0; member < B(XEEN_DS_PARTY_COUNT); ++member) {
        if (!B(XEEN_PARTY_RECORDS + XEEN_CHARACTER_SKILLS + skill + member * XEEN_CHARACTER_BYTES)) {
            continue;
        }
        ++count;
        if (skill >= XEEN_SKILL_COUNT) {
            continue;
        }
        if (skill == XEEN_SKILL_MOUNTAINEER || skill == XEEN_SKILL_PATH_FINDER) { /* two members needed */
            if (count == 2) {
                return 1;
            }
        } else if (skill == XEEN_SKILL_CRUSADER || skill == XEEN_SKILL_SWIMMER) { /* every member needed */
            if (count == B(XEEN_DS_PARTY_COUNT)) {
                return 1;
            }
        } else {
            return 1;
        }
    }
    return 0;
}

/** Appends one sprite draw to the border's draw list. */
static void push_draw(xeen_border_ops *o, uint16_t handle, uint16_t frame, uint16_t x, uint16_t y)
{
    xeen_sprite_draw *d = &o->draws[o->count++];
    d->handle = handle;
    d->frame = frame;
    d->x = x;
    d->y = y;
    d->scale = 0;
    d->flags = 0;
    d->window = 0;
}
/* "mov al,[a]; inc al; mov ah,0; idiv n": 8-bit increment, unsigned modulus. */
static uint8_t step(xeen_movement *m, uint16_t a, unsigned n)
{
    B(a) = (uint8_t)((uint8_t)(B(a) + 1u) % n);
    return B(a);
}

/*
 * An animated border indicator: frame FIRST_FRAME, or FIRST_FRAME + the indicator's frame counter while LIT, at X, Y
 * from the border sprites; then the counter advances (modulo 12) whether lit or not.
 */
static void animated_indicator(xeen_movement *m, xeen_border_ops *o, int lit, uint16_t frame_counter,
                               uint16_t first_frame, uint16_t x, uint16_t y)
{
    push_draw(o, W(XEEN_DS_BORDER_SPRITES), lit ? (uint16_t)(B(frame_counter) + first_frame) : first_frame, x, y);
    step(m, frame_counter, 12);
}
/*
 * Advances the clairvoyance eyes' frames: the left eye cycles 0..3 and the right eye 0Ch..0Fh; an eye whose state is
 * 0 shows the frames 4..7 (left) or 8..0Bh (right) instead, and a still eye (XEEN_CLAIRVOYANCE_STILL) its frame 0
 * or 8. Without Clairvoyance both eyes are closed (0 and 8). Original: 263EF (the right eye 2641D).
 */
static void advance_clairvoyance_eyes(xeen_movement *m)
{
    step(m, XEEN_DS_CLAIRVOYANCE_LEFT_FRAME, 4);
    if (!B(XEEN_DS_CLAIRVOYANCE_LEFT_STATE)) {
        B(XEEN_DS_CLAIRVOYANCE_LEFT_FRAME) = (uint8_t)(B(XEEN_DS_CLAIRVOYANCE_LEFT_FRAME) + 4u);
    } else if (B(XEEN_DS_CLAIRVOYANCE_LEFT_STATE) == XEEN_CLAIRVOYANCE_STILL) {
        B(XEEN_DS_CLAIRVOYANCE_LEFT_FRAME) = 0;
    }
    B(XEEN_DS_CLAIRVOYANCE_RIGHT_FRAME) = (uint8_t)(((uint8_t)(B(XEEN_DS_CLAIRVOYANCE_RIGHT_FRAME) + 1u) % 4u) + 0x0cu);
    if (!B(XEEN_DS_CLAIRVOYANCE_RIGHT_STATE)) {
        B(XEEN_DS_CLAIRVOYANCE_RIGHT_FRAME) = (uint8_t)(B(XEEN_DS_CLAIRVOYANCE_RIGHT_FRAME) + 0xfcu);
    } else if (B(XEEN_DS_CLAIRVOYANCE_RIGHT_STATE) == XEEN_CLAIRVOYANCE_STILL) {
        B(XEEN_DS_CLAIRVOYANCE_RIGHT_FRAME) = 8;
    }
    if (!B(XEEN_DS_EFFECT_CLAIRVOYANCE)) {
        B(XEEN_DS_CLAIRVOYANCE_LEFT_FRAME) = 0;
        B(XEEN_DS_CLAIRVOYANCE_RIGHT_FRAME) = 8;
    }
}
/*
 * The four protection gems (fire, electricity, cold, poison) from the FECP sprites: gem k is frame FIRST_FRAME + 2k,
 * + 1 while its protection lasts. With the 3D view's full border they sit at its corners (2,2), (DBh,2), (2,86h),
 * (DBh,86h) from frame 0; when the right column, combat or event-text window is open, at (8,8), (DBh,8), (8,86h),
 * (DBh,86h) from frame 8.
 */
static void draw_protections(xeen_movement *m, xeen_border_ops *o, int full_border)
{
    static const uint16_t protections[4] = {XEEN_DS_PROTECTION_FIRE, XEEN_DS_PROTECTION_ELECTRICITY,
                                            XEEN_DS_PROTECTION_COLD, XEEN_DS_PROTECTION_POISON};
    static const uint16_t full[4][2] = {{2, 2}, {0xdb, 2}, {2, 0x86}, {0xdb, 0x86}};
    static const uint16_t inset[4][2] = {{8, 8}, {0xdb, 8}, {8, 0x86}, {0xdb, 0x86}};
    const uint16_t(*at)[2] = full_border ? full : inset;
    uint16_t first_frame = full_border ? 0u : 8u;
    unsigned k;
    for (k = 0; k < 4; ++k) {
        push_draw(o, W(XEEN_DS_FECP_SPRITES), (uint16_t)(first_frame + 2u * k + (W(protections[k]) ? 1u : 0u)),
                  at[k][0], at[k][1]);
    }
}
/*
 * The party-effect icons over the bless area (frame 10h of the bless sprites at 21h,89h): Bless, Power Shield, Holy
 * Bonus and Heroism, each while active its animated frame (its counter, DS F465..F462, read and written only here,
 * advanced modulo 4, + the effect's first frame) at its x.
 */
static void draw_party_effects(xeen_movement *m, xeen_border_ops *o)
{
    static const struct {
        uint16_t effect, frame_counter, first_frame, x;
    } effects[4] = {{XEEN_DS_EFFECT_BLESS, 0xf465, 0x00, 0x21},
                    {XEEN_DS_EFFECT_POWER_SHIELD, 0xf464, 0x04, 0x37},
                    {XEEN_DS_EFFECT_HOLY_BONUS, 0xf463, 0x08, 0xa0},
                    {XEEN_DS_EFFECT_HEROISM, 0xf462, 0x0c, 0xb6}};
    unsigned k;
    push_draw(o, W(XEEN_DS_BLESS_SPRITES), 0x10, 0x21, 0x89);
    for (k = 0; k < 4; ++k) {
        if (B(effects[k].effect)) {
            push_draw(o, W(XEEN_DS_BLESS_SPRITES), (uint16_t)(step(m, effects[k].frame_counter, 4) + effects[k].first_frame),
                      effects[k].x, 0x89);
        }
    }
}

/**
 * Builds the draws of the screen border around the 3D view, in the original's order: the border itself; the
 * Levitate, Spot Secret Doors and Danger Sense indicators (lit by the effect, or by the condition with a member
 * having the skill); the clairvoyance eyes (the right one replaced by frame 34h while the right column or combat
 * window is open); the protection gems; the party effects; the compass letter (DS 17A2's names' initials) for a
 * party with Direction Sense outside text entry; and the view frame request (window 0Ch with the bottom window,
 * else 28h with the speech window). Advances every animation counter it shows. A facing above 3 is refused before
 * anything is written.
 */
xeen_status xeen_interface_border(xeen_movement *m, xeen_border_ops *o)
{
    int side_window;
    if (!m || !o) {
        return XEEN_BAD_INPUT;
    }
    if (B(XEEN_DS_PARTY_DIRECTION) > 3u) {
        return XEEN_BAD_INPUT;
    }
    memset(o, 0, sizeof *o);
    push_draw(o, W(XEEN_DS_GLOBAL_SPRITES), 0, 8, 8);
    animated_indicator(m, o, B(XEEN_DS_EFFECT_LEVITATE) != 0, XEEN_DS_LEVITATE_FRAME, 0x10, 0, 0x52);
    animated_indicator(m, o, B(XEEN_DS_SPOT_DOORS_LIT) && xeen_party_skill(m, XEEN_SKILL_SPOT_SECRET_DOORS),
                       XEEN_DS_SECRET_DOORS_FRAME, 0x1c, 0xc2, 0x5b);
    animated_indicator(m, o, B(XEEN_DS_MONSTER_NEARBY) && xeen_party_skill(m, XEEN_SKILL_DANGER_SENSE),
                       XEEN_DS_DANGER_SENSE_FRAME, 0x28, 0x6b, 9);
    advance_clairvoyance_eyes(m);
    side_window = B(XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_RIGHT_COLUMN)) || B(XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_COMBAT));
    push_draw(o, W(XEEN_DS_BORDER_SPRITES), B(XEEN_DS_CLAIRVOYANCE_LEFT_FRAME), 0, 0x20);
    push_draw(o, W(XEEN_DS_BORDER_SPRITES), side_window ? 0x34u : B(XEEN_DS_CLAIRVOYANCE_RIGHT_FRAME), 0xd7, 0x20);
    draw_protections(m, o, !side_window && !B(XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_EVENT_TEXT)));
    draw_party_effects(m, o);
    if (xeen_party_skill(m, XEEN_SKILL_DIRECTION_SENSE) && !B(XEEN_DS_TEXT_ENTRY)) {
        /* 26721: the DS line of the first letter of the party direction's name (CBW; 17A2's far pointers) */
        uint16_t name = W(XEEN_DS_DIRECTION_NAMES + 4u * B(XEEN_DS_PARTY_DIRECTION));
        (void)snprintf(o->text, sizeof o->text, (const char *)m->ds + XEEN_TEXT_DIRECTION_SENSE_LINE,
                       (int)(int8_t)B(name));
        o->has_text = 1;
        o->text_window = 0;
    }
    if (B(XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_BOTTOM))) {
        o->has_frame = 1;
        o->frame_window = 0x0c;
    } else if (B(XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH))) {
        o->has_frame = 1;
        o->frame_window = 0x28;
    }
    return XEEN_OK;
}
