/*
 * Character creation (Phase 246, from game_inn.c): 4063B, opened by the inn's C (443B6): the free roster
 * slots, 40332's stat rolls, 40212's classes, 403D4's text, 3FB10's dice animation and poll, the keys, the
 * name (2894F) and the record (3FF1C), and the stat exchange 3F9C4.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when character creation cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    CREATE_STOP_DICE = 0x40647,              /* dice.vga */
    CREATE_STOP_ICONS = 0x40713,             /* create.icn */
    CREATE_STOP_ROLL = 0x4036b,              /* the stats' roll */
    CREATE_STOP_TEXT = 0x405db,              /* the creation text could not be formed */
    CREATE_STOP_SCREEN_TEXT = 0x40856,       /* the screen's text does not fit */
    CREATE_STOP_REDRAW_TEXT = 0x40d34,
    CREATE_STOP_EXCHANGE_SPRITES = 0x3f9d0,  /* create2.icn */
    CREATE_STOP_EXCHANGE_TEXT = 0x4118d
};
static xeen_status create_xchg_closed(xeen_game *, unsigned);
/* ---------------------------------------- 4063B character creation
 * From the inn's C (443B6). The free roster slots (no name, 0..23) in
 * order; the slot's race (slot/4 % 5) and sex (slot & 1); 40332 rolls the
 * seven stats (three rounds of random(10, 4Fh)/10 each; a roll leaving no
 * class is rolled again); 40212 the classes; 403D4 the text (on the stack;
 * the first draw formats AC0F); 3FB10 the dice animation and its poll.
 * Keys (CS:2FE7): Enter selects the highlighted class, F0/F1 move the
 * highlight, R/Space roll again, F2/F3 choose the slot, C creates (window 6
 * "Enter a Name for this Character", 2894F, then 3FF1C writes the record
 * and a new round starts), Esc returns. The class buttons (a..j) are mouse
 * input; the stat exchange (M I P E S A L, 3F9C4) is an explicit stop. */
/* Driver-segment tables below DS (XEEN.DAT image, analysis 580B0..580F1):
 * the class skill (580B), the race skill and its name offset (580C, 580D),
 * the class skill name length (580E) and the four starting spells per
 * class (580E:000A). */
/* Each class's and race's starting skill (FFh none), by xeen_class / xeen_race. */
/* "None" in creation's bytes: no class chosen or highlighted (create_class, create_highlight, the first allowed
 * class not yet found), a class or race without a skill, an unused spell slot of a class. */
enum {
    CREATE_NO_CLASS = 0xff,
    CREATE_NO_SKILL = 0xff,
    CREATE_NO_SPELL = 0xff
};
static const uint8_t create_class_skill[10] = {
        [XEEN_CLASS_KNIGHT] = XEEN_SKILL_ARMS_MASTER,       [XEEN_CLASS_PALADIN] = XEEN_SKILL_CRUSADER,
        [XEEN_CLASS_ARCHER] = CREATE_NO_SKILL,                         [XEEN_CLASS_CLERIC] = CREATE_NO_SKILL,
        [XEEN_CLASS_SORCERER] = XEEN_SKILL_CARTOGRAPHER,    [XEEN_CLASS_ROBBER] = XEEN_SKILL_THIEVERY,
        [XEEN_CLASS_NINJA] = XEEN_SKILL_THIEVERY,           [XEEN_CLASS_BARBARIAN] = CREATE_NO_SKILL,
        [XEEN_CLASS_DRUID] = XEEN_SKILL_DIRECTION_SENSE,    [XEEN_CLASS_RANGER] = XEEN_SKILL_PATH_FINDER};
static const uint8_t create_race_skill[5] = {[XEEN_RACE_HUMAN] = XEEN_SKILL_SWIMMER, [XEEN_RACE_ELF] = CREATE_NO_SKILL,
                                             [XEEN_RACE_DWARF] = XEEN_SKILL_DANGER_SENSE,
                                             [XEEN_RACE_GNOME] = XEEN_SKILL_SPOT_SECRET_DOORS,
                                             [XEEN_RACE_HALF_ORC] = CREATE_NO_SKILL};
static const uint8_t create_race_offset[5] = {0, 0, 0, 5, 0};
static const uint8_t create_skill_length[10] = {11, 8, 0, 0, 12, 8, 8, 0, 9, 11};
/* Each class's four starting spells, by class (255: CREATE_NO_SPELL). */
static const uint8_t create_spells[40] = {255, 255, 255, 255, 21,  255, 255, 255, 22,  255,
                                          255, 255, 21,  1,   14,  255, 22,  0,   25,  255,
                                          255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                          255, 255, 20,  1,   11,  23,  20,  1,   255, 255};
/* 4085E..40B8B: the static create.icn frames (frame, x, y). */
static const uint8_t create_frames[30][3] = {
        {0xa, 0xa8, 0x13},  {0xc, 0xa8, 0x2b},  {0xe, 0xa8, 0x43},  {0x10, 0xa8, 0x5b},
        {0x12, 0xa8, 0x73}, {0x14, 0xa8, 0x8b}, {0x16, 0xa8, 0xa3}, {0x18, 0xe3, 0x13},
        {0x1a, 0xe3, 0x1e}, {0x1c, 0xe3, 0x29}, {0x1e, 0xe3, 0x34}, {0x20, 0xe3, 0x3f},
        {0x22, 0xe3, 0x4a}, {0x24, 0xe3, 0x55}, {0x26, 0xe3, 0x60}, {0x28, 0xe3, 0x6b},
        {0x2a, 0xe3, 0x76}, {0x32, 0xc3, 0x1f}, {0x33, 0xc3, 0x37}, {0x34, 0xc3, 0x4f},
        {0x35, 0xc3, 0x67}, {0x36, 0xc3, 0x7f}, {0x37, 0xc3, 0x97}, {0x38, 0xc3, 0xaf},
        {0x39, 0x3e, 0x94}, {0x3a, 0x3e, 0x9e}, {0x3b, 0x3e, 0xa8}, {0x3d, 0xdc, 0x13},
        {0x40, 0xdc, 0x9b}, {0x41, 0xdc, 0xaa}};
/** Stops character creation at unsupported BOUNDARY. */
static xeen_status create_stop(xeen_game *g, uint32_t boundary)
{
    g->panel_stage = 0;
    return panel_stop(g, boundary);
}
static xeen_status create_exit(xeen_game *);
/* Which classes the rolled statistics allow, each by its minimums (compared as signed bytes).
 * Original: 40212. */
static void create_classes(xeen_game *g)
{
    const int8_t *s = g->create_stats;
    uint8_t *a = g->create_allowed;
    a[XEEN_CLASS_KNIGHT] = s[XEEN_STAT_MIGHT] >= 15;
    a[XEEN_CLASS_PALADIN] = s[XEEN_STAT_MIGHT] >= 13 && s[XEEN_STAT_PERSONALITY] >= 13 && s[XEEN_STAT_ENDURANCE] >= 13;
    a[XEEN_CLASS_ARCHER] = s[XEEN_STAT_INTELLECT] >= 13 && s[XEEN_STAT_ACCURACY] >= 13;
    a[XEEN_CLASS_CLERIC] = s[XEEN_STAT_PERSONALITY] >= 13;
    a[XEEN_CLASS_SORCERER] = s[XEEN_STAT_INTELLECT] >= 13;
    a[XEEN_CLASS_ROBBER] = s[XEEN_STAT_LUCK] >= 13;
    a[XEEN_CLASS_NINJA] = s[XEEN_STAT_SPEED] >= 13 && s[XEEN_STAT_ACCURACY] >= 13;
    a[XEEN_CLASS_BARBARIAN] = s[XEEN_STAT_ENDURANCE] >= 15;
    a[XEEN_CLASS_DRUID] = s[XEEN_STAT_INTELLECT] >= 15 && s[XEEN_STAT_PERSONALITY] >= 15;
    a[XEEN_CLASS_RANGER] = s[XEEN_STAT_INTELLECT] >= 12 && s[XEEN_STAT_PERSONALITY] >= 12 &&
                           s[XEEN_STAT_ENDURANCE] >= 12 && s[XEEN_STAT_SPEED] >= 12;
}
/* Rolls the seven statistics: three rounds, each adding to every statistic a draw of 10..79 divided by 10 (1..7;
 * the draw's AX is the statistic so far), so each ends 3..21; the whole roll starts again from zero while no class
 * allows the result. Returns 0 without an event random source, else 1.
 * Original: 40332 (504DA(10, 4Fh)), then 40212. */
static int create_roll(xeen_game *g)
{
    unsigned i, round, k;
    if (!g->event_random) {
        return 0;
    }
    for (;;) {
        for (i = 0; i < 7u; ++i) {
            g->create_stats[i] = 0;
        }
        for (round = 0; round < 3u; ++round) {
            for (i = 0; i < 7u; ++i) {
                uint16_t raw = g->event_random(g->event_random_user, (uint8_t)g->create_stats[i]),
                         v = 0;
                (void)xeen_entity_rng_range(raw, 10u, 79u, &v);
                g->create_stats[i] = (int8_t)((uint8_t)g->create_stats[i] + (uint8_t)(v / 10u));
            }
        }
        create_classes(g);
        for (k = 0; k < 10u; ++k) {
            if (g->create_allowed[k]) {
                return 1;
            }
        }
    }
}
/* Builds the creation screen's text: the race, sex and seven statistics, each class in its colour (2 not allowed, 4
 * allowed, 0Ch the highlighted one once a class is chosen), the class's and the race's skills, and a chosen class's
 * name line (into the text buffer). Returns the highlight to show: the first allowed class while none is chosen,
 * else the current highlight; -2 when the text does not fit.
 * Original: 403D4 (the text 7447; the class line "\t062\v168%s" into A827). */
static int create_text(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char skill[14], line[64];
    const char *cls = "", *race_skill = "", *picked = "";
    uint8_t colours[10];
    int first = CREATE_NO_CLASS, k, n;
    unsigned race = g->create_race, cl = g->create_class;
    if (cl != CREATE_NO_CLASS) {
        if (create_class_skill[cl] != CREATE_NO_SKILL) {
            memset(skill, 0, sizeof skill);
            memcpy(skill,
                   far_text(g, XEEN_DS_SKILL_NAMES, create_class_skill[cl]),
                   create_skill_length[cl]);
            cls = skill;
        }
        (void)snprintf(line, sizeof line, ds_text(g, XEEN_TEXT_CREATION_CLASS_LINE),
                       far_text(g, XEEN_DS_CLASS_NAMES, cl));
        ds_put_text(g, XEEN_DS_TEXT_BUFFER, line);
        picked = ds_text(g, XEEN_DS_TEXT_BUFFER);
    }
    if (create_race_skill[race] != CREATE_NO_SKILL) {
        race_skill = ds_text(
                g, (uint16_t)(xeen_read16(m, (uint16_t)(XEEN_DS_SKILL_NAMES + 4u * create_race_skill[race])) +
                              create_race_offset[race]));
    }
    memset(colours, 2, sizeof colours);
    for (k = 0; k < 10; ++k) {
        if (g->create_allowed[k]) {
            if (cl == CREATE_NO_CLASS && first > k) {
                first = k;
            }
            colours[k] = 4;
        }
    }
    if (cl != CREATE_NO_CLASS && g->create_highlight >= 0 && g->create_highlight < 10) {
        colours[g->create_highlight] = 0xc;
    }
    n = snprintf(g->create_screen, sizeof g->create_screen, ds_text(g, XEEN_TEXT_CREATION_ATTRIBUTES),
                 far_text(g, XEEN_DS_RACE_NAMES, race),
                 far_text(g, XEEN_DS_SEX_NAMES, g->create_sex),
                 g->create_stats[0], g->create_stats[1], g->create_stats[2], g->create_stats[3],
                 g->create_stats[4], g->create_stats[5], g->create_stats[6], colours[0], colours[1],
                 colours[2], colours[3], colours[4], colours[5], colours[6], colours[7], colours[8],
                 colours[9], cls, race_skill, picked);
    if (n < 0 || (size_t)n >= sizeof g->create_screen) {
        return -2;
    }
    return cl == CREATE_NO_CLASS ? first : g->create_highlight;
}
/* Draws the creation screen: its 30 frame pieces, the portrait, the buttons and TEXT, then shows it; the highlighted
 * class is marked after it. Original: 50BF6(0); 405F5 marks the highlight. */
static void create_draw(xeen_game *g, const char *text)
{
    xeen_movement *m = &g->movement;
    unsigned k;
    uint16_t h = g->create_icons;
    static const uint8_t buttons[5][3] = {
            {0, 0x84, 0x62}, {2, 0x84, 0x80}, {4, 0x84, 0x9e}, {6, 0x56, 0x62}, {8, 0x56, 0x78}};
    for (k = 0; k < 30u; ++k) {
        exit_draw(g, h, create_frames[k][0], create_frames[k][1], create_frames[k][2], 0);
    }
    exit_draw(g, xeen_read16(m, XEEN_DS_ROSTER_FACE_SPRITE(g->create_free[g->create_slot])), 0, 27,
              102, 0);
    for (k = 0; k < 5u; ++k) {
        exit_draw(g, h, buttons[k][0], buttons[k][1], buttons[k][2], 0);
    }
    menu_window_text(g, XEEN_WINDOW_SCREEN, WINDOW_TEXT_ONLY, text);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
}
/* Marks the highlighted class: the column under the marks (icon frame 61 at 220, 19), the mark (frame 63) on the
 * highlighted class's row, 11 pixels per row from 21, then the screen shown. An earlier native drew the mark inside
 * the screen's draw and never after F0/F1 or the redraw's search for the next allowed class (the visual
 * differential's finding next to owner report V5).
 * Original: 405F5(highlight, SI): create.icn frame 3Dh at (DCh, 13h), frame 3Fh at (DCh, 0Bh x highlight + 15h),
 * 50BF6(0); the search 40C2A. */
static void create_mark(xeen_game *g)
{
    exit_draw(g, g->create_icons, 61, 220, 19, 0);
    exit_draw(g, g->create_icons, 63, 220, (int16_t)(g->create_highlight * 11 + 21), 0);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
}
/**
 * Moves the highlight to the next allowed class after it (Right, and the redraw with no highlight), marking it; with
 * none allowed after it, nothing changes and nothing is drawn. No highlight (FFh) is -1 as a signed byte: the search
 * then starts at class 0.
 * Original: 40C2A (40C12/40C1C when found).
 */
static void create_highlight_next(xeen_game *g)
{
    int k;
    for (k = (int8_t)g->create_highlight + 1; k <= 9; ++k) {
        if (g->create_allowed[k]) {
            g->create_highlight = k;
            create_mark(g);
            break;
        }
    }
}
/**
 * Moves the highlight to the previous allowed class before it (Left), then marks the highlight, moved or not. No
 * highlight (FFh, above 9) searches from class 9 down. With no highlight the original searches downward from past
 * the ten flags (stack bytes, unverified); natively only the ten flags are read.
 * Original: 40BFF (then 40C1C); the flags at [BP-20h + FEh].
 */
static void create_highlight_previous(xeen_game *g)
{
    int k;
    for (k = (g->create_highlight > 9 ? 10 : g->create_highlight) - 1; k >= 0; --k) {
        if (g->create_allowed[k]) {
            g->create_highlight = k;
            break;
        }
    }
    create_mark(g);
}
/**
 * Moves to the previous (UP set) or next free roster place and proposes its race (the place / 4, modulo 5) and sex
 * (its low bit). Returns 0, changing nothing, at the first place (Up) or the last one (Down).
 * Original: 40CBA (Up) and 40CC8 (Down).
 */
static int create_change_place(xeen_game *g, int up)
{
    if (up) {
        if (!g->create_slot) {
            return 0;
        }
        --g->create_slot;
    } else if (++g->create_slot == g->create_count) {
        --g->create_slot;
        return 0;
    }
    g->create_race = (uint8_t)((g->create_free[g->create_slot] / 4u) % 5u);
    g->create_sex = (uint8_t)(g->create_free[g->create_slot] & 1u);
    return 1;
}
/*
 * A new round of character creation (after the setup, and after each created character): the free roster places
 * are listed (none left ends creation); the first free place proposes the race (its number / 4, modulo 5) and the
 * sex (its low bit); the attributes are rolled, the screen drawn with the class mark, and the creation poll waits.
 * Original: 4077C.
 */
static xeen_status create_round(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char out[1200];
    unsigned i;
    int r, n;
    g->create_class = CREATE_NO_CLASS;
    memset(g->create_free, 0xff, sizeof g->create_free);
    g->create_slot = 0;
    g->create_count = 0;
    for (i = 0; i < 24u; ++i) {
        if (!g->roster[XEEN_CHARACTER_BYTES * i]) {
            g->create_free[g->create_count++] = (uint8_t)i;
        }
    }
    if (!g->create_count) {
        return create_exit(g);
    }
    g->create_race = (uint8_t)((g->create_free[0] / 4u) % 5u);
    g->create_sex = (uint8_t)(g->create_free[0] & 1u);
    if (!create_roll(g)) {
        return create_stop(g, CREATE_STOP_ROLL);
    }
    if ((r = create_text(g)) < -1) {
        return create_stop(g, CREATE_STOP_TEXT);
    }
    g->create_highlight = r;
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_CREATION_BUTTONS), g->create_screen);
    if (n < 0 || (size_t)n >= sizeof out || n >= 1000) {
        return create_stop(g, CREATE_STOP_SCREEN_TEXT);
    }
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    create_draw(g, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    if (g->create_highlight != CREATE_NO_CLASS) {
        create_mark(g);
    }
    g->create_sampled = 0;
    g->panel_stage = CREATE_INTRO;
    g->lifecycle_boundary = CREATE_POLL_BOUNDARY;
    return XEEN_SERVICE_REQUIRED;
}
/*
 * Starts character creation from the inn. With all 24 places taken it refuses ("Your Roster is full!" in a scroll,
 * then the inn's poll). Else the screen fades out, the view's window closes and portrait clicks stop; the dice get
 * their sprites, starting places, directions and frames; the UI mode is saved and set to creation; the creation
 * icons and buttons, the mouse hidden, the background and the cursor; then the first round.
 * Original: 443B6 (443F8, 18F2F mode 0, the 441D3 poll); 4A75A(4), 18765(0Bh), 98D6 = 0; 4063B: dice.vga (8DA4),
 * 9A1C saved and 4, create.icn (SI), the buttons 1D2A..1DB0, 4E231, create.raw, the cursor.
 */
xeen_status create_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    if (g->inn_count >= 24u) { /* the roster's 24 places */
        return scroll0_open(g, MESSAGE_INN, ds_text(g, XEEN_TEXT_ROSTER_FULL));
    }
    xeen_presentation_fade(g->presentation, 0, 4); /* the fade out (owner report V6) */
    xeen_window_close(m, XEEN_WINDOW_VIEW);
    ds[XEEN_DS_PORTRAIT_CLICKS] = 0;
    if (!sprite_load(g, ds_text(g, XEEN_TEXT_CREATION_DICE), &h)) {
        return create_stop(g, CREATE_STOP_DICE);
    }
    xeen_write16(m, XEEN_DS_DICE_SPRITE, h);
    { /* the three dice's places, steps and frames (owner report V5; Original: 40652..406FA, at 580E) */
        static const creation_die start[3] = {{.x = 20, .y = 17, .dx = 10, .dy = -10, .frame = 0},
                                              {.x = 112, .y = 35, .dx = -10, .dy = -10, .frame = 2},
                                              {.x = 61, .y = 50, .dx = -10, .dy = 10, .frame = 4}};
        memcpy(g->dice, start, sizeof start);
    }
    g->create_faded = 0;
    g->create_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_CREATION;
    if (!sprite_load(g, ds_text(g, XEEN_TEXT_CREATION_SPRITES), &g->create_icons)) {
        return create_stop(g, CREATE_STOP_ICONS);
    }
    button_tables(m, XEEN_BUTTONS_CREATION);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, g->create_icons);
    xeen_presentation_mouse_off(g->presentation);
    (void)menu_raw(g, ds_text(g, XEEN_TEXT_CREATION_BACKGROUND));
    (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES), 0);
    return create_round(g);
}
/*
 * Ends character creation: the UI mode back, the dice's sprites freed; at the inn the roster is committed and the
 * save files written, the screen fades out, the mouse is hidden, portrait clicks resume, the party is reloaded, and
 * the inn begins again (with the dawn pending when it is before 5:00).
 * Original: 41213: 9A1C back, 4AA3E(8DA4); the inn's 443D6: 43BFE, 19278, 4A75A(4), 4E231, [BP-6] = 1, 98D6 = 1;
 * 43DA0: 19953, 43DA5, 9842 and 43E0B.
 */
static xeen_status create_exit(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint32_t stop;
    xeen_status s;
    ds[XEEN_DS_UI_MODE] = g->create_mode;
    (void)xeen_sprite_free_from(&g->sprites, xeen_read16(m, XEEN_DS_DICE_SPRITE));
    inn_commit(g);
    if ((stop = inn_save_files(g)) != 0) {
        return create_stop(g, stop);
    }
    ++g->saves;
    xeen_presentation_fade(g->presentation, 0, 4); /* the fade out (owner report V7) */
    xeen_presentation_mouse_off(g->presentation);
    g->inn_visited = 1;
    ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    if ((s = menu_party(g)) != XEEN_OK) {
        return s;
    }
    ds[XEEN_DS_DAWN_PENDING] = (uint8_t)(xeen_read16(m, XEEN_DS_MINUTE_OF_DAY) < XEEN_NIGHT_ENDS);
    return inn_begin(g);
}
/* Redraws the creation screen: its text again (create_text), the screen, and the highlight: the next allowed class
 * when none is highlighted, else the highlighted class marked; then the poll. Stops when the text does not fit.
 * Original: 40D14: 403D4, then 40C1C (405F5) or, with no highlight, 40C2A. */
static xeen_status create_redraw(xeen_game *g)
{
    int r = create_text(g);
    if (r < -1) {
        return create_stop(g, CREATE_STOP_REDRAW_TEXT);
    }
    g->create_highlight = r;
    create_draw(g, g->create_screen);
    if (g->create_highlight == CREATE_NO_CLASS) {
        create_highlight_next(g); /* 4108A: 40C2A */
    } else {
        create_mark(g); /* 41094: 40C1C */
    }
    return panel_set(g, CREATE_POLL, CREATE_POLL_BOUNDARY);
}
/* The new character's name was entered (C): the entry closed and its button set popped; an empty name goes back to
 * creation's poll; otherwise the character is made in the slot's roster entry: the name, the inn (map and side),
 * sex, race, class and the rolled statistics; level 1 (5 on Darkside); the race's resistances; born 18 years before
 * this year on this day; the class's starting spells (the first readied) and the class's and race's skills; HP and
 * SP at their maxima; then a new round of creation.
 * Original: 3FF77 after 2894F: 18765(6), 18FFF; 3FF8A (the record at the slot's roster entry, 9A1C = 4). */
xeen_status create_named(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, *r;
    unsigned i, len = g->panel_name_length, race, cl, sp;
    panel_window(g, XEEN_WINDOW_SCROLL, 0);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    g->name_owner = NAME_GAME;
    if (!len) {
        ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_CREATION;
        return panel_set(g, CREATE_POLL, CREATE_POLL_BOUNDARY);
    }
    r = g->roster + XEEN_CHARACTER_BYTES * g->create_free[g->create_slot];
    race = g->create_race;
    cl = g->create_class;
    memset(r, 0, XEEN_CHARACTER_BYTES);
    memcpy(r, g->create_name, len);
    xeen_put_le16(r + XEEN_CHARACTER_INN, ds[XEEN_DS_INN_MAP]);
    r[XEEN_CHARACTER_INN_SIDE] = ds[XEEN_DS_MAP_SIDE];
    r[XEEN_CHARACTER_SEX] = g->create_sex;
    r[XEEN_CHARACTER_RACE] = (uint8_t)race;
    r[XEEN_CHARACTER_CLASS] = (uint8_t)cl;
    for (i = 0; i < 7u; ++i) {
        r[XEEN_CHARACTER_STAT(i)] = (uint8_t)g->create_stats[i];
    }
    r[XEEN_CHARACTER_LEVEL] = ds[XEEN_DS_WORLD] ? 5 : 1;
    r[XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_MAGIC)] = ds[XEEN_DS_RACE_RESISTANCE_MAGIC + race];
    r[XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_FIRE)] = ds[XEEN_DS_RACE_RESISTANCE_FIRE + race];
    r[XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_ELECTRICITY)] = ds[XEEN_DS_RACE_RESISTANCE_ELECTRICITY + race];
    r[XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_COLD)] = ds[XEEN_DS_RACE_RESISTANCE_COLD + race];
    r[XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_ENERGY)] = ds[XEEN_DS_RACE_RESISTANCE_ENERGY + race];
    r[XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_POISON)] = ds[XEEN_DS_RACE_RESISTANCE_POISON + race];
    {
        uint16_t year = (uint16_t)(xeen_read16(m, XEEN_DS_YEAR) - 0x12u);
        xeen_put_le16(r + XEEN_CHARACTER_BIRTH_YEAR, year);
    }
    r[XEEN_CHARACTER_BIRTH_DAY] = ds[XEEN_DS_DAY];
    r[XEEN_CHARACTER_CASTER] = 0;
    r[XEEN_CHARACTER_READY_SPELL] = XEEN_NO_READY_SPELL;
    for (i = 0; i < 4u; ++i) {
        sp = create_spells[cl * 4u + i];
        if (sp != CREATE_NO_SPELL) {
            r[XEEN_CHARACTER_CASTER] = 1;
            r[XEEN_CHARACTER_SPELLS + sp] = 1;
            r[XEEN_CHARACTER_READY_SPELL] = create_spells[cl * 4u];
        }
    }
    if (create_class_skill[cl] != CREATE_NO_SKILL) {
        r[XEEN_CHARACTER_SKILLS + create_class_skill[cl]] = 1;
    }
    if (create_race_skill[race] != CREATE_NO_SKILL) {
        r[XEEN_CHARACTER_SKILLS + create_race_skill[race]] = 1;
    }
    {
        uint16_t hp = xeen_combat_max_hp(m, r);
        xeen_put_le16(r + XEEN_CHARACTER_HP, hp);
    }
    {
        uint16_t mp = xeen_combat_max_sp(m, r);
        xeen_put_le16(r + XEEN_CHARACTER_SP, mp);
    }
    ++g->characters_created;
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_CREATION;
    return create_round(g);
}
/* 410B7 (stat DI, M I P E S A L): 9A1C = 56h, the pressed
 * button (create.icn frame 2 DI + 0Bh at (1D34[DI], 1D5D[DI])), 50BF6(0);
 * 3F9C4(DI + 1): 190A3(create2.icn), 190ED, the 1DC6..1DF8 buttons, 9896
 * = 98D4, A827 = "Exchange %s with..." (2F04[DI]), 1871B(1Ah), 4E38C,
 * create2's frame 0 at (76h, 3Ah), 50BF6(1Ah); then the 3FA73 poll. */
static const char create_stat_keys[] = "MIPESAL";
/** Starts exchanging attribute STAT with another in character creation: the UI mode 56h, the attribute's button
 * shown pressed; the prompt's sprites pushed (the depth undone if they fail: a stop), the button set pushed with the
 * exchange's table, the prompt naming the attribute in its window with its button, shown; then its key poll.
 * Original: 410B7. */
static xeen_status create_xchg_begin(xeen_game *g, unsigned stat)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[0x40];
    ds[XEEN_DS_UI_MODE] = 0x56;
    exit_draw(g, g->create_icons, (uint16_t)(2u * stat + 0xbu),
              (int16_t)xeen_read16(m, XEEN_BUTTON_X(XEEN_BUTTONS_CREATION, 5u + stat)),
              (int16_t)ds[XEEN_BUTTON_Y(XEEN_BUTTONS_CREATION, 5u + stat)], 0);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
    g->create_xchg = (uint8_t)stat;
    xeen_presentation_trace_extra("mark dialog-statx"); /* Evidence: 3F9C4 */
    if (!handle_push_or_undo(g, XEEN_TEXT_CREATION_EXCHANGE_SPRITES)) {
        return create_stop(g, CREATE_STOP_EXCHANGE_SPRITES);
    }
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    button_tables(m, XEEN_BUTTONS_CREATION_EXCHANGE);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_CREATION_EXCHANGE), far_text(g, XEEN_DS_STAT_NAMES, stat));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER, out);
    panel_window(g, XEEN_WINDOW_CREATION_EXCHANGE, 1);
    menu_window_text(g, XEEN_WINDOW_CREATION_EXCHANGE, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER));
    exit_draw_in(g, xeen_read16(m, XEEN_DS_SPRITE_HANDLE), 0, 118, 58, 26);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_CREATION_EXCHANGE);
    return panel_set(g, CREATE_XCHG, CREATE_XCHG_BOUNDARY);
}
/* A key while an attribute waits for the one to exchange with: Escape answers 0, another attribute's key answers it
 * plus 1 (create_xchg_closed); the waiting attribute's own key, and any other key, poll again. The answer first
 * closes the prompt: its window, the key code, its button set and sprites.
 * Original: 3FA7C (CS:18B0); 3FAD1: 18765(1Ah), F5FE = 0, 18FFF, 190CF. */
static xeen_status create_xchg_key(xeen_game *g, uint16_t key)
{
    const char *at = key && key < 0x80u ? strchr(create_stat_keys, key) : NULL;
    if (key != XEEN_KEY_ESCAPE && (!at || (unsigned)(at - create_stat_keys) == g->create_xchg)) {
        return panel_set(g, CREATE_XCHG, CREATE_XCHG_BOUNDARY);
    }
    panel_window(g, XEEN_WINDOW_CREATION_EXCHANGE, 0);
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    xeen_button_stack(g->movement.ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    handle_pop(g);
    return create_xchg_closed(g, key == XEEN_KEY_ESCAPE ? 0u : (unsigned)(at - create_stat_keys) + 1u);
}
/* The exchange's answer (0 none, else the other attribute + 1), with the creation buttons' sprites back: none
 * releases the waiting attribute's button and returns to the creation poll (UI mode 4). Otherwise the other's button
 * shows pressed, the two rolled values are swapped, the allowed classes recomputed, no class chosen, and the screen
 * redrawn (create_redraw; the UI mode stays as the prompt left it). Stops when the text does not fit.
 * Original: 410F7: 9896 = create.icn (SI); 0: frame 2 DI + 0Ah, 50BF6(0), 189BC, 9A1C = 4, the 40BD4 poll (41209);
 * else 40212, BP-9 = FFh, 403D4, 189BC, 40D14 (9A1C stays 56h). */
static xeen_status create_xchg_closed(xeen_game *g, unsigned answer)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned stat = g->create_xchg, other;
    int8_t t;
    int r;
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, g->create_icons);
    if (!answer) {
        exit_draw(g, g->create_icons, (uint16_t)(2u * stat + 0xau),
                  (int16_t)xeen_read16(m, XEEN_BUTTON_X(XEEN_BUTTONS_CREATION, 5u + stat)),
                  (int16_t)ds[XEEN_BUTTON_Y(XEEN_BUTTONS_CREATION, 5u + stat)], 0);
        (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
        xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
        ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_CREATION;
        return panel_set(g, CREATE_POLL, CREATE_POLL_BOUNDARY);
    }
    other = answer - 1u;
    exit_draw(g, g->create_icons, (uint16_t)(2u * other + 0xbu),
              (int16_t)xeen_read16(m, XEEN_BUTTON_X(XEEN_BUTTONS_CREATION, 5u + other)),
              (int16_t)ds[XEEN_BUTTON_Y(XEEN_BUTTONS_CREATION, 5u + other)], 0);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
    t = g->create_stats[stat];
    g->create_stats[stat] = g->create_stats[other];
    g->create_stats[other] = t;
    create_classes(g);
    g->create_class = CREATE_NO_CLASS;
    if ((r = create_text(g)) < -1) {
        return create_stop(g, CREATE_STOP_EXCHANGE_TEXT);
    }
    g->create_highlight = r;
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    return create_redraw(g);
}
/**
 * Handles a key in character creation: Enter chooses the highlighted class; Esc leaves; Space or R rolls again (no
 * class chosen; without the event RNG the roll stops, 4036B); C creates (without a chosen class "Select a class"
 * first) by asking the name; Left and Right move the highlight to the previous or next allowed class; Up and Down
 * change the free roster place, which proposes the race and sex; a statistic's letter starts its exchange; a class
 * button's code chooses an allowed class (a disallowed one only redraws); any other key sounds.
 * Original: R 40C54, C 40C6C (3FF2E, AX 0), Left 40BFF (then 40C1C), Right 40C2A (40C12/40C1C when found), Up/Down
 * 40CBA/40CC8, the statistics 41097..410B4, the class buttons 411DC (then 40D14), the sound 41201's 1880E(15h).
 */
static xeen_status create_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    switch (key) {
        case XEEN_KEY_ENTER:
            g->create_class = (uint8_t)g->create_highlight;
            return create_redraw(g);
        case XEEN_KEY_ESCAPE:
            return create_exit(g);
        case ' ':
        case 'R':
            if (!create_roll(g)) {
                return create_stop(g, CREATE_STOP_ROLL);
            }
            g->create_class = CREATE_NO_CLASS;
            return create_redraw(g);
        case 'C':
            m->ds[XEEN_DS_UI_MODE] = 0xff;
            if (g->create_class == CREATE_NO_CLASS) {
                return scroll0_open(g, MESSAGE_CREATION_CLASS, ds_text(g, XEEN_TEXT_CREATION_SELECT_CLASS));
            }
            g->name_owner = NAME_CHARACTER;
            memset(g->create_name, 0, sizeof g->create_name);
            return name_begin(g);
        case XEEN_KEY_LEFT:
            create_highlight_previous(g);
            return panel_set(g, CREATE_POLL, CREATE_POLL_BOUNDARY);
        case XEEN_KEY_RIGHT:
            create_highlight_next(g);
            return panel_set(g, CREATE_POLL, CREATE_POLL_BOUNDARY);
        case XEEN_KEY_UP:
        case XEEN_KEY_DOWN:
            if (!create_change_place(g, key == XEEN_KEY_UP)) {
                return panel_set(g, CREATE_POLL, CREATE_POLL_BOUNDARY);
            }
            return create_redraw(g);
        case 'M':
        case 'I':
        case 'P':
        case 'E':
        case 'S':
        case 'A':
        case 'L':
            return create_xchg_begin(g,
                                     (unsigned)(strchr(create_stat_keys, key) - create_stat_keys));
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
        case 'i':
        case 'j':
            /* a class button (a click's code): an allowed class becomes the chosen one and the highlight */
            if (!g->create_allowed[key - 'a']) {
                return create_redraw(g);
            }
            g->create_class = (uint8_t)(key - 'a');
            g->create_highlight = key - 'a';
            return create_redraw(g);
        default:
            exit_event(g, "sound 15h");
            return panel_set(g, CREATE_POLL, CREATE_POLL_BOUNDARY);
    }
}
/* Keeps one coordinate of a die, *AT, inside LOW..HIGH: below LOW it is put on LOW, then above HIGH on HIGH, each
 * wall negating the die's *STEP on that axis (the two tests in turn, as the original makes them). */
static void die_bounce(int16_t *at, int8_t *step, int low, int high)
{
    if (*at < low) {
        *at = (int16_t)low;
        *step = (int8_t)-*step;
    }
    if (*at > high) {
        *at = (int16_t)high;
        *step = (int8_t)-*step;
    }
}
/* One step of the rolling dice (owner report V5; an earlier native drew none): the box's background (frame 7 at
 * (12, 11)), then for each die the next frame of seven, its move by its steps and the box's bounces (die_bounce: x
 * between 13 and 163 less the new frame's width, y between 12 and 93 less its height), drawn in the dice window.
 * Presentation only. Original: 3FB10: 4AC92 of dice.vga (8DA4) in window 20h; the bounds 0Dh, A3h - width[frame]
 * (5809:0000), 0Ch, 5Dh - height[frame] (580A:0000). */
static void dice_step(xeen_game *g)
{
    static const uint8_t width[7] = {27, 27, 27, 31, 27, 27, 27},
                         height[7] = {26, 20, 27, 30, 26, 20, 27};
    enum { DICE_LEFT = 13, DICE_RIGHT = 163, DICE_TOP = 12, DICE_BOTTOM = 93 }; /* where the dice bounce */
    uint16_t h = xeen_read16(&g->movement, XEEN_DS_DICE_SPRITE);
    exit_draw(g, h, 7, 12, 11, 0);
    for (unsigned i = 0; i < 3u; ++i) {
        creation_die *die = &g->dice[i];
        die->frame = (uint8_t)((int8_t)(die->frame + 1u) % 7);
        die->x = (int16_t)(die->x + die->dx);
        die->y = (int16_t)(die->y + die->dy);
        die_bounce(&die->x, &die->dx, DICE_LEFT, DICE_RIGHT - width[die->frame]);
        die_bounce(&die->y, &die->dy, DICE_TOP, DICE_BOTTOM - height[die->frame]);
        exit_draw(g, h, die->frame, die->x, die->y, 0);
    }
}
/* The creation screen fades in once, after its introduction's first key or tick.
 * Original: 40BC8: 4A74F(4) once ([BP-0Eh]) after the intro 3FB10 (owner report V6: the native once recorded none). */
static void create_fade_in(xeen_game *g)
{
    if (!g->create_faded) {
        xeen_presentation_fade(g->presentation, 1, 4);
    }
    g->create_faded = 1;
}
/* Character creation's waits: the introduction's and the screen's key polls, each pass marking the frame clock once
 * and rolling the dice animation one step; a key in the introduction fades the screen in, a key on the screen goes to
 * create_key; without a key the tick wait follows, and a counted tick starts the next pass (in the introduction, the
 * fade in). The exchange prompt polls its own key. Other stages go to the shop's resume.
 * Original: 3FB10: 4EA0D, the dice, 50BF6(20h), 189E3, then 4EA1C; the intro call's result (40BBC) is not used;
 * 4A74F(4) once (BP-0Eh). */
xeen_status create_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    uint16_t key;
    switch (g->panel_stage) {
        case CREATE_INTRO:
        case CREATE_POLL:
            if (!g->create_sampled) {
                status = frame_base(g, bios, user);
                if (status != XEEN_OK) {
                    return status;
                }
                g->create_sampled = 1;
                dice_step(g);
                (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_DICE);
            }
            /* Evidence: the original-byte replay answers the intro's
         * 3FF08 189E3 in its create-intro-00 adapter (no 189E3 call). */
            if (g->panel_stage == CREATE_INTRO) {
                xeen_presentation_trace_extra("adapterpoll 3ff08");
            }
            key = panel_poll(g);
            if (key && g->panel_stage == CREATE_POLL) {
                return create_key(g, key);
            }
            if (key && g->panel_stage == CREATE_INTRO) {
                create_fade_in(g);
                g->create_sampled = 0;
                return panel_set(g, CREATE_POLL, CREATE_POLL_BOUNDARY);
            }
            g->dialog_loop_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
            g->panel_stage = g->panel_stage == CREATE_INTRO ? CREATE_INTRO_TICK : CREATE_TICK;
            g->lifecycle_boundary = CREATE_TICK_BOUNDARY;
            return XEEN_SERVICE_REQUIRED;
        case CREATE_XCHG: /* 3FA73 */
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            return create_xchg_key(g, key);
        case CREATE_INTRO_TICK:
        case CREATE_TICK:
            if ((status = timer_ticks(g, &g->dialog_loop_elapsed, bios, user)) != XEEN_OK) {
                return status;
            }
            if (!g->timer_registers.ax) {
                g->panel_stage = g->panel_stage == CREATE_INTRO_TICK ? CREATE_INTRO : CREATE_POLL;
                g->lifecycle_boundary = CREATE_POLL_BOUNDARY;
                return XEEN_SERVICE_REQUIRED;
            }
            g->create_sampled = 0;
            if (g->panel_stage == CREATE_INTRO_TICK) {
                create_fade_in(g);
                return panel_set(g, CREATE_POLL, CREATE_POLL_BOUNDARY);
            }
            g->panel_stage = CREATE_POLL;
            g->lifecycle_boundary = CREATE_POLL_BOUNDARY;
            return XEEN_SERVICE_REQUIRED;
        default:
            return shop_resume(g, bios, user); /* Stages */
    }
}
