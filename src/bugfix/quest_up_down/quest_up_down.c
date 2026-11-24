/* Bug fix quest_up_down (Phase 269; owner-approved, NOT original behavior): see
 * include/xeen/bugfix/quest_up_down.h. */
#include "xeen/bugfix/quest_up_down.h"
#include "xeen/input_poll.h"

int xeen_bugfix_quest_up_down_on;

uint16_t xeen_bugfix_quest_key(uint16_t key)
{
    if (!xeen_bugfix_quest_up_down_on) {
        return key;
    }
    return key == 'U' ? (uint16_t)XEEN_KEY_UP : key == 'D' ? (uint16_t)XEEN_KEY_DOWN : key;
}
