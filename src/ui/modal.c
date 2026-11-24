#include "xeen/modal.h"
#include "xeen/ds_globals.h"
#include "xeen/event_dispatch.h"
#include "xeen/presentation.h"
/** Starts a modal dialog: saves the DS state the modal replaces and routes the keys to SEGMENT. */
xeen_status xeen_modal_begin(xeen_modal *c, xeen_movement *m, uint16_t segment)
{
    if (!c || !m || c->active) {
        return XEEN_BAD_INPUT;
    }
    xeen_button_stack(m->ds, XEEN_BUTTON_PUSH, segment);
    c->mode = m->ds[XEEN_DS_UI_MODE];
    c->faces = m->ds[XEEN_DS_PORTRAIT_CLICKS];
    c->active = 1;
    m->ds[XEEN_DS_UI_MODE] = (uint8_t)(c->mode == XEEN_UI_MODE_TREASURE ? XEEN_UI_MODE_TREASURE : 8);
    m->ds[XEEN_DS_PORTRAIT_CLICKS] = 0;
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    return XEEN_OK;
}
/** Ends a modal dialog: restores the DS state saved at its start. */
xeen_status xeen_modal_finish(xeen_modal *c, xeen_movement *m, uint16_t segment)
{
    if (!c || !m || !c->active) {
        return XEEN_BAD_INPUT;
    }
    m->ds[XEEN_DS_CLAIRVOYANCE_RIGHT_STATE] = XEEN_CLAIRVOYANCE_STILL;
    m->ds[XEEN_DS_CLAIRVOYANCE_LEFT_STATE] = XEEN_CLAIRVOYANCE_STILL;
    xeen_button_stack(m->ds, XEEN_BUTTON_POP, segment);
    m->ds[XEEN_DS_PORTRAIT_CLICKS] = c->faces;
    m->ds[XEEN_DS_UI_MODE] = c->mode;
    m->ds[XEEN_DS_YESNO_BUTTON_Y] = XEEN_YESNO_BUTTON_ROW;
    m->ds[XEEN_DS_YESNO_BUTTON_Y + 1u] = XEEN_YESNO_BUTTON_ROW;
    c->active = 0;
    return XEEN_OK;
}
void xeen_window_open(xeen_movement *m, unsigned n)
{
    uint16_t top;
    xeen_presentation_trace("open %u", n); /* Evidence: each 1871B */
    if (!m || n >= 48u || m->ds[XEEN_DS_WINDOW_OPEN + n]) {
        return;
    }
    ++m->ds[XEEN_DS_WINDOW_OPEN + n];
    top = (uint16_t)(xeen_read16(m, XEEN_DS_WINDOW_DEPTH) + 1u);
    xeen_write16(m, XEEN_DS_WINDOW_DEPTH, top);
    m->ds[(uint16_t)(XEEN_DS_WINDOW_STACK + top)] = (uint8_t)n;
}
void xeen_window_close_all(xeen_movement *m)
{
    int slot;
    if (!m) {
        return;
    }
    for (slot = 0x28; slot >= 0; --slot) {
        uint8_t n = m->ds[XEEN_DS_WINDOW_STACK + (unsigned)slot];
        if (m->ds[(uint16_t)(XEEN_DS_WINDOW_OPEN + n)]) {
            m->ds[(uint16_t)(XEEN_DS_WINDOW_OPEN + n)] = 0;
        }
    }
    xeen_write16(m, XEEN_DS_WINDOW_DEPTH, 0xffffu);
}
void xeen_window_close(xeen_movement *m, unsigned n)
{
    xeen_presentation_trace("close %u", n); /* Evidence: each 18765 */
    if (!m || n >= 48u || !m->ds[XEEN_DS_WINDOW_OPEN + n]) {
        return;
    }
    m->ds[XEEN_DS_WINDOW_OPEN + n] = 0;
    xeen_write16(m, XEEN_DS_WINDOW_DEPTH, (uint16_t)(xeen_read16(m, XEEN_DS_WINDOW_DEPTH) - 1u));
}
