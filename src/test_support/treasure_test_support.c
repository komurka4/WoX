/*
 * Treasure rules' single-step entries used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/characters/treasure.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "xeen/party.h"
#include "xeen/items.h"
#include "xeen/item_tables.h"
#include "xeen/ds_globals.h"
#include "xeen/treasure.h"
#include "xeen/combat.h"
#include "xeen/character_record.h"
#include "xeen/byte_order.h"
#include <stdio.h>
#include <string.h>

xeen_status xeen_treasure_collect(xeen_movement *m, unsigned preferred)
{
    return xeen_treasure_collect_placed(m, preferred, NULL);
}
xeen_status xeen_treasure_settle(xeen_movement *m, unsigned preferred, unsigned *placed)
{
    uint8_t mode;
    xeen_status status;
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    mode = m->ds[XEEN_DS_UI_MODE];
    if (mode != 2u) {
        m->ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_EXPLORATION;
    }
    status = xeen_treasure_collect_placed(m, preferred, placed);
    if (status != XEEN_OK) {
        m->ds[XEEN_DS_UI_MODE] = mode;
    }
    return status;
}
