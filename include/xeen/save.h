#ifndef XEEN_SAVE_H
#define XEEN_SAVE_H
#include "xeen/party.h"
/* Whether the map save 30F34 writes nothing for the map whose ID word is MAZE in WORLD (0 Clouds): no map loaded,
 * or Clouds map 85. Original: 30F36..30F50 (the map ID word C0CC, then 55h with the world byte 0094 clear). */
static inline int xeen_map_save_skipped(uint16_t maze, uint8_t world)
{
    return !maze || (maze == 85u && !world);
}
/* Original WOX/CUR directory and unmodified payloads, not a ScummVM save.
 * Storage profile (party_size) and selected world are independent: combined
 * Darkside saves can select Clouds while retaining all 1530 party bytes. */
typedef struct {
    uint8_t header[31], roster[XEEN_ROSTER_BYTES], party[XEEN_DARK_PARTY_BYTES];
    size_t party_size;
    uint8_t world, map, x, y, direction;
} xeen_save_snapshot;
typedef struct {
    uint8_t *cloud, *dark;
    size_t cloud_size, dark_size;
} xeen_save_pair;
/* Transactional read/restore; input bytes are borrowed only for this call.
 * Restore copies header, party, active members only; archive selection, map
 * caches, native handles, RNG and clocks belong to startup/runtime services.
 * It does not normalize coordinates, conditions, flags or missing suffixes. */
int xeen_save_read(xeen_save_snapshot *, const uint8_t *, size_t, char *, size_t);
int xeen_save_restore(const xeen_save_snapshot *, uint8_t *, size_t, char *, size_t);
/* Original 30F34/30E49 current-map write into an already cloned active-world
 * archive. Used by the map-transition owner before replacing the live map.
 * A later member failure can leave earlier members changed, so callers that
 * require rollback must pass a private clone. */
int xeen_save_map_archive(uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *, size_t,
                          char *, size_t);
/* The same 30F34/30E49 write as the 29669 call of the in-game
 * Save makes it: on the live DS, so the Clouds map 15 out-of-bounds
 * monster check sets party flag 56 (2F8F3 at F01F) in that DS. */
int xeen_save_map_live(uint8_t *, size_t, uint8_t *, size_t, const uint8_t *, size_t, char *,
                       size_t);
/* 50776: one member's bytes rewritten in place (its directory size must be
 * size). Returns 0 when the member is missing (the driver's fatal 505CA). */
int xeen_save_member_replace(uint8_t *, size_t, const char *, const uint8_t *, size_t);
/* Build an original-format paired save from a live restored DS. The combined
 * party/header and active records are committed to the Darkside component,
 * matching original 29502/19278. First, as 29669 does, the current map is
 * saved into the active world's component (original 30F34/30E49): the
 * maze DAT cache slot 0 (C0CC; Clouds map 85 skipped), indoor neighbour slots
 * 1..8, and the MOB image rebuilt from runtime object/monster/wall positions,
 * each in place at its recorded size. The legacy entry preserves EVT; the
 * event-aware variant below supplies the current mutable handle-store image.
 * Unrelated members and the inactive world's map members remain
 * byte-identical. Transient pending rewards, missing or resized map members,
 * inconsistent map identity and oversize entity counts are rejected. */
int xeen_save_build_pair(xeen_save_pair *, const uint8_t *, size_t, const uint8_t *, size_t,
                         const uint8_t *, size_t, char *, size_t);
/* Runtime-owner variant used when the current event handle store is available.
 * A non-NULL store must have the exact loaded EVT member size and replaces only
 * that fixed-size member before the DAT/MOB writes. */
int xeen_save_build_pair_event(xeen_save_pair *, const uint8_t *, size_t, const uint8_t *, size_t,
                               const uint8_t *, size_t, const uint8_t *, size_t, char *, size_t);
void xeen_save_pair_free(xeen_save_pair *);
/* Publish XEEN.WOX and DARK.WOX in a newly renamed directory. An already
 * published byte-identical pair is an idempotent success; replacing a
 * different existing generation is deliberately unsupported. */
int xeen_save_commit_pair(const char *, const uint8_t *, size_t, const uint8_t *, size_t,
                          const uint8_t *, size_t, char *, size_t);
int xeen_save_commit_pair_event(const char *, const uint8_t *, size_t, const uint8_t *, size_t,
                                const uint8_t *, size_t, const uint8_t *, size_t, char *, size_t);
#endif
