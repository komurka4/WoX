#ifndef XEEN_PARTY_H
#define XEEN_PARTY_H
#include <stddef.h>
#include <stdint.h>
#define XEEN_CHARACTER_BYTES    0x162u  /* one character record (xeen/character_record.h) */
#define XEEN_PARTY_RECORDS      0xB41Eu /* DS offset of the party: XEEN_CHARACTER_BYTES per member */
/* The party's member slots: the records before the scratch record (B41E + 6 x 162h = BC6A). */
#define XEEN_PARTY_SLOTS        6u
/* The DS offset of party member N's record (B41E + 162h N, 16-bit). */
#define XEEN_PARTY_RECORD(n)    ((uint16_t)(XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * (n)))
/* The record after the six members' (B41E + 6 x 162h = BC6A): a scratch character record, the item generation's
 * target, the blacksmith's wares (the shop's Buy list) and the event gives' generated item (2DECE). */
#define XEEN_DS_SCRATCH_RECORD  0xbc6au
/* The record before the party's (B41E - 162h = B2BC): the base the original uses for a 1-based member number N (its
 * record at + 162h N): the event's selected member's name (op_display_member_text) and Bash's first basher, whose
 * number is 0 when no member is able (3D6F8: then this record itself, the quirk bash.c reproduces). */
#define XEEN_DS_NUMBERED_MEMBERS 0xb2bcu
/* maze.chr: the roster's 30 character records. */
#define XEEN_ROSTER_MEMBERS     30u
#define XEEN_ROSTER_BYTES       (XEEN_ROSTER_MEMBERS * XEEN_CHARACTER_BYTES)
#define XEEN_CLOUDS_PARTY_BYTES 812u
#define XEEN_DARK_PARTY_BYTES   1530u
typedef enum {
    XP_OK,
    XP_INPUT,
    XP_SIZE,
    XP_UNSUPPORTED,
    XP_MEMBERSHIP
} xeen_party_status;
/* Bounded original CUR restoration: the party file PARTY (maze.pty, PARTY_SIZE bytes: the world's size) -> ED8C; the
 * ROSTER (maze.chr, XEEN_ROSTER_BYTES) records its member list names -> B41E, as original 43DAC. DS is the 64 KiB
 * data segment (DS_SIZE). Returns XP_INPUT (a NULL or a short DS), XP_UNSUPPORTED (WORLD above 1, more than 8
 * members), XP_SIZE, XP_MEMBERSHIP (a roster index past 29 or repeated) before any write, else XP_OK. Does not run
 * later startup services, set world, scan conditions, relocate the party, or modify F60A. Inputs must not overlap
 * DS. Unknown record and party bytes are retained verbatim. */
xeen_party_status xeen_party_restore(const uint8_t *roster, size_t roster_size, const uint8_t *party,
                                     size_t party_size, unsigned world, uint8_t *ds, size_t ds_size);
#endif
