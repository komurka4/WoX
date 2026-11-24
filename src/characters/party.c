#include "xeen/party.h"
#include "xeen/ds_globals.h"
#include <string.h>
xeen_party_status xeen_party_restore(const uint8_t *roster, size_t roster_size, const uint8_t *party,
                                     size_t party_size, unsigned world, uint8_t *ds, size_t ds_size)
{
    const uint8_t *members;
    unsigned count;
    if (!roster || !party || !ds || ds_size < 65536u) {
        return XP_INPUT;
    }
    if (world > 1u) {
        return XP_UNSUPPORTED;
    }
    if (roster_size != XEEN_ROSTER_BYTES ||
        party_size != (world ? XEEN_DARK_PARTY_BYTES : XEEN_CLOUDS_PARTY_BYTES)) {
        return XP_SIZE;
    }
    count = party[XEEN_PARTY_FILE_OFFSET(XEEN_DS_PARTY_COUNT)];
    members = party + XEEN_PARTY_FILE_OFFSET(XEEN_DS_PARTY_MEMBERS); /* each member's roster index (ED8E..) */
    /* The recovered loop (43DA5) uses ED8C only: ED8D may differ (the inn's Remove lowers ED8C; ED8D follows at its
     * exit, 4445B). An unsupported count or roster reference is rejected before any state mutation. */
    if (count > 8u) {
        return XP_UNSUPPORTED;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (members[i] >= XEEN_ROSTER_MEMBERS) {
            return XP_MEMBERSHIP;
        }
        for (unsigned j = 0; j < i; ++j) {
            if (members[i] == members[j]) {
                return XP_MEMBERSHIP;
            }
        }
    }
    memcpy(ds + XEEN_DS_PARTY_FILE, party, party_size);
    for (unsigned i = 0; i < count; ++i) {
        memcpy(ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES,
               roster + (size_t)members[i] * XEEN_CHARACTER_BYTES, XEEN_CHARACTER_BYTES);
    }
    return XP_OK;
}
