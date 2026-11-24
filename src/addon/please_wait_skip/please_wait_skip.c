/* please_wait_skip (Phase 310 add-on, NOT original behavior): the map load's "Please Wait..." box is never shown
 * (include/xeen/addon/please_wait_skip.h). The box is drawn as the original draws it and its pixels are put back
 * before the flip; nothing else changes. Self-contained: no other add-on, no bug fix. */
#include <string.h>
#include "xeen/addon/please_wait_skip.h"
#include "xeen/ds_texts.h"

int xeen_please_wait_skip_on;

int xeen_please_wait_skip_hides(uint16_t text)
{
    return xeen_please_wait_skip_on && text == XEEN_TEXT_PLEASE_WAIT;
}
static int rect_ok(size_t size, const uint16_t r[4])
{
    return r && r[0] + (size_t)r[2] <= 320u && r[1] + (size_t)r[3] <= 200u && 64000u <= size;
}
int xeen_please_wait_skip_save(const uint8_t *framebuffer, size_t size, const uint16_t r[4], uint8_t *saved,
                               size_t saved_size)
{
    unsigned y;
    if (!framebuffer || !saved || !rect_ok(size, r) || (size_t)r[2] * r[3] > saved_size) {
        return 0;
    }
    for (y = 0; y < r[3]; ++y) {
        memcpy(saved + (size_t)y * r[2], framebuffer + (size_t)(r[1] + y) * 320u + r[0], r[2]);
    }
    return 1;
}
void xeen_please_wait_skip_restore(uint8_t *framebuffer, size_t size, const uint16_t r[4], const uint8_t *saved)
{
    unsigned y;
    if (!framebuffer || !saved || !rect_ok(size, r)) {
        return;
    }
    for (y = 0; y < r[3]; ++y) {
        memcpy(framebuffer + (size_t)(r[1] + y) * 320u + r[0], saved + (size_t)y * r[2], r[2]);
    }
}
