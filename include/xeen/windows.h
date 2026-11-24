#ifndef XEEN_WINDOWS_H
#define XEEN_WINDOWS_H
/* The driver's window numbers (Phase 265, from presentation.h): a header without dependencies, so that the event and
 * scene libraries can name the windows whose open flags they read (XEEN_DS_WINDOW_FLAG) without the graphics
 * interface. presentation.h includes it. */
/*
 * Windows of the driver's window table by the screen that uses them, named only where every native user of the number
 * draws that screen (the others stay numbers until their users are reviewed). The value is the table index.
 */
enum xeen_window {
    /* Shared windows, named by their place on the screen (the driver's table, src/graphics/window_table.inc): many
     * screens draw into them */
    XEEN_WINDOW_SCREEN = 0,             /* (0, 0) 320 x 200: the whole screen */
    XEEN_WINDOW_MINIMAP = 1,            /* (237, 9) 80 x 65: the minimap at the top of the right column (its cells start
                                         * at 237, 12); a scene pass updates it with the view */
    XEEN_WINDOW_COMBAT = 2,             /* (225, 1) 94 x 72: combat's box over the minimap (the monsters in reach and
                                         * the target arrow); while it is open the minimap is not drawn */
    XEEN_WINDOW_VIEW_FRAME = 3,         /* (0, 0) 230 x 149: the 3D view with its frame */
    XEEN_WINDOW_SCROLL = 6,             /* (52, 149) 216 x 48: the scroll box at the bottom (messages, prompts) */
    XEEN_WINDOW_RIGHT_COLUMN = 10,      /* (226, 0) 93 x 146: the column right of the view */
    XEEN_WINDOW_VIEW = 11,              /* (8, 8) 216 x 132: the 3D view itself */
    XEEN_WINDOW_BOTTOM = 12,            /* (0, 143) 320 x 56: the bottom panel over the party bar */
    XEEN_WINDOW_MESSAGE_BOX = 21,       /* (99, 59) 138 x 82: a centred message box (saving, creating files) */
    XEEN_WINDOW_UPPER_SCREEN = 24,      /* (0, 0) 320 x 146: the screen above the party bar (the character view,
                                         * Quick Reference, the Quests list) */
    XEEN_WINDOW_VIEW_DIALOG = 25,       /* (27, 6) 180 x 136: a list over the 3D view (the save and load slots, a
                                         * guild's spells, the name entry) */
    XEEN_WINDOW_LIST_BUTTONS = 29,      /* (0, 101) 320 x 45: the button row under the full-width list */
    XEEN_WINDOW_LIST = 30,              /* (0, 0) 320 x 108: a full-width list (a shop's wares, the quests, the
                                         * awards, an item list) */
    XEEN_WINDOW_PARTY_BAR = 33,         /* (8, 147) 216 x 45: the party's portraits and HP gems */
    XEEN_WINDOW_ICONS = 34,             /* (232, 74) 80 x 64: the command icons in the right column (also where the
                                         * yes/no buttons are drawn) */
    /* Windows with one role */
    XEEN_WINDOW_MAP = 5,                /* the M map and its caption (game_combat.c's map) */
    XEEN_WINDOW_WAIT = 9,               /* (103, 156) 114 x 30: "Please Wait..." (a map load, a new game) or a
                                         * fall's "Oops!" */
    XEEN_WINDOW_ITEM_ACTION = 13,       /* an item option's "%s which item?" (game_item_options.c) */
    XEEN_WINDOW_ITEM_IDENTIFY = 14,     /* the Identify Item description */
    XEEN_WINDOW_ELEMENT_CHOICE = 15,    /* the attack spells' "Which Element?" */
    XEEN_WINDOW_SPELL_TARGET = 16,      /* the party spells' "On Whom?" */
    XEEN_WINDOW_IDENTIFY_MONSTER = 17,  /* (20, 142) 270 x 57: Identify Monster's table of the three nearest */
    XEEN_WINDOW_DETECT_MONSTERS = 19,   /* (231, 48) 86 x 93: Detect Monster's map of the 7 x 7 cells around */
    XEEN_WINDOW_TOWN_PORTAL = 20,       /* Town Portal's five towns */
    XEEN_WINDOW_CONFIRM = 22,           /* an event's yes/no confirmation (game_confirm.c) */
    XEEN_WINDOW_CONTROL_PANEL = 23,     /* the control panel (game_panel.c) */
    XEEN_WINDOW_CREATION_EXCHANGE = 26, /* character creation's "Exchange %s with..." */
    XEEN_WINDOW_MEMBER_PROMPT = 31,     /* (50, 112) 216 x 36: "Dismiss whom?", "Exchange with whom?" */
    XEEN_WINDOW_DICE = 32,              /* (12, 11) 152 x 83: character creation's dice box (the dice bounce inside
                                         * 13..163 x 12..93) */
    XEEN_WINDOW_BANK = 35,              /* the bank's deposit and withdrawal */
    XEEN_WINDOW_WHO_WILL = 36,          /* an event's "Who will ..." member choice */
    XEEN_WINDOW_VIEW_DIALOG_TEXT = 37,  /* (27, 6) 168 x 136: the text area of the view dialog (window 25 less its right
                                         * 12 pixels): the guild's spell list, the save and load list */
    XEEN_WINDOW_EVENT_TEXT = 38,        /* (225, 140) 94 x 59: an event's text in the corner below the icons
                                         * ("Nothing here.", a sign's line) */
    XEEN_WINDOW_DEFINED = 28,           /* no rectangle of its own: each screen defines it before use (the main menu's
                                         * box, combat's Info, the inn's "which character?", a statistic's explanation) */
    XEEN_WINDOW_BUTTON_FEEDBACK = 39,   /* a chosen button's pressed frame (the input poll) */
    XEEN_WINDOW_SPEECH = 40             /* (226, 0) 93 x 100: a speaking NPC's portrait, name and text */
};
#endif
