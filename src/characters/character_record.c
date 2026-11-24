#include "xeen/character_record.h"

uint16_t xeen_character_current_level(const uint8_t *character)
{
    return (uint16_t)(character[XEEN_CHARACTER_LEVEL] + character[XEEN_CHARACTER_LEVEL_BONUS]);
}

uint8_t xeen_character_worst_condition(const uint8_t *character)
{
    uint8_t condition = XEEN_CONDITION_COUNT;
    while (condition != 0u) {
        --condition;
        if (character[XEEN_CHARACTER_CONDITIONS + condition] != 0u) {
            return condition;
        }
    }
    return XEEN_CONDITION_GOOD;
}

uint16_t xeen_character_has_xeen_slayer_sword(const uint8_t *character)
{
    for (uint16_t row = 0; row < XEEN_ITEM_ROWS; ++row) {
        if (character[XEEN_CHARACTER_WEAPONS + XEEN_ITEM_ROW_BYTES * row + XEEN_ITEM_ROW_ID] ==
            XEEN_WEAPON_XEEN_SLAYER_SWORD) {
            return 1;
        }
    }
    return 0;
}
