/*
 * Character record queries used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/characters/character_record.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "xeen/character_record.h"

uint16_t xeen_character_skill_count(const uint8_t *character)
{
    uint16_t count = 0;
    for (uint16_t skill = 0; skill < XEEN_SKILL_COUNT; ++skill) {
        if (character[XEEN_CHARACTER_SKILLS + skill] != 0u) {
            ++count;
        }
    }
    return count;
}
