/* Add-on quest_journal (Phase 247, owner-approved, NOT original behavior;
 * include/xeen/addon/quest_journal.h): the Quests dialog's part. Included by
 * src/game/game_quests.c (its translation unit) before 3E24F's functions,
 * which call it through one-line hooks; each is a no-op while the add-on is
 * off. Nothing here writes the game's state. */
#include "../world_map/archives.inc"
/** Nonzero while the add-on is on for this game. */
static int quest_journal_enabled(const xeen_game *g)
{
    return g->addons && g->addons->quest_journal && xeen_quest_journal_on;
}
/** Nonzero while add-on world_map can open its map (the quests' and notes' places need it). */
static int quest_journal_maps(const xeen_game *g)
{
    return g->addons && g->addons->world_map && xeen_world_map_on;
}
/**
 * The list's slots as quests_text (3E375) fills them, by number: IDS[k] the item, quest or note of slot K,
 * -1 for a heading (DS:6E2A/6E2E). Returns the slot count.
 */
static unsigned quest_journal_ids(const xeen_game *g, int ids[135])
{
    /* The three lists as quests_text builds them: the entries, the first Darkside one; the quest items held by their
     * counters, the quests and the notes by their flag bits (entry n is bit n + 1). A mode past the three is the
     * notes, as there. */
    static const struct { unsigned count, darkside_first; uint16_t flags; } lists[3] = {
        {XEEN_QUEST_ITEM_COUNT, QUESTS_DARKSIDE_ITEM, 0},
        {QUESTS_QUEST_COUNT, QUESTS_DARKSIDE_QUEST, XEEN_DS_QUEST_FLAGS},
        {QUESTS_NOTE_COUNT, QUESTS_DARKSIDE_NOTE, XEEN_DS_AUTO_NOTES},
    };
    const uint8_t *ds = g->movement.ds;
    unsigned mode = g->quests_mode == QUESTS_ITEMS ? 0u : g->quests_mode == QUESTS_CURRENT ? 1u : 2u;
    unsigned k = 0, di, i;
    int dark = 0;
    for (i = 0; i < 135u; ++i)
        ids[i] = -1;
    for (di = 0; di < lists[mode].count && k < 135u; ++di) {
        int held = mode == 0u ? ds[(uint16_t)(XEEN_DS_QUEST_ITEMS + XEEN_QUEST_ITEM_FIRST + di)] != 0
                              : xeen_ds_get_bit(ds, lists[mode].flags, di + 1u);
        if (!held)
            continue;
        if (!k && !dark && di < lists[mode].darkside_first)
            ++k;                                 /* the Clouds heading */
        if (di >= lists[mode].darkside_first && !dark) {
            if (k < 135u)
                ++k;                             /* the Darkside heading */
            dark = 1;
        }
        if (k < 135u)
            ids[k++] = (int)di;
    }
    return k;
}
/** Hook (quests_text, the list drawn): its shown rows L (NULL: "no items") for the numbers. */
static void quest_journal_listed(xeen_game *g, const char *const *l)
{
    int ids[135], shown_ids[9];
    unsigned shown = g->quests_mode == 1 ? 3u : 9u, i;
    static const uint16_t formats[3] = {XEEN_TEXT_QUESTS_ITEMS_PAGE, XEEN_TEXT_QUESTS_CURRENT_PAGE, XEEN_TEXT_QUESTS_NOTES_PAGE};
    if (!quest_journal_enabled(g))
        return;
    if (!l || !quest_journal_ids(g, ids)) {
        xeen_quest_journal_listed(g->quests_mode, NULL, NULL, NULL, 0, 0); /* nothing to number */
        return;
    }
    for (i = 0; i < shown; ++i) {
        int at = g->quests_top + (int)i;
        shown_ids[i] = at >= 0 && at < 135 ? ids[at] : -1;
    }
    xeen_quest_journal_listed(g->quests_mode, ds_text(g, formats[g->quests_mode]), l, shown_ids, shown,
                              quest_journal_maps(g));
}
/** Hook (quests_end): the dialog closed. */
static void quest_journal_end(xeen_game *g)
{
    if (quest_journal_enabled(g))
        xeen_quest_journal_ended();
}
/**
 * Hook (shop_resume's QUESTS_POLL, 3E8A3, before the dialog's keys): 0 leaves the answer KEY to quests_key (as
 * the original); 1 when the add-on took it (*ST: stay at the poll; shop_resume re-enters it as a new poll session,
 * Phase 330). While the world map is open every answer is
 * its own (Esc closes it back to the list). While the description window is open: Space or a click inside it
 * closes it; Esc, I, Q and A close it and go to the dialog; everything else is ignored. Otherwise a number 1..9
 * or a click on a numbered row opens that row's description or place.
 */
int quests_addon_answer(xeen_game *g, uint16_t key, xeen_status *st)
{
    xeen_movement *m = &g->movement;
    uint16_t px, py;
    int click, id;
    uint8_t k = (uint8_t)key;
    if (!quest_journal_enabled(g))
        return 0;
    px = xeen_read16(m, XEEN_DS_MOUSE_X);
    py = xeen_read16(m, XEEN_DS_MOUSE_Y);
    click = xeen_read16(m, XEEN_DS_MOUSE_CLICK) == 1u;
    *st = XEEN_SERVICE_REQUIRED;
    if (quest_journal_maps(g) && xeen_world_map_owner() == XEEN_WORLD_MAP_OWNER_QUESTS) {
        int held = g->addons->button_held && g->addons->button_held(g->addons->button_user);
        (void)xeen_world_map_input(key, px, py, click, held); /* 2: Esc closed it, the list is back */
        return 1;
    }
    if (k >= 'a' && k <= 'z')
        k = (uint8_t)(k - 0x20u);
    if (xeen_quest_journal_item_open()) {
        if (k == ' ' || (!key && click && xeen_quest_journal_in_window(px, py))) {
            xeen_quest_journal_close_item();
            return 1;
        }
        if (k == XEEN_KEY_ESCAPE || k == 'I' || k == 'Q' || k == 'A') {
            xeen_quest_journal_close_item(); /* the dialog's own answer follows */
            return 0;
        }
        return key || click;                     /* ignored (nothing: the original's no-answer) */
    }
    if (key > 0xffu)
        return 0;
    id = xeen_quest_journal_pick(k, click, px, py);
    if (id < 0)
        return 0;                                /* not a numbered row: the dialog's answer */
    if (g->quests_mode == 0) {
        (void)xeen_quest_journal_open_item((unsigned)id, far_text(g, XEEN_DS_QUEST_ITEM_NAMES, (unsigned)id), m->ds);
        return 1;
    }
    {
        xeen_world_map_place mark, party;
        xeen_archive tmp;
        const xeen_archive *state, *assets;
        const xeen_movement *live;
        const char *label = NULL;
        int shown;
        if (!xeen_quest_journal_target(g->quests_mode, (unsigned)id, &mark, &label) ||
            !world_map_archives(g, mark.world, &tmp, &state, &assets, &live))
            return 1;                            /* no place, or no copy of that world: nothing */
        shown = xeen_world_map_locate(m, &party);
        (void)xeen_world_map_open_world(XEEN_WORLD_MAP_OWNER_QUESTS, mark.world, state, assets, live,
                                        shown ? &party : NULL, &mark, label);
    }
    return 1;
}
