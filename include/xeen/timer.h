#ifndef XEEN_TIMER_H
#define XEEN_TIMER_H
#include <stdint.h>
#include "xeen/movement.h"
/* Code-segment image for original CS:0C12..0C19. Keep it separate from DS.
 * Callers may initialize the full image from the relocated original segment. */
/* Clock is the last INT 1Ah reading of a sample or an elapsed
 * call (CX:DX plus a day per midnight AL), the game's BIOS clock. */
typedef struct {
    uint8_t cs[65536];
    uint64_t clock;
    uint32_t days;
} xeen_timer;
typedef struct {
    uint16_t ax, cx, dx, flags;
} xeen_timer_registers;
/*
 * The original's three BIOS-tick timers (INT 1Ah function 0). A timer's "mark" routine stores the tick count in its
 * code-segment slot; its "elapsed" routine returns the ticks since that mark (after polling the palette cycles).
 * The routines are named by their original addresses: xeen_timer_sample takes a mark, xeen_timer_elapsed_state.entry
 * an elapsed routine, and the game stops at a mark's address (its lifecycle boundary) until the host supplies the
 * BIOS reading. Which original routines use which timer was read from their far calls (Phase 261).
 */
enum xeen_timer_routine {
    XEEN_TIMER_FRAME_MARK = 0x4ea0d,        /* CS:0C12. The frame timer: the exploration and combat command polls,
                                             * the menus' and dialogs' waits, the cutscenes' timed pauses (53 of the
                                             * original's routines mark it) */
    XEEN_TIMER_FRAME_ELAPSED = 0x4ea1c,
    XEEN_TIMER_SCENE_MARK = 0x4ea35,        /* CS:0C16. The 3D scene's timer: the scene draw 1B503 marks it and its
                                             * frame wait 1C1B3 reads it; also two location scenes (35EBD, 35F41) and
                                             * the CD status code */
    XEEN_TIMER_SCENE_ELAPSED = 0x4ea44,
    XEEN_TIMER_SUBTITLE_MARK = 0x4ea5d,     /* CS:0C1A. The talk scenes' scrolling subtitle (185E3 moves it on once
                                             * more than a tick has passed); also the CD code */
    XEEN_TIMER_SUBTITLE_ELAPSED = 0x4ea6c
};
/* INT 1A/AH=0 is an external BIOS boundary. Entry AX is zero and arithmetic
 * flags are those of SUB AX,AX. A provider explicitly supplies BIOS effects.
 * Failure must leave registers/provider state unchanged. */
typedef xeen_status (*xeen_timer_bios)(void *, xeen_timer_registers *);
xeen_status xeen_timer_sample(xeen_timer *, uint32_t entry, xeen_timer_registers *, xeen_timer_bios,
                              void *);
/* Host-controlled deterministic BIOS fixture, no autonomous time advance.
 * AL consumes the midnight counter; AH and flags retain their input values.
 * tick units are BIOS ticks, never game minutes or frontend commands. */
typedef struct {
    uint32_t ticks;
    uint8_t midnight;
} xeen_bios_ticks;
#define XEEN_BIOS_TICKS_PER_DAY UINT32_C(0x1800b0)
xeen_status xeen_timer_deterministic_bios(void *, xeen_timer_registers *);
/* A native INT 1Ah reading outside the original's calls (the voice
 * model's clock): clock and days advance as a sample's do; the CS sample
 * words are not written. 4EA1C's result never uses AL, so a midnight count
 * this reading takes only moves days, as the next reading would have. */
xeen_status xeen_timer_peek(xeen_timer *, xeen_timer_bios, void *);
xeen_status xeen_timer_advance(xeen_bios_ticks *, uint64_t ticks);
/* Original 4A5C7: unsigned slot>=3 clears all four active counts. */
xeen_status xeen_timer_palette_init(xeen_timer *, uint16_t slot, uint16_t start, uint16_t count,
                                    uint16_t reload);
/* Ordered VGA DAC writes are external presentation effects. The callback must
 * accept every write; absence suspends before any palette mutation. */
typedef void (*xeen_timer_port_out)(void *, uint16_t port, uint8_t value);
/* An elapsed invocation owns a persistent phase: phase 0 runs palette poll,
 * phase 1 awaits BIOS, phase 2 is completed. Initialize phase=0 for each NEW
 * invocation. A pending BIOS never replays palette side effects. */
typedef struct {
    uint8_t phase;
    uint32_t entry;
} xeen_timer_elapsed_state;
xeen_status xeen_timer_elapsed(xeen_timer *, xeen_timer_elapsed_state *, xeen_timer_registers *,
                               xeen_timer_port_out, void *, xeen_timer_bios, void *);
#endif
