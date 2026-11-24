/*
 * Game resource binding used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/game/game_resources.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "../game/game_internal.h"
#include "xeen/monster_record.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

xeen_resource_status xeen_game_bind_scene_resources(xeen_game *g, const uint16_t objects[16],
                                                    const uint16_t walls[16])
{
    unsigned i;
    if (!g || !g->monsters_prepared || g->acted || g->scene_resources_prepared ||
        g->world_entered || !objects || !walls) {
        return XR_BAD_INPUT;
    }
    for (i = 0; i < 16; i++) {
        xeen_write16(&g->movement, (uint16_t)(XEEN_DS_OBJECT_SPRITES + 2u * i), objects[i]);
        xeen_write16(&g->movement, (uint16_t)(XEEN_DS_WALL_ITEM_SPRITES + 2u * i), walls[i]);
    }
    g->scene_resources_prepared = 1;
    return XR_OK;
}
