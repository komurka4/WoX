#ifndef XEEN_MODAL_H
#define XEEN_MODAL_H
#include "xeen/movement.h"
/* Original 2F2FF nonzero-type (any-key) state transitions. Presentation,
 * scene/timer/input continuations remain owned by game.c. No mouse/key,
 * scene, or CD-driver service is silently executed by these helpers. */
typedef struct {
    uint8_t mode, faces, active;
} xeen_modal;
xeen_status xeen_modal_begin(xeen_modal *, xeen_movement *, uint16_t ds_segment);
xeen_status xeen_modal_finish(xeen_modal *, xeen_movement *, uint16_t ds_segment);
/* 1871B(n)/18765(n) window bookkeeping: the 986C+n open flag and the
 * 3064/9843 window stack (18765 leaves the slot). The 4F2A1/4F57B drawing
 * is presentation, and the 9A1C == 9 event refresh 4A1B7 is not included. */
void xeen_window_open(xeen_movement *, unsigned window);
void xeen_window_close(xeen_movement *, unsigned window);
/* 186C0: every window named in the 9843 stack slots 28h..0 whose 986C flag
 * is set is closed (flag cleared; its 4F57B restore is presentation), then
 * 3064 = FFFFh. */
void xeen_window_close_all(xeen_movement *);
#endif
