#ifndef XEEN_BUGFIX_QUEST_UP_DOWN_H
#define XEEN_BUGFIX_QUEST_UP_DOWN_H
#include <stdint.h>
/* Bug fix quest_up_down (Phase 269; owner-approved, NOT original behavior, off by default).
 * The Quests dialog (V) labels its buttons "Up" and "Down" with U and D highlighted, but the original's key table
 * (3E8AC over CS:072E = 3E96E: Esc, A, I, Q, Home, End, PgUp, PgDn, F2h, F3h) and its buttons (DS 1E50: I, Q, A,
 * F2h, F3h, Esc) take only the arrows: a typed U or D just redraws the list. With this bug fix on, U and D in that
 * dialog are the Up and Down arrows. The flag is set by the registry (src/bugfix/bugfixes.c). Evidence:
 * docs/phases/PHASE269_GAMEPLAY_BUGFIX_CUTSCENES_KEYBOARD_QUEST_WARNINGS.md. */
extern int xeen_bugfix_quest_up_down_on;
/* The Quests dialog's key KEY with the bug fix: 'U' the Up arrow (F2h), 'D' the Down arrow (F3h); any other key, or
 * the bug fix off, KEY itself. */
uint16_t xeen_bugfix_quest_key(uint16_t key);
#endif
