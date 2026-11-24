#include "xeen/party.h"
#include "xeen/ds_globals.h"
#include "xeen/character_record.h"
#include "xeen/world_entry.h"
#include "xeen/scene_construct.h"
#include "xeen/modal.h"
#include "xeen/byte_order.h"
#include "xeen/item_tables.h"
#include "xeen/item_tables.h"
#include "xeen/combat.h"
#include <string.h>
/* Where the entry's finish hands a step to the host: the shared UI restoration of open windows (28D80: window 38
 * or 12 open, or an event's mode) and the speech face's tail (28DAF: text off with window 40 up). */
enum {
    ENTRY_WINDOW_RESTORE = 0x28d80,
    ENTRY_SPEECH_FACE = 0x28daf
};
/* Where the finish reports a presentation or map step that failed (the step's call in the original, which the
 * finish resumes at): each stage's one call. */
enum {
    ENTRY_PORTRAITS_STEP = 0x17a39,          /* the party's portraits */
    ENTRY_FIRST_SCREEN_STEP = 0x17a88,       /* the screen shown under the fade-in */
    ENTRY_ICONS_STEP = 0x28d96,              /* the list at DS 2A5C (the command icons) collected and drawn */
    ENTRY_ICON_BOX_STEP = 0x28e2e,           /* the icon box shown */
    ENTRY_SCREEN_STEP = 0x17a95,             /* the whole screen shown */
    ENTRY_PAGE_COPY_STEP = 0x17aa6,          /* the shown page copied to the hidden one */
    ENTRY_METADATA_STEP = 0x17ad6            /* the party cell's metadata */
};
#define B(a)      (m->ds[(uint16_t)(a)])
#define W(a)      xeen_read16(m, (uint16_t)(a))
#define SET(a, v) xeen_write16(m, (uint16_t)(a), (uint16_t)(v))
/* The age brackets of the Endurance correction (xeen_combat_stat's age_ranges): their last entry FFFFh ends the
 * original's unbounded scan for every age but FFFFh. */
#define AGE_BRACKET_END 0xffffu
/* The attribute bonus table's thresholds (24 words; its last FFFFh). */
#define BONUS_THRESHOLDS 24u
xeen_status xeen_world_entry_max_hp(const xeen_movement *m, unsigned member, uint16_t *hp)
{
    const uint8_t *ch;
    uint16_t years, age, endurance;
    unsigned j;
    if (!m || !hp || member >= 6u) {
        return XEEN_BAD_INPUT;
    }
    ch = m->ds + XEEN_PARTY_RECORDS + member * XEEN_CHARACTER_BYTES;
    /* The original scans both tables without a bound. An age word of FFFFh (a birth year after the current year, the
     * age bonus not lifting it) passes the age brackets' end, and an Endurance at or past every threshold the bonus
     * table's: neither has a native continuation, so both are refused here rather than given a value. */
    /* (the cap of the years at 254 cannot make FFFFh, so it does not matter here) */
    years = (uint16_t)(W(XEEN_DS_YEAR) - xeen_get_le16(ch + XEEN_CHARACTER_BIRTH_YEAR));
    age = (uint16_t)(years + ch[XEEN_CHARACTER_AGE_BONUS]);
    if (age == AGE_BRACKET_END) {
        return XEEN_SERVICE_REQUIRED;
    }
    endurance = xeen_combat_stat(m, ch, XEEN_STAT_ENDURANCE, 0);
    for (j = 0; j < BONUS_THRESHOLDS && W(XEEN_DS_ATTRIBUTE_BONUS_THRESHOLD + 2u * j) <= endurance; ++j) {
    }
    if (j == BONUS_THRESHOLDS) {
        return XEEN_SERVICE_REQUIRED;
    }
    *hp = xeen_combat_max_hp(m, ch);
    return XEEN_OK;
}
/** Appends one sprite draw (handle, frame, position and window) to the draw list D of length *N. */
static void push_sprite_draw(xeen_sprite_draw *d, size_t *n, uint16_t h, uint16_t f, uint16_t x,
                             uint16_t y, uint16_t window)
{
    d[*n] = (xeen_sprite_draw){h, f, x, y, 0, 0, window};
    ++*n;
}
/* The HP gem's frame (hpbars.icn) for HP of MAXHP: 4 down (below 1), 3 above the maximum, 0 at it, 2 below a quarter
 * (the quarter rounded down, a negative maximum included: an arithmetic shift), else 1. */
static unsigned hp_gem_frame(int hp, int maxhp)
{
    int quarter = maxhp >= 0 ? maxhp / 4 : -((-maxhp + 3) / 4);
    if (hp < 1) {
        return 4u;
    }
    if (hp > maxhp) {
        return 3u;
    }
    if (hp == maxhp) {
        return 0u;
    }
    return hp < quarter ? 2u : 1u;
}
/* 2ABB8's draws: the bar, the faces by worst condition, the HP gems and the
 * highlight (96C3); in combat the 9720 party map selects the members. */
static xeen_status party_bar_draws(xeen_movement *m, xeen_sprite_draw *d, size_t *count_inout)
{
    size_t n = *count_inout;
    unsigned i, count = xeen_active_party_count(m->ds);
    if (count > 6) {
        return XEEN_BAD_INPUT;
    }
    push_sprite_draw(d, &n, W(XEEN_DS_RESTOREX_SPRITES), 0, 8, 149, 0);
    for (i = 0; i < count; ++i) {
        unsigned member = xeen_active_party_slot(m->ds, i), record, condition, frame, handle;
        if (member >= 6) {
            return XEEN_BAD_INPUT;
        }
        record = XEEN_PARTY_RECORDS + member * XEEN_CHARACTER_BYTES;
        condition = xeen_character_worst_condition(m->ds + record); /* 2AB96, called at 2AC17 */
        frame = B(XEEN_DS_CONDITION_PORTRAIT_FRAME + condition);
        handle = frame > 4u && !W(XEEN_DS_ROSTER_FACE_SPRITES) ? W(XEEN_DS_DARKSIDE_FACE_SPRITES) : W(XEEN_DS_MEMBER_FACE_SPRITE(member));
        if (frame > 4u) {
            frame -= 5u;
        }
        push_sprite_draw(d, &n, (uint16_t)handle, (uint16_t)frame, B(XEEN_DS_PORTRAIT_X + i), 150, 0);
    }
    if (W(XEEN_DS_HPBARS_SPRITES)) {
        for (i = 0; i < count; ++i) {
            unsigned member = xeen_active_party_slot(m->ds, i),
                     record = XEEN_PARTY_RECORDS + member * XEEN_CHARACTER_BYTES, frame;
            int hp = xeen_sign16(W(record + XEEN_CHARACTER_HP));
            uint16_t maxhp;
            xeen_status st = xeen_world_entry_max_hp(m, member, &maxhp);
            if (st != XEEN_OK) {
                return st;
            }
            frame = hp_gem_frame(hp, xeen_sign16(maxhp));
            /* the gems' x: the six bytes after the portraits' (DS 17B8) */
            push_sprite_draw(d, &n, W(XEEN_DS_HPBARS_SPRITES), (uint16_t)frame, B(XEEN_DS_PORTRAIT_X + 6u + i), 182, 0);
        }
    }
    if (B(XEEN_DS_HIGHLIGHTED_PORTRAIT) != 255u) {
        if (B(XEEN_DS_HIGHLIGHTED_PORTRAIT) >= 6u) {
            return XEEN_BAD_INPUT;
        }
        push_sprite_draw(d, &n, W(XEEN_DS_GLOBAL_SPRITES), 8,
                         (uint16_t)(B(XEEN_DS_PORTRAIT_X + B(XEEN_DS_HIGHLIGHTED_PORTRAIT)) - 1u), 149, 0);
    }
    *count_inout = n;
    return XEEN_OK;
}
/** Draws the party bar's portraits and frame (the bar sprites and window 1) to the presentation. */
static xeen_status portraits(xeen_movement *m, xeen_presentation *p)
{
    xeen_sprite_draw d[20];
    size_t n = 0;
    xeen_status st;
    push_sprite_draw(d, &n, W(XEEN_DS_GLOBAL_SPRITES), 5, 232, 9, 1);
    if ((st = party_bar_draws(m, d, &n)) != XEEN_OK) {
        return st;
    }
    return xeen_presentation_draw_sprites(p, d, n);
}
xeen_status xeen_world_entry_party_bar_draws(xeen_movement *m, xeen_sprite_draw *d, size_t cap,
                                             size_t *n)
{
    xeen_sprite_draw all[20];
    size_t k = 0;
    xeen_status st;
    if (!m || !d || !n || cap < 14u) {
        return XEEN_BAD_INPUT;
    }
    if ((st = party_bar_draws(m, all, &k)) != XEEN_OK) {
        return st;
    }
    if (k > cap) {
        return XEEN_BAD_INPUT;
    }
    memcpy(d, all, k * sizeof *d);
    *n = k;
    return XEEN_OK;
}
xeen_status xeen_world_entry_party_bar(xeen_movement *m, xeen_presentation *p, int update)
{
    xeen_sprite_draw d[20];
    size_t n = 0;
    xeen_status st;
    if (!m || !p) {
        return XEEN_BAD_INPUT;
    }
    if ((st = party_bar_draws(m, d, &n)) != XEEN_OK) {
        return st;
    }
    if ((st = xeen_presentation_draw_sprites(p, d, n)) != XEEN_OK) {
        return st;
    }
    return update ? xeen_presentation_update_window(p, XEEN_WINDOW_PARTY_BAR) : XEEN_OK;
}
/* World entry stage 1: the command icons (record 0, the icon box's background, the global sprites; records 1..15 the
 * icon sprites), then with the UI mode 0 the fade out (17A77..17A85: 4A75A(4) before 17A88 50BF6(0): the DAC fades
 * the mm4.pal of 179E9 over the screen still shown). */
static void entry_command_icons(xeen_movement *m, xeen_presentation *p)
{
    unsigned i;
    SET(XEEN_DS_COMMAND_ICONS, W(XEEN_DS_GLOBAL_SPRITES));
    for (i = XEEN_VIEW_LIST_RECORD(XEEN_DS_COMMAND_ICONS, 1u); i <= XEEN_VIEW_LIST_RECORD(XEEN_DS_COMMAND_ICONS, 15u);
         i += XEEN_VIEW_RECORD_BYTES) {
        SET(i, W(XEEN_DS_SPRITE_HANDLE));
    }
    if (!B(XEEN_DS_UI_MODE)) {
        xeen_presentation_fade(p, 0, 4);
    }
}
/* World entry stage 6, before its page copy: the exploration buttons (the height, width, y and x tables, the keys, the
 * sprites and the enabled flags, each a far pointer into DS), the mouse cursor, and the page toggled (4E06B selects
 * the cursor and toggles the page; the full copy follows, leaving the pending-window word unchanged. Borrowed pixels
 * are decoded by the native presentation owner, not synthesized from the handle). */
static void entry_exploration_buttons(xeen_movement *m, xeen_presentation *p)
{
    static const uint16_t pointers[] = {XEEN_DS_EXPLORATION_BUTTON_HEIGHT, XEEN_DS_EXPLORATION_BUTTON_WIDTH,
                                        XEEN_DS_EXPLORATION_BUTTON_Y, XEEN_DS_EXPLORATION_BUTTON_X};
    unsigned i;
    for (i = 0; i < 4; ++i) {
        SET(XEEN_DS_BUTTON_HEIGHT_TABLE + 4u * i, pointers[i]);
        SET(XEEN_DS_BUTTON_HEIGHT_TABLE + 2u + 4u * i, XEEN_DS_SEGMENT);
    }
    SET(XEEN_DS_BUTTON_KEY_TABLE, XEEN_DS_EXPLORATION_BUTTON_KEYS);
    SET(XEEN_DS_BUTTON_KEY_TABLE + 2u, XEEN_DS_SEGMENT);
    SET(XEEN_DS_BUTTON_SPRITES, W(XEEN_DS_SPRITE_HANDLE));
    SET(XEEN_DS_BUTTON_ENABLED_TABLE, XEEN_DS_EXPLORATION_BUTTON_ENABLED);
    SET(XEEN_DS_BUTTON_ENABLED_TABLE + 2u, XEEN_DS_SEGMENT);
    (void)xeen_presentation_select_cursor(p, W(XEEN_DS_MOUSE_SPRITES), 0);
    p->page_selector ^= XEEN_PRESENT_PAGE_2;
}
/* The map entry's finish (17A20..17AF7) as resumable stages (enum xeen_world_entry_stage); contract in
 * xeen/world_entry.h. */
xeen_caller_result xeen_world_entry_finish(xeen_movement *m, xeen_presentation *p, unsigned *stage)
{
    xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_ENTRY_FINISH};
    xeen_status st;
    if (!m || !p || !stage || *stage > XEEN_ENTRY_STAGE_STEP_EVENT) {
        return r;
    }
/* Runs one step; a failure is reported at the step's boundary STEP. */
#define DO(expr, step)         \
    do {                       \
        st = (expr);           \
        if (st != XEEN_OK) {   \
            r.status = st;     \
            r.boundary = step; \
            return r;          \
        }                      \
    } while (0)
    if (*stage == XEEN_ENTRY_STAGE_PORTRAITS) {
        DO(portraits(m, p), ENTRY_PORTRAITS_STEP);
        *stage = XEEN_ENTRY_STAGE_COMMAND_ICONS;
    }
    if (*stage == XEEN_ENTRY_STAGE_COMMAND_ICONS) {
        entry_command_icons(m, p);
        *stage = XEEN_ENTRY_STAGE_FIRST_SCREEN;
    }
    if (*stage == XEEN_ENTRY_STAGE_FIRST_SCREEN) {
        DO(xeen_presentation_update_window(p, XEEN_WINDOW_SCREEN), ENTRY_FIRST_SCREEN_STEP);
        *stage = XEEN_ENTRY_STAGE_ICONS;
    }
    if (*stage == XEEN_ENTRY_STAGE_ICONS) {
        xeen_draw_list list;
        /* 18765(26),18765(C): open saved windows need 4F57B restoration. */
        if (B(XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_EVENT_TEXT)) || B(XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_BOTTOM)) ||
            B(XEEN_DS_UI_MODE) == XEEN_UI_MODE_EVENT) {
            r.status = XEEN_SERVICE_REQUIRED;
            r.boundary = ENTRY_WINDOW_RESTORE;
            return r;
        }
        /* 28D82/28D8A, 18765 of the two closed windows (no state:
         * the flags are clear), as the original calls them. */
        xeen_window_close(m, XEEN_WINDOW_EVENT_TEXT);
        xeen_window_close(m, XEEN_WINDOW_BOTTOM);
        DO(xeen_scene_collect_draw_list(m, 0, XEEN_DS_COMMAND_ICONS, &list), ENTRY_ICONS_STEP);
        DO(xeen_presentation_submit(p, &list), ENTRY_ICONS_STEP);
        if (B(XEEN_DS_TEXT_OFF) && B(XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH))) {
            r.status = XEEN_SERVICE_REQUIRED;
            r.boundary = ENTRY_SPEECH_FACE;
            return r;
        }
        *stage = XEEN_ENTRY_STAGE_ICON_BOX;
    }
    if (*stage == XEEN_ENTRY_STAGE_ICON_BOX) {
        DO(xeen_presentation_update_window(p, XEEN_WINDOW_ICONS), ENTRY_ICON_BOX_STEP);
        *stage = XEEN_ENTRY_STAGE_SCREEN;
    }
    if (*stage == XEEN_ENTRY_STAGE_SCREEN) {
        DO(xeen_presentation_update_window(p, XEEN_WINDOW_SCREEN), ENTRY_SCREEN_STEP);
        *stage = XEEN_ENTRY_STAGE_BUTTONS;
    }
    if (*stage == XEEN_ENTRY_STAGE_BUTTONS) {
        entry_exploration_buttons(m, p);
        DO(xeen_presentation_full_copy(p), ENTRY_PAGE_COPY_STEP);
        *stage = XEEN_ENTRY_STAGE_MONSTERS;
    }
    if (*stage == XEEN_ENTRY_STAGE_MONSTERS) {
        B(XEEN_DS_MONSTERS_ENABLED) = 1;
        B(XEEN_DS_MONSTER_COUNTDOWN) = 0;
        if (!B(XEEN_DS_UI_MODE)) {
            B(XEEN_DS_UI_MODE) = XEEN_UI_MODE_EXPLORATION;
            xeen_presentation_fade(p, 1, 4);
        } /* 17ABF..17ACB; the fade */
        *stage = XEEN_ENTRY_STAGE_METADATA;
    }
    if (*stage == XEEN_ENTRY_STAGE_METADATA) {
        DO(xeen_metadata(m), ENTRY_METADATA_STEP);
        *stage = XEEN_ENTRY_STAGE_STEP_EVENT;
    }
    if (W(XEEN_DS_CELL_STEP_EVENT)) {
        r.status = XEEN_SERVICE_REQUIRED;
        r.boundary = XEEN_LOOP_STEP_EVENT;
        return r;
    }
    r.status = XEEN_OK;
    r.boundary = XEEN_ENTRY_DONE;
    return r;
#undef DO
}
