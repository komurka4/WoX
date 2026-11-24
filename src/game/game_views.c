/*
 * The session's read-only views (Phase 248, from game.c): what the host, the tools and the add-ons read from a
 * session without changing it: the map's cells and metadata, the DS image, the movement state and the monster
 * database, the Cast dialog's view, the driver and program exits and the XEEN.EXE hand-off, the CD side requests,
 * the modal poll's DS, the event store, the roster, the state archive, the scroll list and the details view. The
 * inspection view (xeen_game_inspect) is game_lifecycle.c's.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint16_t xeen_game_tile(const xeen_game *g, unsigned x, unsigned y)
{
    return g && x < 16 && y < 16 ? xeen_read16(&g->movement, (uint16_t)(XEEN_DS_MAP_DATA + y * 32u + x * 2u))
                                 : 0;
}
uint8_t xeen_game_metadata(const xeen_game *g, unsigned x, unsigned y)
{
    return g && x < 16 && y < 16 ? g->movement.ds[XEEN_DS_MAP_FIELD(METADATA) + y * 16u + x] : 0;
}
const uint8_t *xeen_game_ds(const xeen_game *g)
{
    return g ? g->movement.ds : NULL;
}
/* add-on monster_difficulty: read-only views (xeen/game.h). */
const xeen_movement *xeen_game_movement(const xeen_game *g)
{
    return g ? &g->movement : NULL;
}
const uint8_t *xeen_game_monster_database(const xeen_game *g, size_t *size)
{
    if (size) {
        *size = g ? g->monster_database_size : 0u;
    }
    return g && g->monster_database_size ? g->monster_database : NULL;
}
/** Read-only view of the Cast Spell dialog for an add-on; returns 0 when it is not open. */
int xeen_game_cast(const xeen_game *g, xeen_game_cast_view *v) /* add-on view (read-only) */
{
    if (!g || !v || !g->cast_active) {
        return 0;
    }
    v->list_open = g->guild_active && g->list_caster_spells == 1;
    v->record = v->list_open ? g->guild_record : g->cast_record;
    v->list_index = v->list_open && g->list_choice >= 0 && g->list_choice < 40
                            ? (int)g->guild_index[g->list_choice]
                            : -1;
    return 1;
}
/** The casting group of class CLS (cast_category) for an add-on's read-only view. */
unsigned xeen_game_class_spell_list(uint8_t cls)
{
    return cast_category(cls);
}
const char *xeen_game_driver_exit(const xeen_game *g)
{
    return g && g->driver_exit[0] ? g->driver_exit : NULL;
}
const char *xeen_game_program_exit(const xeen_game *g)
{
    return g && g->lifecycle_boundary == PROGRAM_EXIT_BOUNDARY && g->program_exit[0] ? g->program_exit : NULL;
}
const char *xeen_game_handoff(const xeen_game *g)
{
    return g && g->lifecycle_boundary == XEEN_EXE_HANDOFF_BOUNDARY && g->handoff_program[0] ? g->handoff_command
                                                                           : NULL;
}
const char *xeen_game_handoff_program(const xeen_game *g)
{
    return g && g->handoff_program[0] ? g->handoff_program : NULL;
}
int xeen_game_handoff_cfg(const xeen_game *g)
{
    return g ? g->handoff_cfg : 0;
}
unsigned long xeen_game_cd_side_requests(const xeen_game *g, uint8_t *world)
{
    if (g && world) {
        *world = g->cd_side_world;
    }
    return g ? g->cd_side_requests : 0ul;
}
const uint8_t *xeen_game_modal_poll_ds(const xeen_game *g)
{
    return g && g->modal_polls ? g->modal_poll_ds : NULL;
}
const uint8_t *xeen_game_event_store(const xeen_game *g, size_t *size)
{
    if (size) {
        *size = g ? g->resources.record_bytes : 0;
    }
    return g ? g->backing : NULL;
}
const uint8_t *xeen_game_roster(const xeen_game *g, size_t *size)
{
    if (size) {
        *size = g && g->roster ? XEEN_ROSTER_BYTES : 0;
    }
    return g ? g->roster : NULL;
}
const uint8_t *xeen_game_state_archive(const xeen_game *g, size_t *size)
{
    if (size) {
        *size = g ? g->state.size : 0;
    }
    return g ? g->state.bytes : NULL;
}

#include "../addon/slot_files.inc" /* add-ons identify, spell_list_sort: ADDONnn.WOX */
/* add-on mouse_scroll: a read-only view (xeen/game.h). */
int xeen_game_scroll_list(const xeen_game *g, uint16_t rect[4])
{
    int kind = 0;
    uint16_t window = 0;
    if (!g || !rect || !g->presentation) {
        return 0;
    }
    switch (g->panel_stage) {
        case CAST_LIST:
            if (g->lifecycle_boundary == GUILD_POLL_BOUNDARY) {
                kind = XEEN_SCROLL_CAST_NEW;
                window = 0x19;
            }
            break;
        case GUILD_POLL:
            if (g->lifecycle_boundary == GUILD_POLL_BOUNDARY) {
                kind = g->guild_info ? XEEN_SCROLL_SPELL_INFO : XEEN_SCROLL_GUILD_BUY;
                window = 0x19;
            }
            break;
        case QUESTS_POLL:
            if (g->lifecycle_boundary == QUESTS_POLL_BOUNDARY) {
                kind = XEEN_SCROLL_QUESTS;
                window = 0x1e;
            }
            break;
        case AWARDS_POLL:
            if (g->lifecycle_boundary == AWARDS_POLL_BOUNDARY) {
                kind = XEEN_SCROLL_AWARDS;
                window = 0x1e;
            }
            break;
        case INN_POLL:
            if (g->lifecycle_boundary == INN_POLL_BOUNDARY) {
                kind = XEEN_SCROLL_INN;
                window = 0x0b;
            }
            break;
        default:
            break;
    }
    if (!kind || !xeen_presentation_window(g->presentation, window, rect)) {
        return 0;
    }
    return kind;
}
/** Read-only view of the character details panel for an add-on; returns 0 when it is not shown. */
int xeen_game_details(const xeen_game *g, xeen_game_details_view *v) /* add-on view (read-only) */
{
    if (!g || !v || !g->presentation || g->panel_stage != DETAILS_POLL ||
        g->lifecycle_boundary != DETAILS_POLL_BOUNDARY) {
        return 0;
    }
    if (!g->movement.ds[XEEN_DS_WINDOW_OPEN + 0x1cu] ||
        !xeen_presentation_window(g->presentation, 0x1c, v->rect)) {
        return 0;
    }
    v->stat = g->view_cursor;
    v->record = g->view_char;
    v->text = (const char *)g->movement.ds + XEEN_DS_TEXT_BUFFER_2;
    v->under = g->window_saves[0x1c].saved ? g->window_saves[0x1c].pixels : NULL;
    v->displayed = g->presentation->pages[g->presentation->displayed_page];
    v->colour = g->layout_ready ? g->layout.colour : 0;
    v->background = g->layout_ready ? g->layout.background : 153;
    return 1;
}
