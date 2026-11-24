#include "xeen/timer.h"
#include "xeen/byte_order.h"
/* x86 FLAGS bits the waits reproduce: an arithmetic result sets CF, PF, AF, ZF, SF and OF (SUB AX,AX leaves PF and
 * ZF); DF selects the palette copies' direction. */
#define X86_CF         0x0001u
#define X86_PF         0x0004u
#define X86_AF         0x0010u
#define X86_ZF         0x0040u
#define X86_SF         0x0080u
#define X86_DF         0x0400u
#define X86_OF         0x0800u
#define X86_ARITHMETIC (X86_CF | X86_PF | X86_AF | X86_ZF | X86_SF | X86_OF)
/* The three waits' start readings in the driver's code segment, DX then CX (CS:0C12, 0C16, 0C1A): sampled by
 * 4EA0D/4EA35/4EA5D, compared by 4EA1C/4EA44/4EA6C. */
#define CS_WAIT_START(k) (0xc12u + 4u * (k))
/* The four palette cycles' words by cycle (xeen_timer_palette_init): the first colour, the colour count (0: the
 * cycle is off), the reload of the countdown and the countdown; the driver's palette at CS:0F4A, three bytes (the
 * DAC's red, green, blue) per colour, written to the VGA DAC's index and data ports. */
#define CS_CYCLE_START     0x16c0
#define CS_CYCLE_COUNT     0x16c8
#define CS_CYCLE_RELOAD    0x16d0
#define CS_CYCLE_COUNTDOWN 0x16d8
#define CS_PALETTE         0xf4a
#define VGA_DAC_WRITE      0x3c8
#define VGA_DAC_DATA       0x3c9
/**
 * Applies one BIOS time reading (INT 1Ah function 0) to the timer: AL's midnight count advances the
 * day counter and CX:DX become the tick count within the current day.
 */
static void apply_bios_reading(xeen_timer *t, const xeen_timer_registers *r)
{
    t->days += (uint8_t)r->ax;
    t->clock = (uint64_t)t->days * XEEN_BIOS_TICKS_PER_DAY + (((uint32_t)r->cx << 16) | r->dx);
}
/**
 * Reads the BIOS time as the waits do: SUB AX,AX (AX 0; CF, AF, SF and OF cleared, PF and ZF set), then INT 1Ah
 * (BIOS) with R's other registers into NEXT. Returns BIOS's status.
 */
static xeen_status read_bios_time(const xeen_timer_registers *r, xeen_timer_registers *next, xeen_timer_bios bios,
                                  void *user)
{
    *next = *r;
    next->ax = 0;
    next->flags = (uint16_t)((next->flags & (uint16_t)~X86_ARITHMETIC) | X86_PF | X86_ZF);
    return bios(user, next);
}
/** The CS offset of a wait's start reading (CS_WAIT_START): the frame's (0), the scene's (1) or the subtitle's (2). */
static uint16_t wait_start(uint32_t entry)
{
    return entry == XEEN_TIMER_FRAME_MARK || entry == XEEN_TIMER_FRAME_ELAPSED   ? CS_WAIT_START(0)
           : entry == XEEN_TIMER_SCENE_MARK || entry == XEEN_TIMER_SCENE_ELAPSED ? CS_WAIT_START(1)
                                                                                 : CS_WAIT_START(2);
}
xeen_status xeen_timer_sample(xeen_timer *t, uint32_t entry, xeen_timer_registers *r,
                              xeen_timer_bios bios, void *user)
{
    xeen_timer_registers next;
    xeen_status status;
    uint16_t offset;
    if (!t || !r || (entry != XEEN_TIMER_FRAME_MARK && entry != XEEN_TIMER_SCENE_MARK && entry != XEEN_TIMER_SUBTITLE_MARK)) {
        return XEEN_BAD_INPUT;
    }
    if (!bios) {
        return XEEN_SERVICE_REQUIRED;
    }
    /* 4EA0D / 4EA35 / 4EA5D: the reading's DX and CX become the wait's start. */
    if ((status = read_bios_time(r, &next, bios, user)) != XEEN_OK) {
        return status;
    }
    offset = wait_start(entry);
    xeen_put_le16(t->cs + offset, next.dx);
    xeen_put_le16(t->cs + offset + 2, next.cx);
    apply_bios_reading(t, &next);
    *r = next;
    return XEEN_OK;
}
xeen_status xeen_timer_peek(xeen_timer *t, xeen_timer_bios bios, void *user)
{
    xeen_timer_registers next = {0, 0, 0, X86_PF | X86_ZF};
    xeen_status status;
    if (!t) {
        return XEEN_BAD_INPUT;
    }
    if (!bios) {
        return XEEN_SERVICE_REQUIRED;
    }
    status = bios(user, &next);
    if (status != XEEN_OK) {
        return status;
    }
    apply_bios_reading(t, &next);
    return XEEN_OK;
}
/**
 * Deterministic replacement for the BIOS tick service (a xeen_timer_bios callback).
 *
 * USER is a xeen_bios_ticks; its current ticks and pending midnight flag are returned in CX:DX and
 * AL like INT 1Ah function 0, and the flag is cleared once reported. Returns XEEN_BAD_INPUT for a
 * tick count past one day.
 */
xeen_status xeen_timer_deterministic_bios(void *user, xeen_timer_registers *r)
{
    xeen_bios_ticks *b = user;
    if (!b || !r || b->ticks >= XEEN_BIOS_TICKS_PER_DAY) {
        return XEEN_BAD_INPUT;
    }
    r->ax = (uint16_t)((r->ax & 0xff00u) | b->midnight);
    r->cx = (uint16_t)(b->ticks >> 16);
    r->dx = (uint16_t)b->ticks;
    b->midnight = 0;
    return XEEN_OK;
}
xeen_status xeen_timer_advance(xeen_bios_ticks *b, uint64_t amount)
{
    uint64_t days, remainder;
    if (!b || b->ticks >= XEEN_BIOS_TICKS_PER_DAY) {
        return XEEN_BAD_INPUT;
    }
    /* Divide before addition so even UINT64_MAX is defined. */
    days = amount / XEEN_BIOS_TICKS_PER_DAY;
    remainder = amount % XEEN_BIOS_TICKS_PER_DAY + b->ticks;
    days += remainder / XEEN_BIOS_TICKS_PER_DAY;
    b->ticks = (uint32_t)(remainder % XEEN_BIOS_TICKS_PER_DAY);
    b->midnight = (uint8_t)(b->midnight + (uint8_t)days);
    return XEEN_OK;
}
/**
 * Reads the little-endian word at offset A of the driver's code-segment data (the offset wraps at
 * 64K).
 */
static uint16_t cs_read16(const xeen_timer *t, uint16_t a)
{
    return (uint16_t)(t->cs[a] | ((uint16_t)t->cs[(uint16_t)(a + 1u)] << 8));
}
/**
 * Writes V to offset A of the driver's code-segment data as a little-endian word (wrapping at 64K).
 */
static void cs_write16(xeen_timer *t, uint16_t a, uint16_t v)
{
    t->cs[a] = (uint8_t)v;
    t->cs[(uint16_t)(a + 1u)] = (uint8_t)(v >> 8);
}
/* Cycle K's (0..3) word in the table at BASE (CS_CYCLE_START etc.): the original indexes the tables with BX = 2K. */
#define CS_CYCLE(base, k) ((uint16_t)((base) + 2u * (k)))
/* The palette's bytes moved by one DAC writer pass: 16 colours (4A7C2 unrolls 24 word reads, 48 bytes). */
#define DAC_PASS_WORDS 24u
/*
 * One timer tick of palette cycle K (4A614..4A651): an active cycle (a nonzero colour count) counts its countdown
 * down; when it passes below 0 (JNS) the countdown is reloaded and the cycle's colours rotate by one: every colour
 * moves one place towards the cycle's first (REP MOVSW over 3 x count - 3 bytes, then the odd byte), and the first
 * colour (its word and its third byte, read first) goes to the last place. STEP (1, or -1 with DF set) is the string
 * instructions' direction; the last colour's third byte is stored at DI + 2 whatever the direction.
 */
static void rotate_cycle(xeen_timer *t, unsigned k, int step)
{
    uint16_t count = cs_read16(t, CS_CYCLE(CS_CYCLE_COUNT, k)), countdown, bytes, src, dst, first, i;
    uint8_t third;
    if (!count) {
        return;
    }
    countdown = (uint16_t)(cs_read16(t, CS_CYCLE(CS_CYCLE_COUNTDOWN, k)) - 1u);
    cs_write16(t, CS_CYCLE(CS_CYCLE_COUNTDOWN, k), countdown);
    if (!(countdown & 0x8000u)) {
        return;
    }
    cs_write16(t, CS_CYCLE(CS_CYCLE_COUNTDOWN, k), cs_read16(t, CS_CYCLE(CS_CYCLE_RELOAD, k)));
    bytes = (uint16_t)(3u * count - 3u);
    src = (uint16_t)(CS_PALETTE + 3u * cs_read16(t, CS_CYCLE(CS_CYCLE_START, k)));
    dst = src;
    first = cs_read16(t, src); /* LODSW, LODSB */
    src = (uint16_t)(src + 2 * step);
    third = t->cs[src];
    src = (uint16_t)(src + step);
    for (i = 0; i < (uint16_t)(bytes >> 1); ++i) { /* REP MOVSW */
        cs_write16(t, dst, cs_read16(t, src));
        src = (uint16_t)(src + 2 * step);
        dst = (uint16_t)(dst + 2 * step);
    }
    if (bytes & 1u) { /* ADC CX, CX; REP MOVSB */
        t->cs[dst] = t->cs[src];
        dst = (uint16_t)(dst + step);
    }
    cs_write16(t, dst, first);
    t->cs[(uint16_t)(dst + 2u)] = third;
}
/*
 * Active cycle K's colours sent to the VGA DAC (4A65E..4A67E, then 4A7C2): the first colour's index to the write
 * port, then 16 colours (48 bytes, two per word read) per pass for ceil(count / 16) passes; LOOP with CX 0 makes a
 * count above FFF0h (CX wraps to 0) run 65,536 passes. STEP is the LODSW direction.
 */
static void write_cycle_dac(const xeen_timer *t, unsigned k, int step, xeen_timer_port_out out, void *user)
{
    uint16_t count = cs_read16(t, CS_CYCLE(CS_CYCLE_COUNT, k)), start = cs_read16(t, CS_CYCLE(CS_CYCLE_START, k)),
             passes = (uint16_t)((uint16_t)(count + 15u) >> 4), src = (uint16_t)(CS_PALETTE + 3u * start);
    uint32_t loops = passes ? passes : 65536u, w;
    if (!count) {
        return;
    }
    out(user, VGA_DAC_WRITE, (uint8_t)start);
    for (w = 0; w < loops * DAC_PASS_WORDS; ++w) {
        uint16_t v = cs_read16(t, src);
        src = (uint16_t)(src + 2 * step);
        out(user, VGA_DAC_DATA, (uint8_t)v);
        out(user, VGA_DAC_DATA, (uint8_t)(v >> 8));
    }
}
/*
 * The palette poll at the start of the elapsed-time waits (4A607, called by 4EA1C, 4EA44 and 4EA6C): two passes over
 * the four palette cycles, each from cycle 3 down to 0 (BX = 6, 4, 2, 0): first every cycle's tick and rotation
 * (rotate_cycle), then every active cycle's colours to the DAC (write_cycle_dac). FLAGS' DF gives the string copies'
 * direction. Native: OUT may be NULL only while no cycle is active (XEEN_SERVICE_REQUIRED, checked before any change).
 */
static xeen_status xeen_timer_palette_poll(xeen_timer *t, uint16_t flags, xeen_timer_port_out out,
                                           void *user)
{
    int step = (flags & X86_DF) ? -1 : 1;
    unsigned k;
    if (!t) {
        return XEEN_BAD_INPUT;
    }
    for (k = 4; k-- > 0;) {
        if (cs_read16(t, CS_CYCLE(CS_CYCLE_COUNT, k)) && !out) {
            return XEEN_SERVICE_REQUIRED;
        }
    }
    for (k = 4; k-- > 0;) {
        rotate_cycle(t, k, step);
    }
    for (k = 4; k-- > 0;) {
        write_cycle_dac(t, k, step, out, user);
    }
    return XEEN_OK;
}
/**
 * Computes the x86 FLAGS image left by a 16-bit SUB of B from A with the given RESULT.
 *
 * Sets CF, PF, AF, ZF, SF and OF exactly as the CPU does and keeps the other bits of FLAGS; the
 * original's code branches on these flags, so the timer polls reproduce them bit for bit.
 */
static uint16_t subtract_flags(uint16_t flags, uint16_t a, uint16_t b, uint16_t result)
{
    unsigned x = result & 255u;
    unsigned parity = 0;
    while (x) {
        parity ^= x & 1u;
        x >>= 1;
    }
    flags = (uint16_t)(flags & ~X86_ARITHMETIC);
    if (a < b) {
        flags |= X86_CF;
    }
    if (!parity) {
        flags |= X86_PF;
    }
    if ((a ^ b ^ result) & 16u) {
        flags |= X86_AF;
    }
    if (!result) {
        flags |= X86_ZF;
    }
    if (result & 0x8000u) {
        flags |= X86_SF;
    }
    if (((a ^ b) & (a ^ result)) & 0x8000u) {
        flags |= X86_OF;
    }
    return flags;
}
/**
 * Runs one step of the original's elapsed-time waits (entries 4EA1C, 4EA44 and 4EA6C).
 *
 * The state S holds the wait's phase across calls: the palette poll, then the BIOS reading and the
 * comparison with the wait's start. Returns XEEN_BAD_INPUT for an unknown entry or a finished wait,
 * and passes on a failing port or BIOS service status.
 */
xeen_status xeen_timer_elapsed(xeen_timer *t, xeen_timer_elapsed_state *s, xeen_timer_registers *r,
                               xeen_timer_port_out out, void *port_user, xeen_timer_bios bios,
                               void *bios_user)
{
    xeen_timer_registers next;
    xeen_status status;
    uint16_t offset, a, b;
    /* Phase 2: the wait has compared already (a finished wait is not run again). */
    if (!t || !s || !r || s->phase >= 2 ||
        (s->entry != XEEN_TIMER_FRAME_ELAPSED && s->entry != XEEN_TIMER_SCENE_ELAPSED && s->entry != XEEN_TIMER_SUBTITLE_ELAPSED)) {
        return XEEN_BAD_INPUT;
    }
    if (s->phase == 0) {
        status = xeen_timer_palette_poll(t, r->flags, out, port_user);
        if (status != XEEN_OK) {
            return status;
        }
        s->phase = 1;
    }
    if (!bios) {
        return XEEN_SERVICE_REQUIRED;
    }
    if ((status = read_bios_time(r, &next, bios, bios_user)) != XEEN_OK) {
        return status;
    }
    apply_bios_reading(t, &next);
    /* 4EA23 (4EA4B, 4EA73): MOV AX,DX; SUB AX,start DX; JNE to the RETF; SUB CX,start CX; JE to the RETF; DEC AX. */
    offset = wait_start(s->entry);
    a = next.dx;
    b = cs_read16(t, offset);
    next.ax = (uint16_t)(a - b);
    next.flags = subtract_flags(next.flags, a, b, next.ax);
    if (!next.ax) {
        a = next.cx;
        b = cs_read16(t, (uint16_t)(offset + 2u));
        next.cx = (uint16_t)(a - b);
        next.flags = subtract_flags(next.flags, a, b, next.cx);
        if (next.cx) { /* DEC AX: AX 0 -> FFFFh with the flags of 0 - 1, CF unchanged */
            uint16_t carry = next.flags & X86_CF;
            next.ax = 65535;
            next.flags = (uint16_t)((subtract_flags(next.flags, 0, 1, 65535) & ~X86_CF) | carry);
        }
    }
    *r = next;
    s->phase = 2;
    return XEEN_OK;
}

xeen_status xeen_timer_palette_init(xeen_timer *t, uint16_t slot, uint16_t start, uint16_t count,
                                    uint16_t reload)
{
    unsigned i;
    if (!t) {
        return XEEN_BAD_INPUT;
    }
    if (slot >= 3u) {
        for (i = 0; i < 4u; ++i) {
            cs_write16(t, (uint16_t)(CS_CYCLE_COUNT + 2u * i), 0);
        }
    } else {
        slot = (uint16_t)(slot * 2u);
        cs_write16(t, (uint16_t)(CS_CYCLE_START + slot), start);
        cs_write16(t, (uint16_t)(CS_CYCLE_COUNT + slot), count);
        cs_write16(t, (uint16_t)(CS_CYCLE_RELOAD + slot), reload);
        cs_write16(t, (uint16_t)(CS_CYCLE_COUNTDOWN + slot), reload);
    }
    return XEEN_OK;
}
