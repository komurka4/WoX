/*
 * The event interpreter's dialogue services (Phase 248, from game_events.c): the NPC dialogue form 3000D
 * (run_npc_dialogue: its pages and its five tails, 303E3, the speech confirm, the speech end, 304EF's page and
 * 30513) and the 2F9BA Who-will prompt (its verb, title, window and tail). event_service_inner dispatches to them;
 * their presentation is game_event_ui.c's.
 */
#include "game_event.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The yes/no buttons back on their row (1C26/1C27 = 4Bh), where the talk forms moved them. */
static void yesno_buttons_home(uint8_t *ds)
{
    ds[XEEN_DS_YESNO_BUTTON_Y] = ds[XEEN_DS_YESNO_BUTTON_Y + 1u] = XEEN_YESNO_BUTTON_ROW;
}
/* The end of a mode 2 dialogue (the script goes on talking): the CD voice started, the faces still, the dialogue's
 * button set popped, the yes/no buttons home; answer 0. The face and its window (11, or 40 with Text off) stay for
 * the script; the dispatcher frees them. Original: 303E3: 483C7, F466 = F467 = 2, 18FFF, 1C26/1C27 = 4Bh, AX 0
 * (the face 8D56/8D58, window 40 with F384 set). */
static int npc_mode2_tail(xeen_game *g, uint8_t *ds, uint16_t *ax)
{
    cd_voice_start(g);
    clairvoyance_still(ds);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    yesno_buttons_home(ds);
    *ax = 0;
    return 1;
}
/*
 * The end of a spoken (Text off) mode 0 dialogue, the question form: the answer is 1 for Y, else 0. The faces stop,
 * the yes/no's sprites go, the face slots are cleared and the face FACE_NUMBER and frame.fac loaded again into new
 * handles (the dispatcher frees them later), the yes/no's windows restored, the button set popped, the CD stopped and
 * the yes/no buttons home. Window 40 stays open. Returns 1, or -1 when a face file cannot be loaded.
 * Original: 30538..305E1: F466 = F467 = 2; 190CF (confirm.icn); 13FE7(8D56, 0, 10h) without 4AA3E; 30560: F5E8 =
 * the face's name, 8D56 0; 28D80; 18FFF; 4A1B7; 483B4; 1C26/1C27 = 4Bh; the dispatcher's 33AE9.
 */
static int npc_speech_confirm_tail(xeen_game *g, uint8_t *ds, unsigned face_number, uint16_t *ax)
{
    uint16_t face = 0, frame = 0;
    char name[16];
    uint16_t answer = (uint16_t)(g->ui.last_input == 'Y');
    clairvoyance_still(ds);
    npc_confirm_end(g);
    memset(ds + XEEN_DS_ANIMATION_SPRITES, 0, 16);
    if (snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_FACE_SPRITES_FORMAT), face_number) < 0) {
        return -1;
    }
    memcpy(ds + XEEN_DS_FILE_NAME, name, strlen(name) + 1u);
    /* 30580/30591 are 18931 loads before 305D4's stop: each
     * runs 47B7D and the FB06 replay (sprite_load) while the voice plays. */
    if (!sprite_load(g, name, &face)) {
        return -1;
    }
    xeen_write16(&g->movement, XEEN_DS_ANIMATION_SPRITES, face);
    if (!sprite_load(g, ds_text(g, XEEN_TEXT_FACE_FRAME_SPRITES), &frame)) {
        return -1;
    }
    xeen_write16(&g->movement, XEEN_DS_ANIMATION_SPRITE(1), frame);
    npc_confirm_restore(g);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    xeen_native_audio_cd_stop(&g->audio); /* 305D4: 483B4 */
    yesno_buttons_home(ds);
    g->speech_wait = SPEECH_NONE;
    g->modal_poll_pending = MODAL_POLL_START;
    *ax = answer;
    return 1;
}
/*
 * The end of a spoken (Text off) dialogue without a question (its last page, or mode 1): the faces stop, the button
 * set is popped, the CD stopped and the yes/no buttons home; answer 0. The face stays loaded (the dispatcher frees
 * it) and window 40 open.
 * Original: 304EF with no rest, 3050F mode 1: F466 = F467 = 2; 30560 (8D56 set: no load, 3059E's clear skipped);
 * 18FFF; 4A1B7 (the handle's scripts unchanged since the dispatch's restore); 483B4; 1C26/1C27 = 4Bh; AX 0.
 */
static int npc_speech_tail(xeen_game *g, uint8_t *ds, uint16_t *ax)
{
    clairvoyance_still(ds);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    xeen_native_audio_cd_stop(&g->audio); /* 305D4: 483B4 */
    yesno_buttons_home(ds);
    g->speech_wait = SPEECH_NONE;
    g->modal_poll_pending = MODAL_POLL_START;
    *ax = 0;
    return 1;
}
/* An NPC's speech goes on to its next page: the button set popped, the text left after the shown page put under the
 * speaker's name heading as the new text, and that page served (counted). Returns the service's result, or -1 when
 * the page does not fit. Original: 304EF: 18FFF; 30109: DS:6278 of the NAME and AC0F + rest. */
static int npc_next_page(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n, uint16_t *ax,
                         const uint8_t *name, const uint8_t *text)
{
    uint8_t page[XEEN_TEXT_BUFFER_BYTES];
    size_t old = g->npc_rest, length = 0;
    /* the layout's head with the name; its second %s, the rest of the text, is appended below within the page */
    int head = snprintf((char *)page, sizeof page, ds_text(g, XEEN_TEXT_NPC_PAGE_LAYOUT), (const char *)name, "");
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    while (old + length < XEEN_TEXT_BUFFER_BYTES - 1u && ds[XEEN_DS_TEXT_BUFFER_2 + old + length]) {
        ++length;
    }
    if (head < 0 || (size_t)head + length + 1u > sizeof page) {
        return -1;
    }
    memcpy(page + (size_t)head, ds + XEEN_DS_TEXT_BUFFER_2 + old, length);
    page[(size_t)head + length] = 0;
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, page, (size_t)head + length + 1u);
    g->npc_source = old;
    g->npc_page++;
    g->dialogue_waiting = 0;
    g->modal_poll_pending = MODAL_POLL_START;
    return xeen_event_ui_service_texts(&g->ui, r, ds, n, ax, name, text);
}
/*
 * The end of a written dialogue (modes 0 and 1): the answer is 1 for Y in mode 0 (the question; only Y, N and Esc
 * reach its last page), else 0. The faces stop; mode 0 lets the yes/no's sprites go; the face slots are cleared
 * without freeing them (a later free below them does); mode 0 restores the yes/no's windows; then the button set is
 * popped, the CD stopped, the yes/no buttons home and the dialogue's paging reset.
 * Original: 30513..30536; 30544 190CF (confirm.icn), 28D80 (yesno_end's windows 26h and 0Ch and the icons), 18FFF;
 * 3059E's clear, 483B4, 1C26/1C27 = 4Bh.
 */
static void npc_dialogue_tail(xeen_game *g, const xeen_event_request *r, uint8_t *ds, uint16_t *ax)
{
    uint16_t answer = (uint16_t)(!r->second_argument && g->ui.last_input == 'Y');
    clairvoyance_still(ds);
    if (!r->second_argument) {
        npc_confirm_end(g);
    }
    /* 3059E clears 8D56..8D65 without 4AA3E: the face and
     * frame keep their driver handles until a later free below them
     * (the next NPC's load gets the following ones). */
    memset(ds + XEEN_DS_ANIMATION_SPRITES, 0, 16);
    if (!r->second_argument) {
        npc_confirm_restore(g); /* 305B2: 28D80 */
    }
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    xeen_native_audio_cd_stop(&g->audio); /* 305D4: 483B4 */
    yesno_buttons_home(ds);
    g->dialogue_waiting = 0;
    g->modal_poll_pending = MODAL_POLL_START;
    g->npc_page = 0;
    *ax = answer;
}
/*
 * Runs the NPC dialogue form 3000D(face, name, text, mode) for an event (operation 5 and the
 * others: the name in A827, the text in A43F) and for the arena 360C4 (DS:6426's name, its
 * texts): the service, its pages and its tail. Returns 0 for a mode above 2 (not served here),
 * -1 when a face file cannot be loaded, otherwise the UI service's result (1: handled).
 */
int run_npc_dialogue(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n,
                            uint16_t *ax, const uint8_t *name, const uint8_t *text)
{
    int handled;
    if (r->second_argument > 2u) {
        return 0;
    }
    if (!g->ui.pending) {
        xeen_presentation_trace_extra("npcmark npc"); /* Evidence: 3000D's entry */
    }
    handled = xeen_event_ui_service_texts(&g->ui, r, ds, n, ax, name, text);
    if (handled == 1 && r->second_argument == 2) {
        return npc_mode2_tail(g, ds, ax);
    }
    if (handled == 1 && ds[XEEN_DS_TEXT_OFF] && !r->second_argument) {
        return npc_speech_confirm_tail(g, ds, (unsigned)r->argument, ax);
    }
    if (handled == 1 && ds[XEEN_DS_TEXT_OFF]) {
        return npc_speech_tail(g, ds, ax);
    }
    while (handled == 1 && g->npc_rest != XEEN_TEXT_LAYOUT_END) {
        handled = npc_next_page(g, r, ds, n, ax, name, text);
    }
    if (handled == 1) {
        npc_dialogue_tail(g, r, ds, ax);
    }
    return handled;
}
/* The verb DS:[29BC + 4 x INDEX] (32 entries: "search" .. "push", "rub", "pick", "eat", "sign", "close",
 * "look", "try") into VERB (32 bytes). Returns 0, or -1 when it does not fit. */
static int who_will_verb(xeen_game *g, const uint8_t *ds, unsigned index, char verb[32])
{
    uint16_t at = xeen_read16(&g->movement, (uint16_t)(XEEN_DS_WHO_WILL_VERBS + 4u * index));
    size_t k = 0;
    while (k < 31u && ds[(uint16_t)(at + k)]) {
        verb[k] = (char)ds[(uint16_t)(at + k)];
        ++k;
    }
    if (ds[(uint16_t)(at + k)]) {
        return -1;
    }
    verb[k] = 0;
    return 0;
}
/*
 * The prompt's title into TITLE (1000 bytes): with the request's destination set, 15616's copy of the
 * record's text SECOND (2F9EF); mode 0 (2FA2C, 170D4's doors and grates) DS:[29AC + 4 x SECOND] ("Open
 * Grate", "Open Door", ...), with no 15616 copy: A827 keeps its text. Returns 1, 0 for a mode 0 text above
 * 3 (not served), -1 when the text does not fit.
 */
static int who_will_title(xeen_game *g, const xeen_event_request *r, const uint8_t *ds, uint8_t title[1000])
{
    if (r->destination) {
        if (xeen_event_text_copy(&g->resources.texts, r->second_argument, title, XEEN_TEXT_BUFFER_BYTES, 0, XEEN_TEXT_BUFFER_BYTES) != XE_OK) {
            return -1;
        }
    } else {
        /* 2FA2C, mode 0 (170D4's doors and grates): the title
         * is DS:[29AC + 4 x text] ("Open Grate", "Open Door", ...), no
         * 15616 copy: A827 keeps its text. */
        uint16_t at = xeen_read16(&g->movement, (uint16_t)(XEEN_DS_WHO_WILL_TITLES + 4u * r->second_argument));
        size_t k = 0;
        if (r->second_argument > 3u) {
            return 0;
        }
        while (k < XEEN_TEXT_BUFFER_BYTES - 1u && ds[(uint16_t)(at + k)]) {
            title[k] = ds[(uint16_t)(at + k)];
            ++k;
        }
        title[k] = 0;
    }
    return memchr(title, 0, 1000) ? 1 : -1;
}
/*
 * Opens the Who-will prompt's window: the button set pushed (with its default handlers), the event text and bottom
 * windows closed with their pixels back, the prompt's window opened, and its faces taken from the party cell's .hed
 * bytes. The UI mode, the portrait clicks and the key are left as they are (this is not the event yes/no). Returns 0,
 * or -1 when a restoration or the .hed fails.
 * Original: 2F9CF: 190ED, 1919B, 18765(26h), 18765(0Ch); 2FA61 1871B(24h); 2FA7F F467/F466 (not 2F2FF).
 */
static int who_will_open(xeen_game *g, uint8_t *ds)
{
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!window_close_restore(g, XEEN_WINDOW_EVENT_TEXT) || !window_close_restore(g, XEEN_WINDOW_BOTTOM)) {
        return -1;
    }
    xeen_window_open(&g->movement, XEEN_WINDOW_WHO_WILL);
    if (!hed_faces(g)) {
        return -1;
    }
    return 0;
}
/* The end of the Who-will prompt with the answer AX: a member chosen (nonzero) is remembered as the next treasure's
 * first recipient; the faces stop, the routing's button set is popped, window 36 closed and its pixels put back,
 * the CD voice stopped, and the modal poll starts over. Returns 1, or -1 when the window cannot be presented.
 * Original: 2FC17 (2FC09..2FC12 keep a nonzero AX): F466 = F467 = 2, 18FFF, 18765(24h), 483B4. */
static int who_will_tail(xeen_game *g, uint8_t *ds, uint16_t ax)
{
    clairvoyance_still(ds);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    if (ax) {
        g->who_will_member = (uint8_t)ax;
    }
    xeen_window_close(&g->movement, XEEN_WINDOW_WHO_WILL);
    xeen_native_audio_cd_stop(&g->audio); /* 2FC2C: 483B4 */
    for (unsigned row = 0; row < 80u; ++row) {
        memcpy(g->presentation->framebuffer + (93u + row) * XEEN_PRESENT_WIDTH + 225u, g->who_saved + row * 94u, 94);
    }
    if (xeen_presentation_update_window(g->presentation, XEEN_WINDOW_WHO_WILL) != XEEN_OK) {
        return -1;
    }
    g->who_wait = 0;
    g->modal_scene_ready = 0;
    g->modal_poll_pending = MODAL_POLL_START;
    g->who_repoll = 0;
    return 1;
}
/*
 * A script's "Who will VERB?" prompt, which picks the acting member: the record's text is the title; the prompt (the
 * verb's line) goes to its window through the text buffer, and the title is put back afterwards (every verb; mode 0
 * puts the buffer's own text back). Returns 0 for an argument shape it does not serve, -1 when the verb or the
 * title fails, else the prompt's service result. Original: 2F9BA(verb, text, 1): 15616 into A827 (2F9EF); DS:623F
 * with the verb of DS:29BC into a stack buffer.
 */
int serve_who_will_prompt(xeen_game *g, const xeen_event_request *r, uint8_t *ds, size_t n,
                                 uint16_t *ax)
{
    int handled, title_status;
    char verb[32];
    uint8_t title[1000], kept[1000];
    if (r->argument > 31u || r->second_argument > 255u || r->destination > 1u || r->count) {
        return 0;
    }
    if (who_will_verb(g, ds, r->argument, verb) < 0) {
        return -1;
    }
    memcpy(kept, ds + XEEN_DS_TEXT_BUFFER, sizeof kept);
    title_status = who_will_title(g, r, ds, title);
    if (title_status <= 0) {
        return title_status;
    }
    if (!g->ui.pending && who_will_open(g, ds) < 0) {
        return -1;
    }
    {
        int written =
                snprintf((char *)ds + XEEN_DS_TEXT_BUFFER, XEEN_TEXT_BUFFER_BYTES,
                         "\n\r\003c%s\n\nWho will\n%s?\n\v055F1 - F%u",
                         (char *)title, verb, (unsigned)ds[XEEN_DS_PARTY_COUNT]);
        if (written < 0 || written >= (int)XEEN_TEXT_BUFFER_BYTES) {
            return -1;
        }
    }
    handled = xeen_event_ui_service(&g->ui, r, ds, n, ax);
    memcpy(ds + XEEN_DS_TEXT_BUFFER, r->destination ? title : kept, sizeof title);
    if (handled == 1) {
        return who_will_tail(g, ds, *ax);
    }
    return handled;
}
