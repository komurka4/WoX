/* Add-on world_map (Phase 226, owner-approved, NOT original behavior;
 * include/xeen/addon/world_map.h): the M map modal's part. Included by
 * src/game/game_combat.c (its translation unit) before 1DA2D's
 * functions, which call it through one-line hooks; each is a no-op while the
 * add-on is off. Nothing here writes the game's state. */
/** Nonzero while the add-on is on for this game. */
static int world_map_enabled(const xeen_game *g){return g->addons && g->addons->world_map && xeen_world_map_on;}
/** Hook (map_view, after the pass): the button's overlay is live, with the map's blink. */
static void world_map_pass(xeen_game *g){if(world_map_enabled(g))xeen_world_map_pass(g->map_view.blink,&g->assets);}
/** Hook (map_end): the world map and its button end with the M map. */
static void world_map_end(xeen_game *g){if(world_map_enabled(g))xeen_world_map_closed();}
#include "archives.inc"
/**
 * Opens the world map from the M map: the party's place (xeen_world_map_locate, Phase 247: an entrance when it is
 * inside) decides the world (an entrance on the other side opens that side's map); a map with no place, or a
 * world the game holds no copy of, opens the current world without the arrow. 0 when it cannot be built.
 */
static int world_map_open_map(xeen_game *g)
{
    xeen_world_map_place party;xeen_archive tmp;const xeen_archive *state,*assets;const xeen_movement *live;
    int shown=xeen_world_map_locate(&g->movement,&party);
    if(shown && world_map_archives(g,party.world,&tmp,&state,&assets,&live))
        return xeen_world_map_open_world(XEEN_WORLD_MAP_OWNER_MAP,party.world,state,assets,live,&party,NULL,NULL);
    if(!world_map_archives(g,(int)g->world,&tmp,&state,&assets,&live))return 0;
    return xeen_world_map_open_world(XEEN_WORLD_MAP_OWNER_MAP,(int)g->world,state,assets,live,NULL,NULL,NULL);
}
/**
 * Hook (map_poll, the 1E3C7 answer, before every other answer): 0 when the add-on leaves the answer
 * to the M map; 1 when it took it (the map stays open: its next pass, as the original's other
 * answers); 2 for Esc in the world map (the M map closes as for its own Esc). In the M map: W or a
 * click on the World Map button presses the button (shown for one pass); the next answer opens the
 * world map and is its first; while it is open every answer is its own.
 */
static int world_map_answer(xeen_game *g,uint16_t key)
{
    xeen_movement *m=&g->movement;uint16_t px,py;int click,held;
    if(!world_map_enabled(g))return 0;
    px=xeen_read16(m,XEEN_DS_MOUSE_X);py=xeen_read16(m,XEEN_DS_MOUSE_Y);click=xeen_read16(m,XEEN_DS_MOUSE_CLICK)!=0;
    held=g->addons->button_held && g->addons->button_held(g->addons->button_user);
    if(xeen_world_map_is_open())return xeen_world_map_input(key,px,py,click,held);
    if(xeen_world_map_is_pressed()){                /* the pressed button was shown: open, this answer is the world map's */
        if(world_map_open_map(g))return xeen_world_map_input(key,px,py,0,0);
        xeen_world_map_closed();xeen_world_map_pass(g->map_view.blink,&g->assets);return 1;
    }
    if((uint8_t)key=='W' || (uint8_t)key=='w' || (click && xeen_world_map_button_hit(px,py))){
        xeen_world_map_press();                     /* the next pass shows the button pressed */
        return 1;
    }
    return 0;
}
