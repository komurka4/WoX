/*
 * The party-target spells (Phase 248, from game_cast.c): the healing family (its 3977A flash frames and its
 * tails), the 399FA "Cast on whom?" prompt and its answer, and cast_aid, the handlers that act on the member
 * chosen (First Aid, Cure Wounds, Revitalize, Suppress Poison, the cures, Nature's Cure, Raise Dead,
 * Resurrect and the others). cast_dispatch, which calls them, is game_cast.c's.
 */
#include "game_cast.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when a party spell cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json). A sound stop means the host's audio refused the spell's effect.
 */
enum {
    HEAL_STOP_SOUND = 0x2ba47,
    CAST_FAILED_STOP_SCROLL = 0x3964b,       /* "Spell Failed!" could not be shown */
    AID_STOP_MEMBER = 0x39a9e,               /* the chosen member past the eight */
    AID_STOP_UNKNOWN_SPELL = 0x399fa,        /* a spell none of the aid handlers serves */
    POWER_CURE_STOP_SOUND = 0x3a4a5,
    REVITALIZE_STOP_SOUND = 0x39dd6,
    SUPPRESS_POISON_STOP_SOUND = 0x39fcc,
    SUPPRESS_DISEASE_STOP_SOUND = 0x3a1d5,
    RAISE_DEAD_STOP_SOUND = 0x3b29d,
    RESURRECT_STOP_SOUND = 0x3b578,
    STONE_TO_FLESH_STOP_SOUND = 0x3b176,
    /* variable stop sites (stops_variable.json) */
    NATURES_CURE_STOP_SOUND = 0x3a533,       /* each healing spell's sound call */
    AID_STOP_HEAL_SOUND = 0x39d8a,               /* First Aid's and Cure Wounds' (one site) */
    CURE_DISEASE_STOP_SOUND = 0x3abf2,
    CURE_PARALYSIS_STOP_SOUND = 0x3aedc,
    CURE_POISON_STOP_SOUND = 0x3a930
};
/* After 3977A, Revitalize (39E06) clears weak (+145h); it and
 * Suppress Poison end with 2ABB8(1) (portraits: presentation). 
 * Cure Poison (3A970), Cure Disease (3AC32) and Cure Paralysis (3AF1C)
 * clear poison (+146h), disease (+147h) and paralysis (+14Eh) the same
 * way, also on a member 3977A left alone (dead, stoned, eradicated). */
/* Raise Dead (3B2B2) and Resurrect (3B596): the endurance
 * permanent (+1Ah) one less by a byte ADD AL,FFh, and 1 when the result
 * compares below 1 (0 only: 0 -> FFh stays). */
static void endurance_loss(uint8_t *ch)
{
    ch[XEEN_CHARACTER_STAT(XEEN_STAT_ENDURANCE)] =
            (uint8_t)(ch[XEEN_CHARACTER_STAT(XEEN_STAT_ENDURANCE)] - 1u);
    if (!ch[XEEN_CHARACTER_STAT(XEEN_STAT_ENDURANCE)]) {
        ch[XEEN_CHARACTER_STAT(XEEN_STAT_ENDURANCE)] = 1;
    }
}
/** Continues after a healing spell's effect: the tail that the casting spell selects. */
static xeen_status cast_heal_done(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    if (g->heal_tail == XEEN_SPELL_MOON_RAY) {
        g->heal_tail = 0;
        return moon_ray_next(g);
    }
    /* the condition the spell clears (3B409's table) */
    unsigned at = g->heal_tail == XEEN_SPELL_REVITALIZE       ? XEEN_CHARACTER_CONDITION(XEEN_CONDITION_WEAK)
                  : g->heal_tail == XEEN_SPELL_CURE_POISON    ? XEEN_CHARACTER_CONDITION(XEEN_CONDITION_POISONED)
                  : g->heal_tail == XEEN_SPELL_CURE_DISEASE   ? XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DISEASED)
                  : g->heal_tail == XEEN_SPELL_CURE_PARALYSIS ? XEEN_CHARACTER_CONDITION(XEEN_CONDITION_PARALYZED)
                  : g->heal_tail == XEEN_SPELL_STONE_TO_FLESH ? XEEN_CHARACTER_CONDITION(XEEN_CONDITION_STONE)
                  : g->heal_tail == XEEN_SPELL_RESURRECT      ? XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ERADICATED)
                                                              : 0u;
    uint8_t *ch = ds + XEEN_PARTY_RECORD(g->whom_record);
    if (at) {
        ch[at] = 0;
    }
    if (g->heal_tail == XEEN_SPELL_RAISE_DEAD) {
        endurance_loss(ch); /* 3B2B2 Raise Dead */
    }
    if (g->heal_tail == XEEN_SPELL_RESURRECT) { /* 3B596 Resurrect */
        endurance_loss(ch);
        ch[XEEN_CHARACTER_AGE_BONUS] =
                (uint8_t)(ch[XEEN_CHARACTER_AGE_BONUS] + 5u > 250u
                                  ? 250u
                                  : ch[XEEN_CHARACTER_AGE_BONUS] +
                                            5u); /* the temporary age, at most 250 */
    }
    if (g->heal_tail) {
        bar_redraw(g);
    }
    g->heal_tail = 0;
    return cast_handled(g);
}
/* A healing's end once its portrait flash (if any) is over: a member whose HP is now above 0 is no longer
 * unconscious, the party bar redrawn, every member's flash count cleared; then the heal's own end.
 * Original: 3977A(member, amount) after 2B9C7: +14Fh; 2ABB8(1); 8C78..8C7D = 0. */
static xeen_status cast_heal_tail(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t rec = XEEN_PARTY_RECORD(g->flash_member);
    if ((int16_t)xeen_read16(m, (uint16_t)(rec + XEEN_CHARACTER_HP)) > 0) {
        ds[(uint16_t)(rec + XEEN_CHARACTER_CONDITION(XEEN_CONDITION_UNCONSCIOUS))] = 0;
    }
    bar_redraw(g);
    memset(ds + XEEN_DS_MEMBER_FLASHED, 0, 6);
    return cast_heal_done(g);
}
/* One frame of a healing's portrait flash: four frames of the spell-effect sprites over the member's portrait, each
 * a scene pass apart (with the view's window open, at once and the screen presented). After the fourth the party bar
 * is redrawn, the monsters' countdown restored, the member's flash count raised, and the heal's tail follows.
 * Original: 2B9C7(member) from 3977A (when 8C78[member] is clear): 18765(0Ch) with window 12 open, 96C0 saved and 0,
 * 1880E(14h), four frames (4EA0D, the 8C7E frame, 1B503(0) unless window 11 is open, 50BF6(0), a 4EA1C wait the
 * scene's frame wait covers), 2ABB8(1), 96C0 back, 8C78[member] += 1. */
xeen_status cast_flash_frame(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (g->flash_frame >= 4u) {
        bar_redraw(g); /* 2B9C7's 2ABB8(1) */
        ds[XEEN_DS_MONSTER_COUNTDOWN] = g->flash_countdown;
        ++ds[(uint16_t)(XEEN_DS_MEMBER_FLASHED + g->flash_slot)];
        return cast_heal_tail(g);
    }
    exit_draw(g, xeen_read16(m, XEEN_DS_SPELLFX_SPRITES), (uint16_t)g->flash_frame,
              (int16_t)ds[(uint16_t)(XEEN_DS_PORTRAIT_X + g->flash_slot)], 150, 0);
    ++g->flash_frame;
    if (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)]) {
        (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
        return cast_flash_frame(g);
    }
    g->panel_stage = CAST_FLASH_SCENE;
    return scene_wait(g, 0, XEEN_TIMER_FRAME_MARK);
}
/* The slot whose portrait and flash count a healed MEMBER uses: in combat (FAF8 2) the first combat slot holding the
 * member (9720[slot], slots 0..971F-1), else, or when no slot holds it, the member's own number.
 * Original: 2BA15..2BA38 (DI the slot, SI the member). */
static unsigned heal_flash_slot(const uint8_t *ds, unsigned member)
{
    if (ds[XEEN_DS_COMBAT_MODE] == 2u) {
        for (unsigned slot = 0; slot < ds[XEEN_DS_COMBAT_PARTY_COUNT]; ++slot) {
            if (ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + slot)] == member) {
                return slot;
            }
        }
    }
    return member;
}
/* Heals MEMBER by AMOUNT: every flash count cleared; a member dead, stoned or eradicated gets nothing; HP already
 * above the maximum stay as they are; else the HP rise by the amount (capped at the maximum, compared signed) and,
 * unless the member has flashed already, the portrait flashes first (the bottom window closed), then the heal's tail.
 * Original: 3977A(member, amount): 8C78..8C7D = 0; 2AAC2; 2B9C7 (2BA15). */
xeen_status cast_heal(xeen_game *g, unsigned member, uint16_t amount)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t rec = XEEN_PARTY_RECORD(member);
    uint8_t worst = xeen_character_worst_condition(ds + rec);
    int16_t hp, max;
    memset(ds + XEEN_DS_MEMBER_FLASHED, 0, 6);
    g->flash_member = member;
    if (xeen_condition_is_deathlike(worst)) {
        memset(ds + XEEN_DS_MEMBER_FLASHED, 0, 6);
        return cast_heal_done(g);
    }
    max = (int16_t)xeen_combat_max_hp(m, ds + rec);
    hp = (int16_t)xeen_read16(m, (uint16_t)(rec + XEEN_CHARACTER_HP));
    if (hp > max) {
        return cast_heal_tail(g);
    }
    hp = (int16_t)(hp + (int16_t)amount);
    if (hp > max) {
        hp = max;
    }
    XEEN_DAMAGE_NUMBERS(hp_set(ds, rec, (uint16_t)hp)); /* add-on hook */
    xeen_write16(m, (uint16_t)(rec + XEEN_CHARACTER_HP), (uint16_t)hp);
    if (ds[(uint16_t)(XEEN_DS_MEMBER_FLASHED + member)]) {
        return cast_heal_tail(g);
    }
    if (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_BOTTOM)]) {
        xeen_window_close(m, XEEN_WINDOW_BOTTOM);
        if (g->bottom_active) {
            bottom_restore(g);
        }
    }
    /* 2BA15: in combat the portrait (17B2) and the 8C78 count take the member's combat slot. */
    g->flash_slot = heal_flash_slot(ds, member);
    g->flash_countdown = ds[XEEN_DS_MONSTER_COUNTDOWN];
    ds[XEEN_DS_MONSTER_COUNTDOWN] = 0;
    if (sound_effect(g, 20u) != XEEN_OK) {
        return panel_stop(g, HEAL_STOP_SOUND);
    }
    g->flash_frame = 0;
    return cast_flash_frame(g);
}
/* Enters the "On Whom?" prompt's scene wait (a spell cast on a member): each poll follows a scene pass. Its keys:
 * F1..F6 a present member; Escape gives the caster's spell points and gems back and answers FFFFh; any other key polls
 * again after a scene.
 * Original: 399FA(spell): 190ED/1919B, 98D6 saved and 1, 9A1C saved and 3, 1871B(10h), DS:6878, 50BF6(10h); a
 * 4EA0D/1B503(1) scene before each 39A4E poll; Esc 39C8B on the 9800 caster. */
xeen_status cast_whom_scene(xeen_game *g)
{
    g->panel_stage = CAST_WHOM_SCENE;
    return scene_wait(g, 1, XEEN_TIMER_FRAME_MARK);
}
/** Asks which member a spell is cast on (routes the keys to the menu and shows the prompt). */
xeen_status cast_whom(xeen_game *g, unsigned spell)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    g->whom_portrait_clicks = ds[XEEN_DS_PORTRAIT_CLICKS];
    ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    g->saved_ui_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_SPELLS;
    g->whom_spell = spell;
    panel_window(g, XEEN_WINDOW_SPELL_TARGET, 1);
    menu_window_text(g, XEEN_WINDOW_SPELL_TARGET, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_CAST_ON_WHOM));
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SPELL_TARGET);
    return cast_whom_scene(g);
}
static xeen_status cast_aid(xeen_game *, unsigned);
/* The party record a spell's chosen member N stands for: in combat the member at position N of the combat party's
 * map, else member N itself. Original: 399FA's answer; 9720[n] when FAF8 is 2 (39DDE, 39E38, 39F82, 3A18B). */
static unsigned cast_target(const xeen_game *g, unsigned n)
{
    const uint8_t *ds = g->movement.ds;
    return ds[XEEN_DS_COMBAT_MODE] == 2u ? ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + n)] : n;
}
/* The "cast on whom" prompt's answer when Escape cancels it (cast_aid then only ends the spell). */
#define WHOM_CANCELLED 0xffffu
/*
 * Answers the "cast on whom" prompt with KEY: F1..F6 picks that member when the party (the combat party in combat)
 * has one there; Escape refunds the spell's cost (cast_pay's refund) and cancels; any other key shows the prompt again.
 * A choice or a cancel closes the prompt's window, pops its button set, restores the UI mode and portrait clicks and
 * goes on to cast_aid. Original: 399FA's key loop.
 */
xeen_status cast_whom_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned target;
    if (key == XEEN_KEY_ESCAPE) {
        target = WHOM_CANCELLED;
        (void)cast_pay(g, xeen_read16(m, XEEN_DS_ACTING_CHARACTER), g->whom_spell, 1);
    } else if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F6 &&
               key - XEEN_KEY_F1 < (ds[XEEN_DS_COMBAT_MODE] == 2u ? ds[XEEN_DS_COMBAT_PARTY_COUNT]
                                               : ds[XEEN_DS_PARTY_COUNT])) {
        target = key - XEEN_KEY_F1;
    } else {
        return cast_whom_scene(g);
    }
    panel_window(g, XEEN_WINDOW_SPELL_TARGET, 0);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    ds[XEEN_DS_UI_MODE] = g->saved_ui_mode;
    ds[XEEN_DS_PORTRAIT_CLICKS] = g->whom_portrait_clicks;
    return cast_aid(g, target);
}
/* A spell failed: "Spell Failed!" in a message scroll, whose close ends the spell's handler. Stops when the scroll
 * cannot open. Original: 39641 (18F2F mode 1). */
xeen_status cast_failed(xeen_game *g)
{
    g->panel_stage = 0;
    if (!open_scroll_message(g, SCROLL_SPELL_FAILED, "\003c\v010\t000Spell Failed!")) {
        return game_stop(g, CAST_FAILED_STOP_SCROLL);
    }
    return XEEN_SERVICE_REQUIRED;
}
/* Suppress Poison and Suppress Disease on the condition's level: below 4 becomes 1, else 3 less (0 stays 0).
 * Original: 39F6F, 3A178. */
static void suppress_condition(uint8_t *level)
{
    if (*level) {
        *level = (uint8_t)(*level < 4u ? 1u : *level - 3u);
    }
}
/* The items dialog's mode for an item spell. */
static int item_spell_mode(unsigned spell)
{
    switch (spell) {
        case XEEN_SPELL_ENCHANT_ITEM:
            return ITEMS_ENCHANT;
        case XEEN_SPELL_ITEM_TO_GOLD:
            return ITEMS_GOLD;
        default:
            return ITEMS_RECHARGE;
    }
}
/* Whether SPELL is an item spell (Enchant Item, Item to Gold, Recharge Item): it opens the member's items dialog
 * instead of healing. */
static int is_item_spell(unsigned spell)
{
    return spell == XEEN_SPELL_ENCHANT_ITEM || spell == XEEN_SPELL_ITEM_TO_GOLD || spell == XEEN_SPELL_RECHARGE_ITEM;
}
/*
 * The healing spells' rules, in the order cast_aid applies them to the chosen member: whether the member's state
 * refuses the spell (aid_refused), what the spell does before its sound (aid_before_sound), the tail that finishes
 * it after cast_heal's flash (aid_tail) and the HP of that heal (aid_heal). The sound and its stop are cast_aid's.
 */
/* Whether member CH's state refuses SPELL ("Spell Failed!"): First Aid, Cure Wounds, Nature's Cure and Power Cure
 * need a member who is not dead, stoned or eradicated; Raise Dead a dead one; Resurrect an eradicated one. The
 * other healing spells test nothing (they also act on such a member). */
static int aid_refused(unsigned spell, const uint8_t *ch)
{
    switch (spell) {
        case XEEN_SPELL_FIRST_AID:
        case XEEN_SPELL_CURE_WOUNDS:
        case XEEN_SPELL_NATURES_CURE:
        case XEEN_SPELL_POWER_CURE:
            return xeen_condition_is_deathlike(xeen_character_worst_condition(ch));
        case XEEN_SPELL_RAISE_DEAD:
            return !ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DEAD)];
        case XEEN_SPELL_RESURRECT:
            return !ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ERADICATED)];
        default:
            return 0;
    }
}
/* What SPELL does to the member record REC before its sound: Suppress Poison and Suppress Disease lower the
 * condition's level (suppress_condition); Raise Dead clears dead, unconscious and the HP word, so that its heal
 * of 1 leaves 1 HP. Original: 39F6F (+146h), 3A178 (+147h); Raise Dead 3B23D (+150h, +14Fh). */
static void aid_before_sound(xeen_movement *m, unsigned spell, uint16_t rec)
{
    uint8_t *ds = m->ds;
    switch (spell) {
        case XEEN_SPELL_SUPPRESS_POISON:
            suppress_condition(ds + (uint16_t)(rec + XEEN_CHARACTER_CONDITION(XEEN_CONDITION_POISONED)));
            break;
        case XEEN_SPELL_SUPPRESS_DISEASE:
            suppress_condition(ds + (uint16_t)(rec + XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DISEASED)));
            break;
        case XEEN_SPELL_RAISE_DEAD:
            ds[(uint16_t)(rec + XEEN_CHARACTER_CONDITION(XEEN_CONDITION_DEAD))] =
                    ds[(uint16_t)(rec + XEEN_CHARACTER_CONDITION(XEEN_CONDITION_UNCONSCIOUS))] = 0;
            xeen_write16(m, (uint16_t)(rec + XEEN_CHARACTER_HP), 0);
            break;
        default:
            break;
    }
}
/* The tail (heal_tail) that finishes SPELL once cast_heal has flashed the member (cast_heal_done): the condition
 * the spell clears, or Raise Dead's and Resurrect's endurance loss. The HP spells have none (0). */
static unsigned aid_tail(unsigned spell)
{
    switch (spell) {
        case XEEN_SPELL_FIRST_AID:
        case XEEN_SPELL_CURE_WOUNDS:
        case XEEN_SPELL_NATURES_CURE:
        case XEEN_SPELL_POWER_CURE:
            return 0;
        default:
            return spell;
    }
}
/* The HP of SPELL's heal: First Aid 6, Cure Wounds 15, Nature's Cure 25 (it cures no condition); Power Cure a random
 * 2..12 times the caster's level (the product's low word; the draw, entered with 1880E's AX, is made here, after the
 * sound: the silent service answers 0; IMUL); Raise Dead 1; the others 0 (a stoned or eradicated member gets no HP
 * from the heal). Original: 3977A(member, 6, 15 or 25); Power Cure 3A450 (random(2, 12) x 2C909). */
static uint16_t aid_heal(xeen_game *g, unsigned spell)
{
    xeen_movement *m = &g->movement;
    switch (spell) {
        case XEEN_SPELL_FIRST_AID:
            return 6u;
        case XEEN_SPELL_CURE_WOUNDS:
            return 15u;
        case XEEN_SPELL_NATURES_CURE:
            return 25u;
        case XEEN_SPELL_POWER_CURE: {
            uint16_t v = 0;
            (void)xeen_entity_rng_range(g->event_random(g->event_random_user, 0), 2u, 12u, &v);
            return (uint16_t)(v * xeen_character_current_level(m->ds + xeen_read16(m, XEEN_DS_ACTING_CHARACTER)));
        }
        case XEEN_SPELL_RAISE_DEAD:
            return 1u;
        default:
            return 0;
    }
}
/* The item spells on member record REC: the Cast window closes and the items dialog opens on the member in the
 * spell's mode, in UI mode FFh (the mode before kept for items_end, which restores it and ends the spell).
 * Original: Enchant Item 3B443, Item to Gold 3B893, Recharge Item 3B1C5 ([BP-2] = 9A1C, 9A1C = FFh, 18765(0Ah),
 * 44F92(record, 6, 0Bh or 4)). */
static xeen_status item_spell_begin(xeen_game *g, uint16_t rec)
{
    uint8_t *ds = g->movement.ds;
    g->item_spell_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = 0xff;
    panel_window(g, XEEN_WINDOW_RIGHT_COLUMN, 0);
    return items_begin(g, rec, item_spell_mode(g->whom_spell), ITEMS_OWNER_SPELL);
}
/* A healing spell's sound (aid's step after its effects before the sound): effect 30 (1Eh), 20 (14h) for Suppress
 * Poison and Suppress Disease (1880E). Returns XEEN_OK when it played; else the spell's own sound stop, or
 * the unknown spell's stop for a spell none of the aid handlers serves (after its effects, as before). Each stop keeps
 * its own guard (its catalogue classification is the guard's). */
static xeen_status aid_sound(xeen_game *g)
{
    switch (g->whom_spell) {
        case XEEN_SPELL_FIRST_AID:
        case XEEN_SPELL_CURE_WOUNDS:
        case XEEN_SPELL_NATURES_CURE:
            if (sound_effect(g, 30u) != XEEN_OK) {
                return panel_stop(g, g->whom_spell == XEEN_SPELL_NATURES_CURE ? NATURES_CURE_STOP_SOUND : AID_STOP_HEAL_SOUND);
            }
            break;
        case XEEN_SPELL_CURE_DISEASE:
        case XEEN_SPELL_CURE_PARALYSIS:
        case XEEN_SPELL_CURE_POISON:
            if (sound_effect(g, 30u) != XEEN_OK) {
                return panel_stop(g, g->whom_spell == XEEN_SPELL_CURE_DISEASE     ? CURE_DISEASE_STOP_SOUND
                                     : g->whom_spell == XEEN_SPELL_CURE_PARALYSIS ? CURE_PARALYSIS_STOP_SOUND
                                                                                  : CURE_POISON_STOP_SOUND);
            }
            break;
        case XEEN_SPELL_POWER_CURE:
            if (sound_effect(g, 30u) != XEEN_OK) {
                return panel_stop(g, POWER_CURE_STOP_SOUND);
            }
            break;
        case XEEN_SPELL_REVITALIZE:
            if (sound_effect(g, 30u) != XEEN_OK) {
                return panel_stop(g, REVITALIZE_STOP_SOUND);
            }
            break;
        case XEEN_SPELL_SUPPRESS_POISON:
            if (sound_effect(g, 20u) != XEEN_OK) {
                return panel_stop(g, SUPPRESS_POISON_STOP_SOUND);
            }
            break;
        case XEEN_SPELL_SUPPRESS_DISEASE:
            if (sound_effect(g, 20u) != XEEN_OK) {
                return panel_stop(g, SUPPRESS_DISEASE_STOP_SOUND);
            }
            break;
        case XEEN_SPELL_RAISE_DEAD:
            if (sound_effect(g, 30u) != XEEN_OK) {
                return panel_stop(g, RAISE_DEAD_STOP_SOUND);
            }
            break;
        case XEEN_SPELL_RESURRECT:
            if (sound_effect(g, 30u) != XEEN_OK) {
                return panel_stop(g, RESURRECT_STOP_SOUND);
            }
            break;
        case XEEN_SPELL_STONE_TO_FLESH:
            if (sound_effect(g, 30u) != XEEN_OK) {
                return panel_stop(g, STONE_TO_FLESH_STOP_SOUND);
            }
            break;
        default:
            return panel_stop(g, AID_STOP_UNKNOWN_SPELL);
    }
    return XEEN_OK;
}
/*
 * The "On Whom?" spells once a member was chosen (TARGET; FFFFh: Esc, nothing more), on the member cast_target
 * gives. An item spell opens the member's items dialog (item_spell_begin). A healing spell, in order: the member's
 * state may refuse it ("Spell Failed!", aid_refused); its effects before the sound (aid_before_sound); its sound
 * (effect 30; 20 for Suppress Poison and Suppress Disease), each spell's own stop when the host cannot play it; then
 * the heal through cast_heal, whose flash and tail (aid_tail) finish the spell, of the spell's HP (aid_heal):
 *  - First Aid, Cure Wounds, Nature's Cure, Power Cure: refused for a dead, stoned or eradicated member; HP only;
 *  - Revitalize, Cure Disease, Cure Paralysis, Cure Poison and Stone to Flesh: no test; a heal of 0, then the
 *    condition cleared (weak, diseased, paralyzed, poisoned, stoned);
 *  - Suppress Poison and Suppress Disease: the condition's level lowered first, then a heal of 0;
 *  - Raise Dead: only a dead member: dead, unconscious and HP cleared, a heal of 1, then one endurance less;
 *  - Resurrect: only an eradicated member: a heal of 0 while still eradicated (no HP), then eradicated cleared, one
 *    endurance less and the temporary age +5 (at most 250); dead is left as it is.
 * Stops: a chosen member past the eight; a spell none of these serves and each spell's sound (aid_sound).
 * Original: after 399FA's answer: First Aid 39D35, Cure Wounds 39E25, Nature's Cure 3A4DE (2AB96 13..15, 39641,
 * 1880E(1Eh), 3977A); Power Cure 3A450; Revitalize 39DC5 (+145h), Cure Disease 3ABE1, Cure Paralysis 3AECB, Cure
 * Poison 3A91F, Stone to Flesh 3B165 (+151h); Suppress Poison 39F6F, Suppress Disease 3A178, 1880E(14h); Raise Dead
 * 3B23D (3977A(member, 1)); Resurrect 3B52E (+152h, +26h); each ends with 2ABB8(1).
 */
static xeen_status cast_aid(xeen_game *g, unsigned target)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned member;
    uint16_t rec;
    xeen_status st;
    if (target == WHOM_CANCELLED) {
        if (is_item_spell(g->whom_spell)) {
            xeen_presentation_trace("mark spell-end %u", g->whom_spell); /* Evidence: 3B495 */
        }
        return cast_handled(g);
    }
    member = cast_target(g, target);
    if (member >= 8u) {
        return panel_stop(g, AID_STOP_MEMBER);
    }
    rec = XEEN_PARTY_RECORD(member);
    g->heal_tail = 0;
    g->whom_record = member;
    if (is_item_spell(g->whom_spell)) {
        return item_spell_begin(g, rec);
    }
    if (aid_refused(g->whom_spell, ds + rec)) {
        return cast_failed(g);
    }
    aid_before_sound(m, g->whom_spell, rec);
    if ((st = aid_sound(g)) != XEEN_OK) {
        return st;
    }
    g->heal_tail = aid_tail(g->whom_spell);
    return cast_heal(g, member, aid_heal(g, g->whom_spell));
}
