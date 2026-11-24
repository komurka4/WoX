#include "xeen/character_record.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The oracles below use the raw record offsets, independently of the header's layout names;
 * these assertions tie the two together. */
_Static_assert(XEEN_CHARACTER_LEVEL == 0x23u && XEEN_CHARACTER_LEVEL_BONUS == 0x24u, "level bytes");
_Static_assert(XEEN_CHARACTER_SKILLS == 0x27u && XEEN_SKILL_COUNT == 18u, "skill bytes");
_Static_assert(XEEN_CHARACTER_WEAPONS + XEEN_ITEM_ROW_ID == 0xA7u && XEEN_ITEM_ROW_BYTES == 4u &&
                   XEEN_ITEM_ROWS == 9u && XEEN_WEAPON_XEEN_SLAYER_SWORD == 0x22u,
               "weapon id bytes");
_Static_assert(XEEN_CHARACTER_CONDITIONS == 0x143u && XEEN_CONDITION_COUNT == 16u &&
                   XEEN_CONDITION_GOOD == 16,
               "condition bytes");
/* The remaining field names against the raw offsets of the original's readers
 * (docs/coverage/CHARACTER_RECORD_LAYOUT.md): no field overlaps the next, and the last field ends
 * inside the 162h-byte record. */
_Static_assert(XEEN_CHARACTER_SEX == 0x10u && XEEN_CHARACTER_RACE == 0x11u && XEEN_CHARACTER_CLASS == 0x13u,
               "identity bytes");
_Static_assert(XEEN_CHARACTER_STATS == 0x14u && XEEN_CHARACTER_STAT_BYTES == 2u && XEEN_STAT_COUNT == 7u &&
                   XEEN_CHARACTER_STAT(XEEN_STAT_COUNT) == 0x22u && XEEN_CHARACTER_STAT_TEMPORARY(XEEN_STAT_LUCK) == 0x21u,
               "seven stat pairs end at the AC byte");
_Static_assert(XEEN_CHARACTER_AC_BONUS == 0x22u && XEEN_CHARACTER_BIRTH_DAY == 0x25u &&
                   XEEN_CHARACTER_AGE_BONUS == 0x26u,
               "AC and age bytes");
_Static_assert(XEEN_CHARACTER_RESISTANCES == 0x137u && XEEN_RESISTANCE_COUNT == 6u &&
                   XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_COLD) == 0x139u &&
                   XEEN_CHARACTER_RESISTANCE_TEMPORARY(XEEN_RESISTANCE_MAGIC) == 0x142u &&
                   XEEN_CHARACTER_RESISTANCE(XEEN_RESISTANCE_COUNT) == XEEN_CHARACTER_CONDITIONS,
               "six resistance pairs end at the condition bytes");
_Static_assert(XEEN_CHARACTER_CONDITION(XEEN_CONDITION_ERADICATED) == 0x152u &&
                   XEEN_CHARACTER_CONDITION(XEEN_CONDITION_COUNT) < XEEN_CHARACTER_INN && XEEN_CHARACTER_INN == 0x154u &&
                   XEEN_CHARACTER_HP == 0x156u && XEEN_CHARACTER_SP == 0x158u && XEEN_CHARACTER_BIRTH_YEAR == 0x15Au &&
                   XEEN_CHARACTER_EXPERIENCE == 0x15Cu && XEEN_CHARACTER_EXPERIENCE + 4u <= 0x162u,
               "the words and the experience dword");

/* Each buffer ends one byte past the last byte its query may read (ASan catches an overread). */
#define LEVEL_EXTENT     0x25u
#define CONDITION_EXTENT 0x153u
#define SKILL_EXTENT     0x39u
#define WEAPON_EXTENT    0xC8u

/* _Generic checks the public return types, including the AL-only routine 2AB96. */
_Static_assert(_Generic(xeen_character_current_level((const uint8_t *)0), uint16_t: 1, default: 0),
               "2C909, the current level, returns a word");
_Static_assert(_Generic(xeen_character_worst_condition((const uint8_t *)0), uint8_t: 1, default: 0),
               "2AB96, the worst condition, returns a byte");
_Static_assert(_Generic(xeen_character_skill_count((const uint8_t *)0), uint16_t: 1, default: 0),
               "46364, the skill count, returns a word");
_Static_assert(_Generic(xeen_character_has_xeen_slayer_sword((const uint8_t *)0), uint16_t: 1, default: 0),
               "3CF35, the Xeen Slayer Sword check, returns a word");

static void check(uint32_t actual, uint32_t expected, uint32_t case_id,
                  unsigned line)
{
    if (actual != expected) {
        fprintf(stderr, "line %u, case %" PRIu32 ": got %" PRIu32
                ", expected %" PRIu32 "\n", line, case_id, actual, expected);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(actual, expected, id) check((actual), (expected), (id), __LINE__)

static void unchanged(const uint8_t *actual, const uint8_t *before, size_t size)
{
    if (memcmp(actual, before, size) != 0) {
        fputs("routine modified its input\n", stderr);
        exit(EXIT_FAILURE);
    }
}

static void test_2c909(void)
{
    uint8_t bytes[LEVEL_EXTENT];
    uint8_t before[sizeof bytes];
    memset(bytes, 0xFF, sizeof bytes);
    /* Exhaustive ordered pairs, with all unrelated bytes poisoned.
     * Oracle: concatenate operands into a word; its base-256 digits sum.
     * This validates unsigned loads, no byte truncation, and the 510 maximum.
     */
    for (uint32_t pair = 0; pair < UINT32_C(65536); ++pair) {
        bytes[0x23] = (uint8_t)(pair >> 8);
        bytes[0x24] = (uint8_t)pair;
        memcpy(before, bytes, sizeof bytes);
        CHECK(xeen_character_current_level(bytes), pair / 256u + pair % 256u, pair);
        unchanged(bytes, before, sizeof bytes);
    }
    memset(bytes, 0, sizeof bytes);
    CHECK(xeen_character_current_level(bytes), 0, 0);
    bytes[0x23] = UINT8_C(255);
    bytes[0x24] = UINT8_C(255);
    CHECK(xeen_character_current_level(bytes), 510, 1);
}

static void test_2ab96(void)
{
    uint8_t bytes[CONDITION_EXTENT];
    uint8_t before[sizeof bytes];
    memset(bytes, 0xFF, sizeof bytes);
    /* Oracle from a separate occupancy mask: binary digit count minus one.
     * Production scans bytes backwards; the oracle shifts the mask to zero.
     */
    for (uint32_t mask = 0; mask < UINT32_C(65536); ++mask) {
        uint8_t expected = 16u;
        uint32_t remaining = mask;
        if (remaining != 0u) {
            expected = 0;
            while ((remaining >>= 1) != 0u) {
                ++expected;
            }
        }
        for (unsigned i = 0; i < 16u; ++i) {
            bytes[0x143u + i] = (mask & (UINT32_C(1) << i)) != 0u
                ? (i % 2u != 0u ? UINT8_C(255) : UINT8_C(128)) : 0;
        }
        memcpy(before, bytes, sizeof bytes);
        CHECK(xeen_character_worst_condition(bytes), expected, mask);
        unchanged(bytes, before, sizeof bytes);
    }
    /* Every value in every position: nonzero is not merely 1 or positive. */
    for (unsigned i = 0; i < 16u; ++i) {
        for (unsigned value = 0; value < 256u; ++value) {
            memset(bytes, 0, sizeof bytes);
            bytes[0x143u + i] = (uint8_t)value;
            CHECK(xeen_character_worst_condition(bytes), value != 0u ? i : 16u,
                  i * 256u + value);
        }
    }
}

static void test_46364(void)
{
    uint8_t bytes[SKILL_EXTENT];
    uint8_t before[sizeof bytes];
    memset(bytes, 0xFF, sizeof bytes);
    /* Independent population count via clearing the lowest set bit;
     * no byte-range counting loop is used by this oracle.
     */
    for (uint32_t mask = 0; mask < UINT32_C(262144); ++mask) {
        uint32_t expected = 0;
        for (uint32_t bits = mask; bits != 0u; bits &= bits - 1u) {
            ++expected;
        }
        for (unsigned i = 0; i < 18u; ++i) {
            bytes[0x27u + i] = (mask & (UINT32_C(1) << i)) != 0u
                ? (i % 2u != 0u ? UINT8_C(128) : UINT8_C(255)) : 0;
        }
        memcpy(before, bytes, sizeof bytes);
        CHECK(xeen_character_skill_count(bytes), expected, mask);
        unchanged(bytes, before, sizeof bytes);
    }
    for (unsigned i = 0; i < 18u; ++i) {
        for (unsigned value = 0; value < 256u; ++value) {
            memset(bytes, 0, sizeof bytes);
            bytes[0x27u + i] = (uint8_t)value;
            CHECK(xeen_character_skill_count(bytes), value != 0u ? 1u : 0u,
                  i * 256u + value);
        }
    }
}

static void test_3cf35(void)
{
    uint8_t bytes[WEAPON_EXTENT];
    uint8_t before[sizeof bytes];
    /* All 512 match patterns. Poison every ignored byte with the match
     * value, including the three intervening record bytes.
     * Oracle: any match exists exactly when the generated mask is nonzero.
     */
    for (uint32_t mask = 0; mask < UINT32_C(512); ++mask) {
        memset(bytes, 0x22, sizeof bytes);
        for (unsigned i = 0; i < 9u; ++i) {
            bytes[0xA7u + 4u * i] = (mask & (UINT32_C(1) << i)) != 0u
                ? UINT8_C(0x22) : UINT8_C(0xFF);
        }
        memcpy(before, bytes, sizeof bytes);
        CHECK(xeen_character_has_xeen_slayer_sword(bytes), mask != 0u ? 1u : 0u, mask);
        unchanged(bytes, before, sizeof bytes);
    }
    for (unsigned i = 0; i < 9u; ++i) {
        for (unsigned value = 0; value < 256u; ++value) {
            memset(bytes, 0, sizeof bytes);
            bytes[0xA7u + 4u * i] = (uint8_t)value;
            CHECK(xeen_character_has_xeen_slayer_sword(bytes), value == 0x22u ? 1u : 0u,
                  i * 256u + value);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fputs("usage: xeen_character_record_tests {2c909|2ab96|46364|3cf35}\n", stderr);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "2c909") == 0) {
        test_2c909();
    } else if (strcmp(argv[1], "2ab96") == 0) {
        test_2ab96();
    } else if (strcmp(argv[1], "46364") == 0) {
        test_46364();
    } else if (strcmp(argv[1], "3cf35") == 0) {
        test_3cf35();
    } else {
        fputs("unknown contract\n", stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
