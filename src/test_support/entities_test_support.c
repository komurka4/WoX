/*
 * Entity table queries used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/world/entities.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "xeen/map_objects.h"
#include "xeen/monster_record.h"
#include "xeen/ds_globals.h"
#include "xeen/byte_order.h"
#include "xeen/entities.h"

xeen_resource_status xeen_entities_monsters_walls(const uint8_t *mob, size_t size,
                                                  const uint8_t *db, size_t db_size,
                                                  xeen_movement *m, xeen_entity_random_fn random,
                                                  void *user)
{
    return xeen_entities_monsters_walls_aliased(mob, size, db, db_size, db_size, db_size, m, random,
                                                user);
}
