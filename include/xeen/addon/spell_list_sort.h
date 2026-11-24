#ifndef XEEN_ADDON_SPELL_LIST_SORT_H
#define XEEN_ADDON_SPELL_LIST_SORT_H
#include <stddef.h>
#include <stdint.h>
/* Owner-approved add-on spell_list_sort (Phase 211; NOT original XEEN.DAT
 * behavior, off unless --addon spell_list_sort or wox.cfg [addons]
 * spell_list_sort = on): the spell lists (3F05D, window 19h: the Cast
 * dialog's New list, the guild's Buy Spells and Spell Info) get a button
 * (key O) above the list that switches between
 *   A-Z   the original list, unchanged (the class lists DS:201E are
 *         alphabetical);
 *   Type  the spells grouped in seven groups (Healing, Attack, Control,
 *         Protection, Travel, Detection, Utility), each under a centred
 *         "- Group -" line, in the owner's order within the group
 *         (order.inc); Attack spells carry right-aligned dots before the
 *         cost: one = one monster, two = a group, three = every monster
 *         (281A7's 972C).
 * The setting belongs to the character (its roster index, checked against
 * its name), is kept in the save slot's ADDONnn.WOX (xeen/addon/slot_file.h)
 * by Save, read by Load, and cleared by a new game.
 *
 * In Type the digits beside the lines are their page positions, as in the
 * original (a click on a line is that digit, through the game's own
 * buttons); a group line has no digit and does nothing. A chosen spell is
 * handed to the original handler, so choosing, casting, buying and Spell
 * Info are the original's. Presentation only: the DS, the saves, the RNG and
 * the clock are the original's.
 *
 * Code: src/addon/spell_list_sort/ (spell_list_sort.c the groups, rows,
 * line text and the setting; order.inc the order; glue.inc the game's part,
 * included by src/game/game.c). It depends on no other add-on and no bug fix. */

enum { XEEN_SLS_GROUPS=7, XEEN_SLS_ROSTER=30, XEEN_SLS_NAME=16, XEEN_SLS_ROWS=48 };
typedef struct {
    uint8_t mode[XEEN_SLS_ROSTER];              /* per roster character: 0 A-Z, 1 Type */
    char name[XEEN_SLS_ROSTER][XEEN_SLS_NAME];  /* its name when the mode was set */
    /* The open list's view (ephemeral, never saved). */
    unsigned top; uint16_t record; int busy;
} xeen_spell_list_sort;

/* "Healing" .. "Utility". */
const char *xeen_sls_group_name(unsigned group);
/* The rows of a list of count lines (line i is spell[i]): i for a line,
 * -1 - g for group g's heading. Returns the row count (at most cap). */
unsigned xeen_sls_rows(const uint8_t *spell,unsigned count,int16_t *rows,unsigned cap);
/* A list line ("\3l<name>\3r\t000<cost>") for Type: the spell's target dots
 * inserted before the cost, right-aligned at x 144 (window 25h's text from
 * x 35), dark red, then colour back to `colour`. 0 when it does not fit. */
int xeen_sls_line(const char *line,unsigned spell,unsigned colour,char *out,size_t n);

/* The character's mode (0 unless set for this roster index and name). */
int xeen_sls_mode(const xeen_spell_list_sort *,unsigned roster,const char *name);
void xeen_sls_set_mode(xeen_spell_list_sort *,unsigned roster,const char *name,int mode);
void xeen_sls_clear(xeen_spell_list_sort *);
/* The ADDONnn.WOX section "SLST": version 1 (u8), count (u8), then per
 * character in Type: roster (u8), mode (u8), name (16 bytes). NULL with
 * size 0 when no character is in Type. */
uint8_t *xeen_sls_serialize(const xeen_spell_list_sort *,size_t *size);
/* Replaces the settings; 0 (and cleared) for a malformed section. */
int xeen_sls_deserialize(xeen_spell_list_sort *,const uint8_t *,size_t);
#endif
