/*
 * Text window entries used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/ui/text_window.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "xeen/text_window.h"
#include "xeen/byte_order.h"
#include <string.h>

/** Shows TEXT in dialogue window 11 (8,8 216x132, 8-pixel border) with a fresh layout. */
xeen_status xeen_dialogue_show(xeen_dialogue_window *w, const xeen_text_window *font,
                               xeen_presentation *p, const uint8_t *text, size_t size)
{
    static const uint16_t window11[4] = {8, 8, 216, 132};
    xeen_text_layout s;
    xeen_text_layout_init(&s, window11, 8);
    return xeen_dialogue_show_layout(w, font, p, text, size, &s, NULL);
}
