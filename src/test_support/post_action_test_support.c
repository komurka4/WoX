/*
 * The post-action step's single entry used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/world/post_action.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "xeen/post_action.h"
#include "xeen/ds_globals.h"
#include "xeen/character_record.h"
#include "xeen/monster_record.h"
#include "xeen/gameplay_tick.h"
#include "xeen/monster_update.h"
#include "xeen/treasure.h"
#include "xeen/byte_order.h"

xeen_caller_result xeen_post_action_after_movement(xeen_movement *m)
{
    return xeen_post_action_after_movement_native(m, NULL);
}
