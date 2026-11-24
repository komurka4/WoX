/* Add-on identify (owner-approved, NOT original behavior; include/xeen/addon/identify.h).
 * Included by src/game/game_shop.c (its translation unit). */
/* Add-on identify (owner-approved, NOT original behavior): the items list with
 * the known item types' Identify statistics after their names, as a table.
 * The DS (9C6F's lines, A43F, AC0F) keeps the original's bytes; only the
 * drawn text differs: AC0F's nine lines (between its header and the closing
 * "\x01\x0cd") are replaced in a native copy. Geometry (4E38C): the 7F43/7F5E
 * name starts at x 36 ("\t028") of the window's 8..312; a run reaching 312
 * wraps; the price is right-aligned at 312. The columns are fitted to the
 * visible rows: name, then right-aligned to-hit, main value (mm..MM or AC)
 * and modifier, then left-aligned an accessory's label (Res, the attribute)
 * and the power; an empty column takes no space. When that
 * does not fit a row (its price included), the to-hit moves into the
 * modifier column (a metal weapon has no element or attribute); when that
 * does not fit either, each row gets the inline text, without the material
 * word if needed, else the original. A weapon, armor or accessory row whose
 * type is not known (never identified) keeps the original text, but its name
 * is drawn in ADDON_UNKNOWN_COLOUR; the number, the price and a selected
 * row's colour stay the row's (misc rows are never tracked: unchanged).
 * 1 = drawn. */
#define ADDON_UNKNOWN_COLOUR 19u   /* teal: unused elsewhere in the list (owner's choice) */
#define ADDON_LIMIT 275u    /* the last pixel column a run may reach, from x 36 */
#define ADDON_GAP 6u
#define ADDON_HIT_GAP 4u    /* to-hit/main and value/label: one space's width */
/* to-hit, main value and modifier value are right-aligned; an accessory's
 * label ("Res", the attribute) and the power are left-aligned */
#define ADDON_RIGHT(k) ((k)==XEEN_STAT_HIT || (k)==XEEN_STAT_MAIN || (k)==XEEN_STAT_MOD)
/**
 * Returns the width in pixels of T as the original's text routine draws it: trailing spaces, colour
 * codes and back-steps are handled as 4E6E6 does.
 */
static unsigned addons_text_width(const xeen_game *g,const char *t)
{
    int x=0;size_t k,end=strlen(t);
    while(end && t[end-1u]==' ')--end;                                   /* a trailing space is no ink */
    for(k=0;k<end;++k){unsigned ch=(uint8_t)t[k]&0x7fu;
        if(ch==12u){if(t[k+1]=='d')++k;else if(t[k+1])k+=t[k+2]?2u:1u;continue;}  /* 4E6E6's colour skip */
        if(ch==8u){if(t[k+1]==' '){x-=2;++k;}continue;}
        if(ch==32u)x+=4;else if(ch>32u)x+=g->text_window.font[0x1000u+(ch|0x80u)];   /* the reduced font */
    }
    return x<0?0u:(unsigned)x;
}
typedef struct {
    int known;
    char name[200];
    xeen_item_stats cells;
    unsigned name_width,cell_width[XEEN_STAT_CELLS],limit;
    unsigned long cost;
} addons_row;
/* The column positions (from x 36) and widths of one layout of the nine rows R; returns 0 when a known row does
 * not fit its limit. MERGED: the to-hit moves into an empty modifier column first; that move is made in R itself
 * and stays (the inline fallback that follows does not read the cells). Each known row's cell widths are set. */
static int addons_columns(const xeen_game *g,addons_row *r,int merged,unsigned pos[XEEN_STAT_CELLS],unsigned width[XEEN_STAT_CELLS])
{
    unsigned i,k,x=0,last=XEEN_STAT_CELLS;
    for(i=0;i<XEEN_ITEM_ROWS;++i){
        if(!r[i].known)continue;
        if(merged && r[i].cells.cell[XEEN_STAT_HIT][0] && !r[i].cells.cell[XEEN_STAT_MOD][0]){
            memcpy(r[i].cells.cell[XEEN_STAT_MOD],r[i].cells.cell[XEEN_STAT_HIT],sizeof r[i].cells.cell[0]);
            r[i].cells.cell[XEEN_STAT_HIT][0]=0;
        }
        for(k=0;k<XEEN_STAT_CELLS;++k)r[i].cell_width[k]=addons_text_width(g,r[i].cells.cell[k]);
    }
    memset(width,0,XEEN_STAT_CELLS*sizeof *width);
    for(i=0;i<XEEN_ITEM_ROWS;++i)if(r[i].known){
        if(r[i].name_width>x)x=r[i].name_width;
        for(k=0;k<XEEN_STAT_CELLS;++k)if(r[i].cell_width[k]>width[k])width[k]=r[i].cell_width[k];
    }
    for(k=0;k<XEEN_STAT_CELLS;++k){
        pos[k]=0;
        if(!width[k])continue;
        x+=(k==XEEN_STAT_MAIN && last==XEEN_STAT_HIT) || (k==XEEN_STAT_LABEL && last==XEEN_STAT_MOD)?ADDON_HIT_GAP:ADDON_GAP;
        pos[k]=x;x+=width[k];last=k;
    }
    for(i=0;i<XEEN_ITEM_ROWS;++i)if(r[i].known)
        for(k=0;k<XEEN_STAT_CELLS;++k)if(r[i].cells.cell[k][0] &&
            pos[k]+(ADDON_RIGHT(k)?width[k]:r[i].cell_width[k])>r[i].limit)return 0;
    return 1;
}
/* The layouts addons_item_list tries, in order: the columns, the to-hit merged into the modifier column, then each
 * row's inline text. */
enum { ADDON_LAYOUT_COLUMNS, ADDON_LAYOUT_MERGED, ADDON_LAYOUT_INLINE };
#define ADDON_LINE_BYTES 100u /* the list's lines: XEEN_DS_SCRATCH + 100 n (452EA's 9C6F + 100n) */
#define ADDON_ROW_BYTES  400u /* a replacement line */

/* The original's line I of the list, as items_text left it. */
static const char *addons_original_line(xeen_game *g, unsigned i)
{
    return ds_text(g, (uint16_t)(XEEN_DS_SCRATCH + ADDON_LINE_BYTES * i));
}
/* ROW as list line I of TEXT in COLOUR (items_row's formats: the number, the text, and COST when PRICED); returns 1,
 * or 0 with the original line I in ROW when the line does not fit. */
static int addons_format_row(xeen_game *g, char row[ADDON_ROW_BYTES], unsigned i, unsigned colour, const char *text,
                             int priced, unsigned long cost)
{
    int n = priced ? snprintf(row, ADDON_ROW_BYTES, ds_text(g, XEEN_TEXT_ITEMS_PRICED_LINE), colour, (int)i + 1, text,
                              cost)
                   : snprintf(row, ADDON_ROW_BYTES, ds_text(g, XEEN_TEXT_ITEMS_LINE), colour, (int)i + 1, text);
    if (n < 0 || (size_t)n >= ADDON_ROW_BYTES) {
        snprintf(row, ADDON_ROW_BYTES, "%s", addons_original_line(g, i));
        return 0;
    }
    return 1;
}
/* Stage 1 for row I into R: known (the type identified and its statistics and name available), with its name's
 * width and the run limit (less the right-aligned price and a gap when PRICED); otherwise R is cleared (not known). */
static void addons_measure_row(xeen_game *g, uint16_t record, uint16_t start, unsigned cat, int mode, int priced,
                               unsigned i, addons_row *r)
{
    xeen_movement *m = &g->movement;
    const uint8_t *s = item_at(g, record, cat, i);
    unsigned colour = g->items_marks[i];
    memset(r, 0, sizeof *r);
    if (!s[XEEN_ITEM_ROW_ID] || !xeen_addons_item_known(g->addons, cat, s) ||
        !xeen_item_stats_cells(m, record, i, colour, cat, &r->cells) ||
        !xeen_item_name_addon(m, record, i, colour, cat, g->mae, g->mae_size, 0, r->name, sizeof r->name)) {
        return;
    }
    r->known = 1;
    r->name_width = addons_text_width(g, r->name);
    r->limit = ADDON_LIMIT;
    if (priced) {
        char cost[16];
        r->cost = (unsigned long)items_row_cost(g, mode, record, start, i, cat);
        (void)snprintf(cost, sizeof cost, "%lu", r->cost);
        r->limit -= addons_text_width(g, cost) + 1u + 4u; /* the right-aligned price and a gap */
    }
}
/* A known row's table text into TEXT (SIZE bytes): its name, then each nonempty cell at its column POS (right-aligned
 * cells end at POS + WIDTH). Returns the text's length, 0 when it does not fit (or is empty). */
static size_t addons_table_text(const addons_row *r, const unsigned pos[XEEN_STAT_CELLS],
                                const unsigned width[XEEN_STAT_CELLS], char *text, size_t size)
{
    size_t t;
    int w = snprintf(text, size, "%s", r->name);
    if (w < 0 || (size_t)w >= size) {
        return 0;
    }
    t = (size_t)w;
    for (unsigned k = 0; k < XEEN_STAT_CELLS; ++k) {
        const char *v = r->cells.cell[k];
        if (!v[0]) {
            continue;
        }
        /* 09nnn: x = 8 + nnn, so x 36 + p is nnn 28 + p; a right-justified run (03r) ends one pixel before its x. */
        if (ADDON_RIGHT(k)) {
            w = snprintf(text + t, size - t, "\t%03u\x03r%s\x03l", 28u + pos[k] + width[k] + 1u, v);
        } else {
            w = snprintf(text + t, size - t, "\t%03u%s", 28u + pos[k], v);
        }
        if (w < 0 || (size_t)w >= size - t) {
            return 0;
        }
        t += (size_t)w;
    }
    return t;
}
/* A known row's inline text into TEXT (SIZE bytes): with the material word, else without it, the first that fits
 * the row's limit. Returns 0 when neither does. */
static int addons_inline_text(xeen_game *g, uint16_t record, unsigned cat, unsigned i, const addons_row *r,
                              char *text, size_t size)
{
    for (int drop = 0; drop < 2; ++drop) {
        if (xeen_item_text_addon(&g->movement, record, i, g->items_marks[i], cat, g->mae, g->mae_size, drop, text,
                                 size) &&
            addons_text_width(g, text) <= r->limit) {
            return 1;
        }
    }
    return 0;
}
/* Stage 4: an equipment row whose type was never identified, unmarked: its original text with the name in
 * ADDON_UNKNOWN_COLOUR, back to the row's colour for the price. Returns whether ROW was replaced. */
static int addons_unknown_row(xeen_game *g, uint16_t record, uint16_t start, unsigned cat, int mode, int priced,
                              unsigned i, char row[ADDON_ROW_BYTES])
{
    const uint8_t *s = item_at(g, record, cat, i);
    unsigned colour = g->items_marks[i];
    char name[200], text[220];
    int n;
    if (!s[XEEN_ITEM_ROW_ID] || colour || xeen_addons_item_known(g->addons, cat, s) ||
        !xeen_item_text_format(&g->movement, s, ADDON_UNKNOWN_COLOUR, cat, g->mae, g->mae_size, 0u, name)) {
        return 0;
    }
    n = snprintf(text, sizeof text, "%s\f%02u", name, colour);
    if (n < 0 || (size_t)n >= sizeof text) {
        return 0;
    }
    return addons_format_row(g, row, i, colour, text, priced,
                             priced ? (unsigned long)items_row_cost(g, mode, record, start, i, cat) : 0ul);
}
/* Stage 5: AC0F with its nine original lines (of TOTAL bytes, between its header and the closing "\x01\x0cd")
 * replaced by ROWS, drawn in the list window. Returns 0 (nothing drawn) when AC0F does not hold those lines or the
 * copy would not fit. */
static int addons_draw_list(xeen_game *g, char rows[XEEN_ITEM_ROWS][ADDON_ROW_BYTES], size_t total)
{
    static char out[0xf00];
    const char *all = ds_text(g, XEEN_DS_TEXT_BUFFER_2);
    size_t len = strlen(all), prefix, at, k;
    unsigned i;
    if (len < total + 3u) {
        return 0;
    }
    prefix = len - 3u - total;
    for (at = prefix, i = 0; i < XEEN_ITEM_ROWS; ++i) {
        const char *line = addons_original_line(g, i);
        size_t l = strlen(line);
        if (memcmp(all + at, line, l)) {
            return 0;
        }
        at += l;
    }
    if (prefix >= sizeof out) {
        return 0;
    }
    memcpy(out, all, prefix);
    at = prefix;
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        k = strlen(rows[i]);
        if (at + k >= sizeof out) {
            return 0;
        }
        memcpy(out + at, rows[i], k);
        at += k;
    }
    if (at + (len - prefix - total) >= sizeof out) {
        return 0;
    }
    memcpy(out + at, all + prefix + total, len - prefix - total + 1u);
    menu_window_text(g, XEEN_WINDOW_LIST, WINDOW_FRAMED, out);
    return 1;
}
/**
 * Draws the items list of category CAT (weapons, armour, accessories) of RECORD in MODE with the identified types'
 * statistics (the header comment's table, else inline texts) and the never-identified types' names in teal, in
 * place of the AC0F text items_text built; START is the dialog's starting member (the price's Merchant skill).
 * Returns 1 when drawn, 0 when the caller draws the original list (the add-on off, misc, no font or MAE, nothing to
 * change, or an AC0F that is not the nine lines). Static buffers: not re-entrant (one list is drawn at a time).
 */
static int addons_item_list(xeen_game *g, uint16_t record, uint16_t start, unsigned cat, int mode)
{
    static addons_row r[XEEN_ITEM_ROWS];
    static char rows[XEEN_ITEM_ROWS][ADDON_ROW_BYTES];
    unsigned i, pos[XEEN_STAT_CELLS], width[XEEN_STAT_CELLS];
    size_t total = 0;
    int changed = 0, priced = !items_unpriced(mode), layout;
    if (!addons_items(g) || cat >= XEEN_ITEMS_MISC) {
        return 0;
    }
    if (!g->text_window.loaded) {
        (void)xeen_text_window_load(&g->text_window, &g->assets);
    }
    if (!g->text_window.loaded || !mae_ready(g)) {
        return 0;
    }
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        const char *line = addons_original_line(g, i);
        total += strlen(line);
        snprintf(rows[i], sizeof rows[i], "%s", line);
        addons_measure_row(g, record, start, cat, mode, priced, i, &r[i]);
    }
    for (layout = ADDON_LAYOUT_COLUMNS; layout < ADDON_LAYOUT_INLINE; ++layout) {
        if (addons_columns(g, r, layout == ADDON_LAYOUT_MERGED, pos, width)) {
            break;
        }
    }
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        char text[320];
        if (!r[i].known) {
            continue;
        }
        if (layout == ADDON_LAYOUT_INLINE ? !addons_inline_text(g, record, cat, i, &r[i], text, sizeof text)
                                          : !addons_table_text(&r[i], pos, width, text, sizeof text)) {
            continue;
        }
        changed |= addons_format_row(g, rows[i], i, g->items_marks[i], text, priced, r[i].cost);
    }
    for (i = 0; i < XEEN_ITEM_ROWS; ++i) {
        if (!r[i].known) {
            changed |= addons_unknown_row(g, record, start, cat, mode, priced, i, rows[i]);
        }
    }
    return changed && addons_draw_list(g, rows, total);
}
