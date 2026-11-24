#ifndef XEEN_ADDON_MAP_MOVEMENT_H
#define XEEN_ADDON_MAP_MOVEMENT_H
#include "xeen/minimap.h"
/* Owner-approved add-on map_movement (Phase188; NOT original XEEN.DAT
 * behavior, off unless --addon map_movement or wox.cfg [addons]
 * map_movement = on): the M map (1DA2D, opened by M or the Map icon) can be
 * panned with the arrow keys and by dragging it with the mouse; a click inside
 * its window does not close it (a click outside still does, as do Esc and the
 * original's other closing answers).
 *
 * The pan is a different view origin used for drawing only. Units are map
 * cells (10 x 8 screen pixels); an arrow key moves one cell, Right = east
 * (x + 1), Up = north (y + 1); a drag moves the map image with the pointer.
 * The origin stays within valid map content: the maze lookup's domain (one
 * linked neighbour per axis, coordinates -16..31). An axis without a link does
 * not pan (a 16-cell map fills the 16-cell view); a linked axis reaches the
 * neighbour (a 32 x 32 town: exactly the original's own clamps 7..23, 8..24).
 * Unseen cells stay hidden as the original draws them. Every open starts at
 * the original origin; nothing is saved.
 *
 * Code: src/addon/map_movement/ (map_pan.c the view arithmetic, map_modal.inc
 * the map modal's part, included by the game). */
typedef struct {
    int open, dragging;
    int16_t base_x, base_y;          /* the original origin (xeen_map_begin) */
    int16_t x, y;                    /* the shown origin */
    int16_t lo_x, hi_x, lo_y, hi_y;  /* its range */
    uint16_t anchor_px, anchor_py;   /* the drag's press (screen) */
    int16_t anchor_x, anchor_y;      /* the origin at the press */
} xeen_map_pan;

/* A map was opened with VIEW (xeen_map_begin's result): origin = the
 * original's, range from the link words. */
void xeen_map_pan_open(xeen_map_pan *, const xeen_movement *, const xeen_map_view *);
/* The map closed: offset and drag discarded. */
void xeen_map_pan_close(xeen_map_pan *);
/* Nonzero when the shown origin differs from the original's. */
int xeen_map_pan_shifted(const xeen_map_pan *);
/* An arrow key's answer (F0 Left, F1 Right, F2 Up, F3 Down): pans one cell
 * (clamped) and returns 1; 0 for any other key. */
int xeen_map_pan_key(xeen_map_pan *, uint16_t key);
/* The drag: begin at the press point, follow the pointer, end. */
void xeen_map_pan_drag_begin(xeen_map_pan *, uint16_t px, uint16_t py);
void xeen_map_pan_drag_move(xeen_map_pan *, uint16_t px, uint16_t py);
void xeen_map_pan_drag_end(xeen_map_pan *);
/* Whether the screen point lies in the rectangle (x, y, width, height). */
int xeen_map_pan_inside(const uint16_t rect[4], uint16_t px, uint16_t py);
/* The view to draw: BASE with the shown origin and the party arrow moved with
 * the content (the original's formula, xeen_map_begin); the arrow is hidden
 * (blink 0) when the party's cell is outside the 16 x 16 view. */
void xeen_map_pan_view(const xeen_map_pan *, const xeen_movement *, const xeen_map_view *base, xeen_map_view *out);
#endif
