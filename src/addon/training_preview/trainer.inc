/* Add-on training_preview (owner-approved in Phase 190, NOT original
 * behavior; include/xeen/addon/training_preview.h): the training grounds'
 * part. Included by src/game/game_location.c (its translation unit) before
 * the location window; the original code calls it through one-line hooks:
 *   location_window           the panel: Train, Prev, ESC (relocated) and the open ladder
 *   location_animate's end    the open ladder again over the redrawn trainer view
 *   the kind-5 entry     the ladder closed at every visit
 *   the 26E60 poll       the three buttons' click test and key feedback
 *   train_key            P (or the Prev button): open/close; Esc closes it and leaves as before
 * With the add-on off none of them changes anything: the panel, the buttons,
 * the keys and the poll are the original's. With it on the DS, the saves, the
 * RNG and the clock are the original's too: only the screen differs. */

/* The panel's buttons (window 10, inner area x 234..311): three 24x20 tiles
 * two pixels apart, at the original's y (the 1A42 table, 108). */
enum { TP_TRAIN_X=235, TP_PREV_X=261, TP_ESC_X=287 };
#include "prev_icon.inc"

/** Returns the training_preview state, or NULL when the add-on is off. */
static xeen_training_preview *tp_state(xeen_game *g)
{return g->addons && g->addons->training_preview?&g->addons->training:NULL;}
/* The training grounds with their own button set live (2797D: 98AC = DS:1A3C). */
static int tp_here(xeen_game *g)
{return tp_state(g) && g->location_kind==LOCATION_TRAINING && xeen_read16(&g->movement,XEEN_DS_BUTTON_X_TABLE)==0x1a3cu;}

/* Button i of the add-on's set: 0 Train (train.icn frames 2/3), 1 Prev (the
 * generated tile), 2 ESC (frames 0/1). */
static void tp_draw_button(void *user,unsigned i,int pressed)
{
    xeen_game *g=user;xeen_presentation *p=g->presentation;uint16_t y=g->movement.ds[XEEN_BUTTON_Y(XEEN_BUTTONS_ESC_T,0)];
    if(i==1u){
        const uint8_t (*tile)[24]=pressed?prev_icon_pressed:prev_icon_normal;unsigned r;
        if(!p || p->framebuffer_size<320u*200u)return;
        for(r=0;r<20u;++r)memcpy(p->framebuffer+(y+r)*320u+TP_PREV_X,tile[r],24);
        return;
    }
    exit_draw(g,xeen_read16(&g->movement,XEEN_DS_BUTTON_SET),(uint16_t)((i?2u:0u)+(pressed?1u:0u)),i?TP_TRAIN_X:TP_ESC_X,(int16_t)y,0);
}
/* The add-on's three buttons from the training grounds' own set (XEEN_BUTTONS_ESC_T, DS:1A3C; its entry 0 is Esc,
 * entry 1 T): all three at the set's first y, width and height (byte arrays); Train takes entry 1's key and enabled
 * byte, Prev the key P (always on), ESC entry 0's. */
static void tp_buttons(xeen_game *g,xeen_poll_buttons *b)
{
    enum { SET_ESC=0, SET_T=1 };
    const uint8_t *ds=g->movement.ds;unsigned i;
    static const uint16_t xs[3]={TP_TRAIN_X,TP_PREV_X,TP_ESC_X};
    memset(b,0,sizeof *b);b->count=3;b->draw=tp_draw_button;b->user=g;
    for(i=0;i<3u;++i){
        b->x[i]=xs[i];b->y[i]=ds[XEEN_BUTTON_Y(XEEN_BUTTONS_ESC_T,SET_ESC)];
        b->w[i]=ds[XEEN_BUTTON_WIDTH(XEEN_BUTTONS_ESC_T,SET_ESC)];b->h[i]=ds[XEEN_BUTTON_HEIGHT(XEEN_BUTTONS_ESC_T,SET_ESC)];
    }
    b->key[0]=ds[XEEN_BUTTON_KEY(XEEN_BUTTONS_ESC_T,SET_T)];b->enabled[0]=ds[XEEN_BUTTON_ENABLED(XEEN_BUTTONS_ESC_T,SET_T)];
    b->key[1]='P';b->enabled[1]=1;
    b->key[2]=ds[XEEN_BUTTON_KEY(XEEN_BUTTONS_ESC_T,SET_ESC)];b->enabled[2]=ds[XEEN_BUTTON_ENABLED(XEEN_BUTTONS_ESC_T,SET_ESC)];
}

/* A ladder row's status word (the Status column). */
static const char *tp_status(uint8_t s)
{return s==XEEN_TRAIN_ROW_OK?"ok":s==XEEN_TRAIN_ROW_GOLD?"gold short":s==XEEN_TRAIN_ROW_CAP?"town cap":"XP short";}
/*
 * Draws the training ladder of the member in training over the trainer's view (window 11's rectangle, 8,8 216x132):
 * the title (name, level, the town's cap), then one row per ladder row (xeen_training_ladder_build: every level the
 * experience allows, whether or not the gold covers it, then the row that stops the ladder), each with its cost, the
 * running total ("--" on the stopping row) and its status; at most nine rows (a longer ladder shows the first six,
 * "..", the last two); then the gold line (the condition that refuses training, the level the gold covers, or why
 * none). Draws nothing without the window, the ladder or the text font.
 */
static void tp_overlay(xeen_game *g)
{
    xeen_movement *m=&g->movement;const uint8_t *ds=m->ds;xeen_presentation *p=g->presentation;
    static xeen_training_ladder lad;char text[1400],line[96];size_t n=0;unsigned k,shown[9],count=0,y;uint16_t r[4];
    xeen_text_layout layout;
    if(!p || !xeen_presentation_window(p,11,r))return;
    if(!xeen_training_ladder_build(m,ds+g->location_member,xeen_training_top_level(ds[XEEN_DS_WORLD],ds[XEEN_DS_PARTY_MAP]),
                                    xeen_ds_read32(m,XEEN_DS_PARTY_GOLD),&lad))return;
    if(lad.count<=9u)for(k=0;k<lad.count;++k)shown[count++]=k;
    else{for(k=0;k<6u;++k)shown[count++]=k;shown[count++]=~0u;shown[count++]=lad.count-2u;shown[count++]=lad.count-1u;}
#define TP_ADD(...) do{int w_=snprintf(text+n,sizeof text-n,__VA_ARGS__);if(w_<0 || (size_t)w_>=sizeof text-n)return;n+=(size_t)w_;}while(0)
    TP_ADD("\x0d\x03" "c%s, level %u - cap %u\x02\x03" "l",ds_text(g,g->location_member),lad.level,lad.top);
    TP_ADD("\x0b" "014\x09" "000Level\x09" "045Cost\x09" "095Total\x09" "140Status");
    for(k=0,y=25;k<count;++k,y+=9){
        const xeen_training_row *row;
        if(shown[k]==~0u){TP_ADD("\x0b" "%03u\x09" "000..",y);continue;}
        row=&lad.rows[shown[k]];
        if(row->status<=XEEN_TRAIN_ROW_GOLD)(void)snprintf(line,sizeof line,"%lu",(unsigned long)row->total);
        else (void)snprintf(line,sizeof line,"--");
        TP_ADD("\x0b" "%03u\x09" "000%u\x09" "045%lu\x09" "095%s\x09" "140%s",y,row->level,(unsigned long)row->cost,line,tp_status(row->status));
    }
    if(lad.refused)TP_ADD("\x0b" "108\x09" "000%s: cannot train now",far_text(g,XEEN_DS_CONDITION_NAMES,lad.condition));
    else if(lad.afford>lad.level)TP_ADD("\x0b" "108\x09" "000Gold %lu covers level %u",(unsigned long)lad.gold,lad.afford);
    else if(lad.rows[0].status==XEEN_TRAIN_ROW_GOLD)TP_ADD("\x0b" "108\x09" "000Gold %lu: not enough for level %u",(unsigned long)lad.gold,lad.rows[0].level);
    else TP_ADD("\x0b" "108\x09" "000Gold %lu: no level to train",(unsigned long)lad.gold);
    TP_ADD("\x01");
#undef TP_ADD
    if(!g->text_window.loaded)(void)xeen_text_window_load(&g->text_window,&g->assets);
    if(!g->text_window.loaded)return;
    (void)xeen_text_frame(p->framebuffer,p->framebuffer_size,r);
    xeen_text_layout_init(&layout,r,8);layout.colour=0;                  /* its own state: g->layout is not touched */
    (void)xeen_text_layout_run(&layout,&g->text_window,p->framebuffer,(const uint8_t *)text,n+1u,0);
}

/* Hook (the kind-5 entry, 2797D): every visit starts with the ladder closed. */
static void training_preview_enter(xeen_game *g)
{xeen_training_preview *t=tp_state(g);if(t){t->open=0;t->relocated=0;}}

/* The text state the next 4E38C text inherits (the layout, its cursor and their flags): saved and put back around a
 * draw whose own end state must not be the one kept. */
typedef struct { xeen_text_layout layout; int ready; xeen_text_cursor cursor; uint8_t valid, shown; } tp_text_state;
static void tp_text_save(const xeen_game *g,tp_text_state *s)
{s->layout=g->layout;s->ready=g->layout_ready;s->cursor=g->text_cursor;s->valid=g->text_cursor_valid;s->shown=g->text_cursor_shown;}
static void tp_text_restore(xeen_game *g,const tp_text_state *s)
{g->layout=s->layout;g->layout_ready=s->ready;g->text_cursor=s->cursor;g->text_cursor_valid=s->valid;g->text_cursor_shown=s->shown;}

/* Test-only observer (Phase 337; not original behaviour, changes nothing): with the environment variable
 * XEEN_TP_TEXT_STATE set, one draw-trace line with the text state the panel leaves for the next text (the layout, its
 * readiness, the cursor and its two flags), so a two-build case sees a restore that is wrong but draws the same. */
static void tp_trace_text_state(const xeen_game *g)
{
    const xeen_text_layout *l=&g->layout;const xeen_text_cursor *c=&g->text_cursor;
    if(!getenv("XEEN_TP_TEXT_STATE"))return;
    xeen_presentation_trace("mark tp-text-state layout %d %d %d %d %d %d %u %u %u %u ready %d cursor %d %d %u %u %u %u %u %u %u valid %u shown %u",
                            l->left,l->right,l->top,l->bottom,l->x,l->y,l->justify,l->reduced,l->background,l->colour,
                            g->layout_ready,c->x,c->y,c->justify,c->reduced,c->background,c->colours[0],c->colours[1],
                            c->colours[2],c->colours[3],g->text_cursor_valid,g->text_cursor_shown);
}
/* Hook (location_window): the training panel with the labels and buttons moved to
 * make room for Prev. The original text is drawn first and its end state (the
 * 4E38C state the next text inherits) kept; the screen is then put back and
 * the relocated text drawn. 1 when drawn here (the caller skips its own). */
static int training_preview_panel(xeen_game *g)
{
    static const char tail[]="\x02\x03" "c" "\x0b" "122";
    static const char moved[]="\x02\x03" "l" "\x0b" "122\x09" "000\x0c" "37T\x0c" "drain\x09" "029\x0c" "37P\x0c" "drev\x09" "058ESC\x01";
    xeen_training_preview *t=tp_state(g);xeen_presentation *p=g->presentation;
    const char *text,*at;char out[1100];uint8_t *saved;size_t head;unsigned i;
    tp_text_state before,original_end;
    if(!t)return 0;
    t->relocated=0;
    if(!tp_here(g) || !p)return 0;
    text=ds_text(g,XEEN_DS_TEXT_BUFFER_2);
    if(!(at=strstr(text,tail)) || (head=(size_t)(at-text))+sizeof moved>sizeof out)return 0;
    if(!(saved=malloc(p->framebuffer_size)))return 0;
    memcpy(out,text,head);memcpy(out+head,moved,sizeof moved);
    memcpy(saved,p->framebuffer,p->framebuffer_size);
    tp_text_save(g,&before);
    menu_window_text(g,XEEN_WINDOW_RIGHT_COLUMN,WINDOW_FRAMED,text);                                      /* the original's end state */
    tp_text_save(g,&original_end);
    memcpy(p->framebuffer,saved,p->framebuffer_size);free(saved);
    tp_text_restore(g,&before);
    menu_window_text(g,XEEN_WINDOW_RIGHT_COLUMN,WINDOW_FRAMED,out);
    tp_text_restore(g,&original_end);
    tp_trace_text_state(g);
    {   xeen_poll_buttons b;tp_buttons(g,&b);
        for(i=0;i<3u;++i)if(b.enabled[i])tp_draw_button(g,i,0);
    }
    t->relocated=1;
    if(t->open){tp_overlay(g);(void)xeen_presentation_update_window(p,XEEN_WINDOW_VIEW_FRAME);}
    return 1;
}

/* Hook (location_animate, after the trainer's frame): the open ladder over it. */
static void training_preview_view(xeen_game *g)
{
    xeen_training_preview *t=tp_state(g);
    if(t && t->open && t->relocated && tp_here(g))tp_overlay(g);
}

/* Closing: the trainer's portrait frame (26978's) over the ladder, then the view. */
static void tp_close(xeen_game *g)
{
    xeen_movement *m=&g->movement;uint16_t f=xeen_read16(m,XEEN_DS_ANIMATION_FRAME),h=xeen_read16(m,XEEN_DS_ANIMATION_SPRITE(f/8u));
    g->addons->training.open=0;
    if(!h){h=xeen_read16(m,XEEN_DS_ANIMATION_SPRITE(0u));f=0;}
    if(h)exit_draw(g,h,(uint16_t)(f%8u),(int16_t)xeen_read16(m,XEEN_DS_ANIMATION_X),(int16_t)xeen_read16(m,XEEN_DS_ANIMATION_Y),0);
    (void)xeen_presentation_update_window(g->presentation,XEEN_WINDOW_VIEW_FRAME);
}
/* Hook (train_key, 2547B): P opens or closes the ladder (1: the caller
 * redraws, as the original does for a key it ignores); Esc closes it and
 * goes on to the original's leave (0). */
int training_preview_key(xeen_game *g,uint16_t key)
{
    xeen_training_preview *t=tp_state(g);
    if(!t || !t->relocated || !tp_here(g))return 0;
    if(key=='P'){if(t->open)tp_close(g);else t->open=1;return 1;}
    if(key==XEEN_KEY_ESCAPE && t->open)tp_close(g);
    return 0;
}

/* Hook (the 26E60 poll, 189E3): the three relocated buttons answer clicks
 * and show their key presses in place of the 1A3C set's two. */
static uint16_t training_preview_poll(xeen_game *g)
{
    xeen_training_preview *t=tp_state(g);xeen_poll_buttons b;uint16_t key;
    if(!t || !t->relocated || !tp_here(g))return panel_poll(g);
    tp_buttons(g,&b);
    g->input_poll.addon_buttons=&b;key=panel_poll(g);g->input_poll.addon_buttons=NULL;
    return key;
}
