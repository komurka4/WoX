/* Add-on world_map (Phase 247, owner-approved, NOT original behavior;
 * include/xeen/addon/world_map.h): the archives a world map of WORLD is
 * built from, for the game-side callers (map_modal.inc, and add-on
 * quest_journal's quests_modal.inc). Included by the game translation unit
 * that holds the caller; it only reads the game. */
/**
 * For WORLD (0 Clouds, 1 Darkside): its state archive in *STATE, its CC in *ASSETS and in *LIVE the running game
 * when WORLD is the current one; for the other world the game's copy of its state (the CUR image it keeps since
 * the party last left it, 19E82's 30F34 save) opened in TMP, the other CC and no live game. 0 when the game holds
 * no copy of that world (a session that never had one) or no CC for it.
 */
static int world_map_archives(xeen_game *g, int world, xeen_archive *tmp, const xeen_archive **state,
                              const xeen_archive **assets, const xeen_movement **live)
{
    if (world == (int)g->world) {
        *state = &g->state;
        *assets = &g->assets;
        *live = &g->movement;
        return 1;
    }
    if ((world != 0 && world != 1) || !g->other_bound || !g->cur[world].bytes ||
        xeen_archive_open(tmp, g->cur[world].bytes, g->cur[world].size) != XR_OK)
        return 0;
    *state = tmp;
    *assets = &g->other;
    *live = NULL;
    return 1;
}
