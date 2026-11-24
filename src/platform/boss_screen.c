/* The F10 boss screen, a
 * static desktop calculator at the game's 320x200, with its own 6-bit
 * palette. No game imagery, no computation: any key dismisses it. */
#include "xeen/platform.h"
#include <string.h>
enum {
    BG = 1,
    FUNC,
    DIGIT,
    ACCENT,
    BORDER,
    TEXT,
    GREY,
    WHITE
};
/* 5x7 glyphs; the controls 1..7 are the calculator's symbols. */
static const struct {
    char c;
    const char *rows[7];
} font[] = {
        {'0', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
        {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
        {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
        {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
        {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
        {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
        {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
        {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
        {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
        {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
        {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
        {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
        {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
        {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
        {'%', {"##..#", "##.#.", "...#.", "..#..", ".#...", ".#.##", "#..##"}},
        {'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
        {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
        {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
        {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
        {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
        {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
        {'a', {".....", ".....", ".###.", "....#", ".####", "#...#", ".####"}},
        {'d', {"....#", "....#", ".##.#", "#..##", "#...#", "#...#", ".####"}},
        {'n', {".....", ".....", "#.##.", "##..#", "#...#", "#...#", "#...#"}},
        {'r', {".....", ".....", "#.##.", "##..#", "#....", "#....", "#...."}},
        {'t', {".#...", ".#...", "###..", ".#...", ".#...", ".#..#", "..##."}},
        {'x', {".....", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#"}},
        {'\1', {".....", "..#..", ".....", "#####", ".....", "..#..", "....."}}, /* divide */
        {'\2', {".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "....."}}, /* times */
        {'\3', {"..#..", "..#..", "#####", "..#..", "..#..", ".....", "#####"}}, /* plus-minus */
        {'\4', {"..###", "..#..", "..#..", "..#..", "#.#..", ".##..", "..#.."}}, /* square root */
        {'\5', {".##..", "...#.", "..#..", ".###.", ".....", ".....", "....."}}, /* superscript 2 */
        {'\6', {".....", "..###", ".#.##", "#.#.#", ".#.##", "..###", "....."}}, /* backspace */
        {'\7', {".....", "#####", ".....", "#####", ".....", "#####", "....."}}}; /* menu */
static void fill(uint8_t *px, int x0, int y0, int w, int h, uint8_t c)
{
    int x, y;
    for (y = y0; y < y0 + h; ++y) {
        for (x = x0; x < x0 + w; ++x) {
            if (x >= 0 && x < 320 && y >= 0 && y < 200) {
                px[y * 320 + x] = c;
            }
        }
    }
}
/** Returns the width in pixels of S in the boss screen's built-in font at SCALE. */
static int text_width(const char *s, int scale)
{
    int n = (int)strlen(s);
    return n ? n * 6 * scale - scale : 0;
}
/** Draws S in the boss screen's built-in font at X, Y with SCALE and colour C. */
static void draw_text(uint8_t *px, int x, int y, const char *s, int scale, uint8_t c)
{
    for (; *s; ++s, x += 6 * scale) {
        size_t g;
        int r, k;
        for (g = 0; g < sizeof font / sizeof font[0] && font[g].c != *s; ++g)
            ;
        if (g == sizeof font / sizeof font[0]) {
            continue;
        }
        for (r = 0; r < 7; ++r) {
            for (k = 0; k < 5; ++k) {
                if (font[g].rows[r][k] == '#') {
                    fill(px, x + k * scale, y + r * scale, scale, scale, c);
                }
            }
        }
    }
}
void xeen_boss_render(uint8_t pixels[64000], uint8_t palette[768])
{
    /* Light-theme desktop utility colours (8-bit / 4 -> the 6-bit DAC). */
    static const uint8_t colours[][3] = {{0, 0, 0},       {243, 243, 243}, {249, 249, 249},
                                         {255, 255, 255}, {0, 103, 192},   {224, 224, 224},
                                         {26, 26, 26},    {120, 120, 120}, {255, 255, 255}};
    static const char *keys[6][4] = {{"%", "CE", "C", "\6"}, {"1/x", "x\5", "\5\4x", "\1"},
                                     {"7", "8", "9", "\2"},  {"4", "5", "6", "-"},
                                     {"1", "2", "3", "+"},   {"\3", "0", ".", "="}};
    static const char *memory[5] = {"MC", "MR", "M+", "M-", "MS"};
    int row, col, i;
    if (!pixels || !palette) {
        return;
    }
    memset(palette, 0, 768);
    for (i = 0; i < (int)(sizeof colours / sizeof colours[0]); ++i) {
        palette[i * 3] = (uint8_t)(colours[i][0] / 4u);
        palette[i * 3 + 1] = (uint8_t)(colours[i][1] / 4u);
        palette[i * 3 + 2] = (uint8_t)(colours[i][2] / 4u);
    }
    memset(pixels, BG, 64000);
    draw_text(pixels, 8, 6, "\7", 1, TEXT);
    draw_text(pixels, 22, 6, "Standard", 1, TEXT);
    draw_text(pixels, 312 - text_width("0", 5), 20, "0", 5, TEXT);
    for (i = 0; i < 5; ++i) {
        draw_text(pixels, i * 64 + 32 - text_width(memory[i], 1) / 2, 61, memory[i], 1, GREY);
    }
    for (row = 0; row < 6; ++row) {
        for (col = 0; col < 4; ++col) {
            const char *label = keys[row][col];
            int x = 2 + col * 79, y = 72 + row * 21, w = 78, h = 20,
                scale = (int)strlen(label) <= 2 ? 2 : 1;
            int equals = row == 5 && col == 3,
                digit = (row >= 2 && col < 3) || (row == 5 && col < 3);
            uint8_t face = equals ? ACCENT : digit ? DIGIT : FUNC;
            fill(pixels, x, y, w, h, equals ? ACCENT : BORDER);
            fill(pixels, x + 1, y + 1, w - 2, h - 2, face);
            draw_text(pixels, x + (w - text_width(label, scale)) / 2, y + (h - 7 * scale) / 2,
                      label, scale, equals ? WHITE : TEXT);
        }
    }
}
