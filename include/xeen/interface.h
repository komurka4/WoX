#ifndef XEEN_INTERFACE_H
#define XEEN_INTERFACE_H
#include "xeen/presentation.h"
/* Original 2BEF9 party skill check: counts members (ED8C) whose skill byte
 * B445 + n + member*0x162 is nonzero (conditions are not consulted). Skills
 * 9 and 11 need two such members, 5 and 14 every member, n > 17 never; the
 * rest any member. */
int xeen_party_skill(const xeen_movement *, uint16_t skill);

/* Original 26306 interface border (scene suffix 1C0F1): border, levitate,
 * spot-doors, danger-sense and clairvoyance animations, resistance, bless,
 * power-shield, holy-bonus and heroism icons, then the direction letter
 * (window0 text, skill6 and !8C52) and the view frame request (4F6F8 with
 * 0C if 9878, else 28 if 9894). Updates counters 974D/974E/974F, F462..F465,
 * F468/F469 as the original. Facing ED96 > 3 rejects without change. */
#define XEEN_BORDER_MAX_DRAWS 32u
typedef struct {
    size_t count;
    xeen_sprite_draw draws[XEEN_BORDER_MAX_DRAWS];
    int has_text;
    uint16_t text_window;
    char text[32];
    int has_frame;
    uint16_t frame_window;
} xeen_border_ops;
xeen_status xeen_interface_border(xeen_movement *, xeen_border_ops *);
#endif
