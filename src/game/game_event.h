/*
 * The event services' private interface (Phase 249): the windows, forms and prompts that only the event units call
 * among themselves: the service dispatcher (game_events.c), its windows and text displays (game_event_windows.c),
 * the host forms (game_event_forms.c), the UI presenter (game_event_ui.c) and the dialogue services
 * (game_event_dialogue.c). Only these units include it. The session sees the dispatcher, the presenter, the
 * treasure and dispatch windows and the forms its owners open (game_internal.h).
 */
#ifndef XEEN_GAME_EVENT_H
#define XEEN_GAME_EVENT_H
#include "game_internal.h"

/* game_event_dialogue.c: the 2F9BA Who-will prompt's service */
int serve_who_will_prompt(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n, uint16_t *ax);

/* game_event_windows.c: 1871B's window push, the bottom windows' close, the map's mob header and the text displays
 * 31804, 315AE, 31633 and 316DC */
int window_push(xeen_movement *movement, uint8_t *ds, unsigned n);
void bottom_close(xeen_game *g);
int map_mob_header(xeen_game *g, uint8_t out[48]);
int serve_map_text(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n, uint16_t *ax);
int serve_paged_text(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n, uint16_t *ax);
int serve_stat_line_window(xeen_game *g, const xeen_event_request *r, uint8_t *ds);
int serve_two_lines_window(xeen_game *g, const xeen_event_request *r, uint8_t *ds);

/* game_event_forms.c: the 2F5B1 choice, 2F35B's face form, the .hed faces, the window closes, 28D80's restores and
 * the 2F2FF yes/no */
int choice_begin(xeen_game *g, unsigned n);
void choice_end(xeen_game *g);
void face_form(xeen_game *g, const uint8_t *text);
int hed_faces(xeen_game *g);
int window_close_restore(xeen_game *g, unsigned w);
void restore_after_dialog(xeen_game *g);
void yesno_buttons_install(xeen_movement *m);
int yesno_begin(xeen_game *g);
void yesno_end(xeen_game *g);

/* game_event_ui.c: the NPC yes/no form's end and restores; window 40 (the speech page) */
void npc_confirm_end(xeen_game *g);
void npc_confirm_restore(xeen_game *g);
extern const uint16_t window40[4]; /* window 40 (226, 0, 93 x 100) */

#endif
