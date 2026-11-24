/*
 * The game session's private interface (Phase 242), shared by the separately compiled units of src/game/ (39
 * since Phase 246): the session core game.c (the movement services, the CD and voices, drive(), the frame
 * wait, the stop helper and the command tail, the event API and the read-only views); its lifecycle, a map's
 * resources, map transitions, navigation and scene (game_lifecycle.c, game_resources.c, game_transition.c,
 * game_navigation.c, game_scene.c); the event interpreter's host services, its windows and text displays and
 * its UI presenter (game_events.c, game_event_windows.c, game_event_ui.c); the input polls, the 18F2F
 * scrolls and the yes/no confirmation (game_input.c, game_scroll.c, game_confirm.c); the exploration
 * commands, falls, Rest, Shoot, the treasure dialog, the fights and the exit scenes (game_commands.c,
 * game_fall.c, game_rest.c, game_shoot.c, game_treasure.c, game_combat_flow.c, game_exit.c); and the menu,
 * the save pair files, new game, control panel, save, locations, inn, character creation, bank, items
 * dialog, character view, Quests, town, Cast (with its travel and monster-information spells), combat
 * commands, arena, cutscene and ending units (game_menu.c ... game_ending.c, with game_view.c and
 * game_quests.c split from game_shop.c, game_location.c and game_creation.c from game_inn.c, game_pairs.c
 * from game_menu.c, game_save.c from game_panel.c, game_cast_travel.c and game_cast_info.c from
 * game_cast.c). None includes another; the add-on glue fragments are included by the unit they extend. It is
 * not part of the engine's public API (include/xeen/game.h) and is included by no other directory.
 *
 * It holds the session record (struct xeen_game), the stage numbers the session dispatches on (one
 * space per g->menu_stage and g->panel_stage, shared by the units, so they are kept here together),
 * and the operations that cross a unit boundary, grouped by the unit that defines them in the
 * session's order. A function is declared here only when another unit calls it; everything else
 * stays static in its unit.
 */
#ifndef XEEN_GAME_INTERNAL_H
#define XEEN_GAME_INTERNAL_H

#include "xeen/map_objects.h"
#include "xeen/game.h"
#include "xeen/ds_globals.h"
#include <stdlib.h>
#include <string.h>
#include "xeen/program_data.h"
#include "xeen/party.h"
#include "xeen/save.h"
#include "xeen/caller.h"
#include "xeen/post_action.h"
#include "xeen/scene.h"
#include "xeen/scene_construct.h"
#include "xeen/entities.h"
#include "xeen/sprites.h"
#include "xeen/minimap.h"
#include "xeen/interface.h"
#include "xeen/input_poll.h"
#include "xeen/monster_update.h"
#include "xeen/world_entry.h"
#include "xeen/text_window.h"
#include "xeen/modal.h"
#include "xeen/character_record.h"
#include "xeen/byte_order.h"
#include "xeen/town.h"
#include "xeen/treasure.h"
#include "xeen/bash.h"
#include "xeen/projectile.h"
#include "xeen/main_menu.h"
#include "xeen/raster.h"
#include "xeen/items.h"
#include "xeen/spells.h"
#include "xeen/addons.h"
#include "xeen/bugfixes.h"
#include <stddef.h>
#include <stdint.h>

/* xeen_game.event_scene: the handshake by which a script asks for one scene pass. The interpreter's request
 * (event_service_inner and the serve_* helpers) suspends the script; drive() draws the scene and marks it
 * completed; the script's next request then sees the pass done (it returns 1) and the handshake goes back to idle.
 * Original: the events' 1B503(1). */
enum {
    EVENT_SCENE_IDLE = 0,
    EVENT_SCENE_REQUESTED = 1,
    EVENT_SCENE_RUNNING = 2,
    EVENT_SCENE_COMPLETED = 3,
};
/*
 * The stage of a paged text display (serve_paged_text, serve_map_text): idle, writing the next page, or waiting
 * for the key before the next page. It survives the host's suspensions. Both services set page_wait together with
 * PAGE_KEY_WAIT and clear it when the key comes; the "Nothing here." wait (serve_nothing_here) sets page_wait too,
 * with the stage idle.
 * Original: 316DC and 31633, the page written by 4E38C, the key loop 31786 (its poll 31793); "Nothing here." 337D7.
 */
enum {
    PAGE_IDLE = 0,
    PAGE_WRITE = 1,
    PAGE_KEY_WAIT = 2
};
/*
 * xeen_game.scroll_owner: who opened the scroll message (window 6) that is showing, and so who continues when its
 * key closes it (scroll_key). The numbers are kept: scroll_key tests them in this order and as one range.
 * Original: the scroll is 18F2F mode 1, closed by 18FBF's key. The owners' sites in order: 30DB3 from a movement,
 * Shoot 17EF4, Bash 17CEA; Rest 28E69 and 29157; Cast 28CFC (back to 3E11F); Spell Failed 39641 (3E0A9); 2EA68's
 * 2EC44; Space 17DA8; the events' 18F2F(A827, 1) (back to 2DECE); 291FB (18F2F mode 3); 29517; combat
 * 393D3/393F0; Dismiss 2B8A2; Who-will 28CFC (back to 2FAE3); the desert's 30CF3(170) at 172CD; the dispatcher's
 * 2EC49.
 */
enum scroll_owner {
    SCROLL_NONE = 0,
    SCROLL_MOVE_REST = 1,        /* "The Party needs rest!" from a movement's tick */
    SCROLL_SHOOT_REST = 2,       /* the same from Shoot's tick */
    SCROLL_BASH_REST = 3,        /* the same from Bash's tick */
    SCROLL_REST_DANGER = 4,      /* Rest's "Too dangerous to rest here!" */
    SCROLL_REST_DONE = 5,        /* Rest's completion */
    SCROLL_CAST_REFUSED = 6,     /* the Cast dialog's refusal (a member who cannot act): back to the dialog's loop */
    SCROLL_SPELL_FAILED = 7,     /* "Spell Failed!": its close ends the spell's handler */
    SCROLL_PACKS_FULL = 8,       /* "BackPacks Full!" from a command's treasure dialog */
    SCROLL_SPACE_REST = 9,       /* "needs rest" from the time that Space's wait charges */
    SCROLL_EVENT = 10,           /* a script's own scroll message: back to the interpreter */
    SCROLL_PAYMENT = 11,         /* a script's payment refusal ("Not enough gold") */
    SCROLL_NO_SAVE = 12,         /* the control panel's Save refused on a map that forbids saving */
    SCROLL_COMBAT_REST = 13,     /* "needs rest" from the time a combat round charges */
    SCROLL_DISMISS_REFUSED = 14, /* Dismiss refused for the last member */
    SCROLL_WHO_REFUSED = 15,     /* the Who-will prompt's refusal: back to the prompt */
    SCROLL_DESERT_REST = 16,     /* "needs rest" from the desert's extra time */
    SCROLL_DISPATCH_FULL = 17    /* the dispatcher's own "BackPacks Full!" */
};
/* xeen_game.surface_owner: who asked for the surface's effect scene (surface_scene) and so whose tail continues
 * after it. It is also the value space of fall_caller (in xeen_movement and xeen_game; 0 for any other caller of the
 * fall), which fall_return hands on as the owner of its surface scene.
 * Original: the surface routine 17264; the fall 173A0 (its other caller 17F11). */
enum surface_owner {
    SURFACE_MOVEMENT = 1,  /* a movement's tail (Original: 17D50) */
    SURFACE_REST_WAKE = 2, /* Rest's wake (29120) */
    SURFACE_FALL = 3,      /* a fall (173A0(1)) */
    SURFACE_SHOOT = 4      /* Shoot or a paid spell's tick, back to the command tail (17EF4's call at 17EFA, back at
                            * 17EFD -> 17F07) */
};
/* Who opened the character view, and so where its close returns (g->view_owner): exploration's F1..F6 (the monsters
 * move first after an item's use there), the inn's member list, or combat's view of a member. */
enum view_owner {
    VIEW_FROM_EXPLORATION = 1, /* 17DDE */
    VIEW_FROM_INN = 2,         /* 44547, back to the inn's poll 441D3 */
    VIEW_FROM_COMBAT = 3       /* 3933F, back to combat's modal return */
};
/* Who opened the Quests viewer, and so where Esc returns (g->quests_owner). */
enum quests_owner {
    QUESTS_FROM_EXPLORATION = 0, /* V (17E18, then the command tail 17F07) */
    QUESTS_FROM_ITEMS = 1        /* the items dialog's Q (45D4A, then the items dialog again 45152) */
};
/* The party's money and the bank's, as the payments (value_take, value_refused: the original's 29281 and 291FB) and
 * the bank's transactions name them: which kind and where it is kept. A deposit moves funds from the party to the
 * bank, a withdrawal from the bank to the party (g->bank_mode - 1 is the source). */
enum funds_kind {
    FUNDS_GOLD = 0,
    FUNDS_GEMS = 1
};
enum funds_place {
    FUNDS_PARTY = 0, /* F00A gold, F00E gems */
    FUNDS_BANK = 1   /* F012 gold, F016 gems */
};
/* Where a save from the control panel returns after its message (g->panel_return, panel_save's WHERE): the panel's
 * S key back to the panel's redraw, or the Save dialog (after the game's name) back to the dialog's end. A refused
 * save returns the same way. */
enum save_return {
    SAVE_BACK_TO_PANEL = 1,  /* 2987D */
    SAVE_BACK_TO_DIALOG = 2  /* 2B368 -> 2B268 */
};
/* One of the slot dialog's ten save slots (g->save_slots; dialog_slots fills them, dialog_select colours them):
 * header: the slot file's header (all zero when the slot has no file; an empty name shows the empty-slot text);
 * colour: its row's colour, 15 while it is the selected slot, else 0 (the menu text's "{0C}%2u" before each row);
 * line: the text after its name, "A" or "W" (the game's preference) and the level; the name entry rewrites it.
 * Original: 2AD6D's [BP-12h..] (the colours), 199B7's headers, [BP-2BEh + 23h * slot] (the lines). */
typedef struct {
    xeen_save_header header;
    uint8_t colour;
    char line[35];
} dialog_slot;
/* The treasure dialog's item-line waits (g->treasure.item_stage): after an item's blank lines (then its "found" line) and after
 * that line (then the next item). The numbers are the native's own. */
enum item_line_stage {
    ITEM_LINE_NONE = 0,
    ITEM_LINE_BLANKS = 2,
    ITEM_LINE_FOUND = 3
};
/* xeen_game.treasure.owner: who opened the treasure dialog that is showing, continued after its key.
 * Original: the dialog 2EA68. */
enum treasure_owner {
    TREASURE_NONE = 0,
    TREASURE_VOLLEY = 1,      /* Shoot's volley (287FF) */
    TREASURE_JOIN = 2,        /* a join of the step's tails (17ACC, 17AF7, the tile event, the movement post-action) */
    TREASURE_COMBAT = 3,      /* the combat cleanup (394C5) */
    TREASURE_SPELL_VOLLEY = 4 /* a spell's volley (287FF) */
};
/* xeen_game.fall_stage: 173A0 (fall_begin) awaiting one of its two 1B503 scenes, both entered at 4EA35;
 * fall_after_scene continues. Nonzero = a fall's scene is pending (game_scene.c, drive()). */
enum fall_stage {
    FALL_STAGE_NONE = 0,
    FALL_STAGE_FIRST_SCENE = 1, /* 173CA: its 1B503(1); the return (173D7) sets 8C85 = 2 and runs the second */
    FALL_STAGE_SECOND_SCENE = 2 /* its 1B503(0); the return continues with the landing (fall_caller's tail) */
};
/* xeen_game.tavern_wait: the tavern's "Hit a key" wait (tavern_hold, the 26E60 loop as a wait), ended by tavern_held. */
enum tavern_wait {
    TAVERN_WAIT_NONE = 0,
    TAVERN_WAIT_TEXT = 1, /* window 10's text */
    TAVERN_WAIT_TIP = 2   /* window 12's tip or rumour, closed (18765(0Ch)) at the key */
};
/* xeen_game.tavern_food_step: the tavern's food (25AEC, tavern_food) and its 2F2FF(0, 1) yes/no. */
enum tavern_food_step {
    TAVERN_FOOD_NONE = 0,
    TAVERN_FOOD_ASKING = 1, /* the yes/no open over window 12 (its wait runs 26978(1): the only [BP+8] = 1) */
    TAVERN_FOOD_MESSAGE = 2 /* answered: a payment or "already full" message whose close does 25C76's windows */
};
/* xeen_game.treasure.window_owner: who framed window 10 for the treasure (its pixels are saved while nonzero;
 * 2F1A4's 18765(0Ah) restores them). */
enum treasure_window_owner {
    TREASURE_WINDOW_NONE = 0,
    TREASURE_WINDOW_DIALOG = 1, /* the 2EA68 dialog (present_treasure) */
    TREASURE_WINDOW_EARLY = 2   /* 2EBFB..2EC23 before the 2EC44 "BackPacks Full" scroll (treasure_full_window);
                                   the dialog then keeps it without drawing again */
};
/* xeen_game.event_scroll (operation 20's 18F2F lock-pick messages) and event_payment (operation 12's refused
 * take, 291FB(kind, 0, 0, 3)): the event message's state; the event service answers once it is closed. */
enum event_message {
    EVENT_MESSAGE_NONE = 0,
    EVENT_MESSAGE_OPEN = 1,
    EVENT_MESSAGE_CLOSED = 2
};
/* xeen_game.confirm_release: the yes/no's wait until no mouse button is held, still pending: at the open (before its
 * key loop, open_confirm) or after the answer (confirm_finish, before the close).
 * Original: 2937A's 189BC; the key loop 294A2's 189E3; the answer's 294DD. */
enum confirm_release {
    CONFIRM_RELEASE_NONE = 0,
    CONFIRM_RELEASE_OPEN = 1,
    CONFIRM_RELEASE_ANSWERED = 2
};
/* xeen_game.treasure.dispatch_full: the dispatcher's treasure dialog with every pack full: its "BackPacks Full!" scroll is
 * still to open (dispatch_full_open), or its key has closed it and the dispatcher goes on.
 * Original: 2EA68 at 2EC37; the scroll 2EC44; on at 2EC4C. */
enum dispatch_full {
    DISPATCH_FULL_NONE = 0,
    DISPATCH_FULL_TO_OPEN = 1,
    DISPATCH_FULL_CLOSED = 2
};
/* A window's saved pixels (panel_under's slot of a window 0..40): the screen under it from its first save on; whether
 * it has been saved at least once (set by every save, never cleared: a later restore copies the last pixels back);
 * whether it is open, saved and not yet restored (the event windows test it for window 10 and the bottom window).
 * Original: 1871B's 4F2A1 (save), 18765's 4F57B (restore). */
typedef struct {
    uint8_t pixels[XEEN_PRESENT_PIXELS];
    uint8_t saved, shown;
} window_save;
/* One of character creation's three bouncing dice (in XEEN_WINDOW_DICE): its position, its step per frame on each
 * axis and its sprite frame (0..6 of dice.vga). Original: 3FB10; the driver data 580E:0032..0046, five arrays there
 * (the dy and dx bytes, the frame bytes, the y and x words; outside DS, so not observable), set by 4064F. */
typedef struct {
    int16_t x, y;
    int8_t dx, dy;
    uint8_t frame;
} creation_die;
/*
 * The treasure dialog (2EA68: "The Party Found", the found items one line each, then the key that settles the
 * treasure): one dialog at a time, opened by a command owner (treasure_dialog: Shoot's or a spell's volley, a step's
 * join, the combat cleanup) or by the event dispatcher (its own opening, treasure_dialog_head), and settled by its key
 * (treasure_key, or the event service's treasure_settle). The rules it applies are the treasure library's
 * (xeen_treasure_*: what is pending, who receives an item, the settlement); this is the dialog's own progress.
 * Lifetime: the session's (zero at creation); each field is set by the stage that needs it and cleared by the stage
 * that ends it, as noted, so nothing here is reset as a whole. Original: 2EA68 and its callers.
 */
typedef struct {
    /* Who opened the dialog (enum treasure_owner; 0 none): set by treasure_dialog and again once its modal is open,
     * cleared by treasure_key. */
    int owner;
    /* The command owners' opening is running the event service's dialog (treasure_dialog_open; else a dispatcher's
     * request). Original: the 2EA68 prefix. */
    int staged;
    /* The dispatcher's opening asked for its scene pass; cleared when the scene has completed. Original: 2EBB3. */
    int head;
    /* The dispatcher's dialog with every pack full (enum dispatch_full): its "BackPacks Full!" scroll still to open,
     * or closed by its key. */
    int dispatch_full;
    /* The dispatcher's dialog stopped to show its item lines (item_next): drive() runs them. */
    int dispatch_items;
    /* Who framed window 10 for the dialog (enum treasure_window_owner): its pixels are saved while nonzero and restored
     * when the dialog settles. Original: 2F1A4's 18765(0Ah). */
    int window_owner;
    /* How many items the last settlement placed with a member: one "item found" sound each when its key is answered
     * (0 after the item lines' own dialog). Original: 2EEA9's recipient passes in 2EA68. */
    unsigned placed;
    /* The item lines: whether they are shown before the key (every item then placed by them); the pending slot cursor
     * (0..39, xeen_treasure_place_next's), the last item's recipient and its category; the wait after a line
     * (item_stage, enum item_line_stage: none, the one-tick wait after an item's blank lines, or the ten-tick wait
     * after its "found" line), its tick target, its scene pass and its timer state. Original: 2EC54..2F135, 30E05(1)
     * and 30E05(10), 4EA1C. */
    int items_staged;
    unsigned item_cursor, item_member, item_category;
    int item_stage, item_wait, item_scene;
    xeen_timer_elapsed_state item_elapsed;
    /* The item waits' passes over the session and the last pass's timer value, for the headless log
     * (xeen_game_view's item_passes / item_ticks). */
    unsigned long item_passes;
    uint16_t item_ticks;
} treasure_dialog_state;
/* xeen_game.confirm_owner: who opened the yes/no that is showing, continued with its answer (confirm_finish; the
 * owners from 2 on through the control panel's panel_confirmed). 8..10 are not used.
 * Original: the yes/no 2937A; its callers Rest 28E9F, Save over 2B2D7, Quit 29B46, Mr Wizard 29B72, the inn 442BE,
 * Buy 3C2E5, the guild 3F604, the save pair 184DE. */
enum confirm_owner {
    CONFIRM_NONE = 0,
    CONFIRM_REST = 1,              /* Rest's question */
    CONFIRM_SAVE_OVER = 2,         /* the control panel's Save over an existing game */
    CONFIRM_QUIT = 3,              /* Quit */
    CONFIRM_WIZARD = 4,            /* Mr Wizard */
    CONFIRM_NEWGAME_SAVE_OVER = 5, /* Start a New Game's "Ok to save over %s?" */
    CONFIRM_INN_DELETE = 6,        /* the inn's Delete */
    CONFIRM_BUY = 7,               /* a shop's Buy */
    CONFIRM_GUILD = 11,            /* a guild's question */
    CONFIRM_PAIR = 12              /* the save pair's Delete / Exit to DOS */
};
/* xeen_game.speech_wait: the stage of an NPC's speech loop while the text is off (poll_speech): each pass draws the
 * scene, then the face with its mouth, then polls; the timer after the poll decides what comes next.
 * Original: 3000D's loop with F384 set, 30442..304EF. */
enum speech_wait {
    SPEECH_NONE = 0,
    SPEECH_PASS = 1, /* a pass begins: its scene is requested (Original: 30442, 3040C's first 4EA0D, 1B503(0)) */
    SPEECH_FACE = 2, /* the scene runs; on its return the face is drawn (the CD status moves its mouth) and shown
                      * (Original: 47F3D, 50BF6(28h, 3)) */
    SPEECH_POLL = 3, /* the key poll (Original: 304AD's 189E3) */
    SPEECH_TICK = 4  /* the timer after it: a key ends the wait, a tick or more starts the next pass, less polls
                      * again (Original: 4EA1C) */
};
/* xeen_game.exit_stage: the game-over and exit sequence: the death's flames and laugh, the book, then the teardown
 * to the main menu or the hand-off to XEEN.EXE. The inspection view exports it as game_over_stage.
 * Original: 17F28 -> 1596F -> 15647 -> 1624F -> 1660E, or the 1617E hand-off. */
enum exit_stage {
    EXIT_NONE = 0,
    EXIT_FLAMES = 1,     /* the death's flames (Original: 1596F) */
    EXIT_LAUGH = 2,      /* its laugh loop */
    EXIT_BOOK = 3,       /* the book (Original: 15647) */
    EXIT_AT_MENU = 4,    /* stopped where the main menu begins, not yet begun (after the teardown 1624F, or at
                          * program start: Original 1660E) */
    EXIT_FINISHED = 5,   /* the main menu begun (xeen_game_resume_menu), or the XEEN.EXE hand-off reached (123A1) */
    EXIT_VOICE_WAIT = 6  /* a teardown's wait for the voice to end: before the hand-off (161DB) or the menu (1627C),
                          * Original: 187E9(1, 0) */
};
/* xeen_game.name_owner: whose text the line entry (name_key, name_text) is reading. Original: 28876. */
enum name_owner {
    NAME_GAME = 0,      /* the game's name: 20 characters (Original: A827, heading 4772) */
    NAME_CHARACTER = 1, /* a new character's name: 10 characters (create_name; Original: 3FF1C, heading 7418) */
    NAME_AMOUNT = 2,    /* an amount: digits (amount_key; Original: 28ABA) */
    NAME_WORD = 3       /* operation 21's word: 30 characters under the script's title line (Original: 2E8E5, AC0F) */
};
/* xeen_game.amount_owner: who takes an amount the line entry read (amount_key). Original: 28ABA. */
enum amount_owner {
    AMOUNT_BANK = 0,        /* the bank's deposit or withdrawal (bank_amount) */
    AMOUNT_TELEPORT = 1,    /* Teleport's distance (teleport_amount) */
    AMOUNT_TOWN_PORTAL = 2, /* Town Portal's town (portal_amount) */
    AMOUNT_ARENA = 3        /* the arena's choice (arena_amount; Original: 360C4) */
};
/* xeen_game.unlock_stage: a lock-picking attempt waiting for its continuation: the Who-will prompt's key
 * (unlock_key), the attempt's scene (unlock_after_scene), or a trap's party-damage scene, after which the attempt
 * rolls again (unlock_roll).
 * Original: 17249 / 1715A; the prompt 2F9BA (its poll 2FB8D); the scene 1B503(1) entered at 4EA35 and returning to
 * 17250; the trap's 2E68D, whose RETF re-enters 1715A. */
enum unlock_stage {
    UNLOCK_NONE = 0,
    UNLOCK_WHO = 1,
    UNLOCK_SCENE = 2,
    UNLOCK_TRAP = 3
};
/* xeen_game.unlock_text: the unlock prompt's title, "Open Grate" or "Open Door". Original: 170D4's index into the
 * titles at DS:29AC (2FA2C). */
enum unlock_title {
    UNLOCK_TITLE_GRATE = 0,
    UNLOCK_TITLE_DOOR = 1
};
/* xeen_game.event_origin: what started the running event, whose return event_return continues. Original: 310B4. */
enum event_origin {
    EVENT_ORIGIN_NONE = 0,
    EVENT_ORIGIN_SPACE = 1,   /* Space (also a lock's cell opened at once; Original: 17B8F, 171B9) */
    EVENT_ORIGIN_STEP = 2,    /* the step's automatic cell event (17AE5) */
    EVENT_ORIGIN_CONTACT = 3, /* the scene's event, deferred while a contact was pending (1C18A, 9756) */
    EVENT_ORIGIN_CLEANUP = 4  /* the combat cleanup's event, for a fight entered with a sign up (39577) */
};
/* xeen_game.location_message: the message a location shows (location_message), continued by
 * location_message_closed. From LOCATION_MESSAGE_UNPAID on it is the "not enough" message with its own routing
 * push. Original: 18F2F; the "not enough" message 291FB. */
enum location_message {
    LOCATION_MESSAGE_NONE = 0,
    LOCATION_MESSAGE_PLAIN = 1,            /* a member who cannot act (28CFC, A43F) or the tavern's text */
    LOCATION_MESSAGE_UNPAID = 2,           /* a service the party cannot pay for (town_pay) */
    LOCATION_MESSAGE_TRAINING_UNPAID = 3,  /* the same at training, whose voice follows (255D8) */
    LOCATION_MESSAGE_BANK_EMPTY = 4,       /* the bank before the amount: nothing to move */
    LOCATION_MESSAGE_BANK_SHORT = 5,       /* the bank after the amount: too little (bank_redraw) */
    LOCATION_MESSAGE_EVENT_PAYMENT = 6     /* a script's payment refusal under the location's loop (18FC8) */
};
/* xeen_game.modal_poll_pending: the step of a modal wait's pass. 0 and 1 are shared by both owners: poll_modal (the
 * key poll, then its timer ticks) and poll_dialogue (an NPC's dialogue or the Who-will prompt with the view's window
 * open); 2..4 are poll_dialogue's own. Every owner change resets it to MODAL_POLL_START.
 * Original: the poll 189E3, the ticks 4EA1C, the frame mark 4EA0D; the dialogue 304CF, the prompt 2F9BA. */
enum modal_poll_step {
    MODAL_POLL_START = 0,           /* the pass begins: poll_modal's key poll, poll_dialogue's frame mark */
    MODAL_POLL_TICKS = 1,           /* the timer ticks: after poll_modal's poll; the dialogue's three before the face */
    DIALOGUE_POLL_SAMPLE = 2,       /* the dialogue's frame mark after its portrait */
    DIALOGUE_POLL_KEY = 3,          /* poll_dialogue's key poll and its key (dialogue_key) */
    DIALOGUE_POLL_WHO_PORTRAIT = 4  /* a Who-will pass's portrait at once (no ticks first) */
};
/* xeen_game.desert_caller: 17264's caller for owner 16 (desert_scroll), whose own tail runs after 172D5: the
 * movement, Shoot, Space and Rest's wake. */
enum {
    DESERT_MOVEMENT,
    DESERT_SHOOT,
    DESERT_SPACE,
    DESERT_REST_WAKE
};
/*
 * The location kinds: the byte at +6 of an op 17 location record, the index of 277E5's CS:2D62
 * handler table and of its per-kind DS tables (3018, 3026, 1880, 2FFC). 0..5 are the town
 * services, each with its own icons, buttons and voices; 7 has no handler (27D6E); 9..13 are the
 * special encounters (both gatekeeper kinds run 35992).
 */
enum location_kind {
    LOCATION_BANK = 0,
    LOCATION_BLACKSMITH = 1,
    LOCATION_GUILD = 2,
    LOCATION_TAVERN = 3,
    LOCATION_TEMPLE = 4,
    LOCATION_TRAINING = 5,
    LOCATION_ARENA = 6,
    LOCATION_EVENT_FACE = 7, /* no handler (27D6E): 18FC8's location loop under an event face (8D34 is
                                XEEN_ANIMATION_FACE) */
    LOCATION_REAPER = 8,
    LOCATION_GUARDIANS = 9,
    LOCATION_GATEKEEPER = 10,
    LOCATION_SPHINX = 11,
    LOCATION_PYRAMID = 12,
    LOCATION_GATEKEEPER_2 = 13
};
/*
 * 26DAE's Clouds face-cycle restarts: on Clouds (DS:94 clear) a cycle of 20h frames that wraps to 0
 * goes on at frame 11h, one of 1Ah frames at 14h (both world's counts are DS:3026; on Clouds only
 * the guild has 20h and only the temple 1Ah; the Darkside's 1Ah bank and temple wrap to 0). The test
 * is on the count 8D36, not on the kind.
 */
#define CLOUDS_GUILD_FRAMES   0x20u
#define CLOUDS_GUILD_RESTART  0x11u
#define CLOUDS_TEMPLE_FRAMES  0x1au
#define CLOUDS_TEMPLE_RESTART 0x14u

/* What the view (44AE6), Quests (3E24F) and the awards (415E5) save before they read their text file and restore
 * at their end: the text block far pointer (XEEN_DS_TEXT_BLOCK, _SEGMENT) and the file buffer's segment (F42C).
 * 2B6C9 reads the file into the file buffer when there is one (2B6D5: the text block = F42C:0), else into a block
 * it allocates (the native keeps no DOS memory: segment 0); the block's offset is 0 either way. */
typedef struct {
    uint16_t segment, block, file_buffer;
} text_block_saved;

struct xeen_game {
    xeen_movement movement;
    xeen_timer timer;
    xeen_timer_registers timer_registers;
    xeen_event_dispatch dispatcher;
    xeen_game_command_status command_status;
    uint32_t lifecycle_boundary;
    /* The session's lifecycle and preparation (game_startup.c, game_lifecycle.c, game_resources.c):
     * lifecycle: the original's lifecycle runs: events go through the dispatcher (not the bounded interpreter) and
     *   commands stop at the original's boundaries; set by the party's restore, a menu or loaded start and
     *   xeen_game_enable_lifecycle.
     * acted: the session has taken a command or begun an event; the preparation calls refuse it from then on.
     * restored: the party is in DS (the restore, a menu, loaded or saved start); most entry points require it.
     * objects_prepared, monsters_prepared, scene_resources_prepared and map_sprites_prepared (below): the map's
     *   preparation stages done, each once and in that order; a map transition prepares the next session's anew
     *   and copies the flags back.
     * native_audio_enabled: the session plays through its native audio sink. */
    int lifecycle, acted, restored, objects_prepared;
    int native_audio_enabled;
    int monsters_prepared, scene_resources_prepared;
    uint16_t scene_selected_group;   /* the attacking group the scene's shuffle chose (0..3), kept from the shuffle
                                      * across the frame's presentation and the terrain to the group's restoration.
                                      * Original: SI across 4E33F and 22034 */
    xeen_presentation *presentation; /* Borrowed persistent native surfaces. */
    xeen_draw_list draw_list;        /* Scratch for renderer call 2469A. */
    xeen_minimap_draws minimap;      /* Scratch for minimap 1E467 draws. */
    uint8_t monster_database[15360];
    size_t monster_database_size;
    xeen_native_audio audio;
    uint8_t *kept_song;
    size_t kept_song_size; /* the song file's bytes (XEEN_DS_SONG_SEGMENT's), for a sink attached later */
    xeen_archive state, assets;
    uint8_t *state_owned;
    size_t state_owned_size;
    xeen_archive other;
    int other_bound; /* other world's CC, borrowed */
    xeen_archive intro;
    int intro_bound;  /* intro.cc, borrowed (Rest dream) */
    /* program_data: DS 0000..8C4B holds the original initialized image: installed once by
     * xeen_game_load_program_data, or carried with the DS (xeen_game_create_loaded's seed is a menu session's DS, a
     * transition clones the session). Every DS-backed session operation (the party restoration, the world entry, the
     * map preparations, the session hand-off) refuses a session without it; a xeen_game_create session stays DS-less
     * (synthetic) until its caller installs the data. Never serialized. */
    int program_data;
    xeen_sprite_cache sprites;
    /* world_entered: the sprite cache is live (the start-up's resident handles, or the world entry's sprites).
     * map_sprites_prepared: the map's sprites are loaded (the last preparation stage, above). */
    int world_entered, map_sprites_prepared;
    /* The music requests the session recorded for the driver (record_music): how many, the last song's name and its
     * flag; a map transition carries them back. */
    unsigned long music_requests;
    char music_name[16];
    uint16_t music_flag;
    int port_error; /* a palette write the presentation refused during the frame wait's sample (palette_port) */
    /* Evidence counters the session view reports: frame_waits, the scene frame waits sampled; scene_returns, the
     * scenes and presents the session completed. */
    unsigned long frame_waits, scene_returns;
    /* continuous: the session runs the original's play loop (a menu start, or the host's continuous start): its
     *   commands, waits and scenes continue one after another; entry_complete: its world entry has finished;
     *   world_entry_stage: the entry's own stage, kept for xeen_world_entry_finish.
     * service_boundary: the stop a service (the monsters' update, a delivery) reported, for its caller to stop at.
     * frame_elapsed: the scene frame wait's clock read; scene_argument: the argument of the scene the host runs next
     *   (the original's 1B503(0 / 1)). */
    int continuous, entry_complete;
    unsigned world_entry_stage;
    uint32_t service_boundary;
    xeen_timer_elapsed_state frame_elapsed;
    uint16_t scene_argument;
    xeen_input_poll input_poll;
    xeen_input_provider input_provider;
    xeen_timer_elapsed_state idle_elapsed;
    unsigned long accepted_commands, idle_scenes;
    xeen_event_index index;
    xeen_event_resources resources;
    xeen_event_execution execution;
    xeen_event_ui ui;
    xeen_text_window text_window;
    xeen_dialogue_window dialogue_window;
    /* dialogue_waiting: an NPC dialogue page (3000D's speech form) is shown and waits for its key, so the session's
     * wait is the dialogue's poll (poll_dialogue); set when a page of mode 0 or 1 has been drawn, cleared by a page
     * of mode 2 (which waits for nothing), by the next page and by the dialogue's end. Without it, poll_dialogue
     * runs as the Who-will prompt's (its faces).
     * dialogue_first: the sprite handle of the NPC's face the dialogue's portrait animates (drawn at (23, 22) in
     * the frame XEEN_DS_ANIMATION_FRAME); set when the face is drawn, 0 when the event frees the kept face. */
    int dialogue_waiting;
    uint16_t dialogue_first;
    /* modal_scene_ready: the open modal wait (the yes/no or choice, a scroll, a page or "Nothing here." wait, the
     * Who-will prompt, the treasure's key loop) has had its scene pass, so poll_modal goes straight to the key poll;
     * 0: poll_modal first runs the pass (combat's scene for a combat owner, the kept face's animation, else the
     * scene request). Set by a returning scene (frame_wait_next_wait, except for a wait this return's own event
     * just opened), combat's modal scene and the face scene; cleared when a wait opens or closes, when a key was
     * routed (route_modal_key) and when the pass's ticks ran out without a key (the next pass draws again).
     * modal_poll_pending: the step the current modal or dialogue pass resumes at (enum modal_poll_step); START
     * whenever a wait opens, closes or a pass ends.
     * event_origin: what started the running event (enum event_origin), so that its return (event_return) goes on
     * where it began; NONE when no event runs or its return was taken. Read also by the outdoor edge transition
     * (refused while an event runs) and by dispatch_caller_return (an event with an origin in a continuous session
     * returns through event_return, not through the exploration caller's table). */
    int modal_scene_ready, modal_poll_pending, event_origin;
    /* The Who-will prompt polls the keyboard again without redrawing its faces until one tick has passed without a
     * key; then the next pass draws them. Original: 2F9BA's 2FB8D..2FBA0, below SI (1) ticks: 189E3 again without the
     * face form (no 47F3D, 504DA, 4AC92 or 50BF6) until the next 2FAE3 pass. */
    int who_repoll;
    /* A map transition reached through the surface's effect from Shoot's tail (an outdoor Jump past the map's
     * edge): the command tail continues after it. Original: 17334 reached by 17264 from 17EF4, on at 17F07. */
    uint8_t transition_tail;
    int scroll_owner;    /* the open scroll message's owner (enum scroll_owner): 0 none */
    int desert_caller;   /* for a desert's "needs rest" (owner 16): whose tail continues after it (DESERT_*).
                          * Original: 17264's caller, after 172D5 */
    uint8_t scroll_mode; /* the UI mode saved while a scroll shows, restored when it closes. Original: 9A1C at 18F43 */
    int confirm_owner;   /* the open yes/no's owner (enum confirm_owner): 0 none */
    int confirm_wide;    /* the yes/no uses the wide window 22 (2937A's argument nonzero) */
    int confirm_info;    /* an information box: no buttons, and any key answers "no" (the argument's 80h bit) */
    int confirm_release; /* enum confirm_release: whether the mouse button's release is still awaited (189BC) */
    int confirm_answer;  /* the answer once given: 1 yes, 0 no. Original: 2937A's AX */
    unsigned long confirm_ignored;  /* keys that were not an answer, handed back to the poll (294D4) */
    uint8_t confirm_faces;          /* the faces flag (98D6) to restore when the box closes. Original: saved at 29490 */
    uint16_t confirm_handle;        /* the confirm.icn sprites' handle. Original: 2937A's DI */
    xeen_text_box confirm_box;      /* the pixels under window 21, restored when the box closes */
    xeen_gameplay_tick scroll_tick; /* the tick a "needs rest" scroll interrupted, resumed when it closes
                                     * (Original: suspended at 30DB3) */
    int rest_stage; /* Rest waiting for a scene pass: 0 none, 1 a step's, 2 the last one. Original: 28E35's 1B503(1) */
    int surface_stage; /* the surface's effect waiting for a scene pass: 1 its damage scene, 2 the scene after the party
                        * fell (F60A). Original: 17264's 1B503(1) at 17344 and 17380 */
    int surface_owner; /* who asked for the surface's effect (enum surface_owner) */
    int fall_stage;    /* a fall waiting for a scene pass (enum fall_stage). Original: 173A0's 1B503 */
    uint8_t fall_caller;                      /* the stopped fall's caller (xeen_movement's): 0 or an enum surface_owner */
    uint16_t fall_damage; /* the stopped fall's damage, taken from the movement with its caller (0: none, no damage
                           * scene after the landing) */
    unsigned long fall_frames;                /* frames the fall has presented. Original: 1BF78's 50BF6(3) */
    uint8_t fall_screen[XEEN_PRESENT_PIXELS]; /* the page shown before the fall (Original: 1C0E5's 186B2(8C90)) */
    int exit_stage; /* enum exit_stage */
    /* The exit sequence's counters (game_exit.c), each stage's own:
     * exit_step: the flames' step (2, 4 .. 34: the fire's frame and how far the death picture has risen); the laugh's
     *   (its face frame + 1; it restarts for a repetition); the book's page (from 22 down to 0 backwards, from 0 up
     *   to 22 forwards).
     * exit_di: the laugh's repetitions done; the book's hands' position (0..7: down from 7 backwards, up forwards).
     * exit_waiting: this frame is drawn and its clock marked; the next calls wait for its tick.
     * Original: the loops' SI (the step) and DI; 4EA0D. */
    int exit_step, exit_di, exit_waiting;
    uint16_t exit_sprites[20]; /* the flames' six handles, or the book's hand00..15 and marb01..04 */
    xeen_timer_elapsed_state exit_elapsed;
    /* Evidence the session view reports as the game-over's: the frames presented (exit_frames), the sprite draws the
     * presentation refused (exit_skipped; the cutscenes' and the monster information's draws count too), and the
     * session's first 12 evidence marks (exit_events, exit_event_count; exit_event, also called by the locations,
     * the cutscenes, the voices and the panel; never reset). */
    unsigned exit_frames, exit_skipped, exit_event_count;
    char exit_events[12][16];
    uint32_t surface_damage;       /* the damage the party cell's surface deals: 100 at 17264, or a fall's (the
                                    * signed byte 8C81, widened). Original: 17264's SI */
    unsigned rest_step;            /* which of Rest's ten steps runs (each charges time and may wake the party).
                                    * Original: 28EF4's SI */
    uint8_t rest_mode;             /* the UI mode (9A1C) to restore after Rest. Original: saved at 28EE4 */
    uint8_t shelter, shelter_mode; /* Rest inside Super Shelter (3AF2B), and the UI mode it saved */
    unsigned rest_food; /* the food Rest ate, for its text: one per member not dead-like while the party's food lasts */
    int rest_starving;                         /* the party woke starving. Original: 2907C's DI and [BP-4] */
    xeen_timer_elapsed_state rest_elapsed;     /* the dream's wait on the frame timer */
    uint8_t dream_screen[XEEN_PRESENT_PIXELS]; /* the screen under the dream, restored after it (186B2 of page 8C90) */
    /* The member last chosen at a Who-will prompt, plus 1 (0: none since the program started; a prompt answered with
     * no member leaves it): the member a treasure's items go to first. Original: 57C9:0008 (outside DS), written at
     * 2FC12, read by 2ED15. */
    uint8_t who_will_member;
    /* The event text buffer (XEEN_DS_TEXT_BUFFER) as it stood when the treasure dialog opened. The dialog's native
     * window staging writes that buffer, so the text is put back after the opening, after a key that is not an
     * answer and before the settlement's item lines. */
    uint8_t event_text[1000];
    int event_scene; /* an event service's full scene pass (EVENT_SCENE_*: idle, requested, running, completed) */
    xeen_modal modal;
    int yesno; /* the open event modal is the yes/no (2F2FF's argument 0), not its any-key form */
    /* choice: the open numeric choice's count (2F5B1(choice)); choice_x and choice_saved: the button table entry it
     * cut short to offer that many keys (the 197C x word and the bytes at 19A6/19BA/19CE/19E2/19F6), restored when
     * the choice closes. Original: 2F5B1's SI */
    unsigned choice;
    uint16_t choice_x;
    uint8_t choice_saved[5];
    /* the yes/no's wait animates a kept face (8D56 set) every 3 ticks. Original: 2F2FF runs 26978, DI = 3 */
    int modal_face;
    /* event_modal_open: an event's modal is open (the yes/no or any-key wait 2F2FF, or operation 48's numeric choice
     * 2F5B1); its kept face animates in the modal's scene pass. modal_fresh: it has not run its first step (2F517)
     * yet, so the scene that just returned was the operation's, not the modal's. */
    int event_modal_open, modal_fresh;
    /* Window 12 (the message box at the bottom): the screen rows 143..198 it covers while open, restored when it
     * closes. Original: 1871B/18765(0Ch). */
    uint8_t bottom_saved[320u * 56u];
    int bottom_active;
    /* The paged event text: page_stage the paging services' own stage (PAGE_*: writing, or at the key wait between
     * two pages), page_wait and nothing_wait what the polls read (a page's key wait; the "Nothing here." key loop),
     * page_next where the next page starts in the text. Original: 316DC; the key wait 31786/31793. */
    int page_stage;
    int page_wait, nothing_wait;
    size_t page_next;
    /* The text layout carried from one text draw to the next (the original keeps one write state for every text
     * call); layout_ready once initialised. Original: 4E38C's state. */
    xeen_text_layout layout;
    int layout_ready;
    /* One suspended event (the original's stack frame of an outer dispatcher): its "Nothing here." key loop ran the
     * scene pass whose contact started a fight, and that scene's own event then runs nested. Pushed at the contact
     * (nest_push), armed when that scene's tail reaches its frame wait (after the fight or the nested event), popped
     * when the frame wait returns to the outer key loop's poll. One level only: a contact inside the nested
     * dispatcher's own wait is an explicit stop.
     * Original: 310B4 with 337D7's key loop and its 1B503(1): 33815 -> 1C17E 38F5F, then 1C18A's nested 310B4;
     * armed at 1C1B3 (1C18F); popped at 3381A (the poll 3381B); the depth-2 stop at 1C17E. */
    struct {
        int active, armed;
        xeen_event_dispatch dispatcher;
        xeen_event_execution execution;
        xeen_ui_request ui_active;
        uint8_t ui_pending;
        int event_origin, nothing_wait, page_wait;
    } nest;
    uint8_t hed[2048];
    size_t hed_size;
    int hed_valid; /* The loaded map's .hed (hed_cell) */
    /* Operation 50's large text display: the loaded aaze2NNN.txt (and its handle), where the shown line starts, the
     * map's event texts as they were before, and the display's stage. Original: 31633; the load 18931; 9C52/9C5C. */
    uint8_t *large;
    size_t large_size, large_start;
    uint16_t large_handle;
    struct {
        uint16_t size, offset, segment; /* the map's event texts (9C52, 9C5C:9C5E) while the large window shows its own */
    } large_saved;
    int large_stage;
    int who_wait; /* the Who-will prompt's key loop runs: its window (36) open, no yes/no modal. Original: 2F9BA's
                   * 2FAE3..2FBC5 */
    uint8_t who_saved[94u * 80u];
    unsigned npc_page;
    size_t npc_rest, npc_source; /* an NPC's speech: the page shown, the text left after it, the page's start in the
                                  * message buffer. Original: 3000D, 4E38C's rest, AC0F */
    /* npc_confirm: an NPC's speech (mode 0) loaded the yes/no buttons' sprites, held until the speech ends
     * (npc_confirm_end frees them); while set, the speech takes only Esc, N and Y, and any other key starts its
     * wait again from a first pass. Original: 3000D mode 0, confirm.icn by 190A3, freed by 30544's 190CF. */
    int npc_confirm;
    /* The speech layout's wait (an NPC speaking with the speech window): speech_wait, the stage of its loop (enum
     * speech_wait: the scene pass, the face and windows, the key poll, the timer); speech_first, the next pass is the
     * first since the speech (or a restart) began, which marks the frame clock first. face_saved: the pixels under
     * window 40 (226, 0, 93 x 100). Original: 3000D with F384 set; the 30442 loop, 3040C's own 4EA0D. */
    int speech_wait, speech_first;
    uint8_t face_saved[93u * 100u];
    int face_active;
    /* A portrait flash (an event give's, or a healing spell's): the event's flash running (flash_stage), the next of
     * its four frames (flash_frame), and the monster countdown, which the flash holds at 0 and restores at its end.
     * Original: 2B9C7; 96C0. */
    int flash_stage;
    unsigned flash_frame;
    uint8_t flash_countdown;
    uint8_t modal_poll_ds[65536];
    unsigned long modal_polls;
    uint16_t modal_poll_key; /* evidence: each modal 189E3 entry, its key */
    xeen_entity_random_fn event_random;
    void *event_random_user;
    int (*event_random_carry)(void *);
    xeen_combat combat;
    xeen_projectile_delivery projectile;
    /* The delivery of the monsters' queued ranged attacks after a volley (game.c): the next of the 36 shooter slots
     * (XEEN_DS_MONSTER_SHOOTERS; FFh empty) to act; 0 when a delivery begins. */
    unsigned ranged_queue_index;
    xeen_bash_result bash;
    /* How a fight returns to exploration (game_combat_flow.c):
     * shoot_after_combat: the fight was begun by Shoot (the party's cell held monsters): its cleanup runs Shoot's
     *   tail (shoot_tail), except a nested fight's cleanup inside a live outer fight, which leaves it to the outer
     *   one; cleared when the fight could not begin and when a scene contact starts a fight (its tail is the frame
     *   wait's).
     * cleanup_event: the cleanup's event for the party's cell (a pending cell step event) is running; when the host
     *   resumes after it, the fight's tail goes on (back to the contact scene's frame wait).
     * Original: 38F5F at 17EEA / 17EEF; 39577's 310B4 for 3066, then 3957C. */
    int shoot_after_combat;
    int cleanup_event;
    /* While combat waits for a command, the monsters' idle scenes run on the frame clock: combat_idle_armed, the
     * clock is marked for the next one; combat_idle_count, the idle scenes since the loop's top (each passes the
     * incremented count to xeen_combat_idle); combat_idle_top, the loop top they count from (combat.loop_tops: a new
     * top sets the count to 1 and disarms the mark, so its first idle scene passes 2). combat_elapsed: that clock's
     * read. Original: 3912C's wait, 390D2's 4EA0D, its 4EA1C, SI. */
    int combat_idle_armed;
    unsigned combat_idle_count;
    unsigned long combat_idle_top;
    xeen_timer_elapsed_state combat_elapsed;
    unsigned long shots; /* Shoot volleys completed (shoot_after_volley); the session view reports them */
    xeen_timer_elapsed_state modal_elapsed; /* the modal poll's frame clock read (game_modal_poll.c's waits) */
    uint8_t backing[65535], resident[65535], texts[65535];
    uint8_t world;
    uint16_t map_id;
    xeen_game_phase phase;
    xeen_slice_navigation_result navigation;
    xeen_exec_status event_status;
    xeen_ui_present_fn ui_present;
    void *present_user;
    uint16_t response;
    int response_ready;
    int transition_active;
    uint16_t transition_target;
    /* The 1660E main menu and its Load Saved Game path. */
    const xeen_save_files *save_files; /* borrowed host service */
    xeen_addons
            *addons; /* borrowed host add-on state (owner-approved, not original); NULL = none */
    int menu_stage;  /* MENU_* below; 0 when the menu is not running */
    int menu_first;  /* the save-pair check (18363) has run in this call of the menu. Original: 1660E's [BP-6] */
    uint16_t menu_special;    /* the special.icn sprites' handle. Original: 1660E's [BP-8] */
    uint8_t menu_world;       /* the world (DS 94) when the menu began. Original: 1660E's [BP-4] */
    unsigned long menu_polls; /* key polls answered by the menu owners */
    xeen_timer_elapsed_state menu_elapsed;
    int menu_redraw;                                     /* the selection changed: the slot list is redrawn.
                                                          * Original: 2AD6D's DI */
    /* The slot dialog (2AD6D), shared by the menu's Load, the panel's Save and Load, and Start a New Game (which
     * one: dialog_title): the selected slot (1..10, 0 none; clicking the selected slot again deselects it), the
     * faces flag (98D6) and the ten slots (dialog_slot). The panel's save message and the name entry read the
     * selection and its slot after the dialog. Original: 2AD6D's [BP-2]. */
    uint8_t menu_selected, menu_faces;
    dialog_slot save_slots[10];
    uint32_t menu_stop;      /* explicit boundary of an unsupported option */
    /* Each world's CUR image (XEEN.CUR, DARK.CUR), the session's own copy: the state archive a world is entered with,
     * and a save's source for the world not being played (cur_replace hands a new one over; xeen_game_take_session and
     * xeen_game_create_loaded move both). Original: 1978C's copies. */
    xeen_owned_bytes cur[2];
    /* session_ready: a game loaded from the menu has reached its map load (19E82) and waits for the host to take
     * it (xeen_game_take_session, which clears it with menu_stage); meanwhile xeen_game_cur gives the menu's CURs. */
    int session_ready;
    /* dialog_title: which slot dialog runs, by its title text: XEEN_TEXT_DIALOG_LOAD, _SAVE or _START (a selected
     * slot with Start begins a new game there instead of loading it). */
    uint16_t dialog_title;
    /* panel_stage: the stage of the modal dialog that owns the session's resume; 0 none. Despite its name it is
     * one number space for every dialog the session runs between key polls, by range: the control panel, its
     * save/load dialogs and the name entry (PANEL_*, from 1), the town locations and the guild (LOCATION_*,
     * GUILD_*, 20), the inn (INN_*, 30), character creation (CREATE_*, 40), the items dialog, the character view
     * and the quests (50), the Cast dialog and its spells' own dialogs (CAST_*, 60), the cutscenes (from CUT_BASE,
     * mirrored in cut_stage) and combat's modal commands (from COMBAT_UI_BASE). xeen_game_resume_panel (the host's
     * resume while it is set) and panel_after_scene (a scene pass's return) dispatch on it, each unit's resume
     * handing the stages it does not own to the next; while it is set the frame wait returns to
     * panel_after_scene before any other owner. Written by panel_set, panel_scene, cut_set and the owners' own
     * stage changes; cleared when the dialog ends and by panel_stop. */
    int panel_stage;
    /* The control panel (game_panel.c), from its open to panel_close:
     * panel_mode: which slot dialog the panel opened: 0 Save As, 1 Load (panel_dialog); a chosen slot then saves or
     *   loads by it (panel_dialog_key, panel_dialog_done).
     * panel_handle: the panel's sprites (cpanel.icn), freed from it at the close.
     * panel_loaded: the panel's Load copied a game in: the close then restarts the exploration loop with it instead of
     *   returning to the command (or to combat).
     * panel_speech: P toggled speech during this panel (even back): its release runs the CD side's check.
     * Original: 297D9's [BP-0Ah] (the handle), [BP-6] (AX = 1 after a Load), [BP-8] (the toggle); 2AD6D's argument. */
    int panel_mode;
    uint16_t panel_handle;
    int panel_loaded, panel_speech;
    /* The CD side's disc-swap requests the native counted instead of asking (cd_side_check, with speech on) and the
     * side asked last; the session views report them. Original: 19D3C's 47F7F, the CD platform's. */
    unsigned long cd_side_requests;
    uint8_t cd_side_world;
    int panel_debug[3];
    /* The panel's hidden G, O, B, R sequence, which turns on its debug time line (XEEN_DS_PANEL_DEBUG_LINE):
     * panel_debug, which of G, O and B have been typed; panel_debug_line, the line's flag kept aside while the Quit
     * or Wizard confirmation is open. Original: 297D9's DI, SI, [BP-2] and [BP-4] */
    uint8_t panel_debug_line;
    unsigned panel_name_length; /* the length of the name typed so far. Original: 2894F's SI */
    /* The driver's text cursor (CS:0BE4/0BE6 and its
     * modes), kept across menu_window_text calls like 4E38C's; the 28876
     * input line, its window and the cursor animation (CS:0C0B, CS:0C10). */
    xeen_text_cursor text_cursor;
    uint8_t text_cursor_valid, text_cursor_shown;
    unsigned text_cursor_frame;
    uint16_t input_window;
    char input_line[128];
    /* The line entry's loop (28876): the monsters' countdown and the sign flag it clears while the entry waits (put
     * back by the next key); panel_name_scene: each pass owes a scene (outside the menu, no view dialog, a UI mode
     * other than none and the location's); panel_sampled: this pass has taken its frame base already. */
    uint8_t panel_name_monster_countdown, panel_name_sign_shown;
    int panel_name_scene, panel_sampled;
    /* where a refused or finished save goes back to after its message (enum save_return). Original: 29502's return */
    enum save_return panel_return;
    /* Counters the session views and the replay host read: panel_sessions, the modal waits begun (each panel_set: a
     * dialog stopped for its next key); panel_polls, the keys those dialogs polled (panel_poll; the replay's idle
     * test); saves, the games saved (the panel's and the inn's). */
    unsigned long panel_sessions, panel_polls, saves;
    uint32_t panel_stop; /* the stop panel_stop recorded for a modal dialog (0 none); cleared when the panel or a
                          * town location opens */
    /* The frame clock's read of the dialog loop waiting on it: the town location's loop, character creation's and
     * the line entry's (each marks it when its wait begins). */
    xeen_timer_elapsed_state dialog_loop_elapsed;
    window_save window_saves[41]; /* the pixels under each window (panel_under) */
    uint8_t wait_under; /* the "Please Wait..." box saved the pixels under it (reload_wait_open). Original: 19E8A */
    /* Start a New Game (game_newgame.c). */
    int newgame_preference; /* the chosen preference, XEEN_PREFERENCE_ADVENTURER or _WARRIOR (stored in
                             * XEEN_DS_GAME_PREFERENCE). Original: 2AD6D's [BP-8] */
    /* A forward step into an opening door shows one scene before the step goes on:
     * door_pending: the movement asked for the door's scene (XEEN_DS_DOOR_OPENING was 1) during this step's
     *   tick; the navigation turns it into the scene, or drops it with the tick.
     * door_stage: that scene is running; the frame wait returns to door_after_scene, which clears the opening
     *   flag and finishes the step (door_command, the step's command) with the gate open.
     * Original: 178E2..178F7 (9C6E = 1, sound 2Fh, 1B503(1); 9C6E = 0 and AX = 1 after it). */
    int door_pending, door_stage;
    uint16_t door_command;
    uint16_t navigation_command; /* the last navigation; the strafes (B0/B1) still owe their tail after the tick
                                  * (Original: 17CCD after 28178) */
    int bash_stage;              /* the bash's two-tick scene is pending (Original: 3D894's 30E2D(2)) */
    /* damage_stage: the party damage's two-tick scene pending for a surface (the member selector, 98D7, saved
     * meanwhile in damage_member_selector and put back after it); damage_event: a script's party damage scene;
     * scene_site: where the next traced scene pass returns to (the trace's label).
     * Original: 2E68D's 30E2D(2) from 2E8B3, 98D7 saved by 17349; the traced 1B503's return. */
    int damage_stage, damage_event;
    uint8_t damage_member_selector;
    uint32_t scene_site;
    /* Space at an indoor door or grate: the unlock's stage (enum unlock_stage), the wall and title it opens, and the
     * member who tries. Original: 170D4; "Who will try?" 2F9BA (mode 0); the scene 17249; the trap's 2E68D. */
    int unlock_stage;
    uint16_t unlock_wall, unlock_text, unlock_member;
    int loaded_session;      /* created from a loaded game's world entry (xeen_game_create_loaded; Original: 1791F's 19E82) */
    unsigned long new_games; /* new games created (Original: 2B4AE..2B583 completed) */
    /* The town location being visited (game_location.c, location_begin to location_leave):
     * location_kind (enum location_kind): which one; 0 both before a visit and after it (location_leave,
     *   location_stop, a cutscene's end) and during a bank's (LOCATION_BANK is 0). An event's payment message sets
     *   LOCATION_EVENT_FACE while it runs the location loop under the event's face (the visit's kind kept in
     *   payment_loc_kind). Its "a location is open" tests only decide the evidence trace's lwtext / lwrestore lines,
     *   which a bank visit therefore does not get.
     * location_mode: the UI mode at the entry, put back at the leave.
     * Original: 277E5's DI (the kind) and [BP-4] (9A1C). */
    uint8_t location_kind, location_mode;
    /* The arena (game_arena.c): arena_stage, the question whose answer resumes it (0 none); arena_text and
     * arena_mode, the speech's DS text and its NPC dialogue mode; arena_level and arena_count, the fight's level and
     * its opponents' count as answered; arena_ids, the five opponent types arena_generate rolled (FFh before; the
     * map's monsters take them by slot). Original: 360C4; its 3000D; [BP-4] the level; the ids at 50D4:678C (DOS
     * memory). */
    int arena_stage;
    uint16_t arena_text, arena_mode, arena_level, arena_count;
    uint8_t arena_ids[16];
    /* The town location's customer: the DS offset of the party record (B41E + 162h * n) of the member F1..F6 chose
     * (location_member_key) or the items dialog returned (location_after_items); the training grounds' and the
     * temple's texts and services (game_town.c) act for it. Original: 25464's [BP+6] */
    uint16_t location_member;
    /* location_sampled: the location loop has marked its frame clock since its wait (re)started: the first poll
     * marks it; an animation step (from 3 ticks), a message, a service's return and the entry clear it, so the next
     * poll marks again. Original: 26E60's 4EA0D. */
    int location_sampled;
    /* location_blessed: the temple's lucky donation has blessed the party: the donation's price is 0 for the rest of
     * the visit, so D does nothing more (cleared when a location is entered); location_message (enum
     * location_message): the message the location's loop is showing and who continues it when a key closes it (0
     * none), location_message_mode the UI mode message_show replaced.
     * Original: 57AD:0000 (outside DS), cleared at 277E5; 18F2F mode 2 and its 9A1C. */
    uint8_t location_blessed, location_message, location_message_mode;
    /* The bank's open transaction (1 a deposit, 2 a withdrawal; 0 none: the source's enum funds_place + 1), what it
     * moves (enum funds_kind) and the amount typed so far. Original: 26E90's mode and DI, 28ABA's digits */
    uint8_t bank_mode, bank_kind;
    char bank_digits[12];
    /* The guild's spell list (3F05D): the member it lists for, the price of the chosen spell, the top line and the
     * line count, each line's spell index, the chosen line, and whether the list is open; guild_desc, the spell
     * descriptions (spldesc.bin; DOS memory at 50D4:1C7E in the original). Original: 3F05D's [SI], [BP-4], the
     * list at 580E:0047 */
    uint16_t guild_record, guild_cost;
    unsigned guild_top, guild_count;
    uint8_t guild_index[40], guild_choice;
    int guild_active;
    uint8_t *guild_desc;
    size_t guild_desc_size;
    /* guild_desc holds the location's text file: spldesc.bin in the guild, tavern.bin in the tavern.
     * tavern_wait (enum tavern_wait): the tavern's "hit a key" wait, which runs the location loop until a key
     *   (its text in window 10, or a tip or rumour in window 12).
     * tavern_food_step (enum tavern_food_step): the food purchase's yes/no and its answer message (the price and
     *   the amount are in DS, F65A and F624).
     * guild_info: the guild's list was opened with S (Spell Info) rather than B (Buy): it lists the spells the
     *   member already knows too, and the chosen spell shows its description instead of the purchase offer.
     * Original: 2B632; the 26E60 loop as a wait; 2F2FF(0, 1), 25C76; 3F05D's 80h bit (2616B). */
    int tavern_wait, tavern_food_step, guild_info;
    /* The spell list (3F05D), shared by the guild and the Cast dialog's New Spell:
     * list_caster_spells: set while the list shows the caster's spells, to ready one (cast_list_begin sets it, the
     *   list's close clears it); clear for the guild's spells for sale (and every other list owner); the window code
     *   reads it to keep the list's windows open.
     * list_choice: the highlighted line, -1 none (reset when a list opens and when the choice is toggled off);
     * list_colour: each line's text colour.
     * Original: 3F05D's [BP-46h] and DI. */
    int list_caster_spells, list_choice;
    uint8_t list_colour[40];
    /* A cast in progress: the Cast dialog (cast_open), the combat quick cast (quick_cast) or an item's power
     * through Use (use_power), until cast_restore or use_after_power.
     * cast_active: a cast is running; the scene pass, the timer resume and the views read it to route frames and
     *   to show the dialog.
     * cast_done: the chosen spell has been paid and its handler entered (cast_engage); cleared at entry and by a
     *   refund. It is the dialog's result: combat counts the turn as spent, exploration runs the paid spell's
     *   tick (charge_tail) instead of the plain command end.
     */
    int cast_active, cast_done;
    /* cast_index: the caster's slot: in combat the acting combatant's combat index, exploring the party slot (as
     *   unsigned: -1 when no member can cast, a preserved original quirk, see cast_open); the highlight shows it.
     * cast_spell: the spell being cast (cast_engage; Use sets its item's spell). */
    unsigned cast_index, cast_spell;
    uint16_t cast_record; /* the caster's party record (B41E + 162h * member) */
    /* cast_mode, cast_countdown: the UI mode (9A1C) and the monsters' countdown (96C0) when the cast began, restored
     *   at its end (cast_restore); cast_mode == XEEN_UI_MODE_COMBAT is how the handlers tell a combat cast.
     * cast_combat_call: the dialog was opened from combat's command poll (set there; cleared by exploration's C and
     *   when the dialog ends). It, not the UI mode, decides that the dialog returns to combat: the dead party's
     *   combat poll after a nested fight runs in the exploration mode.
     * combat_cell: the combat cell event (combat_cell_event) is running: its window restores are the fight's own.
     * Original: 3D8C1's [BP-2] (caster), [BP-0Ch], [BP-4] (9A1C), [BP-8] (96C0) and [BP-6] (paid); 391E5. */
    uint8_t cast_mode, cast_countdown, cast_combat_call, combat_cell;
    int cast_prefix;  /* the C command's scene pass before the dialog is running (Original: 17E30's 1B503(1), 3D8C1
                       * next) */
    int shoot_prefix; /* Shoot's scene pass before the volley is running (Original: 17EAD's 1B503(1), 17EB5 next) */
    treasure_dialog_state treasure; /* the treasure dialog (2EA68) */
    /* Evidence counters: the frame clock's ticks the location loop or the map view's blink wait read last, and how
     * many reads, which the session view reports (loc_ticks, loc_tick_reads). The loop's own decision is its
     * LOCATION_TICK stage's (an animation step from 3 ticks). Original: the 26E60 loop's 4EA1C, 1E35C. */
    unsigned long location_tick_reads;
    uint16_t location_ticks;
    /* The talking cutscenes (game_cutscene.c and the four game_cut_*.c units: the Reaper, the gatekeeper, the
     * Sphinx and the guards), from cut_enter to the end's map reload or return:
     * cut_stage: the resume point (CUT_* stages, game_cutscene.h; equal to panel_stage while the scene runs, 0
     *   after it ends or stops); cut_si: the frame loop's counter (the approach's scale, the slide's x, a talk's
     *   repetitions: each stage says which).
     * cut_kind: the encounter (enum location_kind); cut_key: the party holds the key or pass, so it may go on
     *   (the destination, voices and line follow it); cut_sampled: the subtitle clock has been marked once since
     *   cut_enter (cut_resume marks it before the first stage); cut_voice: the Reaper's first voice was loaded
     *   (its tower frame starts it).
     * cut_sprites: the encounter's sprite handles; cut_dest: where the party goes (map, x, y, facing; the
     *   current place when it stays); cut_len and cut_text: the spoken line's length and the subtitle's visible
     *   part (cut_subtitle). cut_elapsed: the tick stages' clock read.
     * cut_events, cut_polls, cut_ret, cut_value, cut_is_poll: the evidence log of the last clock read or key poll
     *   (its site, value and kind), read by the session view, not by the scenes.
     * Original: the loops' SI, DI / BP-E, BP-6, BP-4, BP-2, BP-0Ch and BP-16; 185E3's text. */
    int cut_stage, cut_si;
    uint8_t cut_kind, cut_key, cut_sampled, cut_voice;
    uint16_t cut_sprites[3], cut_dest[4], cut_len;
    xeen_timer_elapsed_state cut_elapsed;
    char cut_text[256];
    unsigned long cut_events, cut_polls;
    uint32_t cut_ret;
    uint16_t cut_value;
    uint8_t cut_is_poll;
    /* The spells' own dialogs and their continuations:
     * saved_ui_mode: the UI mode (9A1C) kept while "On Whom?" or a refusal message ("Not enough ...", "Can't cast
     *   ... while engaged!") is open, restored when it closes; whom_portrait_clicks: the portrait-click setting
     *   "On Whom?" replaces with 1 while it is open, restored with the mode.
     * whom_spell: the spell whose target "On Whom?" asks for (cast_whom); its handler runs on the chosen member.
     * flash_member, flash_slot: the member a healing spell heals (cast_heal) and the portrait slot its flash is
     *   drawn at (the combat slot in combat, else the member).
     * heal_tail: what follows the heal and its flash (cast_heal_done): 0 nothing but the spell's end, a cure's
     *   spell number (the condition it then clears; Raise Dead and Resurrect also cost endurance, Resurrect
     *   ages), or XEEN_SPELL_MOON_RAY (the next member of Moon Ray's party heal); whom_record: that member.
     * element_mode, element_value: "Which Element?" (Protection from Elements): the UI mode to restore and the
     *   protection the chosen element gets.
     * moon_next, moon_active: after its attack Moon Ray heals every member by 1..30, one heal (and flash) at a
     *   time: the next member, and whether that loop is running.
     * Original: 399FA, 39B7F, 39AF6 (9A1C); 3977A and 2B9C7; 3B409's condition table; 39817; 3B3E1 after 281A7. */
    uint8_t saved_ui_mode, whom_portrait_clicks;
    unsigned whom_spell;
    unsigned flash_member;
    unsigned heal_tail, whom_record, flash_slot;
    uint8_t element_mode;
    uint16_t element_value;
    unsigned moon_next;
    int moon_active;
    /* The number entry (28ABA): its window, the line's x, the digit limit and who asked (0 the bank, 1 Teleport
     * 3AC88, 2 Town Portal 3AF94). travel_world: Lloyd's Beacon's world (the side at entry, then the beacon's);
     * travel_mode: Town Portal's UI mode to restore. Original: 3A5DA's and 3AF94's [BP-2] */
    uint16_t amount_window, amount_width;
    unsigned amount_max;
    int amount_owner;
    uint8_t travel_world, travel_mode;
    int lloyd_tail;
    /* Detect Monster's own sprites handle. Original: 3A9C1's [BP-8] */
    uint16_t detect_handle;
    uint8_t *spells;
    size_t spells_size; /* spells.xen (archive 1), whose strings F868 names */
    /* completed trainings, temple cures and temple donations: counters the views and tests read (Original: 2549B,
     * 256F9/258B9, 257CF) */
    unsigned long trainings, healings, donations;
    /* The inn (game_inn.c):
     * roster: the 30 character records the inn holds (maze.chr; B41A in the original), owned by the session;
     * inn_list, inn_count: the roster indices the list shows (FFh: an empty line) and their count; inn_top: the list
     *   line at the page's first row; inn_visited: the inn reopens after a Create, so its opening skips the first
     *   visit's fades and (Clouds) the character book;
     * inn_select_mode: the open "which character?" prompt's action (0 Remove: portraits clickable, 1 Delete);
     * inn_select_portrait_clicks: the portrait-click flag (98D6) as it was before that prompt, put back at its end;
     * inn_delete_who: the member chosen for deletion, awaiting the yes/no;
     * book_from_inn: set while the book (15647(0, 0)) is the inn's opening on a first Clouds visit: it pages
     *   forward and returns to the inn (inn_after_book); clear for the exit sequence's book, which pages backwards.
     * Original: 43E0B's [BP-2Ch], [BP-7], DI and BP-6; the roster 19953's B41A; the prompt 43A49 / 43BA8. */
    uint8_t *roster;
    uint8_t inn_list[30], inn_count, inn_select_mode, inn_select_portrait_clicks, inn_delete_who;
    uint8_t dismiss_portrait_clicks; /* Dismiss's portrait clicks so far. Original: 2B75D's [BP-0Ah] */
    unsigned inn_top;
    int inn_visited, book_from_inn;
    /* Character creation (4063B): the rolled stats, which of the ten classes they allow, the chosen race, sex and
     * class (FFh: none yet), the highlighted stat, the roster's free slots with their count and the chosen one,
     * whether the first fade (4A74F) has run, the UI mode to restore (9A1C), the stat being exchanged (410B7),
     * the create.icn sprites' handle, the screen's text and the typed name. name_owner, below, says whose name the
     * name entry asks for (0 a game, 1 a character).
     * Original: 4063B's [BP-16h] stats, [BP-20h] classes, [BP-0Ah] race, [BP-0Bh] sex, [BP-9] class, [BP-8]
     * highlight, [BP-40h] free slots, [BP-0Ch] count, [BP-0Dh] choice, [BP-0Eh] fade, [BP-6] mode, SI icons,
     * [BP-1D0h] text; 410B7's DI; 2894F's name. */
    int8_t create_stats[7];
    uint8_t create_allowed[10], create_race, create_sex, create_class;
    int create_highlight;
    uint8_t create_free[30], create_count, create_slot, create_faded, create_mode, create_xchg;
    uint16_t create_icons;
    int create_sampled;
    char create_screen[0x1d0];
    char create_name[11];
    int name_owner;
    unsigned long characters_created;
    creation_die dice[3]; /* character creation's three bouncing dice, reset at every creation entry (create_begin) */
    char word[31]; /* Operation 21's 2894F word (2E8E5's BP-22h) */
    /* A model of the DOS memory block the original reaches through one far pointer (the native has no such block):
     * it holds what was last loaded there, the first f42c_known bytes being established. The program start loads
     * laff1.voc into it; a location's voices, operation 50's text and the mirror's table load over its start; freeing
     * it (the exit voices) leaves the pointer on the old block and nothing known. The mirror teleport reads its rows
     * here, and the mirror's word compare reads the row just past the table (offset 1984 on Clouds, 1152 on
     * Darkside), which holds the block's older bytes.
     * Original: B416:B418 = F42C:0; 18975(laff1.voc) at 16117; 16A53 frees it; 3196B; 2E9AC. */
    uint8_t f42c[0x10000];
    uint32_t f42c_known;
    /* The voice model's host clock service (xeen_game_set_clock_service:
     * a fresh INT 1Ah peek at each start and query; NULL: the last reading), the
     * VOC an 18975 loaded into F42C (the cutscenes' BP-0C) for a later 187E9
     * start, and which cutscene's end waits in 33D36 (cut_end: enum cut_end, game_cutscene.h). */
    xeen_timer_bios clock_bios;
    void *clock_user;
    uint8_t *voc_bytes;
    size_t voc_size;
    uint8_t voc_kept, cut_end;
    xeen_owned_bytes cut_voc[3]; /* the cutscenes' three loaded voices. Original: their BP-0C, BP-0A, BP-8 */
    /* The wait site of a location's farewell voice (the tavern's goodbye or the Darkside blacksmith's come1.voc),
     * set by location_leave and polled by location_leave_wait. Original: the 187E9(1, 0) loops (2800A, 2804F) */
    uint32_t location_wait_site;
    /* The items dialog (44F92), which the blacksmith (277E5, kind 1), the character view and combat open; the item
     * options (3BAEE); the character view (44AE6). mae: MAE.xen, the material names the F65C offsets point into
     * (DOS memory in the original). */
    uint8_t *mae;
    size_t mae_size;
    /* The items dialog (items_begin .. items_end):
     * items_owner (enum items_owner): who opened it, so its close returns there (0 while none is open);
     * items_mode (enum items_mode): what its options do; combat's Use opens it as ITEMS_OWN with items_combat set;
     * items_cat: the category shown (misc first for combat's Use and Recharge, else weapons); items_index: the
     *   selected row, -1 none;
     * items_stock: it opened in Buy mode, so its close copies the stock record back to the shop's wares;
     * items_char: the record listed: a member's, or in Buy mode the stock in the scratch record (BC6A);
     * items_start: the member it is for (a portrait choice moves it), returned when it closes;
     * items_equip: the equip.icn handle, freed at the close unless the map was reloaded meanwhile;
     * items_marks: each row's colour.
     * Original: 44F92's [bp+0Ah], [bp-0Ch], [bp-2], [bp-0Ah], [bp-0Eh], [bp+6], [bp-6], [bp-14h], [bp-3Eh]; the
     * owners 25699 and 44DC3; 3CFF2 the stock's copy. */
    int items_owner, items_mode, items_cat, items_index, items_combat, items_stock;
    uint16_t items_char, items_start, items_equip;
    uint8_t items_marks[40];
    /* The option's action and row, and which option's yes/no confirmation is open (enum option_owner).
     * Original: 3BAEE's SI and row */
    int option_action, option_owner, option_index;
    uint32_t option_cost; /* the option's price. Original: 3BAEE's [bp-8]/[bp-6] */
    /* The character view: the member and its record, the text block it saved (restored at its close), the UI mode
     * to restore, the text cursor and its blink, and view_owner, who opened it and so where its close returns
     * (enum view_owner).
     * Original: 44AE6's [bp+6], [bp-12h], [bp-0Ch]/[bp-0Eh] (97C2/97C0 and F42C) and [bp-6] (9A1C), [bp-2], SI; the
     * cursor at 50D4:1657; the owners 17DDE, 44547 and combat's 3933F. */
    unsigned view_member;
    uint16_t view_char;
    text_block_saved view_text;
    uint8_t view_mode, view_cursor;
    int view_blink, view_sampled;
    enum view_owner view_owner;
    xeen_timer_elapsed_state view_elapsed;
    /* message_owner: the owner of the open mode-0 message (enum message_owner). Original: 18F2F mode 0, or 422D8's
     * window-6 message. */
    int message_owner;
    uint8_t message_mode;
    /* The Quests viewer (game_quests.c), open from quests_begin to quests_end:
     * quests_owner: who opened it and so where Esc returns (enum quests_owner);
     * quests_mode: the list shown (enum quests_list; the add-on quest_journal reads the same numbers);
     * quests_top: the list line at the page's first row; quests_lines: the shown list's line count, headings
     *   included (the arrows and pages keep the top between 0 and the last line; End sets the last line, -1 for
     *   an empty list);
     * quests_opened: it opened the list windows 29 and 30 itself (they were closed), so it closes them;
     * quests_trace: evidence only: while set, the window texts and restorations are traced byte for byte;
     * quests_ui_mode, quests_text: the UI mode and the text block to restore at the close;
     * quests_notes: qnotes.bin, the quests' texts (entries 0..55) and then the auto notes';
     * quests_slot (below): the 135 line pointers. Each list blanks only its own number of them, so the rest keep an
     *   earlier list's lines; NULL is a slot no list wrote, where the original would read stack bytes.
     * Original: 3E24F's [bp-6], [bp-8], SI, [bp-0Ah], [bp-0Fh], [bp-12h]/[bp-14h], [bp-0Ch] and the far pointers at
     * [bp-21Ch]; the owners 17E18 and 45D4A; the text block 97C2/97C0 and F42C (DOS memory for the notes). */
    enum quests_owner quests_owner;
    int quests_mode, quests_top, quests_lines, quests_opened, quests_trace;
    int panel_trace; /* Evidence: the control panel's 4E38C bytes (297D9..29C6B) */
    uint8_t quests_ui_mode;
    text_block_saved quests_text;
    uint8_t *quests_notes;
    size_t quests_notes_size;
    uint8_t *credits;
    size_t credits_size;
    unsigned credits_page;
    uint8_t credits_ui_mode; /* 164A2's credits.bin, page, saved 9A1C */
    /* Other Options: options_si steps 19h per extra button; options_frame is the image of the menu routine's frame
     * that the live button tables point into (the input poll reads the buttons there). Original: 16B23's SI; 1660E's
     * BP-38..BP-1. */
    int options_si;
    uint8_t options_frame[0x38];
    const char *quests_slot[135]; /* the Quests viewer's line pointers (see quests_owner above) */
    /* The character view's Exchange (4798C, with the saved faces flag 98D6), its Quick Reference (46BA0 from the
     * view: UI mode 10 and the redraw after), the stat details 428F1 and the awards list (41581): its top line,
     * line count, whether it opened windows 29 and 30, the UI mode and text block to restore, the award texts
     * (award.bin; DOS memory in the original) and its 88 line pointers. Original: 4798C's [bp-2]; 41581's [bp-4],
     * [bp-2], [bp-6], [bp-0Bh], [bp-0Eh]/[bp-10h], [bp-8] and the far pointers at [bp-178h] */
    uint8_t xchg_portrait_clicks;
    int qref_view;
    uint32_t qref_view_site;
    uint16_t awards_record;
    int awards_top, awards_count, awards_opened;
    uint8_t awards_ui_mode;
    text_block_saved awards_text;
    uint8_t *awards_bin;
    size_t awards_bin_size;
    const char *awards_slot[XEEN_AWARD_COUNT];
    char awards_counted[90], awards_race[90];
    unsigned long purchases, equips;
    /* The item spells (Enchant Item, Recharge Item and the third, 3B893) open the items dialog as its owner 3:
     * item_spell_mode is the UI mode they keep to restore; option_residue is the word an item option reads but its
     * handler never writes: the stack residue of the key poll that started the option (input_poll.last_residue; 0
     * unknown), which decides the Gold option's accessory and misc branches.
     * Original: 3B443, 3B893, 3B1C5 open 44F92 in mode 6, 0Bh or 4; their [bp-2] (9A1C); 3BAEE's unwritten [BP-4]
     * after the 189E3 poll. */
    uint8_t item_spell_mode;
    uint16_t option_residue;
    unsigned long item_spells;
    /* Use's Enchant (power 63) and Recharge (power 53) open the items dialog again while Use's own items dialog is
     * still open underneath: items_outer keeps the outer dialog's state (the fields above) until the inner one
     * returns, and items_nested is 1 while it is kept. Original: 3B443 and 3B1C5 open 44F92 (owner 3, mode 6 or 4)
     * inside Use's 44F92, which stays on the stack (its [bp+..] and locals) until the return to 3B48F. */
    struct {
        int owner, mode, cat, index, combat, stock, action, confirm, oindex;
        uint16_t chr, start, equip, residue;
        uint8_t marks[40];
        uint32_t cost;
    } items_outer;
    int items_nested;
    unsigned long items_nests;
    /* Use runs an item's power by calling the spell's handler directly (no Cast dialog, no payment): use_active
     * sends the handler's end (cast_handled) to Use's tail; use_record, use_row and use_charges are the used
     * item's member record, row and remaining charges; handler_ax is the value the handler is entered with (the
     * spell id from a cast, the power from Use); use_done: Use has finished (combat's command loop sees it through
     * its turn state).
     * Original: 3BCF7 through 4478B; 3BAEE's record, SI and [BP-4]; the handler's AX from 3DBF9 or 3BDD6; 3BE13,
     * combat's 39249 sees 97FF changed. */
    int use_active, use_done, use_charges;
    uint16_t use_record;
    unsigned use_row, use_power; /* use_power: the item's power number (USE_POWER_*) */
    uint16_t handler_ax;
    unsigned long item_uses, identifies;
    /* driver_check: an item's Town Portal used in combat freed the sprite handles above the items buffer (Use's
     * own release of the handles), so the next sprite draws are judged by the graphics driver's own tests (an
     * original fault: the program can end there); driver_exit: the message of that fatal exit (the program ends,
     * DOS exit code 2).
     * Original: 449BA's (or 3B0B4's) 190CF; the 4AC92 draws; 4AD19 -> 505E3. */
    int driver_check;
    char driver_exit[72];
    /* The save-pair check at the menu's entry, kept across its dialog: each slot's Clouds and Darkside save ids
     * (FFh: no file), the last maze.nam read, the next slot to compare and the world at entry; program_exit: the
     * program's own exit message once it ended there.
     * Original: 18363 across 184DE ([BP-3Ch] the ids, xeen 0..9 and dark 0..9; [BP-28h] 199B7's maze.nam; [BP-2]
     * the slot; [BP-6] DS 94); 2C80D's message. */
    uint8_t pair_ids[2][10], pair_nam[32], pair_slot, pair_world;
    char program_exit[64];
    /* The hand-off to XEEN.EXE (the program's exec, the last thing it does): handoff_command, the command it leaves
     * in the environment (an ending's, or Other Options' string); handoff_program, the file it executes;
     * handoff_cfg, whether xeen.cfg was written (the original does not test it); ending_stop, the stop of an
     * ending's failed save. Original: 16241's 123A1("xeen.exe", 0, 0); the putenv of 33518's AC0F; DS:475C. */
    char handoff_command[96], handoff_program[13];
    int handoff_cfg;
    uint32_t ending_stop;
    /* An event's message scroll (operation 20's lock-pick messages): 1 open (scroll owner 10), 2 closed, and the
     * service answers. Original: 18F2F(A827, 1). */
    int event_scroll; /* enum event_message */
    unsigned long chests;
    /* Operation 12's refused take of gold or gems ("Not enough ..."): 1 its message open, 2 closed, and the service
     * answers; payment_kind its enum funds_kind; payments counts the messages shown. Original: 291FB(kind, 0, 0, 3). */
    int event_payment; /* enum event_message */
    unsigned payment_kind;
    uint8_t payment_loc_kind;
    unsigned long payments, save_refusals;
    int use_marked; /* Evidence: a 3BCF7 draw-trace run is open */
    /* Combat's modal commands (combat_modal): the control panel, Info, Quick Reference, the Quick Fight options, a
     * member's view and the quick cast, each run from combat's command poll.
     * combat_ui_site: which of them is open (its XEEN_COMBAT_MODAL_* site: the place combat resumes, and what the
     *   frame wait and a stop report while it runs); 0 none, and the same dialogs opened from exploration (I, Q, M,
     *   D) clear it. Cleared when the dialog hands back to combat.
     * panel_combat: the control panel was opened by combat's Tab: it leaves the monsters enabled (exploration's
     *   Tab disables them) and returns to combat.
     * cast_quick: the combat quick cast is running (no Cast dialog; cast_handled ends it with quick_cast_end).
     * qref_opened: Quick Reference opened its window itself (it was not open already) and closes it at the end.
     * qfo_record: the member whose Quick Fight option the options dialog shows and changes.
     * Original: 3914E; 297D9 without 17C30's 96C4 writes; 3D8C1(1); 46BA0's window 18h; 3E9B6's record; 29EF0. */
    uint32_t combat_ui_site;
    int panel_combat, cast_quick, qref_opened;
    uint16_t qfo_record;
    /* The map view (M, game_combat.c), from map_begin to map_end:
     * map_view: the view's state (xeen_map_begin's: the shown position and the blink the wait toggles);
     * map_step: the step its resume continues at (enum map_step: the frame mark, the view's draw, the blink wait and
     *   its mark, the caption, the key poll);
     * map_active: the view is open (the window code then emits its evidence lines);
     * map_elapsed: the blink wait's frame clock read.
     * Original: 1DA2D's view, its 1DB56 / 1E35C / 1E36A waits. */
    xeen_map_view map_view;
    uint8_t map_step, map_active;
    xeen_timer_elapsed_state map_elapsed;
};
/* The native audio the session plays its effects, voices and music through: NULL while native audio is off (the
 * models then only count). */
/* The session's state archive takes BUFFER (N bytes, now the game's to free) and its opened archive CUR; the
 * previous buffer is freed (the save, the inn's pair, the location's state and the map load's staged clone). */
static inline void state_adopt(xeen_game *g, uint8_t *buffer, size_t n, xeen_archive cur)
{
    free(g->state_owned);
    g->state_owned = buffer;
    g->state_owned_size = n;
    g->state = cur;
}
/* The dialog's save of the text block and the file buffer (in this order). */
static inline void text_block_save(const xeen_movement *m, text_block_saved *s)
{
    s->segment = xeen_read16(m, XEEN_DS_TEXT_BLOCK_SEGMENT);
    s->block = xeen_read16(m, XEEN_DS_TEXT_BLOCK);
    s->file_buffer = xeen_read16(m, XEEN_DS_FILE_BUFFER_SEGMENT);
}
/* 2B6C9's text block for the dialog's file: the saved file buffer's segment (0: none), offset 0. */
static inline void text_block_in_file_buffer(xeen_movement *m, const text_block_saved *s)
{
    xeen_write16(m, XEEN_DS_TEXT_BLOCK_SEGMENT, s->file_buffer);
    xeen_write16(m, XEEN_DS_TEXT_BLOCK, 0);
}
/* The dialog's end: the saved text block back (the segment, then the offset). */
static inline void text_block_restore(xeen_movement *m, const text_block_saved *s)
{
    xeen_write16(m, XEEN_DS_TEXT_BLOCK_SEGMENT, s->segment);
    xeen_write16(m, XEEN_DS_TEXT_BLOCK, s->block);
}
/* Both Clairvoyance border faces back to their still frames (F466 = F467 = 2): the end of a talk, question or
 * Who-will form, the choice form's start, and a game's start or load. */
static inline void clairvoyance_still(uint8_t *ds)
{
    ds[XEEN_DS_CLAIRVOYANCE_RIGHT_STATE] = XEEN_CLAIRVOYANCE_STILL;
    ds[XEEN_DS_CLAIRVOYANCE_LEFT_STATE] = XEEN_CLAIRVOYANCE_STILL;
}
static inline xeen_native_audio *game_audio(xeen_game *g)
{
    return g->native_audio_enabled ? &g->audio : NULL;
}

/*
 * The program data's button sets (src/data structs.json button_sets: per set the x, y, width, height, key and
 * enabled arrays) as button_tables' six arguments, named by their keys and the dialogs that install them.
 */
#define XEEN_BUTTONS_MAIN_MENU         0x18d2, 0x18e6, 0x18ee, 0x18f6, 0x18fe, 0x1902 /* the title menu's four (main0) */
#define XEEN_BUTTONS_LIST10            0x190a, 0x192c, 0x193c, 0x194c, 0x195c, 0x196c /* Up Down 1..0 Esc PgUp PgDn S */
#define XEEN_BUTTONS_CAST              0x1a1e, 0x1a26, 0x1a29, 0x1a2c, 0x1a38, 0x1a32 /* C N Esc (key table 1A38) */
#define XEEN_BUTTONS_ESC_T             0x1a3c, 0x1a42, 0x1a44, 0x1a46, 0x1a48, 0x1a4a /* Esc T (Lloyd's sets key 0 R) */
#define XEEN_BUTTONS_ELEMENTS          0x1aac, 0x1ab6, 0x1aba, 0x1abe, 0x1ac2, 0x1ac6 /* F E C A: "Which Element?" */
#define XEEN_BUTTONS_INN               0x1b74, 0x1b8c, 0x1b97, 0x1ba2, 0x1bad, 0x1bb8 /* Up Down D R C X Esc 1..4 */
#define XEEN_BUTTONS_CONTROL_PANEL     0x1bce, 0x1be2, 0x1beb, 0x1bf4, 0x1bfd, 0x1c06 /* E M L S A Q T P W */
#define XEEN_BUTTONS_ITEMS             0x1c30, 0x1c58, 0x1c6b, 0x1c7e, 0x1c91, 0x1ca4 /* W A C M E R D Q Esc U 1..9 */
#define XEEN_BUTTONS_ITEMS_SPELL       0x1cb8, 0x1cda, 0x1cea, 0x1cfa, 0x1d0a, 0x1d1a /* W A C M E Esc U 1..9 */
#define XEEN_BUTTONS_CREATION          0x1d2a, 0x1d58, 0x1d6e, 0x1d84, 0x1d9a, 0x1db0 /* R C Esc Up Down MIPESAL a..j */
#define XEEN_BUTTONS_CREATION_EXCHANGE 0x1dc6, 0x1dd8, 0x1de0, 0x1de8, 0x1df0, 0x1df8 /* Esc M I P E S A L */
#define XEEN_BUTTONS_PREFERENCE        0x1e00, 0x1e06, 0x1e08, 0x1e0a, 0x1e0c, 0x1e0e /* A W: the game preference */
#define XEEN_BUTTONS_ESC_1_4           0x1e10, 0x1e1c, 0x1e21, 0x1e26, 0x1e2b, 0x1e30 /* Esc 1..4 */
#define XEEN_BUTTONS_QUESTS            0x1e36, 0x1e44, 0x1e4a, 0x1e50, 0x1e56, 0x1e5c /* I Q A Up Down Esc */
#define XEEN_BUTTONS_AWARDS            0x1e62, 0x1e6a, 0x1e6d, 0x1e70, 0x1e73, 0x1e76 /* Up Down Esc */
#define XEEN_BUTTONS_ESC_1_9           0x1e7a, 0x1e90, 0x1e9a, 0x1ea4, 0x1eae, 0x1eb8 /* Esc 1..9 */
#define XEEN_BUTTONS_CONFIRM           0x1c10, 0x1c16, 0x1c18, 0x1c1a, 0x1c1c, 0x1c1e /* Y N: the confirm dialog */
/* Button I's entry in SET's arrays (SET one of the XEEN_BUTTONS_* above): its x (a word), y, width, height, key and
 * enabled bytes.
 * The dialogs rewrite them in place for a mode (the items dialog's keys and Use button, the inn's and the confirm
 * dialog's positions, Lloyd's key). */
#define XEEN_BUTTON_APPLY_(pick, ...)                 pick(__VA_ARGS__)
#define XEEN_BUTTON_X_(x, y, width, height, key, on)   (x)
#define XEEN_BUTTON_Y_(x, y, width, height, key, on)   (y)
#define XEEN_BUTTON_W_(x, y, width, height, key, on)   (width)
#define XEEN_BUTTON_H_(x, y, width, height, key, on)   (height)
#define XEEN_BUTTON_KEY_(x, y, width, height, key, on) (key)
#define XEEN_BUTTON_ON_(x, y, width, height, key, on)  (on)
#define XEEN_BUTTON_X(set, i)       ((uint16_t)(XEEN_BUTTON_APPLY_(XEEN_BUTTON_X_, set) + 2u * (i)))
#define XEEN_BUTTON_Y(set, i)       ((uint16_t)(XEEN_BUTTON_APPLY_(XEEN_BUTTON_Y_, set) + (i)))
#define XEEN_BUTTON_WIDTH(set, i)   ((uint16_t)(XEEN_BUTTON_APPLY_(XEEN_BUTTON_W_, set) + (i)))
#define XEEN_BUTTON_HEIGHT(set, i)  ((uint16_t)(XEEN_BUTTON_APPLY_(XEEN_BUTTON_H_, set) + (i)))
#define XEEN_BUTTON_KEY(set, i)     ((uint16_t)(XEEN_BUTTON_APPLY_(XEEN_BUTTON_KEY_, set) + (i)))
#define XEEN_BUTTON_ENABLED(set, i) ((uint16_t)(XEEN_BUTTON_APPLY_(XEEN_BUTTON_ON_, set) + (i)))

/* The main menu's stages (g->menu_stage) are enum xeen_menu_stage (game.h): the host reads them in the view. */
/* The control panel's stages (game_panel.c, 297D9): the first range of g->panel_stage (see its contract in struct
 * xeen_game; the other owners' ranges follow below). */
enum {
    PANEL_SCENE = 1,
    PANEL_POLL,
    PANEL_SAVED,
    PANEL_DIALOG_SCENE,
    PANEL_DIALOG_POLL,
    PANEL_NAME_POLL,
    PANEL_NAME_SCENE,
    PANEL_NAME_TICK,
    PANEL_CONFIRM,
    PANEL_PREFERENCE /* The menu's 2B44D game preference poll */
};

/*
 * The location stages (game_location.c: 277E5's location loop; game_bank.c: 3F05D's guild poll).
 */
enum {
    LOCATION_POLL = 20,
    LOCATION_TICK,
    GUILD_POLL,
    LOCATION_LEAVE_WAIT /* 28001 / 28046 */,
    LOCATION_END_WAIT /* 28086 33D36 */
};
/* The title menu's and the control panel's wait sites (each the original call the stage resumes at, reported as its
 * lifecycle boundary while it waits). */
#define MENU_TITLE_BOUNDARY              0x16827u /* the title screen's frame */
#define MENU_TITLE_POLL_BOUNDARY         0x168b3u /* the title's key poll */
#define MENU_FRAME_BOUNDARY              0x16388u /* the main menu's frame */
#define MENU_POLL_BOUNDARY               0x16470u /* the main menu's key poll */
#define MENU_OPTIONS_BOUNDARY            0x16d62u /* Other Options' frame */
#define MENU_OPTIONS_POLL_BOUNDARY       0x16e5eu /* its key poll after the frame */
#define MENU_OPTIONS_STILL_POLL_BOUNDARY 0x16e76u /* its key poll without the Clouds installed (no frame) */
#define MENU_DIALOG_BOUNDARY             0x2b117u /* the save dialog's frame (2AD6D) */
#define MENU_DIALOG_POLL_BOUNDARY        0x2b1c5u /* the save dialog's key poll */
#define SCROLL0_BOUNDARY                 0x18f97u /* 18F2F mode 0, the scroll: its key poll (189E3 until a key) */
#define PANEL_POLL_BOUNDARY              0x29946u /* the control panel's key poll */
#define PANEL_DIALOG_POLL_BOUNDARY       0x2b1eau /* the panel's save dialog's key poll */
#define PANEL_SAVED_BOUNDARY             0x296b9u /* "Your Game ... has been saved": its key wait */
#define PANEL_NAME_POLL_BOUNDARY         0x288c4u /* the name entry's (28876) key poll */
#define PANEL_NAME_TICK_BOUNDARY         0x2890cu /* the name entry's cursor tick */
#define PANEL_PREFERENCE_BOUNDARY        0x2b44du /* the new game's preference choice's key poll */
#define LOCATION_END_WAIT_BOUNDARY       0x2808bu /* a location's leaving voice: 28086's 187E9(1, 0) loop */
#define LOCATION_POLL_BOUNDARY 0x26e65u /* the 26E60 loop's 189E3 */
#define LOCATION_TICK_BOUNDARY 0x26e73u /* its 4EA1C */
#define GUILD_POLL_BOUNDARY    0x3f415u /* 3F05D's 189E3 */

/*
 * The other waits the host resumes. Each is the original call a stage waits at, reported as the lifecycle boundary
 * while it waits; xeen_game_site_kind (game_lifecycle.c) sorts every reported boundary into the kinds the host acts
 * on (xeen_site_kind, game.h), so the host never lists these addresses itself.
 */
#define MENU_ENTRY_BOUNDARY          0x1660eu /* the title-screen main menu, entered at start and after the teardown */
#define WHO_WILL_POLL_BOUNDARY       XEEN_GAME_WHO_WILL_POLL /* the Who-will prompt's key poll (2F9BA; also the unlock's) */
#define MODAL_YES_NO_BOUNDARY        0x2f2ffu /* the yes/no or any-key modal (2F2FF) */
#define MODAL_CHOICE_BOUNDARY        0x2f5b1u /* the numeric choice modal (2F5B1) */
#define SCROLL_POLL_BOUNDARY         0x18faeu /* an open scroll's key poll (18F2F's 18FAE) */
#define PAGE_WAIT_BOUNDARY           0x31793u /* a displayed text page's key wait (316DC) */
#define NOTHING_HERE_POLL_BOUNDARY   0x3381bu /* "Nothing here."'s key poll (337D7) */
#define SPEECH_LOOP_BOUNDARY         0x30442u /* a pass of the speech loop (3000D) */
#define SCENE_CALL_BOUNDARY          0x1b503u /* an operation's scene call (1B503) */
#define PORTRAIT_FLASH_BOUNDARY      0x2b9c7u /* a member's portrait flash (2B9C7) */
#define PARTY_DAMAGE_BOUNDARY        0x2e68du /* the party damage (2E68D) */
#define CONFIRM_POLL_BOUNDARY        0x294a2u /* the Yes/No confirmation's key poll */
#define SCROLL_MESSAGE_POLL_BOUNDARY 0x18fa1u /* a scroll's mode-1 input wait (18F2F) */
#define TREASURE_KEY_POLL_BOUNDARY   0x2f170u /* the treasure's "Hit a key" loop */
#define WHO_WILL_BOUNDARY            0x2f9bau /* the Who-will prompt opens (2F9BA) */
#define PORTRAIT_ANIMATION_BOUNDARY  0x26978u /* a keeper's or speaker's portrait animation (26978) */
#define MENU_LOAD_BOUNDARY           0x19e82u /* a game loaded from the menu: its map load (19E82) */
#define MENU_CREDITS_POLL_BOUNDARY   0x1652du /* the first credits page's key poll; page N's is N strides later */
#define MENU_CREDITS_POLL_STRIDE     0x3fu    /* the distance between two pages' polls (game_menu.c) */
#define MENU_CREDITS_PAGES           4u
#define LOCATION_LEAVE_WAIT_BOUNDARY 0x2800au /* a location's goodbye voice, until it has played (28001's loop) */
#define LOCATION_COME_WAIT_BOUNDARY  0x2804fu /* the Darkside blacksmith's "come" voice (28046's loop) */
#define VOICE_END_WAIT_BOUNDARY      0x33d58u /* 33D36: until the voice has played (a location talk, a cut unload) */
#define REST_DREAM_BOUNDARY          0x28f9du /* the Rest dream's first timed wait (7 ticks) */
#define REST_DREAM_VOICE_BOUNDARY    0x28fbeu /* its first voice, until it has played */
#define REST_LAUGH_VOICE_BOUNDARY    0x29000u /* laff1.voc, until it has played */
#define REST_DREAM_END_BOUNDARY      0x2900cu /* the timed wait after both voices */
#define ITEM_WAIT_BOUNDARY           0x30e05u /* a treasure item line's wait (30E05(ticks)): its start */
#define ITEM_WAIT_POLL_BOUNDARY      0x30e0du /* its key poll and scene */
#define ITEM_WAIT_TICK_BOUNDARY      0x30e1au /* its tick count against the target */
#define EXIT_DEATH_BOUNDARY          0x1596fu /* the party's death: the flames and the laugh */
#define EXIT_BOOK_BOUNDARY           0x15647u /* the book's pages */
#define EXIT_VOICE_WAIT_BOUNDARY     0x1627cu /* the teardown before the title menu waits for the voice */
#define HANDOFF_VOICE_WAIT_BOUNDARY  0x161dbu /* the teardown before XEEN.EXE waits for the voice */
#define CUT_FIRST_SITE               0x33fcbu /* the cutscenes' wait sites, from the Sphinx's first ... */
#define CUT_SITES_END                0x360c8u /* ... to the end of the gate's (exclusive) */
#define CUT_SUBTITLE_SITE            0x185ffu /* a talk's subtitle read (185E3's 4EA6C) */
#define EVENT_TABLE_COMMAND_BOUNDARY 0x17c10u /* a modal's answer redispatched through the command table */
#define EVENT_SPACE_WAIT_BOUNDARY    0x17da8u /* Space's wait after a modal (its own resume) */
#define INTERACTION_BOUNDARY         0x17b63u /* Space or a click in the view: the interaction gate (nothing there: the host's) */
#define COMBAT_CONTACT_BOUNDARY      0x1c17eu /* a contact fight begins at the scene's encounter check */
#define COMBAT_CLEANUP_TAIL_BOUNDARY 0x3957cu /* the combat cleanup's tail after a sign-up fight */
#define COMBAT_MONSTER_TURN_BOUNDARY 0x38049u /* a monster's turn combat stopped at (XC_UNSUPPORTED_CONTINUATION) */
#define DRIVER_EXIT_BOUNDARY         0x4ad19u /* the sound driver's fatal exit (DOS exit code 2) */
#define PROGRAM_EXIT_BOUNDARY        0x2c80du /* the program's own exit ("Exit to DOS!") */
#define XEEN_EXE_HANDOFF_BOUNDARY    0x123a1u /* the exec of XEEN.EXE */
/* Moved from their units (the host's catalogue needs them; Phase 262) */
#define INN_SELECT_BOUNDARY          0x43b35u /* 43A49's 189E3 */
#define ITEMS_POLL_BOUNDARY          0x45b95u /* 44F92's 189E3 */
#define INFO_POLL_BOUNDARY           0x2a012u /* 29EF0's 189E3 */
#define QREF_POLL_BOUNDARY           0x46e31u /* 46BA0's 189E3 */
#define QFO_POLL_BOUNDARY            0x3eab5u /* 3E9B6's 189E3 */
#define MAP_POLL_BOUNDARY            0x1e3c7u /* 1DA2D's 189E3 */
#define MAP_FIRST_BOUNDARY           0x1db56u /* its first pass (4EA0D, no scene) */
#define MAP_PASS_BOUNDARY            0x1db69u /* a later pass, after its 1B503(0) */
#define DISMISS_POLL_BOUNDARY        0x2b844u /* 2B75D's 189E3 */
#define CREATE_XCHG_BOUNDARY         0x3fa73u /* 3F9C4's 189E3 */
#define CREATE_TICK_BOUNDARY         0x3ff11u /* its 4EA1C */

/* The inn roster's stages (game_inn.c 43E0B: its keys are described there). */
enum {
    INN_POLL = 30,
    INN_SELECT,
    INN_BOOK
};
#define INN_POLL_BOUNDARY   0x441d3u /* 441D3 189E3 */

/* Character creation's stages (game_creation.c 4063B and 3FB10's dice poll). */
enum {
    CREATE_INTRO = 40,
    CREATE_INTRO_TICK,
    CREATE_POLL,
    CREATE_TICK,
    CREATE_XCHG /* 3F9C4 */
};
#define CREATE_POLL_BOUNDARY 0x3ff08u /* 3FB10's 189E3 */

/* game_shop.c, game_view.c and game_quests.c: the items dialog (44F92), the character view (44AE6) and its
 * quests (3E24F), exchange (4798C), details (428F1) and awards (41581) lists. */
enum {
    ITEMS_POLL = 50,
    OPTION_WHICH,
    MESSAGE_POLL,
    VIEW_TICK,
    VIEW_POLL,
    IDENTIFY_POLL_STAGE,
    QUESTS_POLL,
    EXCHANGE_POLL,
    DETAILS_POLL,
    AWARDS_POLL /* 4798C, 428F1, 41581 */
};
#define QUESTS_POLL_BOUNDARY   0x3e8a3u /* 3E24F's 189E3 */
#define DETAILS_POLL_BOUNDARY  0x43183u /* 428F1's 189E3 */
#define AWARDS_POLL_BOUNDARY   0x41914u /* 41581's 189E3 */
#define VIEW_TICK_BOUNDARY     0x44c09u /* 44AE6's 4EA1C */
#define VIEW_POLL_BOUNDARY     0x44c2au /* 44AE6's 189E3 */
#define EXCHANGE_POLL_BOUNDARY 0x47a42u /* 4798C's 189E3 */
#define OPTION_WHICH_BOUNDARY  0x3bc05u /* 3BAEE "%s which item?" */
#define EQUIP_MESSAGE_BOUNDARY 0x42494u /* 422D8's window-6 message */
#define IDENTIFY_ITEM_BOUNDARY 0x3c24du /* 3C199's 189E3 (window 0Eh) */

/* game_cast.c (3D8C1, the Cast Spell dialog); CAST_LIST is
 * 3F05D's mode 1 (game_bank.c). */
enum {
    CAST_SCENE = 60,
    CAST_POLL,
    CAST_LIST,
    CAST_WHOM_SCENE,
    CAST_WHOM_POLL,
    CAST_LACK,
    CAST_FLASH_SCENE,
    CAST_ELEMENT_SCENE,
    CAST_ELEMENT_POLL,
    LLOYD_SCENE,
    LLOYD_POLL,
    TRAVEL_SCENE /* 3A5DA's loop, 3AC41's 1B503(1) */,
    DETECT_SCENE,
    DETECT_POLL,
    IDENTIFY_SCENE,
    IDENTIFY_POLL /* 3A9C1's and 3A232's loops */
};
#define CAST_POLL_BOUNDARY    0x3db40u /* 3DB40 189E3 */
#define CAST_WHOM_BOUNDARY    0x39a4eu /* 399FA's 189E3 */
#define CAST_LACK_BOUNDARY    0x39bf3u /* 39B7F's busy 189E3 */
#define CAST_ENGAGED_BOUNDARY 0x39b5cu /* 39AF6's busy 189E3 */
#define CAST_ELEMENT_BOUNDARY 0x39963u /* 39817's 189E3 */
#define LLOYD_BOUNDARY        0x3a7b2u /* 3A5DA's 189E3 */
#define DETECT_BOUNDARY       0x3ab49u /* 3A9C1's 189E3 */
#define IDENTIFY_BOUNDARY     0x3a40au /* 3A232's 189E3 */

/* game_cutscene.c, the 34FAA / 35992 cutscenes (panel stages
 * from CUT_BASE, after the cast stages). */
enum {
    CUT_BASE = 90
};
/* game_combat.c, 3914E's modal commands (panel stages from
 * COMBAT_UI_BASE, after the cutscene stages). */
enum {
    COMBAT_UI_BASE = 200
};
/* panel_after_scene and xeen_game_resume_panel pick a stage's owner by testing these bases from the highest down. */
_Static_assert((int)CAST_SCENE < (int)CUT_BASE && (int)CUT_BASE < (int)COMBAT_UI_BASE, "the panel stage ranges are ordered");

/*
 * game.c: the session core (the movement services, the CD and voices, drive(), the frame wait and its
 * palette port, the stop helper and the command tail)
 */
xeen_status game_stop(xeen_game *g, uint32_t boundary);
xeen_status command_tail(xeen_game *g);
xeen_status command_finish(xeen_game *g);
void palette_port(void *user, uint16_t port, uint8_t value);
/* A timer poll's elapsed ticks (xeen_timer_elapsed on STATE: the 4EA1C-style wait's AX into g->timer_registers) with
 * the palette writes going to palette_port: the timer's failure, XEEN_BAD_INPUT when the presentation refused a
 * palette write, else XEEN_OK. */
xeen_status timer_ticks(xeen_game *g, xeen_timer_elapsed_state *state, xeen_timer_bios bios, void *user);
/* 4EA0D: the frame wait's base sample (the BIOS ticks 4EA1C counts from) into g->timer_registers. */
xeen_status frame_base(xeen_game *g, xeen_timer_bios bios, void *user);
/* The session waits for the host's input at BOUNDARY (XG_WAITING, XGC_PENDING_INPUT): XEEN_SERVICE_REQUIRED. */
xeen_status input_wait(xeen_game *g, uint32_t boundary);
/* 1880E(EFFECT): the sound effect through the session's audio (game_audio). */
xeen_status sound_effect(xeen_game *g, uint16_t effect);
/* The session ends as the original program does at BOUNDARY (XG_STOPPED, XGC_ORIGINAL_EXIT): XEEN_SERVICE_REQUIRED. */
xeen_status original_exit(xeen_game *g, uint32_t boundary);
/* The session stops at BOUNDARY for a host service (XG_STOPPED, XGC_EXTERNAL): XEEN_SERVICE_REQUIRED. */
xeen_status service_wait(xeen_game *g, uint32_t boundary);
/* The command status of a stop at BOUNDARY: 17F28, the exploration loop's exit (17F07 after F60A or F64A: 4E231,
 * 4AA3E, 1919B; with F60A the 1596F death sequence follows), is the original program's exit; any other boundary is the
 * host's continuation. */
static inline xeen_game_command_status exit_or_external(uint32_t boundary)
{
    return boundary == XEEN_TICK_LOOP_EXIT ? XGC_ORIGINAL_EXIT : XGC_EXTERNAL;
}
/* The session stops at BOUNDARY (4EA0D or 4EA35) for the host's 1B503(ARGUMENT) scene (XG_STOPPED, XGC_EXTERNAL):
 * XEEN_SERVICE_REQUIRED; the caller's stage continues after it. */
xeen_status scene_wait(xeen_game *g, uint16_t argument, uint32_t boundary);
void bar_redraw(xeen_game *g);
/* 4ADB1(0, DS:2A5C): the exploration icons (the frame's buttons) drawn again; nothing without a presentation. */
void explore_icons(xeen_game *g);
void drive(xeen_game *g);
xeen_status ranged_delivery(xeen_game *g);
/* A continuous session's tick stopped at 2AAB1 (28195's 2A701 delivery, the monsters' shots): the delivery's tail
 * (ranged_delivery), then 17264 with 96C0 = 3 when it completes, else the delivery's own boundary. Other ticks pass
 * unchanged. */
xeen_gameplay_tick delivery_tail(xeen_game *g, xeen_gameplay_tick tick);
xeen_status run_monster_update(xeen_game *g);
/* Where a command stops when the monsters' update after it (run_monster_update, 2A701) did not say where: the default
 * of the variable stop sites of Cast, Space and the shot (stops_variable.json). */
enum {
    RELEASE_STOP_MONSTER_UPDATE = 0x2a701
};
xeen_gameplay_tick ranged_tick(xeen_game *g, xeen_gameplay_tick tick);
int movement_service(void *u, xeen_movement *m, xeen_service s, uint16_t a, uint16_t b,
                     uint16_t *ax);
int minimap_driver_test(void *cache, const xeen_sprite_draw *d);
void reload_wait_open(xeen_game *g, xeen_movement *m);
void wait_box_present(xeen_game *g, uint16_t text);
void modal_poll_mark(xeen_game *g);
xeen_status combat_ranged_delivery(void *user, xeen_movement *m, uint32_t *boundary);
int restore_after_combat(xeen_game *g);
xeen_status rest_return(xeen_game *g);

/* game_voice.c: the CD and voice services at the game's clock (483C7's play, 47F3D's status and mouth, 47B7D's load
 * hooks, 187E9's voice and the kept VOC)
 */
void voice_cancel(xeen_game *g, uint32_t site);
int voice_busy(xeen_game *g, uint32_t site);
void voc_start(xeen_game *g, uint32_t site);
void voice_clock(xeen_game *g);
void cd_postload(xeen_game *g);
void cd_preload(xeen_game *g);
/* The CD status calls (47F3D) the speech and location screens share, by their return address (cd_status's SITE). */
#define TALK_CD_STATUS_CALL       0x26d7bu /* 26978: a location keeper's or a speech's talking mouth */
#define SPEECH_CD_STATUS_CALL     0x30454u /* the speech loop's mouth */
#define FACE_FORM_CD_STATUS_CALL  0x2fb31u /* the face form's mouth */
int cd_status(xeen_game *g, uint32_t site);
void voc_keep(xeen_game *g, const uint8_t *b, size_t n);
void cd_voice_start(xeen_game *g);
uint16_t cd_mouth(xeen_game *g, uint32_t site);

/* game_lifecycle.c: the session's construction (create, create_seeded, create_saved), the bounded lifecycle checks,
 * the map resources and host bindings, destruction and the host settings
 */
xeen_game *create_seeded(const uint8_t *state, size_t sn, const uint8_t *assets, size_t an,
                         xeen_game_start start, xeen_ui_present_fn fn, void *user,
                         const uint8_t *seed, int flag19, char *error, size_t en);
int clouds_flag19(const uint8_t *ds);

/* game_resources.c: a map's resources: its case (19F31's files), objects (with the map text and selectors),
 * monsters (1A3A9's aliased table), scene resources, the program data, the archives and sprites, and the music
 * requests
 */
xeen_sprite_archives sprite_archives(const xeen_game *g, const xeen_archive *runtime);
/* Entry K of a NUL-separated text file (award.bin, qnotes.bin, the credits; the 50D4:165C word): the text after the
 * file's K-th NUL (entry 0 at its start); NULL when the file has fewer entries or the entry runs past its end. */
const char *nul_entry(const uint8_t *bytes, size_t size, unsigned k);
/* The offset of entry K of a NUL-separated file BYTES of SIZE bytes (entry 0 the start, entry K after the K-th NUL),
 * or (size_t)-1 when the file has fewer NULs; the entry's own end is not checked (nul_entry, tavern_entry). */
size_t nul_offset(const uint8_t *bytes, size_t size, unsigned k);
/* Loads the sprite file NAME from the current world's archives (sprite_archives) into a new handle, *HANDLE; 0 when it
 * cannot be found or loaded. Original: 18931 (18935 / 18964 through the archive hooks). */
int sprite_load(xeen_game *g, const char *name, uint16_t *handle);
/* 18931 of NAME into the 8C86 handle: the load reuses 8C86's buffer when it is set, and 98D4 becomes that
 * handle (else the new one). 0 when the load failed (98D4 unchanged; the caller owns the stop). */
int sprite_load_resident(xeen_game *g, const char *name);
/* The dialogs' sprite stack (98D4, the current dialog sprites, with its depth and saved slots):
 * handle_push: one level deeper, the current handle saved in the new level's slot, then NAME loaded (sprite_load)
 *   and made current; 0 when the load fails, the depth and slot already written (the caller's stop);
 * handle_push_or_undo: handle_push of the DS text NAME that takes the depth back on failure (the slot write stays);
 * handle_pop: the current handle and every handle loaded after it freed, the saved one current again, one level up.
 * Original: 190A3 (push), 190CF (pop; its free is 4AA3E). */
int handle_push(xeen_game *g, const char *name, uint16_t *handle);
int handle_push_or_undo(xeen_game *g, uint16_t name);
void handle_pop(xeen_game *g);
void record_music(void *user, const char *name, uint16_t flag);
uint16_t transition_probe_random(void *user, uint16_t incoming);

/* game_transition.c: the map loads (19E82): the commit on a trial session, the edge steps
 * (xeen_game_resume_transition) and map_reload
 */
xeen_resource_status map_reload(xeen_game *g, uint16_t target);
void map_load_tail(xeen_game *g);

/* game_navigation.c: navigation: the cell's event and interaction, the session's gameplay services, a step's
 * execution, 17264's surface scenes and damage, the door scene, the navigation tail, and the world entry
 */
xeen_gameplay_services session_services(xeen_game *g);
xeen_status surface_scene(xeen_game *g, int stage, int owner);
xeen_status post_action_surface_tail(xeen_game *g);
xeen_caller_result desert_surface(xeen_game *g, xeen_caller_result r);
int begin_event(xeen_game *g);
xeen_slice_navigation_result execute_navigation(xeen_game *g, uint16_t command);
xeen_slice_navigation_result navigation_tail(xeen_game *g, xeen_gameplay_tick tick);
xeen_slice_navigation_result navigation_finish(xeen_game *g, xeen_gameplay_tick tick);
xeen_status damage_scene(xeen_game *g, uint8_t saved);
xeen_status damage_after_scene(xeen_game *g);
xeen_status door_after_scene(xeen_game *g);
xeen_status surface_after_scene(xeen_game *g);

/* game_events.c: the event interpreter's host services (event_service, run_npc_dialogue, the serve_* routines) and
 * the event windows they open
 */
int event_service(void *u, const xeen_event_request *r, uint8_t *ds, size_t n, uint16_t *ax);

/* game_event_dialogue.c: the event's dialogue services: the 3000D NPC dialogue form (run_npc_dialogue and its
 * tails) and the 2F9BA Who-will prompt (serve_who_will_prompt)
 */
int run_npc_dialogue(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n,
                     uint16_t *ax, const uint8_t *name, const uint8_t *text);

/* game_event_windows.c: the event windows (1871B's bookkeeping: windows 12 and 11, the scroll, treasure, dispatch
 * and payment windows, F42C's loads) and the text displays 31804, 315AE, 31633 and 316DC
 */
void page_layout(xeen_game *g, const uint16_t window[4]);
void trace_event_text(unsigned window, const uint8_t *t, size_t n);
void bottom_restore(xeen_game *g);
/* The mirror table's rows (xeenmirr.txt / darkmirr.txt, loaded into the F42C block by 2E9AC): the destination's name
 * at +0 (2E9AC's compare), then its map, x, y and facing (FFh: the party's kept; 3196B). */
#define MIRROR_ROW_BYTES     32u
#define MIRROR_ROW_MAP       0x1cu
#define MIRROR_ROW_X         0x1du
#define MIRROR_ROW_Y         0x1eu
#define MIRROR_ROW_DIRECTION 0x1fu
void f42c_load(xeen_game *g, const uint8_t *b, size_t n);
void dispatch_full_open(xeen_game *g);
void event_payment_open(xeen_game *g);
void event_scroll_open(xeen_game *g);
void interface_text_cursor(xeen_game *g, const char *text);
void treasure_full_window(xeen_game *g);

/* game_event_forms.c: the event operations' host forms: the map's .hed cell, 2F35B's face form, the 2F2FF yes/no,
 * the 2F5B1 choice, the window closes, and operation 7's teleport (3193B)
 */
uint8_t *hed_cell(xeen_game *g);
xeen_status teleport(xeen_game *g);
uint32_t windows_close_all(xeen_game *g);

/* game_event_ui.c: the event UI's presenter (ui_present, ui_poll): the NPC speech and its yes/no form, the Who-will
 * prompt, the treasure window, the NPC pages and the event text
 */
void speech_face_tail(xeen_game *g);
int ui_poll(void *u, xeen_ui_response *r);
int ui_present(void *u, const xeen_ui_request *q);
void face_restore(xeen_game *g);

/* game_modal_poll.c: the modal waits' polls: the yes/no (294A2), 3000D's speech loop, the NPC dialogue and its
 * Who-will prompt, and the modal loop (its scenes, the kept face, the key's owner)
 */
xeen_status poll_confirm(xeen_game *g, xeen_timer_bios bios, void *user);
xeen_status poll_dialogue(xeen_game *g, xeen_timer_bios bios, void *user);
xeen_status poll_modal(xeen_game *g, xeen_timer_bios bios, void *user);
xeen_status poll_speech(xeen_game *g, xeen_timer_bios bios, void *user);
void talk_mouth(xeen_game *g); /* a talking face's next mouth; shared with the location keeper's frames */

/* game_scroll.c: the 18F2F mode-1 scrolls (window 6) by owner: open_scroll, the needs-rest and desert scrolls, the
 * Who-will refusal, and scroll_key, the close that continues the owner
 */
int open_scroll(xeen_game *g, int owner, const char *text, xeen_gameplay_tick tick);
/* A scroll message that interrupts no tick (every owner but the "needs rest" ones, whose TICK the close resumes). */
static inline int open_scroll_message(xeen_game *g, int owner, const char *text)
{
    return open_scroll(g, owner, text, (xeen_gameplay_tick){XSN_INVALID, XT_EXTERNAL, 0, 0, 0});
}
int desert_scroll(xeen_game *g, int caller, xeen_gameplay_tick tick);
xeen_status scroll_key(xeen_game *g);
int who_plain(const xeen_game *g);
int who_refused(xeen_game *g, uint16_t command, xeen_status *status);
int rest_scroll(xeen_game *g, int owner, xeen_gameplay_tick tick);

/* game_confirm.c: the yes/no confirmation 2937A: its layouts, window and buttons, the answer and the owner's
 * continuation
 */
int open_confirm(xeen_game *g, int owner, uint16_t text);
xeen_status confirm_answer(xeen_game *g, int answer);
xeen_status confirm_finish(xeen_game *g);

/* game_commands.c: the exploration commands of 17C10's CS:0EAC table: Space's wait (17DA8), the unlock (170D4) and
 * Bash
 */
xeen_status table_command(xeen_game *g, uint16_t command);
xeen_status unlock_begin(xeen_game *g, uint16_t wall, uint16_t text);
xeen_status unlock_key(xeen_game *g, uint16_t command);
xeen_status bash_after_scene(xeen_game *g);
xeen_status bash_after_tick(xeen_game *g, xeen_gameplay_tick tick);
xeen_status space_after_surface(xeen_game *g, xeen_caller_result next);
xeen_status space_after_tick(xeen_game *g, xeen_gameplay_tick tick);
xeen_status space_wait(xeen_game *g);
xeen_status unlock_after_scene(xeen_game *g);
xeen_status unlock_roll(xeen_game *g, uint16_t member);

/* game_fall.c: the falls (173A0, 17608, operation 52): their begin, scene and callers' continuations, the fall
 * tables and 1BF78's animation
 */
xeen_status fall_animation(xeen_game *g);
xeen_status fall_begin(xeen_game *g);
xeen_status event_fall(xeen_game *g);
xeen_status fall_after_scene(xeen_game *g);
xeen_status fall_event_return(xeen_game *g);

/* game_rest.c: exploration Rest (17E91 -> 28E35): the sleep, the ten steps, the dream, the wake and its 1B503
 * scenes
 */
xeen_status rest(xeen_game *g);
xeen_status rest_scene(xeen_game *g, int stage);
xeen_status rest_after_scene(xeen_game *g);
xeen_status rest_sleep(xeen_game *g);
xeen_status rest_wake_after_surface(xeen_game *g, xeen_caller_result surface);

/* game_shoot.c: Shoot (17EF4): the volley's presentation and charge tail, its tick and surface continuations and
 * the melee prefix
 */
xeen_status charge_tail(xeen_game *g);
void volley_presentation(xeen_game *g);
xeen_status shoot_after_volley(xeen_game *g);
xeen_status shoot(xeen_game *g);
xeen_status shoot_tail(xeen_game *g);
xeen_status shoot_after_surface(xeen_game *g, xeen_caller_result surface);
xeen_status shoot_after_tick(xeen_game *g, xeen_gameplay_tick tick);
xeen_status shoot_melee(xeen_game *g);

/* game_treasure.c: 2EA68's treasure dialog ("The Party Found", its 2F170 loop and keys, the BackPacks Full scroll),
 * the found-item wait 30E05 and the join and combat entries
 */
xeen_status treasure_dialog(xeen_game *g, int owner);
void treasure_window(xeen_game *g);
xeen_status join_treasure(xeen_game *g);
xeen_status combat_treasure(xeen_game *g);
xeen_status item_next(xeen_game *g);
xeen_status treasure_full_closed(xeen_game *g);
xeen_status treasure_key(xeen_game *g, uint16_t command);

/* game_combat_flow.c: the session's fights: their begin (from a contact, a scene or a nested 337D7 loop),
 * xeen_game_resume_combat, the 39430 cleanup's return and the combat route, Rest and Use inside combat
 */
xeen_status combat_route(xeen_game *g, xeen_status status, uint32_t boundary);
xeen_status combat_use_return(xeen_game *g);
xeen_status combat_cast_return(xeen_game *g, int paid);
xeen_status after_combat(xeen_game *g, xeen_status status);
xeen_status combat_use(xeen_game *g);
xeen_status begin_combat(xeen_game *g);
xeen_status combat_rest_closed(xeen_game *g);
void nest_pop(xeen_game *g);

/* game_exit.c: the exit and death scenes' helpers (the drawing, the polls, the saved screen, the event and music
 * records) and 17F28's game over (the book, the death laugh and flames, 1624F's teardown)
 */
void exit_event(xeen_game *g, const char *name);
void exit_draw(xeen_game *g, uint16_t handle, uint16_t frame, int16_t x, int16_t y, uint16_t flags);
void exit_draw_in(xeen_game *g, uint16_t handle, uint16_t frame, int16_t x, int16_t y,
                  uint16_t window);
void button_defaults(xeen_movement *m);
void exit_music_off(xeen_game *g);
uint16_t exit_poll(xeen_game *g);
xeen_status exit_present(xeen_game *g);
void exit_restore(xeen_game *g);
void exit_save(xeen_game *g);
xeen_status book_begin(xeen_game *g);
void keep_song_copy(xeen_game *g, const uint8_t *b, size_t n);
void sink_named(xeen_game *g, uint16_t word, const char *name);

/* game_archive.c: the session's archive and DS-text accessors (the selected CC, 507F0 extraction, 18975 loads,
 * 188F6 raw pictures)
 */
uint8_t *cc_extract(xeen_game *g, unsigned w, const char *name, size_t *size);
/* The text at DS OFFSET: a pointer into the session's DS, never NULL and not bounds-checked (the original's texts are
 * in DS, NUL-terminated). */
const char *ds_text(const xeen_game *g, uint16_t offset);
/* The DS format at OFFSET, for snprintf: returned only when it ends within DS and its conversion specifications, in
 * order, are exactly CONVERSIONS (e.g. "%s%c%03u"; "%%" is text); else NULL, which the caller refuses. The program
 * data's format is the original's; an edited one never reaches snprintf with arguments it does not take. */
const char *ds_format(const xeen_game *g, uint16_t offset, const char *conversions);
/* Copies TEXT and its NUL into DS at OFFSET, unchecked: the caller keeps it within its buffer (the 1,000-byte text
 * buffers A43F, A827 and AC0F, or F5E8's). */
void ds_put_text(xeen_game *g, uint16_t offset, const char *text);
uint8_t *menu_extract(xeen_game *g, const char *name, size_t *size);
uint8_t *menu_load(xeen_game *g, const char *name, size_t *size);
int menu_raw(xeen_game *g, const char *name);
const xeen_archive *selected_cc(const xeen_game *g);
int menu_load_word(xeen_game *g, uint16_t word, const char *name);

/* game_window_text.c: the window text layer (4F6F8 frames, 4E38C text, 4F1BE borders) and the 28876 input line */
void layout_carry(xeen_game *g, const xeen_text_layout *was, int cursor);
void confirm_text(xeen_game *g, const char *text);
void input_cursor_step(xeen_game *g);
void text_cursor_carry(xeen_game *g, const uint16_t inner[4]);
/* menu_window_text's FRAMED: draw the window's frame first (4F6F8; a window not yet open also gets 1871B's font and
 * justification reset), or write the text over what the window already shows. */
enum {
    WINDOW_TEXT_ONLY = 0,
    WINDOW_FRAMED = 1
};
void menu_window_text(xeen_game *g, uint16_t window, int framed, const char *text);
void menu_window_text_on(xeen_game *g, uint16_t window, const char *text);
void input_line(xeen_game *g, uint16_t window, const char *line);
void text_frame_reset(xeen_game *g);

/*
 * game_menu.c: the main menu (1660E), Load Saved Game, the outer loop to the loaded game's 19E82,
 * the menu's sprites, stop and far pointers
 */
xeen_status program_exit(xeen_game *g);
void menu_far(xeen_movement *m, uint16_t word, uint16_t offset);
void button_tables(xeen_movement *m, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t key,
                   uint16_t enabled);
xeen_status menu_set(xeen_game *g, int stage, uint32_t boundary);
xeen_status menu_stop(xeen_game *g, uint32_t boundary);

/* game_menu_load.c: Load Saved Game (the 2AD6D slot dialog and its keys) and the loaded game's path to the 19E82
 * map load (1978C, the 16FAB tail, the outer loop)
 */
void dialog_draw(xeen_game *g, int framed);
int dialog_text(xeen_game *g);
/* Where the save and load dialog (2AD6D: the menu's, the control panel's and the name entry's redraw) stops when it
 * cannot be shown (tools/xeen_replay/stops.json): */
enum {
    DIALOG_STOP_TEXT = 0x2b065,              /* its text could not be formed (an unexpected format, or too long) */
    DIALOG_STOP_FRAME = 0x2b0ee              /* its window could not be presented */
};
int menu_copy(xeen_game *g);
xeen_status outer_restart(xeen_game *g);
void dialog_end(xeen_game *g);
uint32_t dialog_open(xeen_game *g);
xeen_status menu_party(xeen_game *g);
xeen_status outer_world(xeen_game *g);
void dialog_select(xeen_game *g, uint16_t key);

/* game_pairs.c: the save pair files: 19314's CURs, 18363's id scan and 18441's repair (18037's Darkside half,
 * 18200's ids), 184DE's check, continue and answer
 */
xeen_status pair_answer(xeen_game *g, int answer);

/* game_newgame.c: 16A53 Start a New Game (the slot choice, the game preference, the new session) */
uint16_t newgame_draw(void *user, uint16_t low, uint16_t high, uint16_t al);
xeen_status newgame_preference(xeen_game *g);
xeen_status newgame_preferred(xeen_game *g, uint16_t key);

/*
 * game_panel.c: the control panel (297D9): its stages, polls and scene, Save / Save As / Load and
 * the F64A = 2 restart, and the name entry
 */
xeen_status panel_after_scene(xeen_game *g);
xeen_status panel_confirmed(xeen_game *g, int owner, int answer);
xeen_status panel_dialog_done(xeen_game *g, unsigned slot);
xeen_status panel_restart(xeen_game *g);
void panel_under(xeen_game *g, unsigned n, int restore);
/* Mr Wizard's move (game_panel.c) reports SITE when its map load fails: where each caller's load is. */
#define WIZARD_COMBAT_LOAD_SITE 0x395a8u /* after a fight Mr Wizard ended */
#define WIZARD_PANEL_LOAD_SITE  0x29bafu /* from the control panel */
uint32_t wizard_move(xeen_game *g, uint32_t site);
xeen_status panel_set(xeen_game *g, int stage, uint32_t boundary);
void panel_window(xeen_game *g, unsigned n, int open);
/* Closes dialog window N (what it covered restored: panel_window), then pops the dialog's sprites (handle_pop) and its
 * button set, in that order. Original: 18765(N), 190CF, 18FFF. */
void window_pop(xeen_game *g, unsigned n);
xeen_status panel_stop(xeen_game *g, uint32_t boundary);
xeen_status panel_scene(xeen_game *g, int stage);
uint16_t panel_poll(xeen_game *g);
xeen_status panel_begin(xeen_game *g);
unsigned panel_list(xeen_game *g, uint16_t pattern);
xeen_status panel_redraw(xeen_game *g);

/* game_name_entry.c: the 28876 line entry (2894F names, 2E8E5 operation 21's word, its 2E93B mirror and compare)
 * and its poll loop (288BF, 2890C)
 */
xeen_status word_begin(xeen_game *g);
xeen_status name_poll(xeen_game *g);
xeen_status name_begin(xeen_game *g);
xeen_status name_entry_poll(xeen_game *g, xeen_timer_bios bios, void *user);
xeen_status name_entry_tick(xeen_game *g, xeen_timer_bios bios, void *user);

/* game_save.c: the control panel's Save (29502) and 19278's save: the CUR images, the DOS clock and its debug time
 * line, the slot files, the roster and state writes and the event reload
 */
uint32_t save_game(xeen_game *g);
uint32_t save_slot_files(xeen_game *g, uint8_t *active, size_t an, uint8_t *dark, size_t dn,
                         const uint8_t *roster);
uint8_t *cur_copy(const xeen_game *g, unsigned world, size_t *n);
uint32_t cur_owned_sync(xeen_game *g);
uint32_t debug_time_line(xeen_game *g);
xeen_status panel_save(xeen_game *g, enum save_return where);

/* game_location.c: 277E5's locations (the town services' common frame): the keeper's portrait and animation, the
 * voices and music, the buttons and windows, the per-kind setups, the sign-in, the keys and the leave
 */
void location_animate(xeen_game *g, int redraw_border);
xeen_status location_begin(xeen_game *g);
xeen_status location_resume(xeen_game *g, xeen_timer_bios bios, void *user);
xeen_status location_redraw(xeen_game *g);
int location_member_key(xeen_game *g, uint16_t key);
xeen_status location_return(xeen_game *g, uint16_t ax, int moved);
void location_window(xeen_game *g);

/* game_location_frames.c: a location's presentation parts: its buttons and border, its voice and music, its texts,
 * the keeper's portraits and animations by kind, the next frame and its cues
 */
void location_border(xeen_game *g);
void location_voc(xeen_game *g, const char *name);
void location_buttons(xeen_game *g);
void buttons_draw(xeen_game *g, uint16_t xs, uint16_t ys, uint16_t on, uint16_t h, unsigned limit);
void location_voc_load(xeen_game *g, const char *name);
uint16_t location_random(xeen_game *g, uint16_t lo, uint16_t hi);

/* game_inn.c: the inn roster (43E0B) */
xeen_status inn_after_book(xeen_game *g);
xeen_status inn_begin(xeen_game *g);
xeen_status inn_delete_answer(xeen_game *g, int answer);
void inn_compact(xeen_game *g);
void inn_commit(xeen_game *g);
uint32_t inn_save_files(xeen_game *g);

/* game_creation.c: character creation 4063B (from the inn's C): the rolls, classes, text, the dice animation 3FB10,
 * the name and the stat exchange 3F9C4
 */
xeen_status create_named(xeen_game *g);
xeen_status create_begin(xeen_game *g);
xeen_status create_resume(xeen_game *g, xeen_timer_bios bios, void *user);

/*
 * game_bank.c: the bank (277E5 kind 0) and 28ABA's amount entry. Its entries, by caller:
 * - the location loop (game_location.c): the bank's setup, text and keys;
 * - the control panel's stages (game_panel.c): the amount entry's key;
 * - the location messages (game_town.c): the bank's message closed;
 * - the Cast dialog's Teleport and Town Portal (game_cast_travel.c) and the arena (game_arena.c): amount_begin.
 */
xeen_status amount_begin(xeen_game *g);
xeen_status amount_key(xeen_game *g, uint8_t ch);

/*
 * game_guild.c: the guild (277E5 kind 2): membership, its spell list (3F05D), the Cast list built from it, its
 * pages, keys and purchase (the spell_list_sort add-on's hooks). Its entries, by caller:
 * - the location loop (game_location.c): the guild's setup, text and keys;
 * - the control panel's stages (game_panel.c): the purchase's answer;
 * - the Cast dialog (game_cast.c): the list of 3F05D mode 1 (cast_list_build, guild_page, guild_open,
 *   list_member_key);
 * - the character view (game_view.c) and the Cast dialog: spell_name.
 */
xeen_status guild_key(xeen_game *g, uint16_t key);
xeen_status guild_confirmed(xeen_game *g, int answer);
xeen_status guild_open(xeen_game *g);
void guild_page(xeen_game *g);
void guild_scroll(xeen_game *g, uint16_t key);
unsigned cast_list_build(xeen_game *g);
int list_member_key(xeen_game *g, unsigned n);
const char *spell_name(xeen_game *g, unsigned spell);

/* game_shop.c: the blacksmith, the items dialog (44F92), the shared messages and the party's payment */
void driver_exit(xeen_game *g, uint16_t window, uint16_t handle, uint16_t frame);
/*
 * The items dialog's modes (44F92's argument, g->items_mode) as 44F92, 45152, 452EA, 3BAEE's option
 * dispatch and 45BB9's keys read them. ITEMS_COMBAT is the combat Use's argument only: 44F92 runs it as
 * ITEMS_OWN with items_combat set. Modes 3, 5 and 8 are tested by those readers but nothing opens them: no native
 * caller, and none of the original's six direct calls of 44F92, which pass 0 (44DB9), 1 (25699), 4 (3B1ED),
 * 6 (3B46B), 7 (3922A) and 11 (3B8BB); the dialog's B, S, I and F keys switch to 1, 2, 10 and 9. Their meaning is not
 * established, so they stay numbers (an indirect call is not excluded by that scan).
 */
/* Use's item power numbers the dialog tests by number (4478B's table CS:0474: power 28 casts spell 44, Lloyd's
 * Beacon; power 55 spell 71, Town Portal; game_item_options.c use_power). */
enum { USE_POWER_LLOYDS_BEACON = 28, USE_POWER_TOWN_PORTAL = 55 };
enum items_mode {
    ITEMS_OWN = 0,       /* the member's own items (the character view's I) */
    ITEMS_BUY = 1,       /* the blacksmith's wares (BC6A) */
    ITEMS_SELL = 2,      /* the blacksmith's S */
    ITEMS_RECHARGE = 4,  /* the Recharge Item spell */
    ITEMS_ENCHANT = 6,   /* the Enchant Item spell */
    ITEMS_COMBAT = 7,    /* the combat Use (39232) */
    ITEMS_FIX = 9,       /* the blacksmith's F */
    ITEMS_IDENTIFY = 10, /* the blacksmith's I */
    ITEMS_GOLD = 11      /* the Item to Gold spell */
};
/* The dialog passes its mode to xeen_item_cost as the price mode. */
_Static_assert((int)ITEMS_BUY == (int)XEEN_ITEM_COST_BUY && (int)ITEMS_SELL == (int)XEEN_ITEM_COST_SELL &&
                       (int)ITEMS_FIX == (int)XEEN_ITEM_COST_FIX &&
                       (int)ITEMS_IDENTIFY == (int)XEEN_ITEM_COST_IDENTIFY &&
                       (int)ITEMS_GOLD == (int)XEEN_ITEM_COST_TO_GOLD,
               "the items dialog's modes are xeen_item_cost's price modes");
/* The Quests dialog's lists (3E375's [bp-6], g->quests_mode; its keys I, Q and A): the quest items (3E38F, their
 * counts), the current quests (3E58D, XEEN_DS_QUEST_FLAGS) and the auto notes (3E6CC, XEEN_DS_AUTO_NOTES). */
enum quests_list {
    QUESTS_ITEMS = 0,
    QUESTS_CURRENT = 1,
    QUESTS_AUTO_NOTES = 2
};
/* Who opened the items dialog (g->items_owner; 44F92's callers), read by items_end for the return. */
enum items_owner {
    ITEMS_OWNER_NONE = 0,     /* no dialog open */
    ITEMS_OWNER_LOCATION = 1, /* the blacksmith's B (location_after_items) */
    ITEMS_OWNER_VIEW = 2,     /* the character view's I (view_after_items) */
    ITEMS_OWNER_SPELL = 3,    /* an item spell's handler (3B443 etc.; its RETF at 3B48F) */
    ITEMS_OWNER_COMBAT = 4    /* the combat Use (39249) */
};
xeen_status items_begin(xeen_game *g, uint16_t record, int mode, int owner);
xeen_status shop_key(xeen_game *g, uint16_t key);
xeen_status shop_resume(xeen_game *g, xeen_timer_bios bios, void *user);
/* The DS text that entry K of the far-pointer table at TABLE names (4 bytes an entry; only the offset word is read,
 * as a DS offset). */
const char *far_text(const xeen_game *g, uint16_t table, unsigned k);
/* Opens the scroll box (window 6), writes TEXT framed in it and presents it. */
void message_window(xeen_game *g, const char *text);
/* Shows a mode-0 message (contract at the definition, game_shop.c); returns the UI mode it replaced. */
uint8_t message_show(xeen_game *g, const char *text);
/* Closes a mode-0 message: the scroll box closed, the key code cleared and the button set message_show pushed popped.
 * The caller restores the UI mode (message_closed). Original: 18765(6), 189BC (F5FE = 0), 18FFF. */
void message_hide(xeen_game *g);
/* The owner of a mode-0 message (scroll0_open; xeen_game.message_owner), whose close (message_closed) continues it. */
enum message_owner {
    MESSAGE_NONE = 0,
    MESSAGE_ITEM_OPTION = 1,    /* an items-dialog option's refusal: the option ends */
    MESSAGE_EQUIP_REFUSED = 2,  /* Equip's refusal */
    MESSAGE_ITEMS_REFUSED = 3,  /* the items dialog's own refusal (a refused transfer) */
    MESSAGE_PAYMENT = 4,        /* "Not enough ..." inside a payment (its own routing push) */
    MESSAGE_GUILD_PAYMENT = 5,  /* the same for a guild's spell */
    MESSAGE_ITEM_SPELL = 6,     /* an item spell's refusal: its close ends the dialog */
    MESSAGE_VIEW_EXCHANGE = 7,  /* the character view's Exchange in combat: back to the view's loop */
    MESSAGE_CREATION_CLASS = 8, /* creation's C without a class: back to the creation poll */
    MESSAGE_INN = 9,            /* the inn's refusals: back to the inn's poll */
    MESSAGE_EQUIP_WAIT = 10,    /* Equip's own key wait, which keeps the UI mode */
    MESSAGE_PANEL_LOAD = 12,    /* the control panel's Load in combat: back to the panel's scene */
    MESSAGE_PANEL_SAVE = 13     /* its Save in combat: back to the panel's redraw */
};
xeen_status scroll0_open(xeen_game *g, int owner, const char *text);
xeen_status shop_stop(xeen_game *g, uint32_t boundary);
xeen_status items_full(xeen_game *g);
uint8_t *item_at(xeen_game *g, uint16_t record, unsigned cat, unsigned i);
uint32_t item_cost(xeen_game *g, uint16_t record, unsigned i, unsigned mode, unsigned skill, unsigned cat);
uint8_t item_key(xeen_game *g, uint16_t record, unsigned cat, unsigned i);
const char *item_text(xeen_game *g, uint16_t record, unsigned i, unsigned colour, unsigned cat);
xeen_status items_end(xeen_game *g, int close);

/* Whether the map's play is prepared for a command that runs the monsters and the scene: its monsters and sprites
 * loaded and the session's random source bound (Shoot's precondition); play_hosted adds the presentation (Cast,
 * Rest, combat's quick cast). Each caller stops at its own address without them. */
static inline int play_prepared(const xeen_game *g)
{
    return g->monsters_prepared && g->map_sprites_prepared && g->event_random;
}
static inline int play_hosted(const xeen_game *g)
{
    return play_prepared(g) && g->presentation;
}
/* A new copy of the SIZE bytes at BYTES (at least one byte allocated, so an empty copy is not NULL), or NULL without
 * memory. */
static inline uint8_t *bytes_copy(const uint8_t *bytes, size_t size)
{
    uint8_t *copy = malloc(size ? size : 1u);
    if (copy) {
        memcpy(copy, bytes, size);
    }
    return copy;
}
/* Gives world W's CUR image BYTES of SIZE (malloc'd, now the session's) to the session, freeing the one it replaces. */
static inline void cur_replace(xeen_game *g, unsigned w, uint8_t *bytes, size_t size)
{
    free(g->cur[w].bytes);
    g->cur[w].bytes = bytes;
    g->cur[w].size = size;
}

/* game_funds.c: the payments 29281 and their refusal 291FB, for every service that charges */
/* The DS dword holding KIND (enum funds_kind) at WHERE (enum funds_place; any nonzero value counts as the bank, as
 * the original's tests do). */
static inline uint16_t funds_word(unsigned kind, unsigned where)
{
    if (kind) {
        return where ? XEEN_DS_BANK_GEMS : XEEN_DS_PARTY_GEMS;
    }
    return where ? XEEN_DS_BANK_GOLD : XEEN_DS_PARTY_GOLD;
}
void value_refused(xeen_game *g, unsigned kind, unsigned where, int empty);
int value_take(xeen_game *g, unsigned kind, unsigned where, uint32_t amount);
int party_pay(xeen_game *g, uint32_t cost);

/* game_item_options.c: the items dialog's options (3BAEE): which item, equip, buy, sell, fix, identify, discard,
 * remove, the item spells' modes and Use (3BCF7)
 */
xeen_status option_bought(xeen_game *g, int answer);
xeen_status use_after_power(xeen_game *g);

/* game_quests.c: the Quests dialog 3E24F (the quest items, the current quests and the auto notes) */
xeen_status quests_begin(xeen_game *g, enum quests_owner owner);
xeen_status quests_key(xeen_game *g, uint16_t key);
/* Add-on quest_journal's answer at the Quests poll (src/addon/quest_journal/quests_modal.inc): 0 = the
 * dialog's (always while the add-on is off). */
int quests_addon_answer(xeen_game *g, uint16_t key, xeen_status *st);

/* game_view.c: the character view 44AE6 (exploration's F1..F6): its portraits, text and keys, the Exchange 4798C */
void portrait_unhighlight(xeen_game *g);
xeen_status view_command(xeen_game *g, uint16_t key);
void portrait_highlight(xeen_game *g, unsigned n);
xeen_status view_redraw(xeen_game *g);
unsigned view_colour(int value, int maximum);
xeen_status view_begin(xeen_game *g, unsigned member, enum view_owner owner);
int has_award(const uint8_t *ch, uint16_t award);
/* Closes Dismiss's or Exchange's member prompt (window 31 with its esc.icn and buttons: window_pop), puts the
 * portrait-click flag back to SAVED and redraws the party bar. Original: 2B950 / 47B3A: 18765(1Fh), 190CF, 18FFF,
 * 98D6 = SAVED, 2ABB8(1). */
void member_window_close(xeen_game *g, uint8_t saved);

/* game_details.c: a statistic's details (428F1: the skills, the conditions, the age; the character_details add-on's
 * hooks) and the awards list (41581)
 */

/*
 * game_town.c: the training grounds (2547B), the temple (256D9), the location messages and payments
 */
xeen_status train_key(xeen_game *g, uint16_t key);
xeen_status location_message(xeen_game *g, int owner, uint16_t text);

/* game_tavern.c: the tavern's services (25997): drinks, tips, rumours, the food and its answer */
xeen_status tavern_resume(xeen_game *g);

/*
 * game_cast.c: the Cast Spell dialog (3D8C1 in exploration, 391E5 in combat), its stages and
 * polls, the spell costs and payment, the spell handlers (CS:281B), the travel spells' amount
 * entries
 */
xeen_status cast_after_scene(xeen_game *g);
xeen_status cast_command(xeen_game *g);
xeen_status cast_list_key(xeen_game *g, uint16_t key);
xeen_status cast_open(xeen_game *g);
xeen_status cast_scroll_closed(xeen_game *g, int owner);
uint16_t cast_sp(const xeen_game *g, unsigned spell, uint16_t level);
int16_t cast_gems(const xeen_game *g, unsigned spell);
unsigned cast_category(uint8_t cls);
int cast_text_of(xeen_game *g, uint16_t rec);
int cast_pay(xeen_game *g, uint16_t rec, unsigned spell, int refund);
void cast_entry_reset(xeen_game *g);
void cast_engage(xeen_game *g, uint16_t rec, unsigned spell);
xeen_status cast_dispatch(xeen_game *g, unsigned spell);
int exploration_only_spell(unsigned spell);
xeen_status cast_handled(xeen_game *g);
xeen_status cast_resume(xeen_game *g, xeen_timer_bios bios, void *user);
void cast_restore(xeen_game *g);
/* The handlers, prompts and scenes the Cast units call among themselves (game_cast_party.c,
 * game_cast_attack.c, game_cast_travel.c, game_cast_info.c) are in their private game_cast.h. */

/* game_cast_attack.c: the spell volleys (287FF), also fired by a Use item (game_item_options.c) and ended by the
 * treasure (game_treasure.c)
 */
xeen_status cast_volley_done(xeen_game *g);
xeen_status cast_volley(xeen_game *g, uint16_t damage, uint16_t type, uint16_t mode, int fx,
                        unsigned icon, uint32_t site);

/* game_cast_travel.c: the travel spells' amount entries (Teleport's and Town Portal's, answered through the
 * bank's amount line)
 */
xeen_status teleport_amount(xeen_game *g, uint32_t value);
xeen_status portal_amount(xeen_game *g, uint32_t value);

/*
 * game_combat.c: 3914E's modal combat commands (Tab's panel, Info, Options, Quick Reference, the map,
 * Dismiss, the character view, Quick Fight's Cast), and exploration's I, Q, M and D, which share them.
 * Its entries are called by the session core (game.c), the control panel, the character view and the
 * Cast dialog.
 */
xeen_status combat_modal_return(xeen_game *g, uint32_t site, uint16_t ax);
xeen_status combat_modal(xeen_game *g, uint32_t site);
xeen_status combat_ui_after_scene(xeen_game *g);
xeen_status combat_ui_resume(xeen_game *g, xeen_timer_bios bios, void *user);
xeen_status dismiss_begin(xeen_game *g);
xeen_status dismiss_scroll_closed(xeen_game *g);
xeen_status info_begin(xeen_game *g);
xeen_status map_begin(xeen_game *g);
xeen_status qref_begin(xeen_game *g);
xeen_status quick_cast_end(xeen_game *g);

/*
 * game_arena.c: the arena (360C4, location kind 6): its entry from the location loop, its resume, and
 * its amount entry (28ABA's owner 3).
 */
xeen_status arena_resume(xeen_game *g);

/*
 * game_cutscene.c: the location cutscenes' frame loop (34FAA the Reaper, 35992 the gatekeepers, the guards, the
 * sphinx): its resume, its scene return and its poll test (the services and stages: game_cutscene.h)
 */
xeen_status cut_after_scene(xeen_game *g);
int cut_poll_next(const xeen_game *g);
xeen_status cut_resume(xeen_game *g, xeen_timer_bios bios, void *user);

/* game_cut_reaper.c: the Reaper's cutscene (location kind 8, 34FAA): its frame tables, its start and its frame loop */
xeen_status reaper_begin(xeen_game *g);

/* game_cut_gate.c: the town gatekeepers' cutscene (location kinds 10 and 13, 35992): its frame tables, its start
 * and its frame loop
 */
xeen_status gate_begin(xeen_game *g);

/* game_cut_sphinx.c: the sphinx's cutscene: its start, its frames, its end and its frame loop */
xeen_status sphinx_begin(xeen_game *g);

/* game_cut_guard.c: the guards' cutscene: its start, its frames, its end and its frame loop */
xeen_status guard_begin(xeen_game *g);

/* game_ending.c: the endings and the program's exit (33518's tail, 1617E's hand-off teardown) */
int ending_tail(xeen_game *g, uint16_t name_word, uint8_t *ds);
xeen_status handoff_teardown(xeen_game *g);

/*
 * add-on training_preview (src/addon/training_preview/trainer.inc, included by game_location.c): the
 * training key hook
 */
int training_preview_key(xeen_game *g,uint16_t key);

/* add-on spell_list_sort (src/addon/spell_list_sort/glue.inc): the guild list hooks */
void spell_list_sort_page(xeen_game *g);
int spell_list_sort_key(xeen_game *g,uint16_t key,xeen_status *out);
uint16_t spell_list_sort_poll(xeen_game *g);

/* add-ons identify, spell_list_sort (src/addon/slot_files.inc): the ADDONnn.WOX sections */
int addons_items(const xeen_game *g);
int addons_sort(const xeen_game *g);
void addons_slot_load(xeen_game *g,int slot);
void addons_slot_remove(xeen_game *g,int slot);
void addons_slot_save(xeen_game *g,int slot);

#endif
