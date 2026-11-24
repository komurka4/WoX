/* Owner-approved add-on map_movement (Phase188; not original behavior); see
 * include/xeen/addon/map_movement.h. Pure view arithmetic: no DS writes. */
#include "xeen/addon/map_movement.h"
#include "xeen/ds_globals.h"
#include "xeen/map_record.h"
#include <string.h>

/** Reads the little-endian DS word at AT (the high byte's offset wraps at 64K). */
static uint16_t ds16(const xeen_movement *m, uint16_t at) { return (uint16_t)(m->ds[at] | m->ds[(uint16_t)(at + 1u)] << 8); }
/** Returns V limited to LO..HI. */
static int16_t clamp(int v, int lo, int hi) { return (int16_t)(v < lo ? lo : v > hi ? hi : v); }
/** Returns the smaller of A and B. */
static int imin(int a, int b) { return a < b ? a : b; }
/** Returns the larger of A and B. */
static int imax(int a, int b) { return a > b ? a : b; }

void xeen_map_pan_open(xeen_map_pan *p, const xeen_movement *m, const xeen_map_view *v)
{
    uint16_t base;
    if (!p) return;
    memset(p, 0, sizeof *p);
    if (!m || !v) return;
    p->open = 1;
    p->base_x = p->x = v->x; p->base_y = p->y = v->y;
    /* Valid content only: 1C680 resolves one neighbour per axis through the
     * F494 slot's links (+302h N, +304h E, +306h S, +308h W; xeen_map_begin set
     * F494), for x and y in -16..31 (its bit-4 test; above 31 is 8888). The
     * pass reads columns x - 8 .. x + 9 (the indoor edges) and rows y - 8 ..
     * y + 8: an origin from -8 (a west / south link) or 7 / 8 (none) to 23 / 24
     * (an east / north link) or 7 / 8 (none). A 32 x 32 town (N and E links)
     * gets exactly the original's own clamps 7..17h / 8..18h. The original's
     * origin is always kept in range (px 8 unlinked gives 8). */
    base = (uint16_t)(ds16(m, XEEN_DS_MAP_SLOT) * XEEN_MAP_RECORD_BYTES);
    p->lo_x = (int16_t)imin(v->x, ds16(m, (uint16_t)(base + XEEN_DS_MAP_FIELD(WEST))) ? -8 : 7);
    p->hi_x = (int16_t)imax(v->x, ds16(m, (uint16_t)(base + XEEN_DS_MAP_FIELD(EAST))) ? 23 : 7);
    p->lo_y = (int16_t)imin(v->y, ds16(m, (uint16_t)(base + XEEN_DS_MAP_FIELD(SOUTH))) ? -8 : 8);
    p->hi_y = (int16_t)imax(v->y, ds16(m, (uint16_t)(base + XEEN_DS_MAP_FIELD(NORTH))) ? 24 : 8);
}
void xeen_map_pan_close(xeen_map_pan *p) { if (p) memset(p, 0, sizeof *p); }
int xeen_map_pan_shifted(const xeen_map_pan *p) { return p && p->open && (p->x != p->base_x || p->y != p->base_y); }

int xeen_map_pan_key(xeen_map_pan *p, uint16_t key)
{
    int dx = 0, dy = 0;
    if (!p || !p->open) return 0;
    switch (key) {
    case 0xf0u: dx = -1; break;      /* Left */
    case 0xf1u: dx = 1; break;       /* Right */
    case 0xf2u: dy = 1; break;       /* Up: north */
    case 0xf3u: dy = -1; break;      /* Down: south */
    default: return 0;
    }
    p->x = clamp(p->x + dx, p->lo_x, p->hi_x);
    p->y = clamp(p->y + dy, p->lo_y, p->hi_y);
    return 1;
}
void xeen_map_pan_drag_begin(xeen_map_pan *p, uint16_t px, uint16_t py)
{
    if (!p || !p->open) return;
    p->dragging = 1; p->anchor_px = px; p->anchor_py = py; p->anchor_x = p->x; p->anchor_y = p->y;
}
void xeen_map_pan_drag_move(xeen_map_pan *p, uint16_t px, uint16_t py)
{
    int dx, dy;
    if (!p || !p->open || !p->dragging) return;
    /* The image follows the pointer: right shows the west, down the north.
     * C division truncates: no cell moves before a whole cell's distance. */
    dx = ((int)px - (int)p->anchor_px) / 10;
    dy = ((int)py - (int)p->anchor_py) / 8;
    p->x = clamp(p->anchor_x - dx, p->lo_x, p->hi_x);
    p->y = clamp(p->anchor_y + dy, p->lo_y, p->hi_y);
}
void xeen_map_pan_drag_end(xeen_map_pan *p) { if (p) p->dragging = 0; }
int xeen_map_pan_inside(const uint16_t r[4], uint16_t px, uint16_t py)
{
    return r && px >= r[0] && px < (unsigned)r[0] + r[2] && py >= r[1] && py < (unsigned)r[1] + r[3];
}
void xeen_map_pan_view(const xeen_map_pan *p, const xeen_movement *m, const xeen_map_view *base, xeen_map_view *out)
{
    int px, py, col, idx;
    if (!out || !base) return;
    *out = *base;
    if (!p || !m || !p->open) return;
    out->x = p->x; out->y = p->y;
    px = xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]); py = xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]);
    col = px - p->x + 7;             /* 1DA96: [BP-0Bh] = col x 10 + 4 */
    idx = py - p->y + 8;             /* 1DAF6: [BP-0Ch] = (B(4F9E + idx) << 3) + 0Dh */
    if (col < 0 || col > 15 || idx < 0 || idx > 15) { out->blink = 0; return; }
    out->arrow_x = (uint8_t)(col * 10 + 4);
    out->arrow_y = (uint8_t)((m->ds[(uint16_t)(XEEN_DS_MINIMAP_ROWS + (unsigned)idx)] << 3) + 0x0d);
}
