/* character_details (Phase 208 add-on, NOT original behavior): the texts.
 * Internal to src/addon/character_details/. */
#ifndef XEEN_ADDON_CHARACTER_DETAILS_TEXTS_H
#define XEEN_ADDON_CHARACTER_DETAILS_TEXTS_H
#include <stddef.h>
#include <stdint.h>
#include "xeen/combat.h"

/* Colours (the 4E38C colour indices, the text table's 0..39). */
#define CD_HIGHLIGHT 15u   /* the original lists' highlight (the Cast dialog) */
#define CD_RULE_BLUE 13u   /* the explanation lines, the "how it ends" lines */
#define CD_PERSONAL 16u    /* this character's numbers */

/* Stat popups 0..9 (Might .. Armor Class): the explanation for the member at
 * RECORD (plain text, the popup's own line breaks are the layout's). 0 for a
 * stat that gets none. */
int cd_stat_text(char *out,size_t n,const xeen_movement *m,uint16_t record,unsigned stat);
/* Skill 0..17 (+27h + n): the detail window's whole text (title, rule,
 * party rule, this member's line), ending in the colour index COLOUR. */
int cd_skill_text(char *out,size_t n,const xeen_movement *m,uint16_t record,unsigned skill,unsigned colour);
/* Condition 0..15 (+143h + n) or party effect 20..23 (ED A3..A6): the
 * detail window's whole text, ending in the colour index COLOUR. */
int cd_condition_text(char *out,size_t n,const xeen_movement *m,uint16_t record,unsigned id,unsigned colour);
#endif
