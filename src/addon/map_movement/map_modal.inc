/* Add-on map_movement (owner-approved, NOT original behavior): the map
 * modal's add-on part. Included by src/game/game_combat.c (its
 * translation unit) before 1DA2D's functions, which call it through one-line
 * hooks. */
/* Owner-approved add-on map_movement (Phase188; NOT original behavior,
 * include/xeen/addon/map_movement.h): NULL unless enabled. */
static xeen_map_pan *map_pan(xeen_game *g){return g->addons && g->addons->map_movement?&g->addons->map_pan:NULL;}
/* Add-on map_movement: the pass drawn at the panned origin on a scratch copy
 * of the game state (the lookups' F48E..F494 never reach the game's DS); only
 * the view's own animation counter is kept. A view the native cannot draw
 * (a lookup failure) returns to the original origin. SHOWN: the view the
 * pass was drawn with (Phase 207: read by other add-ons' hooks). */
static xeen_status map_pan_scene(xeen_game *g,xeen_map_draws *d,xeen_map_view *shown)
{
    xeen_map_pan *pan=map_pan(g);xeen_movement *scratch=malloc(sizeof *scratch);xeen_map_view view;xeen_status status;
    if(scratch){
        *scratch=g->movement;
        xeen_map_pan_view(pan,scratch,&g->map_view,&view);
        status=xeen_scene_map(scratch,&view,d);
        free(scratch);
        if(status==XEEN_OK){g->map_view.anim=view.anim;*shown=view;return XEEN_OK;}
    }
    pan->x=pan->base_x;pan->y=pan->base_y;pan->dragging=0;
    status=xeen_scene_map(&g->movement,&g->map_view,d);
    *shown=g->map_view;
    return status;
}
/* Add-on map_movement at the 1E3C7 answer: 1 when the add-on takes it (the
 * map stays open: the next pass, as the original's other answers). A drag
 * follows the pointer the poll sampled (F604, F602) until the host reports
 * no button held; Esc still closes. A click inside window 5 is no close (a
 * drag when the button is still held); outside, the original's close. */
static int map_pan_held(xeen_game *g){return g->addons->button_held && g->addons->button_held(g->addons->button_user);}
/**
 * Handles one input of the open map: a drag in progress follows the mouse (and ends when the button
 * is released), a click inside the map window starts a drag, and a key pans the map. Returns 1 when
 * the add-on consumed the input, 0 when the original map handles it.
 */
static int map_pan_answer(xeen_game *g,uint16_t key)
{
    xeen_map_pan *pan=map_pan(g);xeen_movement *m=&g->movement;uint16_t r[4];
    uint16_t px=xeen_read16(m,XEEN_DS_MOUSE_X),py=xeen_read16(m,XEEN_DS_MOUSE_Y);
    if(!pan->open)return 0;
    if(pan->dragging){
        xeen_map_pan_drag_move(pan,px,py);
        if(!map_pan_held(g))xeen_map_pan_drag_end(pan);
        return key!=XEEN_KEY_ESCAPE;
    }
    if(xeen_read16(m,XEEN_DS_MOUSE_CLICK)){
        if(!g->presentation || !xeen_presentation_window(g->presentation,5,r) || !xeen_map_pan_inside(r,px,py))return 0;
        if(map_pan_held(g))xeen_map_pan_drag_begin(pan,px,py);
        return 1;
    }
    return xeen_map_pan_key(pan,key);
}
