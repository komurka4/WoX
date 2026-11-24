#ifndef XEEN_PROGRAM_DATA_H
#define XEEN_PROGRAM_DATA_H
/* The original program's initialized data, compiled in. Generated
 * at build time by tools/data/gen_program_data.py from the JSON in src/data (see
 * src/data/README.md); the game never reads XEEN.DAT. */
#include <stdint.h>

#define XEEN_PROGRAM_DS_SIZE 0x8C4Cu
/* DS:0000..8C4B as the original XEEN.DAT stores it (file 4D890h; the raw,
 * unrelocated far-pointer segment words 0000h/481Dh). DS:8C4C and above is
 * BSS, zero at program load. */
extern const uint8_t xeen_program_ds_image[XEEN_PROGRAM_DS_SIZE];
/* The driver RNG 50525's state at program start: segment 4853 CS:16F8,
 * CS:16FA, CS:16FC (little-endian words in the original image). */
extern const uint16_t xeen_program_rng_seed[3];
/* SHA-256 of xeen_program_ds_image (lower-case hex), checked by the generator. */
extern const char xeen_program_ds_sha256[65];

#endif
