/*
 * The monster-target spells (Phase 248, from game_cast.c): the 39817 element prompt and its answer, the volleys
 * and their end, Moon Ray's next step (its party heal included), and cast_attack, the handlers that strike
 * monsters. cast_dispatch, which calls them, is game_cast.c's.
 */
#include "game_cast.h"
#include "xeen/monster_record.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Where native play stops when an attack spell cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json). */
enum {
    ELEMENT_STOP_SPRITES = 0x39832,          /* element.icn */
    ELEMENT_STOP_SOUND = 0x3a03b,
    MOON_RAY_STOP_SOUND = 0x3b3e7,
    ATTACK_STOP_UNKNOWN_SPELL = 0x281a7,      /* a spell none of the attack handlers serves */
    VOLLEY_STOP_CALL = 0x281a7               /* the volley (281A7) stopped without reporting where */
};
/* Where each attack spell's handler plays its sound effect (1880E): cast_volley stops there when the effect is
 * refused (a variable stop site of cast_volley's last argument, tools/xeen_replay/stops_variable.json). */
enum {
    VOLLEY_STOP_DANCING_SWORD = 0x3b383,
    VOLLEY_STOP_DRAGON_SLEEP = 0x3b7dd,
    VOLLEY_STOP_ELEMENTAL_STORM = 0x3b4ea,
    VOLLEY_STOP_HOLY_WORD = 0x3b51f,
    VOLLEY_STOP_HYPNOTIZE = 0x3b83e,
    VOLLEY_STOP_IMPLOSION = 0x3b65d,
    VOLLEY_STOP_INCINERATE = 0x3b861,
    VOLLEY_STOP_INFERNO = 0x3b617,
    VOLLEY_STOP_MASS_DISTORTION = 0x3b434,
    VOLLEY_STOP_MEGA_VOLTS = 0x3b5f4,
    VOLLEY_STOP_MOON_RAY = 0x3b3d3,
    VOLLEY_STOP_PRISMATIC_LIGHT = 0x3b3af,
    VOLLEY_STOP_STAR_BURST = 0x3b680,
    VOLLEY_STOP_SUN_RAY = 0x3b63a,
    VOLLEY_STOP_ACID_SPRAY = 0x3ab90,
    VOLLEY_STOP_BEAST_MASTER = 0x3b747,
    VOLLEY_STOP_COLD_RAY = 0x3abd2,
    VOLLEY_STOP_DEADLY_SWARM = 0x3ac79,
    VOLLEY_STOP_ENERGY_BLAST = 0x39ef7,
    VOLLEY_STOP_FANTASTIC_FREEZE = 0x3b22e,
    VOLLEY_STOP_FIERY_FLAIL = 0x3af73,
    VOLLEY_STOP_FINGER_OF_DEATH = 0x3aebc,
    VOLLEY_STOP_FIRE_BALL = 0x3a9b2,
    VOLLEY_STOP_FROST_BITE = 0x3b800,
    VOLLEY_STOP_GOLEM_STOPPER = 0x3b81b,
    VOLLEY_STOP_LIGHTNING_BOLT = 0x3a57e,
    VOLLEY_STOP_POISON_VOLLEY = 0x3b917,
    VOLLEY_STOP_TURN_UNDEAD = 0x3a06e,
    VOLLEY_STOP_FLYING_FIST = 0x39db6,
    VOLLEY_STOP_INSECT_SPRAY = 0x3b884,
    VOLLEY_STOP_MAGIC_ARROW = 0x3b8fc,
    VOLLEY_STOP_PAIN = 0x39f3d,
    VOLLEY_STOP_SHRAPMETAL = 0x3b949,
    VOLLEY_STOP_SLEEP = 0x39f1a,
    VOLLEY_STOP_SPARKS = 0x39eb5,
    VOLLEY_STOP_TOXIC_CLOUD = 0x39f60
};
/* Enters the "Which Element?" prompt's scene wait (the prompt Protection from Elements opens): each pass is a scene,
 * the prompt redrawn and a key poll, a tick between passes without a key. Its keys, as the prompt labels them: F
 * "Fire" (answers 2), E "Elec" (3), C "Cold" (4), A "Acid" (5); Escape gives the spell points and gems back and
 * answers -1; any other key polls again; the prompt then closes with the UI mode restored.
 * Original: 39817(spell): 9A1C saved and FFh, 190ED, 190A3(element.icn), the 1AAC.. buttons, 9896 = 98D4,
 * 1871B(0Fh), DS:67DF, 50BF6(0Fh); per poll 4EA0D, 1B503(0), 4F6F8(0Fh), DS:682C, 50BF6(3), 189E3 (39963; no key:
 * 4EA1C); keys CS:03A6 (Esc 39C8B); 18765(0Fh), 190CF, 18FFF. */
xeen_status cast_element_scene(xeen_game *g)
{
    g->panel_stage = CAST_ELEMENT_SCENE;
    return scene_wait(g, 0, XEEN_TIMER_FRAME_MARK);
}
/** Opens the "Which Element?" prompt for a spell that needs one: the UI mode saved and FFh, the button set and the
 * prompt's sprites pushed (the depth undone if they fail: a stop) with the elements' button table, the prompt's
 * window, text and buttons shown; then its scene wait (cast_element_scene). Original: 39817. */
xeen_status cast_element(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    g->element_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = 0xff;
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_CAST_ELEMENT_SPRITES), &h)) {
        ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] - 1u);
        return panel_stop(g, ELEMENT_STOP_SPRITES);
    }
    button_tables(m, XEEN_BUTTONS_ELEMENTS);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    panel_window(g, XEEN_WINDOW_ELEMENT_CHOICE, 1);
    menu_window_text(g, XEEN_WINDOW_ELEMENT_CHOICE, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_CAST_WHICH_ELEMENT));
    location_buttons(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_ELEMENT_CHOICE);
    return cast_element_scene(g);
}
static xeen_status cast_element_done(xeen_game *, int);
/** Answers the element choice with KEY. */
xeen_status cast_element_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int answer;
    if (key == XEEN_KEY_ESCAPE) {
        answer = -1;
        (void)cast_pay(g, xeen_read16(m, XEEN_DS_ACTING_CHARACTER), 0x37u, 1);
    } else if (key == 'F') {
        answer = 2;
    } else if (key == 'E') {
        answer = 3;
    } else if (key == 'C') {
        answer = 4;
    } else if (key == 'A') {
        answer = 5;
    } else {
        return cast_element_scene(g);
    }
    window_pop(g, 15);
    ds[XEEN_DS_UI_MODE] = g->element_mode;
    return cast_element_done(g, answer);
}
/* Protection from Elements answered: Esc (-1) ends the spell at once; Fire, Electricity, Cold or Poison (answers
 * 2..5, the keys F E C A) set that protection to the caster's value (level x 2 + 5, at most 200), an answer of 0 or 1
 * sets none; then the spell's sound and the party bar. Original: 3A013 after 39817: the answer + 1 indexes CS:0A0C;
 * EFFC, EFFE, F000, F002 (2C909); 1880E(14h), 2ABB8(1). */
static xeen_status cast_element_done(xeen_game *g, int answer)
{
    xeen_movement *m = &g->movement;
    if (answer < 0) {
        return cast_handled(g);
    }
    if (answer >= 2) {
        xeen_write16(m, (uint16_t)(XEEN_DS_PROTECTION_FIRE + 2u * (unsigned)(answer - 2)),
                     g->element_value);
    }
    if (sound_effect(g, 20u) != XEEN_OK) {
        return panel_stop(g, ELEMENT_STOP_SOUND);
    }
    bar_redraw(g); /* 3A041 2ABB8(1) */
    return cast_handled(g);
}
/* An attack spell's volley (Magic Arrow and the others), exploring or in combat alike: the volley's fixed DAMAGE (0
 * for the type table's), its TYPE and MODE (XEEN_VOLLEY_ONE the first target, _CELL the first tier with a target,
 * _COLUMN every tier) stored, the volley's presentation, the sound effect FX unless FX is negative (Frost Bite and
 * Magic Arrow pass -1: no sound), then the volley (xeen_combat_spell_volley with the spell's ICON). Stops at SITE when
 * the sound is refused, and at the volley's boundary (the 281A7 call's own when it names none) when the volley stops. A
 * treasure dialog the volley opens (no monster in sight, treasure pending) is the shared one (owner 4); its
 * settlement resumes the spell. Otherwise cast_volley_done ends the spell.
 * Original: 9730, 972E, 972C; 281A7(icon) (37621's 972E table); 287FF's 2EA68; 3E0A9. */
xeen_status cast_volley(xeen_game *g, uint16_t damage, uint16_t type, uint16_t mode, int fx,
                               unsigned icon, uint32_t site)
{
    xeen_movement *m = &g->movement;
    xeen_sprite_archives archives = sprite_archives(g, NULL);
    uint32_t boundary = 0;
    xeen_write16(m, XEEN_DS_VOLLEY_DAMAGE, damage);
    xeen_write16(m, XEEN_DS_DAMAGE_TYPE, type);
    xeen_write16(m, XEEN_DS_VOLLEY_MODE, mode);
    volley_presentation(g);
    if (fx >= 0 && sound_effect(g, (uint16_t)fx) != XEEN_OK) {
        return panel_stop(g, site);
    }
    if (xeen_combat_spell_volley(&g->combat, m, g->monster_database, g->monster_database_size,
                                 &g->sprites, &archives, g->event_random, g->event_random_user,
                                 game_audio(g), icon,
                                 &boundary) != XEEN_OK) {
        return boundary == XEEN_TREASURE_CALL ? treasure_dialog(g, TREASURE_SPELL_VOLLEY)
                                    : panel_stop(g, boundary ? boundary : VOLLEY_STOP_CALL);
    }
    return cast_volley_done(g);
}
/* The handler's code after its 281A7 call, also when a treasure
 * dialog (owner 4) settled inside it: Moon Ray's party loop, else the RETF. */
xeen_status cast_volley_done(xeen_game *g)
{
    if (g->moon_active) {
        return moon_ray_next(g);
    }
    return cast_handled(g);
}
/* Moon Ray's next member (the party's size read again each pass): the sound, a heal of 1..30 (its flash; the heal's
 * tail comes back here); after the last member the bar is redrawn and the handler ends. The draw's AX is 0, as the
 * effect's AX would be replaced before any use (only the trace sees it). Stops when the sound cannot play.
 * Original: 3B3E1..3B41F (SI; 1880E(1Eh), random(1, 30), 3977A(member, roll), heal_tail 48; 2ABB8(1); 504DA enters
 * with 1880E's AX: 0 with sound off (18823), else the driver's value, which INT 21h 2Ch replaces). */
xeen_status moon_ray_next(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint16_t v = 0;
    unsigned member = g->moon_next;
    if (member >= m->ds[XEEN_DS_PARTY_COUNT]) {
        g->moon_active = 0;
        bar_redraw(g); /* 3B418 2ABB8(1) */
        return cast_handled(g);
    }
    ++g->moon_next;
    if (sound_effect(g, 30u) != XEEN_OK) {
        return panel_stop(g, MOON_RAY_STOP_SOUND);
    }
    (void)xeen_entity_rng_range(g->event_random(g->event_random_user, 0), 1u, 30u, &v);
    g->heal_tail = XEEN_SPELL_MOON_RAY;
    g->whom_record = member;
    return cast_heal(g, member, v);
}
/* The CS:281B handlers that only set up 281A7 (the caster's level is 2C909
 * of 9800). Energy Blast 39EC4 draws random(2, 6) before the level; AX is
 * still the spell id from the dispatcher (3DBF9) at that 504DA. 
 * Cold Ray (random(2, 4)), Fire Ball (random(3, 7)) and Lightning Bolt
 * (random(4, 6)) draw the same way; Poison Volley's 972E 11h makes 281A7
 * shoot every party row through 37452 (each member's own 38818 hit test,
 * 9730 10 per hit, 36738 type 3 with 972E 5). */
/* The rest of the volley family (handler listings:
 * phase88/handlers.asm). Dancing Sword draws random(6, 14) before 2C909;
 * Elemental Storm's 972E is random(2, 5), its FX and icon the 57FF:[972E-2]
 * (0Dh, 0Eh, 0Fh, 11h) and 5800:[972E-2] (0, 5, 9, 0Ah) bytes; Prismatic
 * Light's 972E is random(0, 6). Moon Ray continues after 281A7
 * (moon_ray_next). */
/* A volley handler's random(LO, HI) before its 281A7 call: the event RNG entered with the handler's AX (still the
 * spell id from the dispatcher, 3DBF9). */
static uint16_t handler_roll(xeen_game *g, uint16_t lo, uint16_t hi)
{
    uint16_t v = 0;
    (void)xeen_entity_rng_range(g->event_random(g->event_random_user, g->handler_ax), lo, hi, &v);
    return v;
}
xeen_status cast_attack(xeen_game *g, unsigned spell)
{
    xeen_movement *m = &g->movement;
    uint16_t level = xeen_character_current_level(m->ds + xeen_read16(m, XEEN_DS_ACTING_CHARACTER)),
             v = 0;
    static const uint8_t storm_fx[4] = {13, 14, 15, 17},
                         storm_icon[4] = {0, 5, 9, 10};
    switch (spell) {
        case XEEN_SPELL_DANCING_SWORD: /* 3B350 */
            v = handler_roll(g, 6u, 14u);
            return cast_volley(g, (uint16_t)(v * level), XEEN_ATTACK_PHYSICAL, XEEN_VOLLEY_CELL, 18, 14,
                               VOLLEY_STOP_DANCING_SWORD);
        case XEEN_SPELL_DRAGON_SLEEP:
            return cast_volley(g, 0, XEEN_DAMAGE_DRAGON_SLEEP, XEEN_VOLLEY_ONE, 18, 7, VOLLEY_STOP_DRAGON_SLEEP);
        case XEEN_SPELL_ELEMENTAL_STORM: /* 3B4BB */
            v = handler_roll(g, 2u, 5u);
            /* v: XEEN_ATTACK_FIRE .. XEEN_ATTACK_POISON */
            return cast_volley(g, 150, v, XEEN_VOLLEY_COLUMN, storm_fx[v - 2u], storm_icon[v - 2u],
                               VOLLEY_STOP_ELEMENTAL_STORM);
        case XEEN_SPELL_HOLY_WORD:
            return cast_volley(g, 0, XEEN_DAMAGE_HOLY_WORD, XEEN_VOLLEY_CELL, 18, 13, VOLLEY_STOP_HOLY_WORD);
        case XEEN_SPELL_HYPNOTIZE:
            return cast_volley(g, 0, XEEN_DAMAGE_HYPNOTIZE, XEEN_VOLLEY_CELL, 18, 7, VOLLEY_STOP_HYPNOTIZE);
        case XEEN_SPELL_IMPLOSION:
            return cast_volley(g, 1000, XEEN_ATTACK_ENERGY, XEEN_VOLLEY_ONE, 18, 6, VOLLEY_STOP_IMPLOSION);
        case XEEN_SPELL_INCINERATE:
            return cast_volley(g, 250, XEEN_ATTACK_FIRE, XEEN_VOLLEY_ONE, 22, 1, VOLLEY_STOP_INCINERATE);
        case XEEN_SPELL_INFERNO:
            return cast_volley(g, 250, XEEN_ATTACK_FIRE, XEEN_VOLLEY_CELL, 13, 1, VOLLEY_STOP_INFERNO);
        case XEEN_SPELL_MASS_DISTORTION:
            return cast_volley(g, 0, XEEN_DAMAGE_MASS_DISTORTION, XEEN_VOLLEY_CELL, 18, 6, VOLLEY_STOP_MASS_DISTORTION);
        case XEEN_SPELL_MEGA_VOLTS:
            return cast_volley(g, 150, XEEN_ATTACK_ELECTRICITY, XEEN_VOLLEY_CELL, 14, 4, VOLLEY_STOP_MEGA_VOLTS);
        case XEEN_SPELL_MOON_RAY:
            g->moon_active = 1;
            g->moon_next = 0;
            return cast_volley(g, 30, XEEN_ATTACK_ENERGY, XEEN_VOLLEY_COLUMN, 16, 13, VOLLEY_STOP_MOON_RAY);
        case XEEN_SPELL_PRISMATIC_LIGHT: /* 3B392 */
            v = handler_roll(g, 0u, 6u);
            /* v: any attack type, XEEN_ATTACK_PHYSICAL .. XEEN_ATTACK_ENERGY */
            return cast_volley(g, 80, v, XEEN_VOLLEY_COLUMN, 18, 14, VOLLEY_STOP_PRISMATIC_LIGHT);
        case XEEN_SPELL_STAR_BURST:
            return cast_volley(g, 500, XEEN_ATTACK_FIRE, XEEN_VOLLEY_COLUMN, 13, 15, VOLLEY_STOP_STAR_BURST);
        case XEEN_SPELL_SUN_RAY:
            return cast_volley(g, 200, XEEN_ATTACK_ENERGY, XEEN_VOLLEY_COLUMN, 16, 13, VOLLEY_STOP_SUN_RAY);
        case XEEN_SPELL_ACID_SPRAY:
            return cast_volley(g, 15, XEEN_ATTACK_POISON, XEEN_VOLLEY_COLUMN, 17, 10, VOLLEY_STOP_ACID_SPRAY);
        case XEEN_SPELL_BEAST_MASTER:
            return cast_volley(g, 0, XEEN_DAMAGE_BEAST_MASTER, XEEN_VOLLEY_CELL, 18, 7, VOLLEY_STOP_BEAST_MASTER);
        case XEEN_SPELL_COLD_RAY: /* 3AB9F */
            v = handler_roll(g, 2u, 4u);
            return cast_volley(g, (uint16_t)(v * level), XEEN_ATTACK_COLD, XEEN_VOLLEY_COLUMN, 15, 8,
                               VOLLEY_STOP_COLD_RAY);
        case XEEN_SPELL_DEADLY_SWARM:
            return cast_volley(g, 40, XEEN_ATTACK_PHYSICAL, XEEN_VOLLEY_CELL, 13, 15, VOLLEY_STOP_DEADLY_SWARM);
        case XEEN_SPELL_ENERGY_BLAST: /* 39EC4 */
            v = handler_roll(g, 2u, 6u);
            return cast_volley(g, (uint16_t)(v * level), XEEN_ATTACK_ENERGY, XEEN_VOLLEY_ONE, 16, 13,
                               VOLLEY_STOP_ENERGY_BLAST);
        case XEEN_SPELL_FANTASTIC_FREEZE:
            return cast_volley(g, 40, XEEN_ATTACK_COLD, XEEN_VOLLEY_CELL, 15, 8, VOLLEY_STOP_FANTASTIC_FREEZE);
        case XEEN_SPELL_FIERY_FLAIL:
            return cast_volley(g, 100, XEEN_ATTACK_FIRE, XEEN_VOLLEY_ONE, 13, 2, VOLLEY_STOP_FIERY_FLAIL);
        case XEEN_SPELL_FINGER_OF_DEATH:
            return cast_volley(g, 0, XEEN_DAMAGE_FINGER_OF_DEATH, XEEN_VOLLEY_CELL, 18, 14,
                               VOLLEY_STOP_FINGER_OF_DEATH);
        case XEEN_SPELL_FIRE_BALL: /* 3A97F */
            v = handler_roll(g, 3u, 7u);
            return cast_volley(g, (uint16_t)(v * level), XEEN_ATTACK_FIRE, XEEN_VOLLEY_CELL, 13, 0,
                               VOLLEY_STOP_FIRE_BALL);
        case XEEN_SPELL_FROST_BITE:
            return cast_volley(g, 35, XEEN_ATTACK_COLD, XEEN_VOLLEY_ONE, -1, 8, VOLLEY_STOP_FROST_BITE);
        case XEEN_SPELL_GOLEM_STOPPER:
            return cast_volley(g, 0, XEEN_DAMAGE_GOLEM_STOPPER, XEEN_VOLLEY_ONE, 16, 6, VOLLEY_STOP_GOLEM_STOPPER);
        case XEEN_SPELL_LIGHTNING_BOLT: /* 3A54B */
            v = handler_roll(g, 4u, 6u);
            return cast_volley(g, (uint16_t)(v * level), XEEN_ATTACK_ELECTRICITY, XEEN_VOLLEY_CELL, 14, 3,
                               VOLLEY_STOP_LIGHTNING_BOLT);
        case XEEN_SPELL_POISON_VOLLEY:
            return cast_volley(g, 10, XEEN_DAMAGE_POISON_VOLLEY, XEEN_VOLLEY_COLUMN, 49, 11, VOLLEY_STOP_POISON_VOLLEY);
        case XEEN_SPELL_TURN_UNDEAD:
            return cast_volley(g, 0, XEEN_DAMAGE_TURN_UNDEAD, XEEN_VOLLEY_CELL, 18, 13, VOLLEY_STOP_TURN_UNDEAD);
        case XEEN_SPELL_FLYING_FIST:
            return cast_volley(g, 6, XEEN_ATTACK_PHYSICAL, XEEN_VOLLEY_ONE, 18, 14, VOLLEY_STOP_FLYING_FIST);
        case XEEN_SPELL_INSECT_SPRAY:
            return cast_volley(g, 0, XEEN_DAMAGE_INSECT_SPRAY, XEEN_VOLLEY_CELL, 17, 10, VOLLEY_STOP_INSECT_SPRAY);
        case XEEN_SPELL_MAGIC_ARROW:
            return cast_volley(g, 0, XEEN_DAMAGE_MAGIC_ARROW, XEEN_VOLLEY_ONE, -1, 11, VOLLEY_STOP_MAGIC_ARROW);
        case XEEN_SPELL_PAIN:
            return cast_volley(g, 8, XEEN_ATTACK_PHYSICAL, XEEN_VOLLEY_CELL, 18, 14, VOLLEY_STOP_PAIN);
        case XEEN_SPELL_SHRAPMETAL:
            return cast_volley(g, (uint16_t)(level << 1), XEEN_ATTACK_PHYSICAL, XEEN_VOLLEY_CELL, 16, 15,
                               VOLLEY_STOP_SHRAPMETAL);
        case XEEN_SPELL_SLEEP:
            return cast_volley(g, 0, XEEN_DAMAGE_SLEEP, XEEN_VOLLEY_CELL, 18, 7, VOLLEY_STOP_SLEEP);
        case XEEN_SPELL_SPARKS:
            return cast_volley(g, (uint16_t)(level << 1), XEEN_ATTACK_ELECTRICITY, XEEN_VOLLEY_CELL, 14, 5,
                               VOLLEY_STOP_SPARKS);
        case XEEN_SPELL_TOXIC_CLOUD:
            return cast_volley(g, 10, XEEN_ATTACK_POISON, XEEN_VOLLEY_CELL, 17, 10, VOLLEY_STOP_TOXIC_CLOUD);
        default:
            return panel_stop(g, ATTACK_STOP_UNKNOWN_SPELL);
    }
}
