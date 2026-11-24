/*
 * The character view (Phase 246, from game_shop.c): 44AE6, exploration's F1..F6, with its portrait
 * highlight, its text, cursor tick and keys (view_key; I opens the 44F92 items dialog of game_shop.c), the
 * Exchange 4798C. A statistic's details (428F1, with the character_details add-on's hooks) and the awards list
 * (41581) are game_details.c's.
 */
#include "game_items.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when a character's view or the exchange cannot go on; each value is the original address
 * of the step (tools/xeen_replay/stops.json).
 */
enum {
    VIEW_STOP_NO_PRESENTATION = 0x44ae6,
    VIEW_STOP_TEXTS = 0x2b6ec,               /* viewtext.bin */
    VIEW_STOP_SPRITES = 0x44b60,             /* view.icn */
    VIEW_STOP_CHARACTER_TEXT = 0x46b5c,      /* the character's text could not be formed */
    VIEW_STOP_TEXT = 0x44bd8,                /* the screen's text does not fit */
    VIEW_STOP_NO_RANDOM = 0x17e08,           /* no random source for the end */
    VIEW_STOP_ITEMS_RETURN_SPRITES = 0x44de3,/* view.icn again after the items screen */
    EXCHANGE_STOP_SPRITES = 0x479ae,          /* esc.icn */
    /* variable stop sites (stops_variable.json) */
    VIEW_STOP_RELEASE = 0x17e08              /* the monsters' release after the view stopped without reporting where */
};
/* Highlights member N's portrait: nothing when it is highlighted already or highlighting is off (FEh); else the old
 * highlight's portrait gets its plain frame back (unless there was none, FFh), N's portrait gets the highlight frame,
 * and the party bar is shown. Original: 2A1F6(n): F458 frame 96C3 + 9 at (17B2[96C3] - 1, 95h), frame 8 at
 * (17B2[n] - 1, 95h), 96C3 = n, 50BF6(21h). */
void portrait_highlight(xeen_game *g, unsigned n)
{
    uint8_t *ds = g->movement.ds;
    uint8_t old = ds[XEEN_DS_HIGHLIGHTED_PORTRAIT];
    uint16_t h = xeen_read16(&g->movement, XEEN_DS_GLOBAL_SPRITES);
    if (old == n || old == 0xfeu) {
        return;
    }
    if (old != 0xffu) {
        exit_draw(g, h, (uint16_t)((int)(int8_t)old + 9),
                  (int16_t)(ds[(uint16_t)(XEEN_DS_PORTRAIT_X + (unsigned)(int)(int8_t)old)] - 1), 149, 0);
    }
    exit_draw(g, h, 8, (int16_t)(ds[(uint16_t)(XEEN_DS_PORTRAIT_X + n)] - 1), 149, 0);
    ds[XEEN_DS_HIGHLIGHTED_PORTRAIT] = (uint8_t)n;
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_PARTY_BAR);
}
/* Removes the portrait highlight (the view's, a location's and the Cast dialog's end): the highlighted portrait (any
 * at 0 or above, compared signed) gets its plain frame back, none is highlighted, and the party bar is shown. (An
 * earlier native cleared the highlight without the draw, so the yellow frame stayed until another bar redraw.)
 * Original: 42298 (44E93, 28056, the Cast end): F458 frame 96C3 + 9 at (17B2[96C3] - 1, 95h), 96C3 = FFh,
 * 50BF6(21h). */
void portrait_unhighlight(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    int old = (int8_t)ds[XEEN_DS_HIGHLIGHTED_PORTRAIT];
    if (old < 0) {
        return;
    }
    exit_draw(g, xeen_read16(&g->movement, XEEN_DS_GLOBAL_SPRITES), (uint16_t)(old + 9),
              (int16_t)(ds[(uint16_t)(XEEN_DS_PORTRAIT_X + (unsigned)old)] - 1), 149, 0);
    ds[XEEN_DS_HIGHLIGHTED_PORTRAIT] = XEEN_NO_PORTRAIT;
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_PARTY_BAR);
}
/* --------------------------------------------- the character view
 * F1..F6 open a present member's view (OWNER: who opened it): the event text block is kept aside and the view's
 * texts loaded in its place, the cursor reset, the UI mode kept and set to the view's (10), the buttons pushed, the
 * cursor selected, the view's sprites loaded as a new handle (its 24 icons and buttons use it), the member's
 * portrait highlighted and the upper screen opened. In combat MEMBER is a combat slot.
 * Original: 44AE6 from 17DDE (F5FE - C9h): 97C2/97C0 and F42C saved; viewtext.bin by 2B6C9 (into the F42C buffer
 * when it is set, the offsets at 50D4:165C); the cursor 50D4:1657; 9A1C = 0Ah; 190ED, 4E06B(F460, 0); view.icn into
 * the 98D4 push (8C86); the draw list 2C54 + 12n; 46321; 2A1F6; 1871B(18h). */
xeen_status view_begin(xeen_game *g, unsigned member, enum view_owner owner)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    size_t n = 0;
    uint8_t *b;
    unsigned i;
    if (!g->presentation) {
        return shop_stop(g, VIEW_STOP_NO_PRESENTATION);
    }
    xeen_presentation_trace_extra("mark dialog-view"); /* Evidence: 44AE6 */
    text_block_save(m, &g->view_text);
    if (!(b = menu_load(g, ds_text(g, XEEN_TEXT_VIEW_TEXTS_FILE), &n))) {
        return shop_stop(g, VIEW_STOP_TEXTS);
    }
    free(b);
    /* The texts go into the voice buffer when it is set, else a buffer of their own (DOS memory, which the native
     * does not keep). Original: 2B6C9: F42C (2B6D5), else 18975 (2B709..2B713: 97C2 = its segment, 0 here); 97C0 = 0. */
    text_block_in_file_buffer(m, &g->view_text);
    g->view_cursor = 0;
    g->view_blink = 0;
    g->view_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = 0x0a;
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES), 0);
    ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] + 1u);
    xeen_write16(m, XEEN_DS_HANDLE_SLOT(ds), xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    if (!sprite_load_resident(g, ds_text(g, XEEN_TEXT_VIEW_SPRITES))) {
        return shop_stop(g, VIEW_STOP_SPRITES);
    }
    for (i = 0; i < 24u; ++i) {
        xeen_write16(m, (uint16_t)(XEEN_DS_ICON_DRAW_LIST + XEEN_VIEW_RECORD_BYTES * i),
                     xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    }
    view_buttons(g);
    g->view_member = member;
    g->view_owner = owner;
    g->view_char =
            (uint16_t)(XEEN_PARTY_RECORDS +
                       XEEN_CHARACTER_BYTES *
                               (g->view_mode == XEEN_UI_MODE_COMBAT ? ds[XEEN_DS_COMBAT_PARTY + member] : member));
    portrait_highlight(g, member);
    panel_window(g, XEEN_WINDOW_UPPER_SCREEN, 1); /* once */
    return view_redraw(g);
}
/* The view's buttons: the six button tables copied from the program data, their sprites the loaded handle.
 * Original: 46321 (menu_far from 1ACA..1B5C; 98D4). */
void view_buttons(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    menu_far(m, XEEN_DS_BUTTON_X_TABLE, 0x1aca);
    menu_far(m, XEEN_DS_BUTTON_Y_TABLE, 0x1afc);
    menu_far(m, XEEN_DS_BUTTON_WIDTH_TABLE, 0x1b14);
    menu_far(m, XEEN_DS_BUTTON_HEIGHT_TABLE, 0x1b2c);
    menu_far(m, XEEN_DS_BUTTON_KEY_TABLE, 0x1b44);
    menu_far(m, XEEN_DS_BUTTON_ENABLED_TABLE, 0x1b5c);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
}
/**
 * Returns a character's age in years from the current year (EFF2) and the birth year, capped at 254
 * (a signed comparison as in the original; 2BAC7); NATURAL selects the age without the magical ageing.
 */
uint16_t view_age(const xeen_movement *m, const uint8_t *ch, int natural)
{
    uint16_t years = (uint16_t)(xeen_read16(m, XEEN_DS_YEAR) -
                                (uint16_t)xeen_get_le16(ch + XEEN_CHARACTER_BIRTH_YEAR));
    if ((int16_t)years > 254) {
        years = 254u;
    }
    return natural ? years : (uint16_t)(years + ch[XEEN_CHARACTER_AGE_BONUS]);
}
void member_window_close(xeen_game *g, uint8_t saved)
{
    window_pop(g, XEEN_WINDOW_MEMBER_PROMPT);
    g->movement.ds[XEEN_DS_PORTRAIT_CLICKS] = saved;
    bar_redraw(g);
}
/* The colour a view value is shown in against its MAXIMUM: 6 below 1, 2 above the maximum, 15 at it, 32 below a
 * quarter of it, else 9. Original: 46B68(value, maximum). */
unsigned view_colour(int value, int maximum)
{
    if (value < 1) {
        return 6;
    }
    if (value > maximum) {
        return 2;
    }
    if (value == maximum) {
        return 15;
    }
    return (maximum >> 2) > value ? 32u : 9u;
}
/* The member's total experience, as the view shows it (the stored value counts from the current level): level 1
 * the stored value; up to level 12, the class's base shifted by level - 2, plus it; beyond, (level - 12) x 1,024,000
 * plus the base shifted by 10, plus it (xeen_level_experience).
 * Original: 28C76 (+15Ch; the class table 2920). */
static uint32_t next_experience(const xeen_movement *m, const uint8_t *ch)
{
    return xeen_level_experience(m, ch);
}
/* Whether the member has AWARD (0..87; the view's count and the awards list): the award's record byte
 * (xeen_award_slot: awards 73 and 81 read bytes 62 and 63), its low nibble for awards below 64, else the high one.
 * Original: 46384(record, award); 416A2. */
int has_award(const uint8_t *ch, uint16_t award)
{
    uint8_t b = ch[XEEN_CHARACTER_AWARDS + xeen_award_slot(award)];
    if (!b) {
        return 0;
    }
    return !xeen_award_high_nibble(award) ? (b & 0x0f) != 0 : (b & 0xf0) != 0;
}
/* How many of the member's 18 skills it has (a nonzero skill byte). */
static unsigned skill_count(const uint8_t *ch)
{
    unsigned i, n = 0;
    for (i = 0; i < XEEN_SKILL_COUNT; ++i) {
        n += ch[XEEN_CHARACTER_SKILLS + i] != 0;
    }
    return n;
}
/* How many of the 88 awards the member has (has_award). */
static unsigned award_count(const uint8_t *ch)
{
    unsigned i, n = 0;
    for (i = 0; i < XEEN_AWARD_COUNT; ++i) {
        n += (unsigned)has_award(ch, (uint16_t)i);
    }
    return n;
}
/* The resistances' total the view shows: the six resistances' base and temporary bytes, then the equipped items'
 * bonus to each (xeen_combat_item_bonus 11..16), summed as an int (the view prints its low 16 bits). */
static int resistance_total(const xeen_movement *m, const uint8_t *ch)
{
    unsigned i;
    int total = 0;
    for (i = 0; i < 2u * XEEN_RESISTANCE_COUNT; ++i) {
        total += ch[XEEN_CHARACTER_RESISTANCES + i];
    }
    for (i = 0; i < XEEN_RESISTANCE_COUNT; ++i) {
        total += xeen_combat_item_bonus(m, ch, XEEN_ITEM_BONUS_RESISTANCES + i);
    }
    return total;
}
/* The level the view shows: the level plus its temporary bonus. Its test for a negative sum is the original's own;
 * both bytes are unsigned there too, so it never applies. Original: 2C909. */
static int shown_level(const uint8_t *ch)
{
    int level = ch[XEEN_CHARACTER_LEVEL] + ch[XEEN_CHARACTER_LEVEL_BONUS];
    return level < 0 ? 0 : level;
}
/* The view's "+" for a party effect (the DS byte EFFECT nonzero), shown only while the member's worst condition
 * COND is Good; else empty. */
static const char *effect_mark(const xeen_game *g, unsigned cond, uint16_t effect)
{
    return cond == XEEN_CONDITION_GOOD && g->movement.ds[effect] ? ds_text(g, XEEN_TEXT_VIEW_EFFECT_MARK) : "";
}
/* The character view's text for the member at RECORD into OUT (SIZE bytes): "Party Gold", the name, sex, race and
 * class, then each value in its colour (view_colour) against its maximum or base: Might, Accuracy, HP (against the
 * maximum), experience, Intellect, Luck, SP (against the maximum), the gold, Personality, the age (with and without
 * the magical ageing), the resistances' total, the gems, Endurance, the level (against the base level), the skill
 * count, the food (per member, divided by three; "s" plural), Speed, the AC, the award count, the worst condition's
 * colour and name, and a "+" for each of the four party effects while the condition is Good. 0 when it does not fit
 * or the party is empty (the original would divide the food by 0; a member is always open here).
 * Original: 4666A(record, 9C6F) into 818A (docs/phases/PHASE59_AUTHENTIC_BLACKSMITH_TRADING_AND_EQUIPMENT.md);
 * 46B68 of 2BD94(0) against 2BD94(1); 2AAC2, 28C76, 46470, 2BAC7, the resistances +137h..142h and 2BAFA 0Bh..10h;
 * 2C909 against +23h; 46364; the food / members / 3; 4660F(0) against 4660F(1); 46446; EDA3..EDA6 with condition
 * 16. */
static int view_text(xeen_game *g, uint16_t record, char *out, size_t size)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    const uint8_t *ch = ds + record;
    unsigned cond = xeen_character_worst_condition(ch), food, i;
    int n, stat[XEEN_STAT_COUNT], base[XEEN_STAT_COUNT], hp, sp, age, age_base, level, ac, ac_base;
    if (!ds[XEEN_DS_PARTY_COUNT]) {
        return 0;
    }
    food = (unsigned)xeen_read16(m, XEEN_DS_PARTY_FOOD) / (unsigned)ds[XEEN_DS_PARTY_COUNT] / 3u;
    for (i = 0; i < XEEN_STAT_COUNT; ++i) {
        stat[i] = xeen_combat_stat(m, ch, i, 0); /* with the temporary and item bonuses */
        base[i] = xeen_combat_stat(m, ch, i, 1);
    }
    hp = (int16_t)xeen_get_le16(ch + XEEN_CHARACTER_HP);
    sp = (int16_t)xeen_get_le16(ch + XEEN_CHARACTER_SP);
    age = (int)view_age(m, ch, 0);
    age_base = (int)view_age(m, ch, 1); /* without the magical ageing */
    level = shown_level(ch);
    ac = xeen_combat_armor_class(m, ch, 0);
    ac_base = xeen_combat_armor_class(m, ch, 1);
    n = snprintf(
            out, size, ds_text(g, XEEN_TEXT_VIEW_STATS), ds_text(g, XEEN_TEXT_VIEW_PARTY_GOLD), (const char *)ch,
            far_text(g, XEEN_DS_SEX_NAMES, ch[XEEN_CHARACTER_SEX]),
            far_text(g, XEEN_DS_RACE_NAMES, ch[XEEN_CHARACTER_RACE]),
            far_text(g, XEEN_DS_CLASS_NAMES, ch[XEEN_CHARACTER_CLASS]),
            view_colour(stat[XEEN_STAT_MIGHT], base[XEEN_STAT_MIGHT]), (unsigned)stat[XEEN_STAT_MIGHT],
            view_colour(stat[XEEN_STAT_ACCURACY], base[XEEN_STAT_ACCURACY]), (unsigned)stat[XEEN_STAT_ACCURACY],
            view_colour(hp, xeen_combat_max_hp(m, ch)), hp, (unsigned long)next_experience(m, ch),
            view_colour(stat[XEEN_STAT_INTELLECT], base[XEEN_STAT_INTELLECT]), (unsigned)stat[XEEN_STAT_INTELLECT],
            view_colour(stat[XEEN_STAT_LUCK], base[XEEN_STAT_LUCK]), (unsigned)stat[XEEN_STAT_LUCK],
            view_colour(sp, xeen_combat_max_sp(m, ch)), (unsigned)(uint16_t)sp,
            (unsigned long)xeen_ds_read32(m, XEEN_DS_PARTY_GOLD),
            view_colour(stat[XEEN_STAT_PERSONALITY], base[XEEN_STAT_PERSONALITY]),
            (unsigned)stat[XEEN_STAT_PERSONALITY], view_colour(age, age_base), (unsigned)age,
            (unsigned)(uint16_t)resistance_total(m, ch), (unsigned long)xeen_ds_read32(m, XEEN_DS_PARTY_GEMS),
            view_colour(stat[XEEN_STAT_ENDURANCE], base[XEEN_STAT_ENDURANCE]), (unsigned)stat[XEEN_STAT_ENDURANCE],
            view_colour(level, ch[XEEN_CHARACTER_LEVEL]), (unsigned)level, skill_count(ch), food,
            food == 1u ? ' ' : 's',
            view_colour(stat[XEEN_STAT_SPEED], base[XEEN_STAT_SPEED]), (unsigned)stat[XEEN_STAT_SPEED],
            view_colour(ac, ac_base), (unsigned)ac, award_count(ch), (unsigned)ds[XEEN_DS_CONDITION_COLOURS + cond],
            far_text(g, XEEN_DS_CONDITION_NAMES, cond), effect_mark(g, cond, XEEN_DS_EFFECT_BLESS),
            effect_mark(g, cond, XEEN_DS_EFFECT_POWER_SHIELD), effect_mark(g, cond, XEEN_DS_EFFECT_HOLY_BONUS),
            effect_mark(g, cond, XEEN_DS_EFFECT_HEROISM));
    return n >= 0 && (size_t)n < size;
}
/* Redraws the character view: the record's text (view_text) under the view's headings in the upper screen window,
 * the view's icons drawn and the window presented; then the view's tick. Stops when a text does not fit.
 * Original: 44BB6: 4666A into 9C6F, AC0F = the 7D0D frame with it, window 18h, the 2C54 draw list, 50BF6(18h),
 * 4EA0D. */
xeen_status view_redraw(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char text[0x7d0], out[0x7d0];
    int n;
    if (!view_text(g, g->view_char, text, sizeof text)) {
        return shop_stop(g, VIEW_STOP_CHARACTER_TEXT);
    }
    ds_put_text(g, XEEN_DS_SCRATCH, text);
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_VIEW_HEADINGS), ds_text(g, XEEN_DS_SCRATCH));
    if (n < 0 || n >= 2000) {
        return shop_stop(g, VIEW_STOP_TEXT);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    menu_window_text(g, XEEN_WINDOW_UPPER_SCREEN, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    { /* 44BF4 4ADB1(2C54, 18h), the view's icon list. */
        xeen_draw_list list;
        if (xeen_scene_collect_draw_list(m, 0x18, XEEN_DS_ICON_DRAW_LIST, &list) == XEEN_OK) {
            (void)xeen_presentation_submit(g->presentation, &list);
        }
    }
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_UPPER_SCREEN);
    g->view_sampled = 0;
    return panel_set(g, VIEW_TICK, VIEW_TICK_BOUNDARY);
}
/* Ends the character view (Esc; CLOSE: its window is still open): the window closed, the highlight off, the view's
 * sprite level popped (the saved handle current again, the depth taken back; nothing freed), the text block
 * restored, the in-combat items flag cleared, the button set popped and the UI mode restored. Then its owner goes on:
 * the inn's poll, combat's member view, or exploration, where an item used from the view first runs the monsters'
 * update (run_monster_update) before the command tail.
 * Original: 44E93: 18765(18h), 42298, the 98D4 pop, viewtext.bin freed when F42C was 0, 97C2/97C0, 97C6 = 0, 18FFF,
 * 9A1C; the callers 17DFE (3067 set: 2A701, else 17F07), 44550 (441D3) and combat's 3933F. */
static xeen_status view_end(xeen_game *g, int close)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (close) {
        panel_window(g, XEEN_WINDOW_UPPER_SCREEN, 0);
    }
    portrait_unhighlight(g);
    xeen_write16(m, XEEN_DS_SPRITE_HANDLE, xeen_read16(m, XEEN_DS_HANDLE_SLOT(ds)));
    ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] - 1u);
    text_block_restore(m, &g->view_text);
    ds[XEEN_DS_VIEW_ITEMS_IN_COMBAT] = 0;
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    ds[XEEN_DS_UI_MODE] = g->view_mode;
    g->panel_stage = 0;
    if (g->view_owner == VIEW_FROM_INN) {
        return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
    }
    if (g->view_owner == VIEW_FROM_COMBAT) {
        g->combat_ui_site = 0;
        return combat_modal_return(g, XEEN_COMBAT_MODAL_VIEW_MEMBER, 0);
    } /* combat's 3933F */
    if (ds[XEEN_DS_PARTY_ACTED]) {
        /* 17E08, 2A701 after an item's use (3067 set), then 17F07. */
        xeen_status status;
        if (!g->event_random) {
            return game_stop(g, VIEW_STOP_NO_RANDOM);
        }
        g->service_boundary = 0;
        status = run_monster_update(g);
        if (status != XEEN_OK) {
            return game_stop(g, g->service_boundary ? g->service_boundary : VIEW_STOP_RELEASE);
        }
        m->status = XEEN_OK;
    }
    return command_tail(g);
}
/* The character view's items dialog (I) has returned RECORD: none (an item was used) marks that the party acted and
 * ends the view without closing its window; otherwise the view goes on with that record, its sprites loaded into the
 * resident handle again, UI mode 10 and a full redraw. Stops when view.icn cannot be loaded.
 * Original: 44DA2 (9A1C back, 97C6 = (9A1C = 2), 44F92(record, 0)): NULL sets 3067 and leaves at 44E9B (no 18765);
 * else view.icn into 8C86, 9A1C = 0Ah, the redraw. */
xeen_status view_after_items(xeen_game *g, uint16_t record)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (!record) {
        ds[XEEN_DS_PARTY_ACTED] = 1;
        return view_end(g, 0);
    }
    g->view_char = record;
    if (!sprite_load_resident(g, ds_text(g, XEEN_TEXT_VIEW_SPRITES))) {
        return shop_stop(g, VIEW_STOP_ITEMS_RETURN_SPRITES);
    }
    ds[XEEN_DS_UI_MODE] = 0x0a;
    return view_redraw(g);
}
/* Draws the character view's cursor, frame 30h + FRAME of the dialog sprites, at its place in the view's 4 x 5 grid
 * (column view_cursor / 5, row view_cursor % 5); nothing for a cursor of 20 or more. Original: 431DE(SI): the cursor
 * 50D4:1657, 98D4, the columns 5818 and rows 5819. */
static void view_cursor_draw(xeen_game *g, unsigned frame)
{
    static const uint8_t cx[4] = {9, 60, 111, 176}, cy[5] = {23, 46, 69, 92, 115};
    if (g->view_cursor < 20u) {
        exit_draw(g, xeen_read16(&g->movement, XEEN_DS_SPRITE_HANDLE), (uint16_t)(0x30u + frame),
                  cx[g->view_cursor / 5u], cy[g->view_cursor % 5u], 0);
    }
}
/* ------------------------------------- 4798C the Exchange */
/* The character view's Exchange (E outside combat): the button set pushed, esc.icn and the Esc / 1..4 buttons
 * installed, the portrait clicks saved and turned on, "Exchange with whom?" in the member prompt window with its Esc
 * button, presented; then the prompt's poll. Stops without esc.icn.
 * Original: 4798C(&member) from 44E19 (9A1C = the view's [bp-2]): 190ED, 190A3(esc.icn), the 1E10..1E30 buttons,
 * 9896 = 98D4, [bp-2] = 98D6, 98D6 = 1; 1871B(1Fh), 4E38C(1Fh), 98D4 frame 0 at (E1h, 78h), 50BF6(1Fh); 47A42. */
static xeen_status exchange_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    xeen_presentation_trace_extra("mark dialog-exchange"); /* Evidence: 4798C */
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push_or_undo(g, XEEN_TEXT_VIEW_ESC_SPRITES)) {
        return shop_stop(g, EXCHANGE_STOP_SPRITES);
    }
    button_tables(m, XEEN_BUTTONS_ESC_1_4);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    g->xchg_portrait_clicks = ds[XEEN_DS_PORTRAIT_CLICKS];
    ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    panel_window(g, XEEN_WINDOW_MEMBER_PROMPT, 1);
    menu_window_text(g, XEEN_WINDOW_MEMBER_PROMPT, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_VIEW_EXCHANGE_WHOM));
    exit_draw_in(g, xeen_read16(m, XEEN_DS_SPRITE_HANDLE), 0, 225, 120, 31);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_MEMBER_PROMPT);
    return panel_set(g, EXCHANGE_POLL, EXCHANGE_POLL_BOUNDARY);
}
/* Ends Exchange: the member prompt closed (the portrait clicks back, the party bar redrawn), the highlight on the
 * viewed member in its new place, the key cleared; the character view goes on with that member's record in UI mode
 * 10 at its tick, without a redraw. Original: 47B3A: 18765(1Fh), 190CF, 18FFF, 98D6, 2ABB8(1), 2A1F6(DI), F5FE = 0;
 * the view 44E2C: 9A1C = 0Ah, 44E89, the loop at 44C09. */
static xeen_status exchange_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    member_window_close(g, g->xchg_portrait_clicks);
    portrait_highlight(g, g->view_member);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->view_char = XEEN_PARTY_RECORD(g->view_member);
    ds[XEEN_DS_UI_MODE] = 0x0a;
    return panel_set(g, VIEW_TICK, VIEW_TICK_BOUNDARY);
}
/* Swaps the party members A and B: their records, their roster numbers (ED8E) and their face sprites (9806), in that
 * order; A == B leaves everything as it was. Original: 47A6E..47B14 (10671 through [bp-16Ch]; ED8E; 9806). */
static void swap_members(xeen_movement *m, unsigned a, unsigned b)
{
    uint8_t *ds = m->ds;
    uint8_t t[XEEN_CHARACTER_BYTES], number;
    uint16_t face;
    memcpy(t, ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * a, XEEN_CHARACTER_BYTES);
    memcpy(ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * a, ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * b,
           XEEN_CHARACTER_BYTES);
    memcpy(ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * b, t, XEEN_CHARACTER_BYTES);
    number = ds[XEEN_DS_PARTY_MEMBERS + a];
    ds[XEEN_DS_PARTY_MEMBERS + a] = ds[XEEN_DS_PARTY_MEMBERS + b];
    ds[XEEN_DS_PARTY_MEMBERS + b] = number;
    face = xeen_read16(m, XEEN_DS_MEMBER_FACE_SPRITE(a));
    xeen_write16(m, XEEN_DS_MEMBER_FACE_SPRITE(a), xeen_read16(m, XEEN_DS_MEMBER_FACE_SPRITE(b)));
    xeen_write16(m, XEEN_DS_MEMBER_FACE_SPRITE(b), face);
}
/* A key while the view's Exchange asks with whom: F1..F6 of a present member (the party count compared signed)
 * swaps the two members' records, their roster numbers and their face sprites, and the view then shows the chosen
 * one; choosing the member itself swaps it with itself. Either way the exchange ends. Escape ends it unchanged; any
 * other key polls again.
 * Original: 47A42..47B32 (CS:3591): ED8C > n; 10671 through [bp-16Ch]; ED8E; 9806; DI; SI = 1Bh. */
xeen_status exchange_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F6) {
        unsigned n = key - XEEN_KEY_F1;
        if ((int)ds[XEEN_DS_PARTY_COUNT] <= (int)n) {
            return panel_set(g, EXCHANGE_POLL, EXCHANGE_POLL_BOUNDARY);
        }
        swap_members(m, n, g->view_member);
        g->view_member = n;
        return exchange_end(g);
    }
    if (key == XEEN_KEY_ESCAPE) {
        return exchange_end(g);
    }
    return panel_set(g, EXCHANGE_POLL, EXCHANGE_POLL_BOUNDARY);
}
static void view_cursor_to(xeen_game *g, unsigned to);
/* Opens the details of the view's line KEY ('a' + the line: Enter on the cursor's line, or a statistic's button): the
 * key stored as the input, the view's UI mode back, the cursor moved to that line and the screen presented, then
 * the line's details (details_begin).
 * Original: 44D4D (Enter at 44D3D with F5FE = the cursor + 61h, or a stat button). */
static xeen_status view_details(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    xeen_write16(m, XEEN_DS_INPUT_CODE, key);
    m->ds[XEEN_DS_UI_MODE] = g->view_mode;
    view_cursor_to(g, (unsigned)(key - 'a'));
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_UPPER_SCREEN);
    return details_begin(g, g->view_char, (unsigned)(key - 'a'));
}
/* Moves the view's cursor to line TO: the old line's cursor erased, the new one drawn. Original: 431DE(0), 431DE(1). */
static void view_cursor_to(xeen_game *g, unsigned to)
{
    view_cursor_draw(g, 0);
    g->view_cursor = (uint8_t)to;
    view_cursor_draw(g, 1);
}
/* F1..F8 in the view: member N (stored as the input code F5FE) of the party, or of the combat party in a combat
 * view, becomes the viewed member (its highlight, then the view redrawn); a member not present changes nothing and
 * the view's tick goes on (VIEW_TICK after the screen is presented, as for an ignored key).
 * Original: 44E39..44E86 (CS:0922 keys C9h..D0h; F5FE - C9h against 971F or ED8C; 9720[n]; 2A1F6, then 44BB5), else
 * 44E89. */
static xeen_status view_member_key(xeen_game *g, unsigned n)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int combat = g->view_mode == XEEN_UI_MODE_COMBAT;
    unsigned count = combat ? ds[XEEN_DS_COMBAT_PARTY_COUNT] : ds[XEEN_DS_PARTY_COUNT];
    xeen_write16(m, XEEN_DS_INPUT_CODE, (uint16_t)n);
    if (count <= n) {
        (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_UPPER_SCREEN);
        return panel_set(g, VIEW_TICK, VIEW_TICK_BOUNDARY);
    }
    g->view_member = n;
    g->view_char = (uint16_t)(XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * (combat ? ds[XEEN_DS_COMBAT_PARTY + n] : n));
    portrait_highlight(g, n);
    return view_redraw(g);
}
/*
 * The character view's keys (the 44C4F table, CS:0922), each in the view's own UI mode:
 *   I            the member's items (the items' combat flag set when the view's mode is combat's);
 *   Q            the quick reference (qref_begin, the view's combat site carried to it; 44DF0 46BA0, then 44DFB);
 *   E            the exchange (refused with a message in combat; 44E03);
 *   Enter, a..t  the details of the cursor's entry, or of that letter's, of the sheet's 20;
 *   arrows       the cursor within the sheet's four columns of five (left/right a column, up/down an entry);
 *   Esc          the view ends (view_end);
 *   F1..F8       another member (view_member_key).
 * An arrow, and every other key, presents the upper screen and goes back to the view's tick.
 */
xeen_status view_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    switch (key) {
        case 'I':
            ds[XEEN_DS_UI_MODE] = g->view_mode;
            ds[XEEN_DS_VIEW_ITEMS_IN_COMBAT] = (uint8_t)(ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_COMBAT);
            return items_begin(g, g->view_char, ITEMS_OWN, ITEMS_OWNER_VIEW);
        case 'Q': /* 44DF0 46BA0, then 44DFB */
            ds[XEEN_DS_UI_MODE] = g->view_mode;
            g->qref_view = 1;
            g->qref_view_site = g->combat_ui_site;
            g->combat_ui_site = 0;
            return qref_begin(g);
        case 'E': /* 44E03 */
            if (g->view_mode == XEEN_UI_MODE_COMBAT) {
                return scroll0_open(
                        g, MESSAGE_VIEW_EXCHANGE, ds_text(g, XEEN_TEXT_VIEW_NO_COMBAT_EXCHANGE)); /* "Exchanging in combat is not allowed!" */
            }
            ds[XEEN_DS_UI_MODE] = g->view_mode;
            return exchange_begin(g); /* 44E19: 4798C */
        case XEEN_KEY_ENTER:
            return view_details(g, (uint16_t)(g->view_cursor + 'a'));
        /* 44C94, 44C53, 44CCB, 44D00: a move inside 0..19 erases the cursor
         * (431DE(0)), moves it and draws it (431DE(1)); then 44C89. */
        case XEEN_KEY_LEFT: /* a column left */
            if (g->view_cursor >= 5u) {
                view_cursor_to(g, g->view_cursor - 5u);
            }
            break;
        case XEEN_KEY_RIGHT:
            if (g->view_cursor + 5u < 20u) {
                view_cursor_to(g, g->view_cursor + 5u);
            }
            break;
        case XEEN_KEY_UP:
            if (g->view_cursor > 0u) {
                view_cursor_to(g, g->view_cursor - 1u);
            }
            break;
        case XEEN_KEY_DOWN:
            if (g->view_cursor + 1u < 20u) {
                view_cursor_to(g, g->view_cursor + 1u);
            }
            break;
        case XEEN_KEY_ESCAPE:
            return view_end(g, 1);
        default:
            if (key >= 'a' && key <= 't') { /* the 20 entries of the sheet */
                return view_details(g, key);
            }
            if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F8) {
                return view_member_key(g, key - XEEN_KEY_F1);
            }
            break;
    }
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_UPPER_SCREEN);
    return panel_set(g, VIEW_TICK, VIEW_TICK_BOUNDARY);
}
/* Exploration's F1..F6: the member's number stored as the input code; a member present opens the character view
 * (view_begin), an absent one ends the command (command_tail). Original: 17DDE. */
xeen_status view_command(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    unsigned n = key - XEEN_KEY_F1;
    xeen_write16(m, XEEN_DS_INPUT_CODE, (uint16_t)n);
    if (m->ds[XEEN_DS_PARTY_COUNT] <= n) {
        return command_tail(g);
    }
    return view_begin(g, n, VIEW_FROM_EXPLORATION);
}

/*
 * The character view's cursor tick: the frame clock marked once; above 4 ticks the cursor's blink toggles and the
 * clock is marked again; the cursor drawn in its blink state and the screen shown, then the view's poll.
 * Original: 44C04 4EA0D once, 44C09 4EA1C; 431DE(SI), 50BF6(18h).
 */
xeen_status view_tick(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    if (!g->view_sampled) {
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        g->view_sampled = 1;
        g->view_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
    }
    if ((status = timer_ticks(g, &g->view_elapsed, bios, user)) != XEEN_OK) {
        return status;
    }
    g->view_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
    if (g->timer_registers.ax > 4u) {
        g->view_blink ^= 1;
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
    }
    view_cursor_draw(g, (unsigned)g->view_blink);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_UPPER_SCREEN);
    g->panel_stage = VIEW_POLL;
    g->lifecycle_boundary = VIEW_POLL_BOUNDARY; /* the same poll session */
    return XEEN_SERVICE_REQUIRED;
}
