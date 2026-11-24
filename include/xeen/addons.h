#ifndef XEEN_ADDONS_H
#define XEEN_ADDONS_H
#include <stddef.h>
#include <stdint.h>
#include "xeen/addon/identify.h"
#include "xeen/addon/map_movement.h"
#include "xeen/addon/training_preview.h"
#include "xeen/addon/damage_numbers.h"
#include "xeen/addon/monster_difficulty.h"
#include "xeen/addon/spell_short_description.h"
#include "xeen/addon/mouse_scroll.h"
#include "xeen/addon/town_icons.h"
#include "xeen/addon/character_details.h"
#include "xeen/addon/spell_list_sort.h"
#include "xeen/addon/world_map.h"
#include "xeen/addon/quest_journal.h"
#include "xeen/addon/slot_file.h"
#include "xeen/addon/please_wait_skip.h"
#include "xeen/addon/wizard_eye_plus.h"
/* Owner-approved add-ons: NOT original XEEN.DAT behavior. Each is off by
 * default and enabled only by the command line (--addon NAME..., several
 * names, repeatable) or wox.cfg ([addons] NAME = on). With every add-on off
 * the game is the original.
 *
 * This is the shared registry (src/addon/addons.c): the names, the enabled
 * flags and each add-on's state. Each add-on is its own module:
 *   identify      identified item statistics   xeen/addon/identify.h,     src/addon/identify/
 *   map_movement  a pannable M map  xeen/addon/map_movement.h, src/addon/map_movement/
 *   training_preview  the trainer's level ladder  xeen/addon/training_preview.h,
 *                 src/addon/training_preview/
 *   damage_numbers  damage, healing and misses as numbers
 *                 xeen/addon/damage_numbers.h, src/addon/damage_numbers/;
 *                 damage_numbers.stack selects its stack layout (only
 *                 together with damage_numbers)
 *   monster_difficulty  a difficulty icon over each monster
 *                 xeen/addon/monster_difficulty.h, src/addon/monster_difficulty/
 *   spell_short_description  the Cast dialog's spell box
 *                 xeen/addon/spell_short_description.h,
 *                 src/addon/spell_short_description/
 *   mouse_scroll  the mouse wheel scrolls the lists
 *                 xeen/addon/mouse_scroll.h, src/addon/mouse_scroll/
 *   town_icons    the town places' icons on the M map and the minimap
 * xeen/addon/town_icons.h, src/addon/town_icons/
 *   character_details  what the character view's stats, skills and
 *                 conditions do xeen/addon/character_details.h,
 *                 src/addon/character_details/
 *   spell_list_sort  the spell lists sorted A-Z or by type
 *                 xeen/addon/spell_list_sort.h, src/addon/spell_list_sort/
 *   world_map     the whole world's map from the M map
 *                 xeen/addon/world_map.h, src/addon/world_map/
 *   quest_journal the Quests dialog (V) as a journal: item descriptions,
 *                 quests' and notes' places on the world map
 *                 xeen/addon/quest_journal.h, src/addon/quest_journal/
 *   please_wait_skip  the map load's "Please Wait..." box is not shown (off:
 *                 the original box) xeen/addon/please_wait_skip.h,
 *                 src/addon/please_wait_skip/
 *   wizard_eye_plus  under Wizard Eye with the = automap on, the unseen cells of
 *                 the minimap through a checkerboard (wizard_eye_plus.indoor,
 *                 .outdoor, .sky: 1px, 2px or black) xeen/addon/wizard_eye_plus.h,
 *                 src/addon/wizard_eye_plus/
 * identify and spell_list_sort keep their part of the save slot's
 * ADDONnn.WOX (xeen/addon/slot_file.h, src/addon/slot_file.c and
 * src/addon/slot_files.inc).
 * The original code calls them only through one-line hooks guarded by the
 * enabled flag. */
#define XEEN_ADDON_NAMES                                                                         \
    "identify, map_movement, training_preview, damage_numbers, damage_numbers.stack, "           \
    "monster_difficulty, spell_short_description, mouse_scroll, town_icons, character_details, " \
    "spell_list_sort, world_map, quest_journal, please_wait_skip, wizard_eye_plus"
struct xeen_addons {
    /* identify */
    int item_stats;  /* enabled */
    uint32_t *known; /* the known item types: sorted keys (saved per slot) */
    size_t count, capacity;
    /* map_movement */
    int map_movement;     /* enabled */
    xeen_map_pan map_pan; /* the open map's pan (ephemeral, never saved) */
    /* training_preview */
    int training_preview;           /* enabled */
    xeen_training_preview training; /* the ladder's state (ephemeral, never saved) */
    /* damage_numbers (its state is the module's own: damage_numbers.h) */
    int damage_numbers;       /* enabled */
    int damage_numbers_stack; /* the stack layout (with damage_numbers only) */
    /* monster_difficulty (its state is the module's own: monster_difficulty.h) */
    int monster_difficulty; /* enabled */
    /* spell_short_description (its state is the module's own) */
    int spell_short_description; /* enabled */
    /* mouse_scroll (its state is the module's own) */
    int mouse_scroll; /* enabled */
    /* town_icons (its state is the module's own) */
    int town_icons; /* enabled */
    /* character_details (its state is the module's own) */
    int character_details; /* enabled */
    /* spell_list_sort */
    int spell_list_sort; /* enabled */
    xeen_spell_list_sort
            sort; /* the characters' setting (saved per slot) and the open list's view */
    /* world_map (its state is the module's own) */
    int world_map; /* enabled */
    /* quest_journal (its state is the module's own) */
    int quest_journal; /* enabled */
    /* please_wait_skip (its flag is the module's own: please_wait_skip.h) */
    int please_wait_skip; /* enabled */
    /* wizard_eye_plus (its state is the module's own: wizard_eye_plus.h) */
    int wizard_eye_plus;                         /* enabled */
    uint8_t wizard_eye_plus_style[XEEN_WEP_KINDS]; /* indoor, outdoor, sky: XEEN_WEP_* (0, black, by default) */
    /* The host's mouse button state for map_movement's drag: nonzero while a
     * button is held (NULL: no drag, as headless). */
    int (*button_held)(void *);
    void *button_user;
};

/* "identify", "map_movement", "training_preview", "damage_numbers",
 * "damage_numbers.stack", "monster_difficulty", "spell_short_description"
 * "mouse_scroll", "town_icons", "character_details", "spell_list_sort", "world_map", "quest_journal" or
 * "please_wait_skip" or "wizard_eye_plus" -> sets the flag; 0 for an unknown name. */
int xeen_addons_enable(xeen_addons *, const char *name);
void xeen_addons_free(xeen_addons *);
#endif
