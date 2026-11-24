/* 4AEB8's colour modes 1..6 (included by raster.c).
 *
 * Transliterated from the driver's routines (listings:
 * xeen_dat_evidence/phase81/listings/modes-4c3cc.asm), keeping their
 * register arithmetic, 16-bit wrap, the carry flag where a later
 * instruction reads it, and their quirks:
 *   mode 1  4C3CC / mirrored 4C830  colour remap (src & m) + a, tables
 *                                   CS:181A/1832 by the timer byte; the
 *                                   pattern op remaps in place (cumulative)
 *                                   and the pair op adds the word (the low
 *                                   byte's carry reaches the high one)
 *   mode 2  4CCAC / mirrored 4CF38  scatter: each kept pixel moves by the
 *                                   pixel generator CS:18D2/18D4 (4CEC8),
 *                                   seeded at entry with two 50525 draws;
 *                                   4CEC8 leaves BX changed, and the
 *                                   pattern op steps with that BX
 *   modes 3..6 4D1C0 / mirrored 4D62A, the pixel op CS:196A:
 *           3  4DA3A  shift the destination's low nibble by (src & m) - k
 *           4  4DA80  store when (src & 0Fh) >= t
 *           5  4DA9E  store when the pixel generator's next word > t
 *                     (seeded at entry with two 50525 draws, 4D187)
 *           6  4DAD2  store src ^ k
 * Modes 1 and 3..6 clip columns to the scene window (row + 8 ..
 * row + 223) in two phases; a left-clipped copy (4C750, 4D4F4) loads SI
 * with the copy's offset word itself, and the mirrored modes 3..6 read
 * the opcode's bit 7 inverted while right-clipped (4D893), as the
 * original does. Mode 2 clips
 * each moved pixel (row band, 0A08h..AEA0h); its skip-rows path leaves
 * the band of the row it skipped from. */

/* The driver variables these routines share, by role (the original's CS offsets in the comments). Rows: the cell's
 * rows are walked from its head (rows_left of them); y_mask (rotated per source row) keeps or drops each; row_y counts
 * the page rows for the row clip. Columns: x_mask (after the x offset) keeps or drops each source column. */
typedef struct {
    uint8_t *page;                    /* ES: a 64 KB page; 0..63999 visible */
    const uint8_t *src;               /* DS: the sprite, 64 KB */
    uint16_t row_y;                   /* C26: the page row of the next kept row (the row clip's operand) */
    uint16_t clip_top, clip_bottom;   /* C28, C2A: the row clip, clip_top .. clip_bottom - 1 */
    uint16_t x_mirrored;              /* C2C: rd_window_mirrored's x (the cell's right column) */
    uint16_t next_edge;               /* 16E0: the window edge of the page row after the current one */
    uint16_t visible;                 /* 16E2: the row's visible columns left (rd_row_ops; the original holds the
                                       * row's starting edge there first: the walkers' EDGE) */
    uint16_t next_row;                /* 16E4: the page offset of the next page row (mode 2: with the x) */
    uint16_t rows_left;               /* 16E6: the cell's source rows left (signed: the walk ends below 0) */
    uint16_t x_mask, y_mask;          /* 17C2, 17C4: the column and the row scale masks */
    uint16_t x;                       /* 17C8: the cell's x (rd_window, mode 2) */
    uint16_t gen[2];                  /* 18D2, 18D4: the pixel generator (rd_generator) */
    uint16_t band_start, band_end;    /* 18D6, 18D8: mode 2's row band (4CEC8 keeps a pixel inside it) */
    uint16_t scatter_timer_mask;      /* 18DA: mode 2's offset mask by the table entry */
    uint16_t op_bias, op_param;       /* 18E4, 18E6: the pixel op's parameters (rd_op; mode 2 writes 18E6 only) */
    uint16_t scatter_scale_mask;      /* 1908: mode 2's offset mask by scale */
    uint16_t remap_add, remap_mask;   /* 184A, 184C: mode 1's remap (rd_m1) */
    int op;                           /* the pixel op: 1 (mode 1's remap) or 3..6 */
    unsigned cf;                      /* the carry flag */
} rdrv;

static const uint16_t steps16e8[8] = {0x100, 0x101, 0x202, 0x303, 0xff00, 0xffff, 0xfefe, 0xfdfd};
static const uint8_t m1_add[24] = {0x30, 0xc0, 0xb0, 0x10, 0x41, 0x20, 0x40, 0x21,
                                   0x48, 0x46, 0x43, 0x40, 0xd0, 0xd3, 0xd6, 0xd8,
                                   0x01, 0x04, 0x07, 0x0a, 0xea, 0xee, 0xf2, 0xf6};
static const uint8_t m1_mask[24] = {0xf, 0xf, 0xf, 0xf, 0xf, 0x7, 0x7, 0xf, 0x7, 0x7, 0x7, 0x7,
                                    0x7, 0x7, 0x7, 0x7, 0xf, 0xf, 0xf, 0xf, 0x7, 0x7, 0x7, 0x7};
static const uint16_t m2_timer[4] = {0xe, 0x1e, 0x3e, 0x7e};                        /* CS:18DC */
static const uint16_t m2_e6[16] = {3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0}; /* CS:18E8 */
static const uint16_t m2_190a[16] = {0x7e, 0x7e, 0x7e, 0x7e, 0x3e, 0x3e, 0x3e, 0x3e, /* CS:190A */
                                     0x1e, 0x1e, 0x1e, 0x1e, 0xe,  0xe,  0xe,  0xe};
static const int16_t m2_offsets[64] = {-3, 3, 0, 0, 0, 0,   0,  0,  -5, 5, 0, 0, 0, 0, 0,   0,  -7,
                                       7,  0, 0, 0, 0, 0,   0,  -9, 9,  0, 0, 0, 0, 0, 0,   -7, 7,
                                       0,  0, 0, 0, 0, 0,   -9, 9,  0,  0, 0, 0, 0, 0, -11, 11, 0,
                                       0,  0, 0, 0, 0, -13, 13, 0,  0,  0, 0, 0, 0}; /* CS:184E */
static const uint16_t m3_e6[4] = {0xf01, 0xf03, 0xf07, 0xf0f}, m3_e4[4] = {1, 2, 4, 8};
static const uint16_t m4_e6[4] = {4, 7, 10, 13}, m5_e6[4] = {0x3333, 0x6666, 0x999a, 0xcccd};
static const uint16_t m6_e6[16] = {1, 2, 4, 8, 1, 3, 7, 0xf, 8, 0xc, 0xe, 0xf, 1, 2, 1, 2};

#define LO(v) ((uint8_t)(v))
#define HI(v) ((uint8_t)((v) >> 8))
/** LODSB on the mode driver's source: returns the byte at *SI and advances SI (wrapping at 64K). */
static uint8_t rd_lodsb(rdrv *r, uint16_t *si)
{
    uint8_t v = r->src[*si];
    *si = (uint16_t)(*si + 1u);
    return v;
}
/**
 * LODSW on the mode driver's source: returns the word at *SI and advances SI by two (wrapping at
 * 64K).
 */
static uint16_t rd_lodsw(rdrv *r, uint16_t *si)
{
    uint16_t v = (uint16_t)(r->src[*si] | r->src[(uint16_t)(*si + 1u)] << 8);
    *si = (uint16_t)(*si + 2u);
    return v;
}
/* 4B1CC: the scaled count of AX source pixels by the mask BP (rotated);
 * AL only unless the count is a multiple of 8. */
static uint16_t rd_1cc(uint16_t count, uint16_t *bp)
{
    unsigned al = 0, over = 0, i;
    if (!count) {
        return 0;
    }
    for (i = 1; i <= count; i++) {
        unsigned bit = *bp >> 15;
        *bp = (uint16_t)(*bp << 1 | bit);
        al += bit;
        if (al == 256u) {
            al = 0;
            over++;
        }
    }
    return (uint16_t)(count % 8u ? al : al + 256u * over);
}
/* 4B224: the same by the memory mask 17C4; AL only. */
static uint16_t rd_224(rdrv *r, uint16_t count)
{
    unsigned al = 0, i;
    for (i = 0; i < count; i++) {
        unsigned bit = r->y_mask >> 15;
        r->y_mask = (uint16_t)(r->y_mask << 1 | bit);
        al = (al + bit) & 255u;
    }
    return (uint16_t)al;
}
/** ROL by one of the 16-bit *V; returns the bit rotated out (the carry). */
static unsigned rd_rol(uint16_t *v)
{
    unsigned bit = *v >> 15;
    *v = (uint16_t)(*v << 1 | bit);
    return bit;
}
/** Writes pixel V at offset DI of the driver's page; offsets past the screen are ignored. */
static void rd_put(rdrv *r, uint16_t di, uint8_t v)
{
    if (di < XEEN_PRESENT_PIXELS) {
        r->page[di] = v;
    }
}
/** Reads the pixel at offset DI of the driver's page (0 past the screen). */
static uint8_t rd_get(const rdrv *r, uint16_t di)
{
    return di < XEEN_PRESENT_PIXELS ? r->page[di] : 0;
}
/** Returns the page offset of screen row BX (320 pixels per row): the original reads its 134A table below row 200 and
 * multiplies otherwise, the same value (16-bit). */
static uint16_t rd_row(uint16_t bx)
{
    return (uint16_t)(bx * 320u);
}
/* The pixel generator step (4CEC8 and 4DA9E): RCL, SBB, two RCR, XOR. */
static uint16_t rd_generator(rdrv *r)
{
    uint16_t bx = r->gen[0];
    unsigned c = r->cf, nc;
    uint32_t sub;
    nc = bx >> 15;
    bx = (uint16_t)(bx << 1 | c);
    c = nc;
    sub = (uint32_t)r->gen[1] + c;
    c = (uint32_t)bx < sub;
    bx = (uint16_t)(bx - sub);
    r->gen[0] = bx;
    nc = r->gen[1] & 1u;
    r->gen[1] = (uint16_t)(r->gen[1] >> 1 | c << 15);
    c = nc;
    r->gen[1] = (uint16_t)(r->gen[1] >> 1 | c << 15);
    r->gen[1] ^= bx;
    r->cf = 0;
    return bx;
}
/* 4CEC8 (mode 2): move the pixel, keep it in its row band and the view.
 * Returns BX as it leaves it (not saved: the pattern op steps with it). */
static uint16_t rd_scatter(rdrv *r, uint16_t di, uint8_t al)
{
    uint16_t bx = rd_generator(r);
    uint8_t ah = HI(bx);
    bx = (uint16_t)m2_offsets[(bx & r->scatter_timer_mask & r->scatter_scale_mask) >> 1];
    di = (uint16_t)(di + bx);
    if (di < r->band_start) {
        r->cf = 1;
        return bx;
    }
    if (di >= r->band_end) {
        r->cf = 0;
        return bx;
    }
    bx = (uint16_t)(LO((uint16_t)m2_offsets[(ah & r->scatter_timer_mask & r->scatter_scale_mask) >> 1]) << 8);
    di = (uint16_t)(di + bx);
    bx = (uint16_t)((int16_t)bx >> 2);
    di = (uint16_t)(di + bx);
    if (di < 0x0a08u) {
        r->cf = 1;
        return bx;
    }
    if (di > 0xaea0u) {
        r->cf = 0;
        return bx;
    }
    rd_put(r, di, al);
    r->cf = di < 0xaea0u;
    return bx;
}
/* The CS:196A pixel op of modes 3..6; CF as each leaves it. */
static void rd_op(rdrv *r, uint16_t di, uint8_t al)
{
    if (r->op == 3) {
        uint8_t dest = rd_get(r, di),
                v = (uint8_t)((uint8_t)(al & LO(r->op_param)) - LO(r->op_bias) + (uint8_t)(dest & HI(r->op_param)));
        if (v & 0x80u) {
            rd_put(r, di, (uint8_t)(dest & 0xf0u));
        } else if (v > 0x0fu) {
            rd_put(r, di, (uint8_t)(dest | 0x0fu));
        } else {
            rd_put(r, di, (uint8_t)((dest & 0xf0u) | v));
        }
        r->cf = 0;
    } else if (r->op == 4) {
        r->cf = (al & 0x0fu) < LO(r->op_param);
        if (!r->cf) {
            rd_put(r, di, al);
        }
    } else if (r->op == 5) {
        uint16_t ax = rd_generator(r);
        r->cf = ax < r->op_param;
        if (ax > r->op_param) {
            rd_put(r, di, al);
        }
    } else {
        rd_put(r, di, (uint8_t)(al ^ LO(r->op_param)));
        r->cf = 0;
    }
}
/* Mode 1's remap of AL (CS:184C mask, CS:184A add). */
static uint8_t rd_m1(const rdrv *r, uint8_t al)
{
    return (uint8_t)((al & LO(r->remap_mask)) + LO(r->remap_add));
}


/* The cell's x from the head's first two words (x offset, width), both counted by the column scale mask *MASK: x0
 * plus (mirrored minus) the kept offset columns, then half the width's dropped columns (the scaled cell centred in its
 * unscaled box); mirrored the kept width - 1 is added as well (x is the right column) and the width is counted from
 * the scale mask's first bit again (y_mask still holds it). x_mask is the column mask after the offset. */
static void rd_head_x(rdrv *r, uint16_t *in, uint16_t x0, uint16_t *mask, int mirrored, uint16_t *x)
{
    uint16_t offset = rd_lodsw(r, in), width, kept;
    if (!mirrored) {
        *x = (uint16_t)(x0 + rd_1cc(offset, mask));
        r->x_mask = *mask;
        width = rd_lodsw(r, in);
        kept = rd_1cc(width, mask);
    } else {
        *x = (uint16_t)(x0 - rd_1cc(offset, mask));
        r->x_mask = *mask;
        *mask = r->y_mask;
        width = rd_lodsw(r, in);
        kept = rd_1cc((uint16_t)(width - 1u), mask);
        *x = (uint16_t)(*x + kept);
    }
    *x = (uint16_t)(*x + ((uint16_t)(width - kept) >> 1));
}

/*
 * An op byte's kind, by its top bits; its low bits give the op's count.
 */
typedef enum {
    RD_LITERAL, /* 00xxxxxx */
    RD_REPEAT,  /* 010xxxxx */
    RD_COPY,    /* 011xxxxx */
    RD_PAIR,    /* 100xxxxx */
    RD_SKIP,    /* 101xxxxx */
    RD_PATTERN  /* 11xxxxxx */
} rd_op_kind;
static rd_op_kind rd_kind(uint8_t al)
{
    if (!(al & 0x80u)) {
        return !(al & 0x40u) ? RD_LITERAL : !(al & 0x20u) ? RD_REPEAT : RD_COPY;
    }
    if (!(al & 0x40u)) {
        return !(al & 0x20u) ? RD_PAIR : RD_SKIP;
    }
    return RD_PATTERN;
}

/*
 * The pen of one row's ops in modes 1 and 3..6 (4C4F4 left to right, 4C978 mirrored): the sprite's read position IN
 * (the original's SI), the page position OUT (DI; stepped by STEP: 1, or FFFFh mirrored, in 16 bits), the column mask
 * MASK (BP; rd_rol: a column is drawn only when its bit is set), the row's end END in the sprite (DX) and whether the
 * mode is 1 (M1).
 *
 * A row is drawn in up to two phases (rd_row_ops). While the pen is outside the scene window it is CLIPPING (4C64A,
 * mirrored 4CAD4): EDGE is the window's edge on the side the row starts from (row + 8 left to right, row + DFh
 * mirrored), a kept column outside it only moves the pen and nothing counts the row's columns. Inside the window
 * r->visible counts the row's visible columns left; the row ends when it reaches 0, or when the ops reach END.
 */
typedef struct {
    uint16_t in, out, mask, end, step, edge;
    int m1, clipping;
} rd_pen;
/* Whether a clipping pen's column is outside the window: left of the edge, or mirrored right of it. */
static int rd_pen_outside(const rd_pen *p)
{
    return p->step == 1u ? p->out < p->edge : p->out > p->edge;
}
/*
 * Whether a clipping pen has reached the window after an op: left to right at or past the edge (every op's JB back
 * to 4C64A); mirrored left of it (JAE back to 4CAD4), a skip already on it (4CBD7: JBE). The test at the row's start
 * is the same (4C4E4 is reached when OUT >= the left edge, 4C966 when OUT < the right edge).
 */
static int rd_pen_reached(const rd_pen *p, int after_skip)
{
    if (p->step == 1u) {
        return p->out >= p->edge;
    }
    return after_skip ? p->out <= p->edge : p->out < p->edge;
}
/* The pen moved over N kept columns without drawing them. */
static void rd_pen_move(rd_pen *p, uint16_t n)
{
    p->out = (uint16_t)(p->step == 1u ? p->out + n : p->out - n);
}

/* How a byte reaches the page in mode 1 (modes 3..6 run their pixel op on the byte itself in every case). */
typedef enum {
    RD_REMAPPED,   /* literal, copy: stored through rd_m1, the byte itself unchanged */
    RD_AS_IS,      /* pair: the word was remapped as a whole before */
    RD_REMAP_CARRY /* pattern: remapped in place, the next step adds to the remapped value */
} rd_store_kind;
/*
 * The next column of the mask at the pen, for byte *AL. A kept column is drawn and the pen steps over it; while
 * clipping, a kept column outside the window only moves the pen. Drawn: mode 1 stores the byte remapped through
 * CS:184C/184A (rd_m1) as KIND says; modes 3..6 run their CS:196A pixel op (rd_op) with the carry set first: 1
 * inside the window; while clipping it is the original's CMP DI,edge before the store (OUT < edge: 0 left to right,
 * mirrored 1 left of the edge and 0 on it), except the left-to-right pattern's 1 (4C806: its store loop has no CMP).
 * Returns 1 when the column was the row's last visible one (never while clipping).
 */
static int rd_pen_column(rdrv *r, rd_pen *p, uint8_t *al, rd_store_kind kind)
{
    if (!rd_rol(&p->mask)) {
        return 0;
    }
    if (p->clipping && rd_pen_outside(p)) {
        p->out = (uint16_t)(p->out + p->step);
        return 0;
    }
    if (p->m1) {
        uint8_t v = kind == RD_AS_IS ? *al : rd_m1(r, *al);
        rd_put(r, p->out, v);
        if (kind == RD_REMAP_CARRY) {
            *al = v;
        }
    } else {
        r->cf = !p->clipping || (kind == RD_REMAP_CARRY && p->step == 1u) ? 1u : p->out < p->edge;
        rd_op(r, p->out, *al);
    }
    p->out = (uint16_t)(p->out + p->step);
    return !p->clipping && !--r->visible;
}
/* Each op below returns 1 when the row is done (its last visible column drawn or skipped), 0 for the next op. */
/* Literal (00xxxxxx): the next AL + 1 bytes. */
static int rd_op_literal(rdrv *r, rd_pen *p, uint8_t al)
{
    uint16_t cnt = (uint16_t)(al + 1u);
    do {
        uint8_t v = rd_lodsb(r, &p->in);
        if (rd_pen_column(r, p, &v, RD_REMAPPED)) {
            return 1;
        }
    } while (--cnt);
    return 0;
}
/* CNT columns of the repeated byte AL from the pen on (the mask was applied to the count): mode 1 remaps it once and
 * stores it, modes 3..6 run the pixel op with the carry cleared once (the op's own carry then reaches the next). */
static void rd_repeat_columns(rdrv *r, rd_pen *p, uint8_t al, uint16_t cnt)
{
    if (p->m1) {
        al = rd_m1(r, al);
        while (cnt--) {
            rd_put(r, p->out, al);
            p->out = (uint16_t)(p->out + p->step);
        }
    } else {
        r->cf = 0;
        while (cnt--) {
            rd_op(r, p->out, al);
            p->out = (uint16_t)(p->out + p->step);
        }
    }
}
/* Repeat (010xxxxx): one byte over (AL & 1Fh) + 3 columns of the mask, every one written: inside the window at most
 * r->visible of them; while clipping (4C664 / 4CAEE) those past the edge, from the edge on. */
static int rd_op_repeat(rdrv *r, rd_pen *p, uint8_t al)
{
    uint16_t cnt = rd_1cc((uint16_t)((al & 0x1fu) + 3u), &p->mask);
    al = rd_lodsb(r, &p->in);
    if (p->clipping) {
        uint16_t end;
        rd_pen_move(p, cnt);
        if (!rd_pen_reached(p, 0)) {
            return 0;
        }
        end = p->out;
        p->out = p->edge;
        rd_repeat_columns(r, p, al, (uint16_t)(p->step == 1u ? end - p->edge : p->edge - end));
        return 0;
    }
    if (cnt > r->visible) {
        cnt = r->visible;
    }
    r->visible = (uint16_t)(r->visible - cnt);
    rd_repeat_columns(r, p, al, cnt);
    return !r->visible;
}
/* Copy (011xxxxx): (AL & 1Fh) + 4 bytes from the word's distance back in the sprite, then the reading goes on after
 * the word. Clipping left to right the bytes come from the word itself taken as the offset (4C750: MOV SI,[SI-2]), as
 * the original does; mirrored (4CBDC) from the distance back. */
static int rd_op_copy(rdrv *r, rd_pen *p, uint8_t al)
{
    uint16_t cnt = (uint16_t)((al & 0x1fu) + 4u), word = rd_lodsw(r, &p->in), after = p->in;
    p->in = p->clipping && p->step == 1u ? word : (uint16_t)(p->in - word);
    do {
        uint8_t v = rd_lodsb(r, &p->in);
        if (rd_pen_column(r, p, &v, RD_REMAPPED)) {
            return 1;
        }
    } while (--cnt);
    p->in = after;
    return 0;
}
/* Pair (100xxxxx): the word's two bytes (mode 1: the word remapped, (AX & m) + a) alternately over
 * 2 x ((AL & 1Fh) + 2) columns, low byte first. */
static int rd_op_pair(rdrv *r, rd_pen *p, uint8_t al)
{
    uint16_t cnt = (uint16_t)(2u * ((al & 0x1fu) + 2u)), ax = rd_lodsw(r, &p->in);
    if (p->m1) {
        ax = (uint16_t)((ax & r->remap_mask) + r->remap_add);
    }
    do {
        uint8_t v = LO(ax);
        if (rd_pen_column(r, p, &v, RD_AS_IS)) {
            return 1;
        }
        ax = (uint16_t)(ax >> 8 | ax << 8);
    } while (--cnt);
    return 0;
}
/* Skip (101xxxxx): (AL & 1Fh) + 1 columns of the mask. Left to right (4C5A2) the row ends when r->visible reaches 0
 * or below, else the pen moves past them; mirrored the pen moves back first, then the row ends when r->visible is at
 * most the count (still reduced). Clipping, the pen only moves. */
static int rd_op_skip(rdrv *r, rd_pen *p, uint8_t al)
{
    uint16_t n = rd_1cc((uint16_t)((al & 0x1fu) + 1u), &p->mask);
    int done;
    if (p->clipping) {
        rd_pen_move(p, n);
        return 0;
    }
    if (p->step == 1u) {
        r->visible = (uint16_t)(r->visible - n);
        if (!r->visible || (r->visible & 0x8000u)) {
            return 1;
        }
        p->out = (uint16_t)(p->out + n);
        return 0;
    }
    p->out = (uint16_t)(p->out - n);
    done = r->visible <= n;
    r->visible = (uint16_t)(r->visible - n);
    return done;
}
/* Pattern (11xxxxxx): a byte over (AL & 7) + 3 columns, stepped after each column by the table's low byte, then its
 * high byte, alternately (in mode 1 a drawn column's remapped byte is the one stepped: rd_pen_column). */
static int rd_op_pattern(rdrv *r, rd_pen *p, uint8_t al)
{
    uint16_t step = steps16e8[(al >> 3) & 7u], cnt = (uint16_t)((al & 7u) + 3u), col;
    al = rd_lodsb(r, &p->in);
    for (col = 0; col < cnt; col++) {
        if (rd_pen_column(r, p, &al, RD_REMAP_CARRY)) {
            return 1;
        }
        al = (uint8_t)(al + (col & 1u ? HI(step) : LO(step)));
    }
    return 0;
}
static int rd_run_op(rdrv *r, rd_pen *p, rd_op_kind kind, uint8_t al)
{
    switch (kind) {
    case RD_LITERAL:
        return rd_op_literal(r, p, al);
    case RD_REPEAT:
        return rd_op_repeat(r, p, al);
    case RD_COPY:
        return rd_op_copy(r, p, al);
    case RD_PAIR:
        return rd_op_pair(r, p, al);
    case RD_SKIP:
        return rd_op_skip(r, p, al);
    default:
        return rd_op_pattern(r, p, al);
    }
}
/*
 * A row's ops from the pen in its two phases. Clipping (the pen started outside the window): op by op until the pen
 * has reached the window, or the row's bytes end first (nothing more is drawn). The original measures each op's kept
 * columns first and replays only the op that crosses the edge; the replay of every op here is the same, since the
 * columns outside store nothing. The mirrored modes 3..6 read the op byte's bit 7 inverted in this phase (4D893 tests
 * it with JS where 4CADD has JNS); the counts still come from the whole byte. Then (4C4E4 / 4C966) r->visible is the
 * row's visible columns left, from the pen to the window's far side (D8h columns), and the ops go on until the row
 * is done or its bytes end.
 */
static void rd_row_ops(rdrv *r, rd_pen *p)
{
    p->clipping = !rd_pen_reached(p, 0);
    while (p->clipping) {
        uint8_t al;
        rd_op_kind kind;
        if (p->in == p->end) {
            return;
        }
        al = rd_lodsb(r, &p->in);
        kind = rd_kind(p->step == 1u || p->m1 ? al : (uint8_t)(al ^ 0x80u));
        (void)rd_run_op(r, p, kind, al);
        p->clipping = !rd_pen_reached(p, kind == RD_SKIP);
    }
    r->visible = (uint16_t)(p->step == 1u ? p->edge + 0xd8u - p->out : p->out + 0xd8u - p->edge);
    if (!r->visible || (r->visible & 0x8000u)) {
        return;
    }
    while (p->in != p->end) {
        uint8_t al = rd_lodsb(r, &p->in);
        if (rd_run_op(r, p, rd_kind(al), al)) {
            return;
        }
    }
}

/*
 * The row walkers' shared steps. A row starts with its length byte (the bytes after it); 0 starts a run of empty
 * rows (rd_skip_rows). y_mask is the vertical scale mask: a row whose bit is clear is dropped (the page row does not
 * advance). row_y counts the page rows, tested against the row clip.
 */
/* A zero row length (4C44A, 4C8BC, 4CDEC): the next byte N counts the empty rows after this one. rows_left loses N,
 * row_y counts this row and the rows y_mask keeps of the N + 1 (4B224), and the page moves by those
 * kept rows: the return value (rows x 320, 16 bits). */
static uint16_t rd_skip_rows(rdrv *r, uint16_t *in)
{
    uint16_t n, kept;
    r->row_y++;
    n = rd_lodsb(r, in);
    r->rows_left = (uint16_t)(r->rows_left - n);
    kept = rd_224(r, (uint16_t)(n + 1u));
    r->row_y = (uint16_t)(r->row_y + kept);
    return (uint16_t)(kept * 320u);
}
/* Whether the kept row's page row row_y (counted here) lies inside the row clip clip_top .. clip_bottom - 1. */
static int rd_row_in_clip(rdrv *r)
{
    uint16_t row = r->row_y++;
    return row >= r->clip_top && row < r->clip_bottom;
}
/* The pen at the start of a kept row of LEN bytes at *IN, page position OUT (its x applied), direction STEP, window
 * edge EDGE, mode 1's remap M1: the row ends at *IN + LEN; its first byte counts the leading transparent columns,
 * taken by x_mask (rd_1cc), and the pen moves past them. Not clipping (rd_row_ops decides its own); *IN passes the
 * count byte. */
static rd_pen rd_row_pen(rdrv *r, uint16_t *in, uint16_t out, uint16_t len, uint16_t step, uint16_t edge, int m1)
{
    rd_pen p;
    uint16_t lead;
    p.end = (uint16_t)(*in + len);
    lead = rd_lodsb(r, in);
    p.mask = r->x_mask;
    lead = rd_1cc(lead, &p.mask);
    p.in = *in;
    p.out = out;
    p.step = step;
    p.edge = edge;
    p.m1 = m1;
    p.clipping = 0;
    rd_pen_move(&p, lead);
    return p;
}
/* One kept row of modes 1 and 3..6 at page position OUT (its x applied) of LEN bytes at *IN: its leading transparent
 * columns (by x_mask), then its ops (rd_row_ops) against the window edge EDGE; *IN moves to the row's end. */
static void rd_window_row(rdrv *r, uint16_t *in, uint16_t out, uint16_t len, uint16_t step, uint16_t edge)
{
    rd_pen p = rd_row_pen(r, in, out, len, step, edge, r->op == 1);
    rd_row_ops(r, &p);
    *in = p.end;
}

/* Modes 1 and 3..6, left to right: 4C3CC (op 1) and 4D1C0. The page rows are walked by next_row; next_edge is the
 * left edge of the row after the current one. */
static void rd_window(rdrv *r, uint16_t in, uint16_t y, uint16_t x0, uint16_t mask0)
{
    uint16_t mask = mask0, x, out, len, edge;
    r->y_mask = mask;
    rd_head_x(r, &in, x0, &mask, 0, &x);
    r->x = x;
    y = (uint16_t)(y + rd_224(r, rd_lodsw(r, &in)));
    r->rows_left = rd_lodsw(r, &in);
    r->row_y = y;
    r->next_row = rd_row(y);
    r->next_edge = (uint16_t)(r->next_row + 8u);
    for (;;) {
        /* 4C47A: the next page row and its left edge */
        out = r->next_row;
        r->next_row = (uint16_t)(r->next_row + 320u);
        edge = r->next_edge;
        r->next_edge = (uint16_t)(r->next_edge + 320u);
        for (;;) { /* 4C496: up to the next row the vertical mask keeps */
            if ((int16_t)--r->rows_left < 0) {
                return;
            }
            len = rd_lodsb(r, &in);
            if (!len) { /* the page moves past the kept empty rows; the edge stays the one of the row skipped from */
                uint16_t dist = rd_skip_rows(r, &in);
                r->next_edge = (uint16_t)(r->next_edge + dist);
                r->next_row = (uint16_t)(r->next_row + dist);
                out = r->next_row;
            } else if (!rd_rol(&r->y_mask)) {
                in = (uint16_t)(in + len);
            } else {
                break;
            }
        }
        if (!rd_row_in_clip(r)) {
            in = (uint16_t)(in + len);
            continue;
        }
        rd_window_row(r, &in, (uint16_t)(out + r->x), len, 1u, edge);
    }
}

/* Modes 1 and 3..6, mirrored (right to left): 4C830 (op 1) and 4D62A. As rd_window, with next_edge the right edge
 * (row + DFh); here an empty-row run moves the edge with the page, and a dropped row puts OUT on the row before
 * next_row. */
static void rd_window_mirrored(rdrv *r, uint16_t in, uint16_t y, uint16_t x0, uint16_t mask0)
{
    uint16_t mask = mask0, x, out, len, edge;
    r->y_mask = mask;
    rd_head_x(r, &in, x0, &mask, 1, &x);
    r->x_mirrored = x;
    y = (uint16_t)(y + rd_224(r, rd_lodsw(r, &in)));
    r->rows_left = rd_lodsw(r, &in);
    r->row_y = y;
    r->next_row = rd_row(y);
    r->next_edge = (uint16_t)(r->next_row + 0xdfu);
    for (;;) {
        /* 4C8FE: the next page row and its right edge */
        out = r->next_row;
        r->next_row = (uint16_t)(r->next_row + 320u);
        edge = r->next_edge;
        r->next_edge = (uint16_t)(r->next_edge + 320u);
        for (;;) { /* 4C91A */
            if ((int16_t)--r->rows_left < 0) {
                return;
            }
            len = rd_lodsb(r, &in);
            if (!len) {
                uint16_t dist = rd_skip_rows(r, &in);
                r->next_row = (uint16_t)(r->next_row + dist);
                out = r->next_row;
                r->next_edge = (uint16_t)(r->next_edge + dist);
                edge = r->next_edge;
            } else if (!rd_rol(&r->y_mask)) {
                in = (uint16_t)(in + len);
                out = (uint16_t)(r->next_row - 320u);
            } else {
                break;
            }
        }
        if (!rd_row_in_clip(r)) {
            in = (uint16_t)(in + len);
            continue;
        }
        rd_window_row(r, &in, (uint16_t)(out + r->x_mirrored), len, 0xffffu, edge);
    }
}

/* One column of mode 2: when the mask keeps it, pixel V is scattered (4CEC8, entered with CF set) and the pen steps;
 * returns 1 then, with *STEP the BX 4CEC8 leaves (the pattern op steps by it). */
static int rd_scatter_column(rdrv *r, rd_pen *p, uint8_t v, uint16_t *step)
{
    if (!rd_rol(&p->mask)) {
        return 0;
    }
    r->cf = 1;
    *step = rd_scatter(r, p->out, v);
    p->out = (uint16_t)(p->out + p->step);
    return 1;
}
/* Mode 2's ops of a row (4CDAA) until its bytes end: no window and no column count (4CEC8 clips each pixel). The
 * repeat scatters all its kept columns, entering 4CEC8 with the carry cleared once (each call's carry then reaches the
 * next); the pattern steps by 4CEC8's BX after a kept column (the table's step until the first). */
static void rd_scatter_ops(rdrv *r, rd_pen *p)
{
    while (p->in != p->end) {
        uint8_t al = rd_lodsb(r, &p->in);
        uint16_t cnt, word, step;
        switch (rd_kind(al)) {
        case RD_LITERAL:
            cnt = (uint16_t)(al + 1u);
            do {
                (void)rd_scatter_column(r, p, rd_lodsb(r, &p->in), &step);
            } while (--cnt);
            break;
        case RD_REPEAT:
            cnt = rd_1cc((uint16_t)((al & 0x1fu) + 3u), &p->mask);
            al = rd_lodsb(r, &p->in);
            r->cf = 0;
            while (cnt--) {
                (void)rd_scatter(r, p->out, al);
                p->out = (uint16_t)(p->out + p->step);
            }
            break;
        case RD_COPY: {
            uint16_t after;
            cnt = (uint16_t)((al & 0x1fu) + 4u);
            word = rd_lodsw(r, &p->in);
            after = p->in;
            p->in = (uint16_t)(p->in - word);
            do {
                (void)rd_scatter_column(r, p, rd_lodsb(r, &p->in), &step);
            } while (--cnt);
            p->in = after;
            break;
        }
        case RD_PAIR:
            cnt = (uint16_t)(2u * ((al & 0x1fu) + 2u));
            word = rd_lodsw(r, &p->in);
            do {
                (void)rd_scatter_column(r, p, LO(word), &step);
                word = (uint16_t)(word >> 8 | word << 8);
            } while (--cnt);
            break;
        case RD_SKIP:
            rd_pen_move(p, rd_1cc((uint16_t)((al & 0x1fu) + 1u), &p->mask));
            break;
        default: {
            uint16_t col;
            step = steps16e8[(al >> 3) & 7u];
            cnt = (uint16_t)((al & 7u) + 3u);
            al = rd_lodsb(r, &p->in);
            for (col = 0; col < cnt; col++) {
                (void)rd_scatter_column(r, p, al, &step);
                al = (uint8_t)(al + (col & 1u ? HI(step) : LO(step)));
            }
            break;
        }
        }
    }
}

/* Mode 2 (4CCAC, mirrored 4CF38): every kept pixel goes through 4CEC8. next_row is the next page row with the x
 * applied; band_start/band_end the row band 4CEC8 keeps a pixel in (row + 8 .. row + DFh). An empty-row run moves the
 * page only: the band stays the one of the row skipped from; a dropped row puts OUT on the row before next_row. */
static void rd_scattered(rdrv *r, uint16_t in, uint16_t y, uint16_t x0, uint16_t mask0, int mirrored)
{
    uint16_t mask = mask0, x, out, len;
    r->y_mask = mask;
    rd_head_x(r, &in, x0, &mask, mirrored, &x);
    r->x = x;
    y = (uint16_t)(y + rd_224(r, rd_lodsw(r, &in)));
    r->rows_left = rd_lodsw(r, &in);
    r->row_y = y;
    r->next_edge = (uint16_t)(rd_row(y) + 8u);
    r->next_row = (uint16_t)(r->next_edge + r->x - 8u);
    for (;;) {
        /* 4CD4C: the next page row and its band */
        out = r->next_row;
        r->next_row = (uint16_t)(r->next_row + 320u);
        r->band_start = r->next_edge;
        r->band_end = (uint16_t)(r->next_edge + 0xd8u);
        r->next_edge = (uint16_t)(r->next_edge + 320u);
        for (;;) { /* 4CD6E */
            if ((int16_t)--r->rows_left < 0) {
                return;
            }
            len = rd_lodsb(r, &in);
            if (!len) {
                r->next_row = (uint16_t)(r->next_row + rd_skip_rows(r, &in));
                out = r->next_row;
            } else if (!rd_rol(&r->y_mask)) {
                in = (uint16_t)(in + len);
                out = (uint16_t)(r->next_row - 320u);
            } else {
                break;
            }
        }
        if (!rd_row_in_clip(r)) {
            in = (uint16_t)(in + len);
            continue;
        }
        {
            rd_pen p = rd_row_pen(r, &in, out, len, mirrored ? 0xffffu : 1u, 0, 0); /* no edge, no mode 1 remap */
            rd_scatter_ops(r, &p);
            in = p.end;
        }
    }
}

#define RD_ENTRIES(table) (sizeof(table) / sizeof((table)[0]))
/* The parallel tables a mode indexes with one index, and the scale tables (scale 0..15). */
_Static_assert(RD_ENTRIES(m1_mask) == RD_ENTRIES(m1_add), "mode 1's tables");
_Static_assert(RD_ENTRIES(m3_e4) == RD_ENTRIES(m3_e6), "mode 3's tables");
_Static_assert(RD_ENTRIES(m2_e6) == 16u && RD_ENTRIES(m2_190a) == 16u && RD_ENTRIES(masks) == 16u, "the scale tables");
/* 4AEB8 for modes 1..6 (scale bit 15 clear): one frame cell at SI of the
 * 64 KB sprite image. flags & 0F00h selects the mode, its low byte the
 * table entry, bit 15 the mirrored routine; scale indexes CS:17A2.
 * rng holds the two 50525 results and carries of this call (modes 2 and 5).
 * Returns 0 for an entry outside the tables (a boundary). */
static int rd_cell(rdrv *r, uint16_t si, uint16_t x, uint16_t y, uint16_t scale, uint16_t flags,
                   const uint16_t rng[2], const uint8_t carry[2])
{
    unsigned mode = XEEN_DRAW_MODE_OF(flags), t = flags & 0xffu,
             mirrored = (flags & XEEN_DRAW_MIRRORED) != 0;
    if (scale > 15u || !mode) {
        return 0;
    }
    if (mirrored && mode > 6u) {
        mode = 6; /* 4B2AC: mirrored 6..15 -> 4D608 */
    }
    switch (mode) {
        case 1:
            if (t >= RD_ENTRIES(m1_add)) {
                return 0;
            }
            r->remap_add = (uint16_t)(m1_add[t] * 0x101u);
            r->remap_mask = (uint16_t)(m1_mask[t] * 0x101u);
            break;
        case 2:
            if (t >= RD_ENTRIES(m2_timer)) {
                return 0;
            }
            r->scatter_timer_mask = m2_timer[t];
            r->op_param = m2_e6[scale];
            r->scatter_scale_mask = m2_190a[scale];                /* 4CCC9: CS:190A[scale] into 1908 */
            r->gen[0] = (uint16_t)(r->gen[0] + rng[0] + carry[0]); /* ADC 18D2 */
            r->gen[1] = (uint16_t)(r->gen[1] - rng[1] - carry[1]); /* SBB 18D4 */
            rd_scattered(r, si, y, x, masks[scale], (int)mirrored);
            return 1;
        case 3:
            if (t >= RD_ENTRIES(m3_e6)) {
                return 0;
            }
            r->op_param = m3_e6[t];
            r->op_bias = m3_e4[t];
            break;
        case 4:
            if (t >= RD_ENTRIES(m4_e6)) {
                return 0;
            }
            r->op_param = m4_e6[t];
            break;
        case 5:
            if (t >= RD_ENTRIES(m5_e6)) {
                return 0;
            }
            r->op_param = m5_e6[t];
            r->gen[0] = (uint16_t)(r->gen[0] + rng[0] + carry[0]); /* the same ADC and SBB as mode 2 */
            r->gen[1] = (uint16_t)(r->gen[1] - rng[1] - carry[1]);
            break;
        case 6:
            if (t >= RD_ENTRIES(m6_e6)) {
                return 0;
            }
            r->op_param = m6_e6[t];
            break;
        default: /* 7..15, plain */
            return 0;
    }
    r->op = (int)mode; /* the window modes 1 and 3..6 */
    if (mirrored) {
        rd_window_mirrored(r, si, y, x, masks[scale]);
    } else {
        rd_window(r, si, y, x, masks[scale]);
    }
    return 1;
}
