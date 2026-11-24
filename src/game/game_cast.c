#include "game_cast.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/*
 * Where native play stops when a cast cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json). A sound stop means the host's audio refused the spell's effect.
 */
enum {
    CAST_STOP_NO_PRESENTATION = 0x17e3c,       /* the scene before the Cast window needs a presentation */
    CAST_STOP_ACTOR = 0x3d9e5,                 /* the combat actor outside the combat party */
    CAST_STOP_ACTOR_MEMBER = 0x3d9e8,          /* the combat actor's member past the six */
    CAST_STOP_SPRITES = 0x3d93c,               /* cast.icn did not load */
    CAST_STOP_WINDOW_TEXT = 0x3dab7,           /* the Cast window's text could not be formed */
    CAST_STOP_PAID_OUTSIDE_COMBAT = 0x391ed,   /* combat's Cast returned outside combat with a paid spell */
    CAST_STOP_LACK_SOUND = 0x39b9c,            /* "Not enough ..." */
    CAST_STOP_LACK_TEXT = 0x39bc7,
    CAST_STOP_ENGAGED_SOUND = 0x39b13,         /* "Can't cast ... while engaged!" */
    CAST_STOP_ENGAGED_TEXT = 0x39b30,
    CAST_STOP_CANNOT_ACT_TEXT = 0x28d32,       /* the caster cannot act: the message could not be formed */
    CAST_STOP_CANNOT_ACT_AT_LOCATION = 0x28d4c,/* the same at a location: not served there */
    CAST_STOP_CANNOT_ACT_SCROLL = 0x28d4c,     /* the same: its scroll could not be shown */
    CAST_STOP_LIST_MEMBER = 0x3f847,           /* the spell list's F1..F6 member choice failed */
    CAST_STOP_SCENE_STAGE = 0x1c1b3,           /* a scene ended in no cast stage */
    JUMP_STOP_QUERY_AHEAD = 0x3a08a,           /* outdoors: the terrain of the cell ahead */
    JUMP_STOP_QUERY_TWO_AHEAD = 0x3a09c,       /* outdoors: the cell two ahead */
    JUMP_STOP_WALL_AHEAD = 0x3a112,            /* indoors: the wall ahead */
    JUMP_STOP_WALL_TWO_AHEAD = 0x3a133,        /* indoors: the wall of the cell ahead */
    JUMP_STOP_SOUND = 0x3a165,
    LIGHT_STOP_SOUND = 0x39cec,
    AWAKEN_STOP_SOUND = 0x39d2e,
    CREATE_FOOD_STOP_SOUND = 0x3af8d,
    SUPER_SHELTER_STOP_SOUND = 0x3af4c,
    DIVINE_INTERVENTION_STOP_SOUND = 0x3b722,
    DAY_OF_PROTECTION_STOP_SOUND = 0x3b791,
    DAY_OF_SORCERY_STOP_SOUND = 0x3b7c2,
    /* variable stop sites (stops_variable.json) */
    CAST_STOP_COMBAT_UNPREPARED = 0x391e5,   /* Cast in combat without the monsters, sprites or services */
    CAST_STOP_UNPREPARED = 0x17e30,          /* Cast in exploration without them */
    CLAIRVOYANCE_STOP_SOUND = 0x3b75d,       /* each effect spell's sound call (after its flag is set) */
    LEVITATE_STOP_SOUND = 0x3a218,
    WALK_ON_WATER_STOP_SOUND = 0x3a5b0,
    WIZARD_EYE_STOP_SOUND = 0x3a22b
};

/* ------------------------- spellcasting
 * C exploring: while the monsters' countdown runs they first move, with one scene pass (as Shoot's prefix); then the
 * Cast Spell dialog: its sprites, the right column, and before each key poll a scene pass (in the spells' UI mode: no
 * countdown, no contact). C casts the ready spell (an index into the class's spell list, or none): a caster who
 * cannot act is refused, the spell points and gems are paid before any target is chosen, and the paid spell's
 * handler runs (cast_dispatch); N chooses the ready spell (the spell-list dialog); F1..F8 choose another caster; Esc
 * leaves. The dialog reports whether a spell was paid: the command tail then charges a tick and the surface's
 * effect. In combat the same dialog serves the acting combatant: its F-keys loop, the exploration-only spells are
 * refused as engaged, and its end returns to the round. Every spell's handler is native, here (cast_dispatch) and in
 * game_cast_*.c: the targets ("On Whom?", "Which Element?"), the amounts (Teleport, Town Portal), the item spells'
 * dialog, and the windows and key loops of Lloyd's Beacon, Detect Monster and Identify Monster; a spell without a
 * native case would stop at its handler's address.
 * Original: 3D8C1; 17E30 (96C0: 2A701, 1B503(1)); cast.icn, window 0Ah, before each 3DB40 poll 4EA0D/1B503(1)
 * (9A1C 3); +A4 into DS:201E (FFh none); 28CFC; 39C16; the table CS:281B; N 3F05D(member, 1); [BP-6]; 17EF4 charges
 * 28178 and runs 17264 before 17F07; in combat 391E5 (9A1C 2, 97FF; 39AF6; back to 391ED); 399FA, 39817, 28ABA;
 * Super Shelter's mode-0Ch Rest. */
static xeen_status cast_loop(xeen_game *);
static xeen_status cast_poll_again(xeen_game *);

/* The casting group whose spell list a class casts from: 1 archers and sorcerers, 2 druids and rangers, 0 every
 * other class (the clerics' list, which the paladins share). Original: 3DA0F (3F7A8). */
unsigned cast_category(uint8_t cls)
{
    static const uint8_t t[8] = {[XEEN_CLASS_ARCHER - 2] = 1,    [XEEN_CLASS_CLERIC - 2] = 0,
                                 [XEEN_CLASS_SORCERER - 2] = 1,  [XEEN_CLASS_ROBBER - 2] = 0,
                                 [XEEN_CLASS_NINJA - 2] = 0,     [XEEN_CLASS_BARBARIAN - 2] = 0,
                                 [XEEN_CLASS_DRUID - 2] = 2,     [XEEN_CLASS_RANGER - 2] = 2};
    return cls >= XEEN_CLASS_ARCHER && cls <= XEEN_CLASS_RANGER ? t[cls - 2u] : 0u;
}
/* A spell's spell-point cost for a caster of LEVEL: the cost table's entry, or for a negative entry that many points
 * per level. Original: 3EB8B, the table DS:1EC2. */
uint16_t cast_sp(const xeen_game *g, unsigned spell, uint16_t level)
{
    int16_t v = (int16_t)xeen_read16(&g->movement, (uint16_t)(XEEN_DS_SPELL_SP_COST + 2u * spell));
    return v < 0 ? (uint16_t)((uint16_t)(v * (int16_t)level) * 0xffffu) : (uint16_t)v;
}
int16_t cast_gems(const xeen_game *g, unsigned spell)
{
    return (int8_t)g->movement.ds[(uint16_t)(XEEN_DS_SPELL_GEM_COST + spell)];
}
/* The member's ready spell from their casting group's list: the list's slot 39 (its "no spell" entry) when none
 * is readied. Original: DS:201E[40 x category + (+A4, 27h for none)]. */
static unsigned cast_ready(const xeen_game *g, uint16_t rec)
{
    const uint8_t *ds = g->movement.ds;
    uint8_t at = ds[(uint16_t)(rec + XEEN_CHARACTER_READY_SPELL)];
    return ds[(uint16_t)(XEEN_DS_CLASS_SPELL_LISTS +
                         XEEN_CLASS_SPELL_LIST * cast_category(ds[(uint16_t)(rec + XEEN_CHARACTER_CLASS)]) +
                         (at == XEEN_NO_READY_SPELL ? 39u : at))];
}
/* The caster's portrait highlight, as the character view's (portrait_highlight). Original: 2A1F6. */
static void cast_highlight(xeen_game *g, unsigned n)
{
    portrait_highlight(g, n);
}
/* The Cast Spell window's text for the member at REC, into text buffer 2: the caster's name, the ready spell's name,
 * its spell-point and gem costs (the gems sign-extended) and the caster's spell points; 0 when it does not fit.
 * Original: AC0F = DS:[6E18] (DS:6E4A). */
int cast_text_of(xeen_game *g, uint16_t rec)
{
    xeen_movement *m = &g->movement;
    const uint8_t *ch = m->ds + rec;
    unsigned spell = cast_ready(g, rec);
    char out[1000];
    int n = snprintf(out, sizeof out, far_text(g, XEEN_DS_CAST_SPELL_TEXT, 0), ds_text(g, rec),
                     spell_name(g, spell),
                     (unsigned)cast_sp(g, spell, xeen_character_current_level(ch)),
                     (unsigned)(uint16_t)cast_gems(g, spell),
                     (unsigned)xeen_read16(m, (uint16_t)(rec + XEEN_CHARACTER_SP)));
    if (n < 0 || (size_t)n >= sizeof out) {
        return 0;
    }
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    return 1;
}
/** Draws the Cast Spell dialog's text for the casting character. */
static int cast_text(xeen_game *g)
{
    return cast_text_of(g, g->cast_record);
}
/* Redraws the dialog for the current caster (after F1..F8 and the spell list too): the text in the right column,
 * the buttons, then the key loop. Original: 3DA06: AC0F, 4E38C(AC0F, 0Ah), 50BF6(0Ah); 3DB2E. */
static xeen_status cast_redraw(xeen_game *g)
{
    if (!cast_text(g)) {
        return panel_stop(g, CAST_STOP_WINDOW_TEXT);
    }
    menu_window_text(g, XEEN_WINDOW_RIGHT_COLUMN, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    location_buttons(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN);
    return cast_loop(g);
}
/* Before each key poll the dialog runs one scene pass; the frame wait returns to cast_after_scene, which polls.
 * Original: 3DB33: 4EA0D, 1B503(1); the poll 3DB40. */
static xeen_status cast_poll_again(xeen_game *g)
{
    g->panel_stage = CAST_SCENE;
    return scene_wait(g, 1, XEEN_TIMER_FRAME_MARK);
}
/* The top of the dialog's key loop: the spells' UI mode (no monster countdown, no contact in its scene passes),
 * then the scene pass and the poll (cast_poll_again). Original: 3DB2E (9A1C = 3). */
static xeen_status cast_loop(xeen_game *g)
{
    g->movement.ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_SPELLS;
    return cast_poll_again(g);
}
/* Undoes the cast's start (cast_entry_reset): the monsters enabled again, the UI mode and the monsters' countdown
 * restored, the key cleared; the cast no longer active. Shared by the dialog's end and the combat quick cast's.
 * Original: 3E12F's restores (96C4 = 1, 9A1C, 96C0, F5FE = 0), also 3D8C1(1)'s. */
void cast_restore(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    ds[XEEN_DS_MONSTERS_ENABLED] = 1;
    ds[XEEN_DS_UI_MODE] = g->cast_mode;
    ds[XEEN_DS_MONSTER_COUNTDOWN] = g->cast_countdown;
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->panel_stage = 0;
    g->cast_active = 0;
}
/* The Cast Spell dialog ends: its window and cast.icn go, the caster's portrait loses its highlight, the routing,
 * mode and monster countdown are restored and the key cleared. Then its caller goes on: combat's command loop, or in
 * exploration a paid spell's tick (charge_tail) or the plain command end.
 * Original: 3E12F with argument 0 (18765(0Ah), 190CF, 18FFF, 42298 sets 96C3 back to FFh; 96C4 = 1, 9A1C and 96C0
 * restored, F5FE = 0); a paid spell continues at 17EF4, none at 17F07. */
static xeen_status cast_end(xeen_game *g)
{
    window_pop(g, XEEN_WINDOW_RIGHT_COLUMN);
    portrait_unhighlight(g);
    cast_restore(g);
    /* Called from combat's command poll, the dialog returns there whatever the mode was: the dead party's poll has
     * mode 1, so the caster was chosen the exploration way. A spell paid outside combat from there has never been
     * observed in the original and stops. Original: 3E12F returns to 3D8C1's caller, combat's 391EC; 3D986 chose
     * the caster from 97BE. */
    if (g->cast_combat_call) {
        g->cast_combat_call = 0;
        if (g->cast_mode != XEEN_UI_MODE_COMBAT && g->cast_done) {
            return game_stop(g, CAST_STOP_PAID_OUTSIDE_COMBAT);
        }
        return combat_cast_return(g, g->cast_done);
    }
    if (g->cast_mode == XEEN_UI_MODE_COMBAT) {
        return combat_cast_return(g, g->cast_done);
    }
    return g->cast_done ? charge_tail(g) : command_tail(g);
}
/*
 * The C command: exploring with the monsters' countdown running, the monsters move and one scene pass runs first;
 * then the Cast dialog opens (cast_open).
 * Original: 17E30: 2A701 and 1B503(1) when 96C0 is set; then 3D8C1(0).
 */
xeen_status cast_command(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    xeen_status status;
    /* Whether this is combat's cast is the caller's to say, not the UI mode's: the dead party's combat poll after a
     * nested fight has the exploration mode. Original: combat's 391E5 calls 3D8C1(0) at once, exploration's 17E30
     * runs its 96C0 prefix first; 9A1C is 1 after a nested 38F5F. */
    int combat = g->cast_combat_call;
    if (!play_hosted(g)) {
        return game_stop(g, combat ? CAST_STOP_COMBAT_UNPREPARED : CAST_STOP_UNPREPARED);
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0x43u);
    if (!combat && ds[XEEN_DS_MONSTER_COUNTDOWN]) {
        g->service_boundary = 0;
        status = run_monster_update(g);
        if (status != XEEN_OK) {
            return game_stop(g, g->service_boundary ? g->service_boundary : RELEASE_STOP_MONSTER_UPDATE);
        }
        /* Before the dialog, one whole scene (no frame mark first); its contact test can start a fight, whose
         * cleanup returns to the scene's frame wait; then the dialog opens. Original: 17E3C's 1B503(1) entered at
         * 4EA35; 1C17E 38F5F; the 1C1B3 frame wait; 3D8C1. */
        if (!g->presentation) {
            return game_stop(g, CAST_STOP_NO_PRESENTATION);
        }
        g->cast_prefix = 1;
        return scene_wait(g, 1, XEEN_TIMER_SCENE_MARK);
    }
    return cast_open(g);
}
/*
 * The caster of a Cast dialog opened outside combat: the member the dialog last used (97BE) while that is a present
 * member, else the last member able to cast (the record's caster byte), who becomes the remembered member.
 * No member can cast (a party of Knights and Robbers only, shrunk at the inn below the remembered member): -1, the
 * remembered member is kept, and the dialog runs on "member -1", the record-sized DS bytes before the party (zero,
 * then the memory metrics, so its SP word reads B414); the highlight is cleared. A preserved original quirk.
 * Original: 3D9CB: [BP-2] -1, 97BE; B2BC, 18886's B410..B41D (+158h = B414); 2A1F6(-1).
 */
static int remembered_caster(uint8_t *ds)
{
    int who = (int8_t)ds[XEEN_DS_CAST_MEMBER];
    if (who >= 0 && who < (int)ds[XEEN_DS_PARTY_COUNT]) {
        return who;
    }
    for (who = (int)ds[XEEN_DS_PARTY_COUNT] - 1; who >= 0; --who) {
        if (ds[(uint16_t)(XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * (unsigned)who + XEEN_CHARACTER_CASTER)]) {
            ds[XEEN_DS_CAST_MEMBER] = (uint8_t)who;
            return who;
        }
    }
    return -1;
}
/*
 * Opens the Cast dialog: the entry state saved (cast_entry_reset); the caster is the combat actor in a fight, else
 * the member the dialog last used, or when that is not a present member the last member able to cast; the dialog's
 * sprites and buttons, the caster highlighted, the right column opened.
 * Original: 17E44: 3D8C1(0): 96C4 = 96E1 = 0, [BP-4] = 9A1C, [BP-8] = 96C0 and 96C0 = 0; 190ED,
 * 190A3(cast.icn) [96C1, 95A8, 98D4], the 1A1E.. buttons (98AC..9898), 9896 = 98D4; the caster 97BE or the last
 * member with +A3 set.
 */
xeen_status cast_open(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    int who;
    int combat = ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT;
    unsigned member;
    cast_entry_reset(g);
    if (combat) {
        /* In combat the caster is the acting combatant (the highlight shows that combat index); its record is the
         * combat party's entry. Original: 3D98C (9A1C 2): 97FF, 2A1F6, 9720[97FF]. */
        who = (int8_t)ds[XEEN_DS_TURN_ACTOR];
        if (who < 0 || who >= (int)ds[XEEN_DS_COMBAT_PARTY_COUNT]) {
            return game_stop(g, CAST_STOP_ACTOR);
        }
        member = ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + (unsigned)who)];
        if (member >= 6u) {
            return game_stop(g, CAST_STOP_ACTOR_MEMBER);
        }
    } else {
        who = remembered_caster(ds);
        member = (unsigned)who;
    }
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_CAST_DIALOG_SPRITES), &h)) {
        ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] - 1u);
        return game_stop(g, CAST_STOP_SPRITES);
    }
    button_tables(m, XEEN_BUTTONS_CAST);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    g->cast_index = (unsigned)who;
    g->cast_record = XEEN_PARTY_RECORD(member);
    g->cast_active = 1;
    cast_highlight(g, g->cast_index);
    panel_window(g, XEEN_WINDOW_RIGHT_COLUMN, 1);
    return cast_redraw(g);
}

/*
 * Pays SPELL's cost for the member at REC (or, with REFUND, gives it back, as "On Whom?"'s Esc does): returns 1
 * with too few spell points (compared signed), 2 with too few gems (the party's 32-bit count), else 0 with both
 * paid, before any target is chosen. Original: 39C16(member, spell), the SP rule 3EB8B, gems F00E; the refund 39C8B
 * (399FA's Esc).
 */
int cast_pay(xeen_game *g, uint16_t rec, unsigned spell, int refund)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t sp = cast_sp(g, spell, xeen_character_current_level(ds + rec)),
             have = xeen_read16(m, (uint16_t)(rec + XEEN_CHARACTER_SP));
    int32_t gems = cast_gems(g, spell);
    uint32_t pool = xeen_ds_read32(m, XEEN_DS_PARTY_GEMS);
    if (refund) {
        xeen_write16(m, (uint16_t)(rec + XEEN_CHARACTER_SP), (uint16_t)(have + sp));
        xeen_ds_write32(m, XEEN_DS_PARTY_GEMS, (uint32_t)(pool + (uint32_t)gems));
        return 0;
    }
    if ((int16_t)have < (int16_t)sp) {
        return 1;
    }
    if ((uint32_t)gems > pool) {
        return 2; /* the cost sign-extended to 32 bits, then compared unsigned (the original's CWDE) */
    }
    xeen_write16(m, (uint16_t)(rec + XEEN_CHARACTER_SP), (uint16_t)(have - sp));
    xeen_ds_write32(m, XEEN_DS_PARTY_GEMS, (uint32_t)(pool - (uint32_t)gems));
    return 0;
}
/* The frame of the two refusal messages (cast_lack, cast_engaged): the buttons pushed and the UI mode kept, then
 * FFh while the message is open (cast_lack_closed restores it). Original: 39B7F's 190ED/1919B and 9A1C. */
static void refusal_begin(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    g->saved_ui_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = 0xff;
}
/* Shows a refusal's TEXT in the message box and waits for a key at BOUNDARY (no scene pass meanwhile). */
static xeen_status refusal_wait(xeen_game *g, const char *text, uint32_t boundary)
{
    message_window(g, text);
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    return panel_set(g, CAST_LACK, boundary);
}
/* A spell the caster cannot pay for: the refusal sound, "Not enough %s to Cast %s" (the reason: spell points or
 * gems) in the scroll box until a key, without a scene; the routing and mode restored, then the Cast dialog's loop.
 * Original: 39B7F(spell, reason) (190ED/1919B, 9A1C saved and FFh, 1880E(15h), the DS:68AA text with DS:27B2's
 * reason, window 6, 189BC, the busy 189E3, 18765(6), 189BC, 18FFF, 9A1C back); then 3E0A9 (9A1C = 3). */
static xeen_status cast_lack(xeen_game *g, unsigned spell, int reason)
{
    xeen_movement *m = &g->movement;
    char out[160];
    uint16_t name = xeen_read16(m, (uint16_t)(XEEN_DS_SPELL_COST_NAME_BASE + 4u * (unsigned)reason));
    refusal_begin(g);
    if (sound_effect(g, XEEN_CUE_REFUSED) != XEEN_OK) {
        return panel_stop(g, CAST_STOP_LACK_SOUND);
    }
    if (snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_CAST_NOT_ENOUGH), ds_text(g, name), spell_name(g, spell)) < 0) {
        return panel_stop(g, CAST_STOP_LACK_TEXT);
    }
    return refusal_wait(g, out, CAST_LACK_BOUNDARY);
}
/* Cast in combat refuses a spell that only works exploring (exploration_only_spell): the spell points and gems are
 * given back and nothing counts as cast, then the lack's message frame shows "Can't cast %s while engaged!" until a
 * key; cast_lack_closed goes on. Original: 39AF6(spell) ([BP-4] 2): the refund 39C8B, [BP-6] = 0; 39B7F's frame with
 * DS:6887, window 6, the busy 189E3 (39B5C); 3E0A9 and the dialog's loop. */
static xeen_status cast_engaged(xeen_game *g, unsigned spell)
{
    xeen_movement *m = &g->movement;
    char out[160];
    (void)cast_pay(g, xeen_read16(m, XEEN_DS_ACTING_CHARACTER), spell, 1);
    g->cast_done = 0;
    refusal_begin(g);
    if (sound_effect(g, XEEN_CUE_REFUSED) != XEEN_OK) {
        return panel_stop(g, CAST_STOP_ENGAGED_SOUND);
    }
    if (snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_CAST_ENGAGED), spell_name(g, spell)) < 0) {
        return panel_stop(g, CAST_STOP_ENGAGED_TEXT);
    }
    return refusal_wait(g, out, CAST_ENGAGED_BOUNDARY);
}
/**
 * Continues after a refusal message closed (not enough spell points or gems, or "engaged"): the message hidden and
 * the UI mode restored, then by who cast: Use's Town Portal runs anyway (cast_portal), any other Use power goes on
 * to Use's end (use_after_power), and the Cast dialog itself returns to its spell list (cast_loop).
 */
static xeen_status cast_lack_closed(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    message_hide(g);
    ds[XEEN_DS_UI_MODE] = g->saved_ui_mode;
    /* Use's Town Portal runs even after the "engaged" refusal, without cast.icn loaded (an original quirk, kept).
     * Original: 449A7 jumps to 449B5 (3AF94) past 449A9's 190A3. */
    if (g->use_active && g->use_power == USE_POWER_TOWN_PORTAL) {
        return cast_portal(g);
    }
    if (g->use_active) {
        return use_after_power(g); /* 39B7E, 4478B's 449E1 */
    }
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_SPELLS;
    return cast_loop(g);
}


/* One side's wall in a cell word (1C680's mask argument: the 4 bits at the side's shift). */
#define JUMP_WALL_MASK 0x0fu
/* Jump outdoors (original 3A088..3A0C0): the cell ahead (1C1C1(7)) fails on terrain 1 (F48E); the cell two ahead
 * (1C1C1(0Eh)) fails on surface 8 or 0 (F490) or terrain 1. Sets *BLOCKED when the jump fails; returns the stop's
 * status when a cell cannot be queried, else XEEN_OK. */
static xeen_status jump_blocked_outdoors(xeen_game *g, int *blocked)
{
    xeen_movement *m = &g->movement;
    uint16_t query_result = 0;
    if (xeen_navigation_query(m, XEEN_STEP_AHEAD, &query_result) != XEEN_OK) {
        return panel_stop(g, JUMP_STOP_QUERY_AHEAD);
    }
    if (xeen_read16(m, XEEN_DS_CELL_WALLS) == 1u) {
        *blocked = 1;
        return XEEN_OK;
    }
    if (xeen_navigation_query(m, XEEN_STEP_TWO_AHEAD, &query_result) != XEEN_OK) {
        return panel_stop(g, JUMP_STOP_QUERY_TWO_AHEAD);
    }
    *blocked = xeen_read16(m, XEEN_DS_CELL_SURFACE) == 8u || !xeen_read16(m, XEEN_DS_CELL_SURFACE) ||
               xeen_read16(m, XEEN_DS_CELL_WALLS) == 1u;
    return XEEN_OK;
}
/* Jump indoors (original 3A0C3..3A13D): the wall on the facing side (shift 279E[dir]) of the party's own cell, then
 * of the cell ahead (DS:0655/0715 step XEEN_STEP_AHEAD), each 1C680(x, y, 0Fh, shift). Sets *BLOCKED at the first
 * wall; returns the stop's status when a cell cannot be read, else XEEN_OK. */
static xeen_status jump_blocked_indoors(xeen_game *g, int *blocked)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned dir = ds[XEEN_DS_PARTY_DIRECTION] & 3u;
    uint16_t wall = 0, shift = xeen_read16(m, XEEN_DS_DIRECTION_WALL_SHIFT(dir));
    int16_t x = (int16_t)xeen_signed_byte(ds[XEEN_DS_PARTY_X]), y = (int16_t)xeen_signed_byte(ds[XEEN_DS_PARTY_Y]);
    int16_t ahead_x = (int16_t)(x + xeen_signed_byte(ds[XEEN_DS_STEP_X(dir, XEEN_STEP_AHEAD)]));
    int16_t ahead_y = (int16_t)(y + xeen_signed_byte(ds[XEEN_DS_STEP_Y(dir, XEEN_STEP_AHEAD)]));
    if (xeen_maze_lookup(m, x, y, JUMP_WALL_MASK, shift, &wall) != XEEN_OK) {
        return panel_stop(g, JUMP_STOP_WALL_AHEAD);
    }
    if (wall) {
        *blocked = 1;
        return XEEN_OK;
    }
    if (xeen_maze_lookup(m, ahead_x, ahead_y, JUMP_WALL_MASK, shift, &wall) != XEEN_OK) {
        return panel_stop(g, JUMP_STOP_WALL_TWO_AHEAD);
    }
    *blocked = wall != 0;
    return XEEN_OK;
}
/* Jump (exploration only): the party moves two cells ahead. The way is tested outdoors (jump_blocked_outdoors) or
 * indoors (jump_blocked_indoors): a blocked way fails ("Spell Failed!", 39641). A jump past the map's edge is
 * committed by the cast's tail (the step at 17264). Then the move (3A13F: DS:065C/071C, the step two ahead), the
 * sound (1880E(33h)) and the move counts as the party's action (3067 = 1).
 * Original: 3A07D; 17334 19D94. */
static xeen_status cast_jump(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned dir = ds[XEEN_DS_PARTY_DIRECTION] & 3u;
    int blocked = 0;
    xeen_status st = xeen_read16(m, XEEN_DS_OUTDOORS) ? jump_blocked_outdoors(g, &blocked)
                                                      : jump_blocked_indoors(g, &blocked);
    if (st != XEEN_OK) {
        return st;
    }
    if (blocked) {
        return cast_failed(g);
    }
    ds[XEEN_DS_PARTY_X] = (uint8_t)(ds[XEEN_DS_PARTY_X] + ds[XEEN_DS_STEP_X(dir, XEEN_STEP_TWO_AHEAD)]);
    ds[XEEN_DS_PARTY_Y] = (uint8_t)(ds[XEEN_DS_PARTY_Y] + ds[XEEN_DS_STEP_Y(dir, XEEN_STEP_TWO_AHEAD)]);
    if (sound_effect(g, XEEN_CUE_TRANSPORTED) != XEEN_OK) {
        return panel_stop(g, JUMP_STOP_SOUND);
    }
    ds[XEEN_DS_PARTY_ACTED] = 1;
    return cast_handled(g);
}

/* A spell's handler has finished: the combat quick cast ends, an item's power returns to Use, and the Cast dialog
 * closes (in the spells' UI mode first). Original: 3E0A9 with [BP-6] set (9A1C = 3); the quick cast's 3E12F; Use's
 * RETF to 3BDE3. */
xeen_status cast_handled(xeen_game *g)
{
    if (g->cast_quick) {
        return quick_cast_end(g); /* 3D8C1(1)'s 3E12F */
    }
    if (g->use_active) {
        return use_after_power(g); /* 4478B's RETF to 3BDE3 */
    }
    g->movement.ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_SPELLS;
    return cast_end(g);
}
/* The caster's level (2C909 of the acting character), the value the party effects store. */
static uint16_t caster_level(const xeen_game *g)
{
    const xeen_movement *m = &g->movement;
    return xeen_character_current_level(m->ds + xeen_read16(m, XEEN_DS_ACTING_CHARACTER));
}
/* The eight spells that only work outside combat. Cast refuses them in combat with the "engaged" message and a
 * refund (cast_dispatch), the combat quick cast refunds them silently (quick_cast) and Use refuses all but Enchant
 * Item (use_power). Original: 39AF6 from 3DCA4, 3DCF3, 3DDCA, 3DE29, 3DF18, 3DF6F, 3DFBE, 3E01C. */
int exploration_only_spell(unsigned spell)
{
    return spell == XEEN_SPELL_ENCHANT_ITEM || spell == XEEN_SPELL_ETHEREALIZE ||
           spell == XEEN_SPELL_JUMP || spell == XEEN_SPELL_LLOYDS_BEACON ||
           spell == XEEN_SPELL_SUPER_SHELTER || spell == XEEN_SPELL_TELEPORT ||
           spell == XEEN_SPELL_TOWN_PORTAL || spell == XEEN_SPELL_WIZARD_EYE;
}
/* Day of Protection (handler 3B764): four effect bytes (Walk on Water, Heroism, Holy Bonus, Bless), the four
 * protections and the light = the caster's level (AL; the words with AH 0). Returns its sound (20), which
 * cast_dispatch then plays. */
static uint16_t apply_day_of_protection(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, level = (uint8_t)caster_level(g);
    ds[XEEN_DS_EFFECT_WALK_ON_WATER] = ds[XEEN_DS_EFFECT_HEROISM] = ds[XEEN_DS_EFFECT_HOLY_BONUS] =
            ds[XEEN_DS_EFFECT_BLESS] = level;
    xeen_write16(m, XEEN_DS_PROTECTION_POISON, level);
    xeen_write16(m, XEEN_DS_PROTECTION_COLD, level);
    xeen_write16(m, XEEN_DS_PROTECTION_ELECTRICITY, level);
    xeen_write16(m, XEEN_DS_PROTECTION_FIRE, level);
    xeen_write16(m, XEEN_DS_PARTY_LIGHT, level);
    return 20u;
}
/* Day of Sorcery (handler 3B79A): four effect bytes (Power Shield, Clairvoyance, Wizard Eye, Levitate) and the light
 * = the caster's level (AL; the light with AH 0), the minimap switch off. Returns its sound (20). */
static uint16_t apply_day_of_sorcery(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, level = (uint8_t)caster_level(g);
    ds[XEEN_DS_EFFECT_POWER_SHIELD] = ds[XEEN_DS_EFFECT_CLAIRVOYANCE] = ds[XEEN_DS_EFFECT_WIZARD_EYE] =
            ds[XEEN_DS_EFFECT_LEVITATE] = level;
    xeen_write16(m, XEEN_DS_PARTY_LIGHT, level);
    ds[XEEN_DS_MINIMAP_SHOWN] = 0;
    return 20u;
}
/* Divine Intervention: the caster's magical ageing +5 (at most 250); every member's 16 condition bytes cleared but
 * eradication kept, and a member not eradicated healed to the maximum HP; then the sound and the bar redrawn. Stops
 * when the sound cannot play. Original: 3B68F (+26h; 13FE7 over +143h..+152h; 2AAC2), 1880E(14h), 2ABB8(1). */
static xeen_status cast_divine_intervention(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned i;
    uint8_t *caster = ds + xeen_read16(m, XEEN_DS_ACTING_CHARACTER);
    caster[XEEN_CHARACTER_AGE_BONUS] =
            (uint8_t)(caster[XEEN_CHARACTER_AGE_BONUS] + 5u > 250u
                              ? 250u
                              : caster[XEEN_CHARACTER_AGE_BONUS] + 5u);
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        uint8_t *ch = ds + XEEN_PARTY_RECORD(i),
                eradicated = ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ERADICATED)];
        memset(ch + XEEN_CHARACTER_CONDITIONS, 0, 16);
        ch[XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ERADICATED)] = eradicated;
        if (!eradicated) {
            XEEN_DAMAGE_NUMBERS(hp_restore(ds, XEEN_PARTY_RECORD(i), xeen_combat_max_hp(m, ch))); /* add-on hook */
            xeen_write16(m, (uint16_t)(XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * i + XEEN_CHARACTER_HP),
                         xeen_combat_max_hp(m, ch));
        }
    }
    if (sound_effect(g, 20u) != XEEN_OK) {
        return panel_stop(g, DIVINE_INTERVENTION_STOP_SOUND);
    }
    bar_redraw(g);
    return cast_handled(g);
}
/* The handler each spell runs, spell by spell: the original's jump table CS:281B. Every spell has a native case in
 * cast_dispatch, so the table is read only where a handler stops before changing anything (the blessings' sound,
 * whose stop reports the handler's entry) and by the unreachable default. Its values stay original addresses
 * because traces and the stop registry name the handlers by them. */
static const uint32_t spell_handlers[XEEN_SPELL_COUNT] = {
        0x3ab7c, 0x39cf3, 0x3b733, 0x3a1f5, 0x3b756, 0x3ab9f, 0x3af82, 0x3abe1, 0x3aecb,
        0x3a91f, 0x39e25, 0x3b350, 0x3b764, 0x3b79a, 0x3ac65, 0x3a9c1, 0x3b68f, 0x3b7c9,
        0x3b4bb, 0x3b443, 0x39ec4, 0x3b2d6, 0x3b21a, 0x3af5f, 0x3aea8, 0x3a97f, 0x39d35,
        0x39da2, 0x3b7ec, 0x3b807, 0x3a58d, 0x3a434, 0x3b50b, 0x3b82a, 0x3a232, 0x3b649,
        0x3b84d, 0x3b603, 0x3b870, 0x3b893, 0x3a07d, 0x3a211, 0x39cda, 0x3a54b, 0x3a5da,
        0x3b8e8, 0x3b420, 0x3b5e0, 0x3b3be, 0x3a4de, 0x39f29, 0x3b903, 0x3a450, 0x3a903,
        0x3b392, 0x39fec, 0x3b23d, 0x3b1c5, 0x3b52e, 0x39dc5, 0x3b926, 0x39f06, 0x39e92,
        0x3b66c, 0x3b165, 0x3b626, 0x3af2b, 0x3a178, 0x39f6f, 0x3ac88, 0x3ac41, 0x3af94,
        0x39f4c, 0x3a05a, 0x3a5a9, 0x3a21f};
/* The party effect a blessing sets to the caster's level. */
static uint16_t blessing_effect(unsigned spell)
{
    switch (spell) {
        case XEEN_SPELL_BLESS:
            return XEEN_DS_EFFECT_BLESS;
        case XEEN_SPELL_HEROISM:
            return XEEN_DS_EFFECT_HEROISM;
        case XEEN_SPELL_HOLY_BONUS:
            return XEEN_DS_EFFECT_HOLY_BONUS;
        default:
            return XEEN_DS_EFFECT_POWER_SHIELD;
    }
}
/* The party effect a switch spell turns on (to 1). */
static uint16_t switched_effect(unsigned spell)
{
    switch (spell) {
        case XEEN_SPELL_CLAIRVOYANCE:
            return XEEN_DS_EFFECT_CLAIRVOYANCE;
        case XEEN_SPELL_LEVITATE:
            return XEEN_DS_EFFECT_LEVITATE;
        case XEEN_SPELL_WALK_ON_WATER:
            return XEEN_DS_EFFECT_WALK_ON_WATER;
        default:
            return XEEN_DS_EFFECT_WIZARD_EYE;
    }
}
/* Awaken: every member's sleep ends, and unconsciousness too for a member whose HP is above 0 (compared signed). */
static void awaken_party(xeen_movement *m)
{
    uint8_t *ds = m->ds;
    unsigned i;
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        uint16_t rec = XEEN_PARTY_RECORD(i);
        ds[(uint16_t)(rec + XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ASLEEP))] = 0;
        if ((int16_t)xeen_read16(m, (uint16_t)(rec + XEEN_CHARACTER_HP)) > 0) {
            ds[(uint16_t)(rec + XEEN_CHARACTER_CONDITION(XEEN_CONDITION_UNCONSCIOUS))] = 0;
        }
    }
}
/* Awaken (handler 39CF3): the party woken (awaken_party), the bar redrawn. Returns its sound (30). */
static uint16_t apply_awaken(xeen_game *g)
{
    awaken_party(&g->movement);
    bar_redraw(g);
    return 30u;
}
/* The item spells (Enchant Item 3B443, Item to Gold 3B893, Recharge Item 3B1C5) choose a member, then an item; the
 * trace marks where their dialog begins. */
static xeen_status cast_item_spell(xeen_game *g, unsigned spell)
{
    xeen_presentation_trace("mark spell-begin %u", spell);
    return cast_whom(g, spell);
}
/* Protection from Elements (handler 39FEC): "Which Element?" with the caster's level x 2 + 5 (the level signed), at
 * most 200. */
static xeen_status cast_protection_from_elements(xeen_game *g)
{
    int v = (int)(int16_t)caster_level(g) * 2 + 5;
    g->element_value = (uint16_t)(v > 200 ? 200 : v);
    return cast_element(g);
}
/* A switch spell (Clairvoyance 3B756, Levitate 3A211, Walk on Water 3A5A9, Wizard Eye 3A21F) turns its effect on
 * (Wizard Eye also turns the minimap off). Returns its sound (20), played after the effect: a failed sound stops at the
 * sound's call (owner decision, Phase 239; the calls at 3B75D, 3A218, 3A5B0 and 3A22B). */
static uint16_t apply_switch_spell(xeen_game *g, unsigned spell)
{
    uint8_t *ds = g->movement.ds;
    ds[switched_effect(spell)] = 1;
    if (spell == XEEN_SPELL_WIZARD_EYE) {
        ds[XEEN_DS_MINIMAP_SHOWN] = 0;
    }
    return 20u;
}
/* Light (handler 39CDA): one more step of party light. While the view is lit the cast also counts as the party's
 * action, which the next scene build acts on (39CE5 sets 3067 only when F60C is nonzero). Returns its sound (39). */
static uint16_t apply_light(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_write16(m, XEEN_DS_PARTY_LIGHT, (uint16_t)(xeen_read16(m, XEEN_DS_PARTY_LIGHT) + 1u));
    if (xeen_read16(m, XEEN_DS_VIEW_LIGHT_LEVEL)) {
        m->ds[XEEN_DS_PARTY_ACTED] = 1;
    }
    return 39u;
}
/* Create Food (handler 3AF82): food + the party size (a 16-bit add: wraps at 65536). Returns its sound (20). */
static uint16_t apply_create_food(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_write16(m, XEEN_DS_PARTY_FOOD, (uint16_t)(xeen_read16(m, XEEN_DS_PARTY_FOOD) + m->ds[XEEN_DS_PARTY_COUNT]));
    return 20u;
}
/* Super Shelter (handler 3AF2B), on a map that allows it: the UI mode is saved and set to the shelter's. Returns its
 * sound (30); after it the party rests (cast_dispatch): that mode skips the can't-rest test and the ten sleeping steps
 * (the eight hours pass at once: no monster can approach or interrupt); rest_return restores the mode.
 * Original: 9A1C = 0Ch, 1880E(1Eh), 28E35; 2FC7E(480); 3AF57. */
static uint16_t apply_super_shelter(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    g->shelter = 1;
    g->shelter_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_SHELTER;
    return 30u;
}
/*
 * Runs a paid spell. The caller has paid it and made the caster the acting character (cast_engage); the three
 * callers differ before this point and the spell's end differs after it (cast_handled):
 *  - the Cast dialog (cast_try): a spell that only works exploring is refused here in combat with the "engaged"
 *    message (cast_engaged), which refunds it;
 *  - the combat quick cast (quick_cast): the same spells are refunded silently before this call;
 *  - an item's power through Use (use_power): Use refuses its own list first (the same spells but Enchant Item, whose
 *    power works in combat), so this function never refuses an item's spell.
 * After that refusal it routes: the volleys (cast_attack), the member's spells and the item spells ("On Whom?",
 * cast_whom), the spells with their own dialog or routine, and the party's own effects, each applied by a function
 * named for its spell (apply_*, the handler's address in its contract) that returns the spell's sound, which the
 * dispatcher plays. Every stop of these handlers stays here: a failed sound stops at that sound's call, or for the
 * blessings, which play their sound before changing anything, at the handler's entry (the table's value); the default,
 * which no spell reaches, reports the table too. Spell numbers from XEEN_SPELL_COUNT up do nothing. Original: the jump
 * table CS:281B; the combat refusal tests [BP-4] = 2, which Use's 4478B never passes.
 */
xeen_status cast_dispatch(xeen_game *g, unsigned spell)
{
    if (spell >= XEEN_SPELL_COUNT) {
        return cast_handled(g);
    }
    if (g->cast_mode == XEEN_UI_MODE_COMBAT && !g->use_active && exploration_only_spell(spell)) {
        return cast_engaged(g, spell);
    }
    switch (spell) {
        /* The attack spells: the volley and its targets (Original: 281A7). */
        case XEEN_SPELL_ACID_SPRAY:
        case XEEN_SPELL_ENERGY_BLAST:
        case XEEN_SPELL_FLYING_FIST:
        case XEEN_SPELL_INSECT_SPRAY:
        case XEEN_SPELL_MAGIC_ARROW:
        case XEEN_SPELL_PAIN:
        case XEEN_SPELL_SHRAPMETAL:
        case XEEN_SPELL_SLEEP:
        case XEEN_SPELL_SPARKS:
        case XEEN_SPELL_TOXIC_CLOUD:
        case XEEN_SPELL_BEAST_MASTER:
        case XEEN_SPELL_COLD_RAY:
        case XEEN_SPELL_DEADLY_SWARM:
        case XEEN_SPELL_FANTASTIC_FREEZE:
        case XEEN_SPELL_FIERY_FLAIL:
        case XEEN_SPELL_FINGER_OF_DEATH:
        case XEEN_SPELL_FIRE_BALL:
        case XEEN_SPELL_FROST_BITE:
        case XEEN_SPELL_GOLEM_STOPPER:
        case XEEN_SPELL_LIGHTNING_BOLT:
        case XEEN_SPELL_POISON_VOLLEY:
        case XEEN_SPELL_TURN_UNDEAD:
        case XEEN_SPELL_DANCING_SWORD:
        case XEEN_SPELL_DRAGON_SLEEP:
        case XEEN_SPELL_ELEMENTAL_STORM:
        case XEEN_SPELL_HOLY_WORD:
        case XEEN_SPELL_HYPNOTIZE:
        case XEEN_SPELL_IMPLOSION:
        case XEEN_SPELL_INCINERATE:
        case XEEN_SPELL_INFERNO:
        case XEEN_SPELL_MASS_DISTORTION:
        case XEEN_SPELL_MEGA_VOLTS:
        case XEEN_SPELL_MOON_RAY:
        case XEEN_SPELL_PRISMATIC_LIGHT:
        case XEEN_SPELL_STAR_BURST:
        case XEEN_SPELL_SUN_RAY:
            return cast_attack(g, spell);
        /* "On Whom?", then the chosen member's handler (Original: 399FA). */
        case XEEN_SPELL_FIRST_AID:
        case XEEN_SPELL_CURE_WOUNDS:
        case XEEN_SPELL_REVITALIZE:
        case XEEN_SPELL_SUPPRESS_POISON:
        case XEEN_SPELL_POWER_CURE:
        case XEEN_SPELL_CURE_DISEASE:
        case XEEN_SPELL_CURE_PARALYSIS:
        case XEEN_SPELL_CURE_POISON:
        case XEEN_SPELL_NATURES_CURE:
        case XEEN_SPELL_RAISE_DEAD:
        case XEEN_SPELL_RESURRECT:
        case XEEN_SPELL_STONE_TO_FLESH:
        case XEEN_SPELL_SUPPRESS_DISEASE:
            return cast_whom(g, spell);
        case XEEN_SPELL_ENCHANT_ITEM:
        case XEEN_SPELL_ITEM_TO_GOLD:
        case XEEN_SPELL_RECHARGE_ITEM:
            return cast_item_spell(g, spell);
        /* The spells with their own dialog or routine. */
        case XEEN_SPELL_PROTECTION_FROM_ELEMENTS:
            return cast_protection_from_elements(g);
        case XEEN_SPELL_JUMP:
            return cast_jump(g);
        case XEEN_SPELL_DIVINE_INTERVENTION:
            return cast_divine_intervention(g);
        case XEEN_SPELL_ETHEREALIZE:
            return cast_etherealize(g);
        case XEEN_SPELL_LLOYDS_BEACON:
            return cast_lloyd(g);
        case XEEN_SPELL_TELEPORT:
            return cast_teleport(g);
        case XEEN_SPELL_TIME_DISTORTION:
            return cast_time_distortion(g);
        case XEEN_SPELL_TOWN_PORTAL:
            return cast_portal(g);
        case XEEN_SPELL_DETECT_MONSTER:
            return cast_detect(g);
        case XEEN_SPELL_IDENTIFY_MONSTER:
            return cast_identify(g);
        /* The party's own effects: apply_* applies the effect and returns the spell's sound, played here; a failed
         * sound stops at its call (for the switch spells, whose effect is already on then, by owner decision,
         * Phase 239). */
        case XEEN_SPELL_CLAIRVOYANCE:
        case XEEN_SPELL_LEVITATE:
        case XEEN_SPELL_WALK_ON_WATER:
        case XEEN_SPELL_WIZARD_EYE:
            if (sound_effect(g, apply_switch_spell(g, spell)) != XEEN_OK) {
                return panel_stop(g, spell == XEEN_SPELL_CLAIRVOYANCE    ? CLAIRVOYANCE_STOP_SOUND
                                     : spell == XEEN_SPELL_LEVITATE      ? LEVITATE_STOP_SOUND
                                     : spell == XEEN_SPELL_WALK_ON_WATER ? WALK_ON_WATER_STOP_SOUND
                                                                         : WIZARD_EYE_STOP_SOUND);
            }
            return cast_handled(g);
        case XEEN_SPELL_DAY_OF_PROTECTION:
            if (sound_effect(g, apply_day_of_protection(g)) != XEEN_OK) {
                return panel_stop(g, DAY_OF_PROTECTION_STOP_SOUND);
            }
            return cast_handled(g);
        case XEEN_SPELL_DAY_OF_SORCERY:
            if (sound_effect(g, apply_day_of_sorcery(g)) != XEEN_OK) {
                return panel_stop(g, DAY_OF_SORCERY_STOP_SOUND);
            }
            return cast_handled(g);
        case XEEN_SPELL_LIGHT:
            if (sound_effect(g, apply_light(g)) != XEEN_OK) {
                return panel_stop(g, LIGHT_STOP_SOUND);
            }
            return cast_handled(g);
        case XEEN_SPELL_AWAKEN:
            if (sound_effect(g, apply_awaken(g)) != XEEN_OK) {
                return panel_stop(g, AWAKEN_STOP_SOUND);
            }
            return cast_handled(g);
        case XEEN_SPELL_CREATE_FOOD:
            if (sound_effect(g, apply_create_food(g)) != XEEN_OK) {
                return panel_stop(g, CREATE_FOOD_STOP_SOUND);
            }
            return cast_handled(g);
        case XEEN_SPELL_SUPER_SHELTER: /* 39641: "Spell Failed!" on a map that forbids it; else the party rests */
            if (xeen_read16(&g->movement, XEEN_DS_MAP_RESTRICTIONS) & XEEN_MAP_NO_SUPER_SHELTER) {
                return cast_failed(g);
            }
            if (sound_effect(g, apply_super_shelter(g)) != XEEN_OK) {
                return panel_stop(g, SUPER_SHELTER_STOP_SOUND);
            }
            return rest(g);
        /* The blessings play their sound first and only then set their effect to the caster's level, so a failed
         * sound stops at the handler's entry, the table's value. */
        case XEEN_SPELL_BLESS:
        case XEEN_SPELL_HEROISM:
        case XEEN_SPELL_HOLY_BONUS:
        case XEEN_SPELL_POWER_SHIELD:
            if (sound_effect(g, spell == XEEN_SPELL_POWER_SHIELD ? 20u : 30u) != XEEN_OK) {
                return panel_stop(g, spell_handlers[spell]);
            }
            g->movement.ds[blessing_effect(spell)] = (uint8_t)caster_level(g);
            return cast_handled(g);
        default:
            return panel_stop(g, spell_handlers[spell]);
    }
}
/* What a cast saves and resets at its start (the dialog's and the combat quick cast's): the monsters disabled and the
 * map-reloaded flag cleared, the UI mode and the monsters' countdown kept for the end (the countdown then 0),
 * nothing paid yet. cast_restore undoes it. Original: 3D8C1's entry: 96C4 = 96E1 = 0, [BP-4] = 9A1C, [BP-8] = 96C0. */
void cast_entry_reset(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    ds[XEEN_DS_MONSTERS_ENABLED] = 0;
    ds[XEEN_DS_MAP_RELOADED] = 0;
    g->cast_mode = ds[XEEN_DS_UI_MODE];
    g->cast_done = 0;
    g->cast_countdown = ds[XEEN_DS_MONSTER_COUNTDOWN];
    ds[XEEN_DS_MONSTER_COUNTDOWN] = 0;
}
/* A paid spell's start, just before its handler (also the combat quick cast's): the cast counts as done, the spell
 * is recorded (and is the value the handler is entered with), the caster at REC becomes the acting character and the
 * cast's UI mode is back. Original: 3DBF9: [BP-6] = 1, AX = the spell, 9800 = REC, 9A1C = [BP-4]. */
void cast_engage(xeen_game *g, uint16_t rec, unsigned spell)
{
    xeen_movement *m = &g->movement;
    g->cast_done = 1;
    g->cast_spell = spell;
    g->handler_ax = (uint16_t)spell; /* 3DBF9: AX = the spell */
    xeen_write16(m, XEEN_DS_ACTING_CHARACTER, rec);
    xeen_write16(m, XEEN_DS_ACTING_CHARACTER + 2u, XEEN_DS_SEGMENT);
    m->ds[XEEN_DS_UI_MODE] = g->cast_mode;
}
/* C, cast the ready spell. With none readied the key loop goes on. A caster who cannot act gets "%s is not in any
 * condition to perform actions!" in a message scroll (at a location: not served), whose close returns to the loop. A
 * spell the caster cannot afford shows the lack (cast_lack). Otherwise it is paid and run (cast_engage,
 * cast_dispatch). Original: 3DB7C; 28CFC (A43F, 18F2F mode 1, 9A1C 3); 39C16, 39B7F. */
static xeen_status cast_try(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t rec = g->cast_record;
    unsigned spell;
    int lack;
    if (ds[(uint16_t)(rec + XEEN_CHARACTER_READY_SPELL)] == XEEN_NO_READY_SPELL) {
        return cast_loop(g);
    }
    if (xeen_training_refused(ds + rec)) {
        char out[160];
        int n = snprintf(out, sizeof out, far_text(g, XEEN_DS_MEMBER_CANNOT_ACT_TEXT, 0), ds_text(g, rec));
        if (n < 0 || (size_t)n >= sizeof out) {
            return panel_stop(g, CAST_STOP_CANNOT_ACT_TEXT);
        }
        memcpy(ds + XEEN_DS_TEXT_BUFFER_0, out, (size_t)n + 1u);
        if (ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_LOCATION) {
            return panel_stop(g, CAST_STOP_CANNOT_ACT_AT_LOCATION);
        }
        {
            char text[200];
            (void)snprintf(text, sizeof text, ds_text(g, XEEN_DS_MESSAGE_FORMAT), out);
            g->panel_stage = 0;
            if (!open_scroll_message(g, SCROLL_CAST_REFUSED, text)) {
                return game_stop(g, CAST_STOP_CANNOT_ACT_SCROLL);
            }
        }
        return XEEN_SERVICE_REQUIRED;
    }
    spell = cast_ready(g, rec);
    if ((lack = cast_pay(g, rec, spell, 0)) != 0) {
        return cast_lack(g, spell, lack);
    }
    cast_engage(g, rec, spell);
    return cast_dispatch(g, spell);
}
/* A message scroll the dialog opened was closed (scroll_key): after the cannot-act refusal the key loop goes on;
 * after "Spell Failed!" the spell's handler ends (Lloyd's Beacon through its own end). Original: 28CFC's return to
 * 3E11F; 39641's to 3E0A9. */
xeen_status cast_scroll_closed(xeen_game *g, int owner)
{
    g->panel_stage = CAST_SCENE;
    if (owner == SCROLL_SPELL_FAILED && g->cast_spell == XEEN_SPELL_IDENTIFY_MONSTER) {
        xeen_presentation_trace("mark spell-end 34"); /* Evidence: 3A251 -> 3A433 */
    }
    if (owner == SCROLL_SPELL_FAILED && g->lloyd_tail) {
        return lloyd_end(g);
    }
    return owner == SCROLL_CAST_REFUSED ? cast_loop(g) : cast_handled(g);
}
static xeen_status cast_list_begin(xeen_game *);
/* A key in the Cast dialog: Escape ends it (the UI mode back); C casts the readied spell (cast_try); N opens the
 * new-spell list; F1..F8 choose the caster (a present member exploring; in combat they only loop); any other key
 * loops. Original: 3DB45 (CS:28B3; C9h..D0h; an absent member 3E0FA -> 3E11F -> 3DB33 keeps the dialog's mode). */
static xeen_status cast_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (key == XEEN_KEY_ESCAPE) {
        ds[XEEN_DS_UI_MODE] = g->cast_mode;
        return cast_end(g);
    }
    if (key == 'C') {
        return cast_try(g);
    }
    if (key == 'N') {
        ds[XEEN_DS_UI_MODE] = g->cast_mode;
        return cast_list_begin(g);
    }
    if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F8) {
        unsigned n = key - XEEN_KEY_F1;
        if (g->cast_mode == XEEN_UI_MODE_COMBAT) {
            return cast_loop(g);
        }
        ds[XEEN_DS_UI_MODE] = g->cast_mode;
        xeen_write16(m, XEEN_DS_INPUT_CODE, (uint16_t)n);
        /* An absent member (3E0FA) goes through 3E11F to 3DB33,
         * past 3DB2E's 9A1C = 3: the next poll keeps the dialog's mode. */
        if (ds[XEEN_DS_PARTY_COUNT] <= n) {
            return cast_poll_again(g);
        }
        g->cast_record = XEEN_PARTY_RECORD(n);
        cast_highlight(g, n);
        ds[XEEN_DS_CAST_MEMBER] = (uint8_t)n;
        return cast_redraw(g);
    }
    return cast_loop(g);
}

/* The Cast dialog's N (new spell): the guild's list dialog over the caster's known spells, to ready one: "Spell Pts"
 * and the points below the list; a digit highlights its line (colour 15; again: none); Enter or S readies the
 * highlighted spell; Escape leaves; F1..F6 choose another member; the arrows and PgUp/PgDn scroll.
 * Original: 3F05D(member, 1) (3EBD6 mode 1; +A4 by 3F915; 97BE, 2A1F6). */
static xeen_status cast_list_begin(xeen_game *g)
{
    xeen_presentation_trace_extra(
            "clmark %x",
            (unsigned)xeen_read16(&g->movement, XEEN_DS_INPUT_CODE)); /* Evidence: 3E0B0 */
    g->list_caster_spells = 1;
    g->list_choice = -1;
    memset(g->list_colour, 9, sizeof g->list_colour);
    g->guild_record = g->cast_record;
    return guild_open(g);
}
/** Closes the spell list (window 19h) and returns to the Cast dialog. */
static xeen_status cast_list_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    window_pop(g, XEEN_WINDOW_VIEW_DIALOG);
    if (g->list_choice >= 0) {
        ds[(uint16_t)(g->guild_record + XEEN_CHARACTER_READY_SPELL)] = g->guild_index[g->list_choice];
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->guild_active = 0;
    g->list_caster_spells = 0;
    g->cast_record = g->guild_record;
    xeen_presentation_trace_extra("clend"); /* Evidence: 3E0C3, after 3F05D's RETF */
    return cast_redraw(g);
}
/* The spell list's poll again (every key but Esc, a chosen Enter or S, and a failed member key ends there). */
static xeen_status cast_list_poll(xeen_game *g)
{
    return panel_set(g, CAST_LIST, GUILD_POLL_BOUNDARY);
}
/* Digit N (1..9, 10 for 0) on the list, of the lines shown from the list's top: a line past the list, or an empty
 * list, does nothing; the highlighted line again clears the highlight (colour 9: no choice); another line becomes the
 * only highlighted one (colour 15) and the choice. The page is redrawn when a line was chosen or cleared. */
static void cast_list_choose(xeen_game *g, unsigned n)
{
    int index;
    if (!g->guild_count || n > g->guild_count) {
        return;
    }
    index = (int)(n - 1u + g->guild_top);
    if (index == g->list_choice) {
        g->list_colour[index] = 9;
        g->list_choice = -1;
    } else {
        memset(g->list_colour, 9, sizeof g->list_colour);
        g->list_choice = index;
        g->list_colour[index] = 0xf;
    }
    guild_page(g);
}
/* PgUp or PgDn (KEY): the list's top ten lines up or down, not below 0; the list is built again (cast_list_build) and
 * a top that would show fewer than ten lines shows the last ten, or the first line when that top is below 1. Unlike
 * the guild's own list (guild_key), whose top is only kept at or above 0. */
static void cast_list_page(xeen_game *g, uint16_t key)
{
    int top = (int)g->guild_top + (key == XEEN_KEY_PAGE_UP ? -10 : 10);
    if (top < 0) {
        top = 0;
    }
    g->guild_count = cast_list_build(g);
    if (top + 10 > (int)g->guild_count) {
        top = (int)g->guild_count - 10;
        if (top < 1) {
            top = 0;
        }
    }
    g->guild_top = (unsigned)top;
    guild_page(g);
}
/*
 * A key on the Cast dialog's spell list (add-on spell_list_sort first). Esc leaves without a choice; Enter or S leaves
 * with the highlighted spell readied (cast_list_end), or polls again when none is; 1..9 and 0 highlight a line
 * (cast_list_choose); Up and Down scroll a line (guild_scroll), PgUp and PgDn ten (cast_list_page); F1..F6 show that
 * member's list (list_member_key, the list's top 0 when it changed). Every other key polls again. Stop: the member's
 * list. Original: 3F05D mode 1's key loop (the member keys 3F6F1, shared with the guild; the top 3F67A).
 */
xeen_status cast_list_key(xeen_game *g, uint16_t key)
{
    {
        xeen_status st;
        if (spell_list_sort_key(g, key, &st)) {
            return st;
        }
    } /* add-on spell_list_sort (off: 0) */
    if (key == XEEN_KEY_ESCAPE) {
        g->list_choice = -1;
        return cast_list_end(g);
    }
    if (key == XEEN_KEY_ENTER || key == 'S') {
        return g->list_choice < 0 ? cast_list_poll(g) : cast_list_end(g);
    }
    if (key >= '0' && key <= '9') {
        cast_list_choose(g, key == '0' ? 10u : (unsigned)(key - '0'));
    } else if (key == XEEN_KEY_UP || key == XEEN_KEY_DOWN) {
        guild_scroll(g, key);
        guild_page(g);
    } else if (key == XEEN_KEY_PAGE_UP || key == XEEN_KEY_PAGE_DOWN) {
        cast_list_page(g, key);
    } else if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F6) {
        int r = list_member_key(g, key - XEEN_KEY_F1); /* 3F6F1, shared */
        if (r < 0) {
            return panel_stop(g, CAST_STOP_LIST_MEMBER);
        }
        if (r > 0) {
            g->guild_top = 0;
            guild_page(g);
        } /* SI 0, 3F67A */
    }
    return cast_list_poll(g);
}

/* The stages' scene ends (panel_after_scene) and polls (xeen_game_resume_panel). */
xeen_status cast_after_scene(xeen_game *g)
{
    switch (g->panel_stage) {
        case CAST_SCENE:
            return panel_set(g, CAST_POLL, CAST_POLL_BOUNDARY);
        case CAST_WHOM_SCENE:
            return panel_set(g, CAST_WHOM_POLL, CAST_WHOM_BOUNDARY);
        case CAST_FLASH_SCENE:
            (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
            return cast_flash_frame(g);
        case LLOYD_SCENE:
            return panel_set(g, LLOYD_POLL, LLOYD_BOUNDARY);
        case TRAVEL_SCENE:
            return cast_handled(g);
        case DETECT_SCENE:
            return panel_set(g, DETECT_POLL, DETECT_BOUNDARY);
        case IDENTIFY_SCENE: {
            uint16_t r[4];
            xeen_presentation *p = g->presentation;
            if (p && xeen_presentation_window(p, XEEN_WINDOW_IDENTIFY_MONSTER, r)) {
                (void)xeen_text_border(p->framebuffer, p->framebuffer_size, r);
            }
            xeen_presentation_trace("frame %u", (unsigned)XEEN_WINDOW_IDENTIFY_MONSTER); /* Evidence: each 4F6F8 */
            (void)xeen_presentation_update_window(p, XEEN_WINDOW_VIEW_FRAME);
            return panel_set(g, IDENTIFY_POLL, IDENTIFY_BOUNDARY);
        }
        case CAST_ELEMENT_SCENE: /* 398FE..39962 */
            menu_window_text(g, XEEN_WINDOW_ELEMENT_CHOICE, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_CAST_WHICH_ELEMENT_AGAIN));
            location_buttons(g);
            (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_FRAME);
            return panel_set(g, CAST_ELEMENT_POLL, CAST_ELEMENT_BOUNDARY);
        default:
            return panel_stop(g, CAST_STOP_SCENE_STAGE);
    }
}
xeen_status cast_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    uint16_t key;
    (void)bios;
    (void)user;
    switch (g->panel_stage) {
        case CAST_POLL:
            key = panel_poll(g);
            return key ? cast_key(g, key) : cast_loop(g); /* 3DB4A: a tick, the scene again */
        case CAST_WHOM_POLL:
            key = panel_poll(g);
            return key ? cast_whom_key(g, key) : cast_whom_scene(g);
        case CAST_LACK:
            key = panel_poll(g);
            return key ? cast_lack_closed(g) : XEEN_SERVICE_REQUIRED;
        case CAST_LIST:
            key = spell_list_sort_poll(g); /* add-on spell_list_sort (off: panel_poll) */
            return key ? cast_list_key(g, key) : XEEN_SERVICE_REQUIRED;
        case CAST_ELEMENT_POLL:
            key = panel_poll(g);
            return key ? cast_element_key(g, key) : cast_element_scene(g);
        case LLOYD_POLL: /* 3A7B2 */
            key = panel_poll(g);
            return key ? lloyd_key(g, key) : lloyd_scene(g);
        case DETECT_POLL: /* 3AB49 */
            key = panel_poll(g);
            return key ? detect_end(g) : detect_scene(g);
        case IDENTIFY_POLL: /* 3A40A */
            key = panel_poll(g);
            return key ? identify_end(g) : identify_scene(g);
        default:
            return XEEN_BAD_INPUT;
    }
}
