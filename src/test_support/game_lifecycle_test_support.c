/*
 * Game lifecycle queries used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/game/game_lifecycle.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "../game/game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint8_t *xeen_game_timer_cs(const xeen_game *g)
{
    return g ? g->timer.cs : NULL;
}
