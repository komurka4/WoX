#!/usr/bin/env python3
"""Add-on training_preview (owner-approved, NOT original): generates
prev_icon.inc, the Prev button's 24x20 normal and pressed frames.

The stone is the original train.icn ESC tile (frame 0, the same pixels and
palette entries in both worlds; captured from the native trainer screen) with
its X glyph replaced by stone sampled from the tile's own top rows and an
staircase-and-arrow glyph (the levels) drawn in the X's stroke colours. The
pressed frame follows the rule measured on the original's ESC tile (frame 1):
the picture moved one pixel right and down, the top row and the left and
right columns colour 1Fh, the bottom row kept.
    python3 src/addon/training_preview/make_prev_icon.py > src/addon/training_preview/prev_icon.inc
"""
ESC = [[151, 153, 159, 156, 155, 158, 152, 148, 151, 150, 147, 151, 152, 149, 155, 157, 152, 160, 162, 165, 161, 224, 31, 31], [163, 161, 155, 157, 161, 156, 156, 163, 163, 160, 161, 164, 157, 153, 157, 157, 155, 155, 159, 159, 161, 174, 224, 31], [153, 159, 156, 172, 173, 168, 170, 173, 170, 163, 169, 158, 169, 164, 170, 174, 171, 168, 161, 163, 161, 172, 174, 31], [148, 159, 162, 169, 225, 225, 225, 225, 225, 150, 164, 161, 170, 225, 225, 225, 225, 225, 145, 154, 162, 172, 172, 31], [152, 166, 154, 170, 225, 225, 225, 151, 149, 151, 161, 163, 155, 150, 172, 225, 225, 225, 146, 157, 159, 172, 172, 31], [194, 166, 158, 170, 225, 166, 225, 225, 151, 161, 164, 158, 155, 169, 225, 225, 158, 225, 150, 154, 162, 173, 172, 31], [149, 165, 163, 166, 225, 150, 169, 225, 225, 150, 169, 158, 169, 225, 225, 153, 169, 225, 150, 158, 160, 172, 173, 31], [156, 166, 163, 162, 149, 151, 161, 170, 225, 225, 153, 166, 225, 225, 148, 160, 162, 154, 152, 168, 161, 169, 172, 31], [158, 162, 162, 156, 155, 160, 156, 163, 173, 225, 225, 225, 225, 149, 168, 166, 163, 170, 161, 156, 163, 169, 169, 31], [150, 159, 162, 153, 159, 155, 153, 161, 171, 225, 225, 225, 225, 148, 173, 158, 154, 162, 166, 166, 163, 173, 169, 31], [153, 167, 160, 172, 171, 163, 158, 170, 225, 225, 151, 168, 225, 225, 151, 166, 163, 171, 172, 162, 164, 172, 173, 31], [151, 168, 162, 170, 225, 150, 169, 225, 225, 150, 166, 163, 172, 225, 225, 151, 168, 225, 151, 172, 169, 172, 172, 31], [152, 156, 166, 174, 225, 169, 225, 225, 147, 161, 165, 165, 161, 168, 225, 225, 155, 225, 154, 166, 169, 173, 172, 31], [148, 163, 166, 172, 225, 225, 225, 152, 165, 169, 163, 162, 172, 173, 172, 225, 225, 225, 151, 166, 163, 224, 173, 31], [150, 161, 166, 171, 225, 225, 225, 225, 225, 152, 158, 162, 171, 225, 225, 225, 225, 225, 152, 163, 169, 224, 224, 31], [153, 159, 164, 163, 151, 152, 152, 147, 155, 153, 165, 162, 168, 156, 155, 150, 153, 159, 151, 169, 169, 224, 224, 31], [155, 166, 168, 172, 224, 224, 225, 224, 172, 169, 164, 169, 164, 163, 173, 172, 169, 164, 160, 166, 172, 225, 224, 31], [31, 30, 225, 225, 224, 173, 224, 225, 224, 224, 173, 172, 224, 172, 224, 225, 224, 173, 173, 224, 173, 225, 225, 31], [31, 31, 30, 225, 225, 224, 173, 224, 225, 224, 224, 173, 172, 224, 172, 224, 225, 224, 173, 173, 224, 173, 225, 31], [31, 31, 31, 31, 31, 31, 31, 31, 30, 30, 29, 30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31]]
GLYPH, LIT = 0xE1, 0x97                    # the X's dark stroke and its bright edge
pool = [v for row in ESC[1:3] for v in row[1:21] if 0x93 <= v <= 0xAE]
tile = [list(r) for r in ESC]
box = [(y, x) for y in range(3, 15) for x in range(4, 19)]   # the X's box: stone again
for y, x in box:
    tile[y][x] = pool[(y * 7 + x * 13 + (x * y) % 5) % len(pool)]
for _ in range(2):                          # smoothed (the 93h..AEh ramp is ordered by brightness)
    prev = [list(r) for r in tile]
    for y, x in box:
        near = [prev[y + dy][x + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1)]
        tile[y][x] = round(sum(near) / 9)
# the levels: a staircase two pixels thick rising to the right, an arrow at its top
path = [(5, 13), (8, 13), (8, 10), (11, 10), (11, 7), (14, 7), (14, 4), (17, 4)]
mask = [[0] * 24 for _ in range(20)]
for (x0, y0), (x1, y1) in zip(path, path[1:]):
    for x in range(min(x0, x1), max(x0, x1) + 1):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            mask[y][x] = mask[y + 1][x] = mask[y][x + 1] = 1
for x, y in ((16, 2), (15, 3), (16, 3), (17, 3), (18, 3)):   # the arrow head over the top step
    mask[y][x] = 1
for y in range(20):
    for x in range(24):
        if mask[y][x]:
            tile[y][x] = GLYPH
        elif (x > 0 and mask[y][x - 1]) or (y > 0 and mask[y - 1][x]):
            tile[y][x] = LIT                # the bright edge right of and below the stroke
def press(n):
    p = [[0x1F] * 24 for _ in range(20)]
    for y in range(1, 19):
        for x in range(1, 23):
            p[y][x] = n[y - 1][x - 1]
    p[19] = list(n[19])
    for y in range(20):
        p[y][0] = p[y][23] = 0x1F
    return p
def emit(name, t):
    print('static const uint8_t %s[20][24]={' % name)
    for r in t:
        print('    {' + ','.join('0x%02x' % v for v in r) + '},')
    print('};')
print('/* Generated by make_prev_icon.py (add-on training_preview, NOT original):')
print(' * the Prev button, palette indices; normal and pressed. Do not edit. */')
emit('prev_icon_normal', tile)
emit('prev_icon_pressed', press(tile))
