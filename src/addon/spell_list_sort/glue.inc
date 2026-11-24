/* Add-on spell_list_sort (owner-approved in Phase 211, NOT original
 * behavior; include/xeen/addon/spell_list_sort.h): the game's part.
 * Included by src/game/game_location.c (its translation unit); game_bank.c
 * reaches it through game_internal.h. The original code calls it through
 * one-line hooks:
 *   guild_page's end      the list again over the page (Type) and the button
 *   guild_key, cast_list_key's head   O, and in Type the arrows, PgUp/PgDn and
 *                         the digits (a line goes to the original handler)
 *   the GUILD_POLL and CAST_LIST polls   the button's click test and feedback
 * With the add-on off none of them changes anything. With it on the DS, the
 * saves, the RNG and the clock are the original's: the list's own state (top,
 * choice, count, the DS lines) is untouched; only the screen differs. */

/* The button: 28x10 at the left end of the title row (above the list), drawn
 * like scroll.icn's Select/Exit (border 193 top/left, 167 bottom/right, fill
 * 196, text 187: colour 33 is the nearest table entry, 189, remapped). */
enum { SLS_X=37, SLS_Y=14, SLS_W=28, SLS_H=10, SLS_LINES=10 };

/** Returns the spell_list_sort state, or NULL when the add-on is off. */
static xeen_spell_list_sort *sls_state(xeen_game *g)
{return g->addons && g->addons->spell_list_sort?&g->addons->sort:NULL;}
/* The list is open (3F05D's poll) on a party member: its roster index; else -1
 * (no button: the Cast dialog's caster-less record B2BC, a closed list). */
static int sls_roster(const xeen_game *g)
{
    const uint8_t *ds=g->movement.ds;unsigned n;
    if(!g->guild_active || g->guild_record<XEEN_PARTY_RECORDS || (g->guild_record-XEEN_PARTY_RECORDS)%XEEN_CHARACTER_BYTES)return -1;
    n=(g->guild_record-XEEN_PARTY_RECORDS)/XEEN_CHARACTER_BYTES;
    if(n>=ds[XEEN_DS_PARTY_COUNT] || n>=8u || ds[XEEN_DS_PARTY_MEMBERS+n]>=XEEN_SLS_ROSTER)return -1;
    return ds[XEEN_DS_PARTY_MEMBERS+n];
}
/** Returns 1 when the current character's guild or Cast list is sorted by type. */
static int sls_type(xeen_game *g)
{
    xeen_spell_list_sort *s=sls_state(g);int r=sls_roster(g);
    return s && r>=0 && xeen_sls_mode(s,(unsigned)r,ds_text(g,g->guild_record))==1;
}
/* The spell of the open list's line I: the record's class list (DS:201E, XEEN_CLASS_SPELL_LIST entries per class)
 * at guild_index[I], as 3EBD6 built the line; FFh for an index past the class list. */
static unsigned sls_line_spell(const xeen_game *g,unsigned i)
{
    const uint8_t *ds=g->movement.ds;
    unsigned cat=cast_category(ds[(uint16_t)(g->guild_record+XEEN_CHARACTER_CLASS)]);
    return g->guild_index[i]<XEEN_CLASS_SPELL_LIST?ds[XEEN_DS_CLASS_SPELL_LISTS+XEEN_CLASS_SPELL_LIST*cat+g->guild_index[i]]:0xffu;
}
/* The rows of the open list (its count lines, at most XEEN_CLASS_SPELL_LIST, each line's spell: sls_line_spell). */
static unsigned sls_rows(xeen_game *g,int16_t *rows)
{
    uint8_t spell[XEEN_CLASS_SPELL_LIST];unsigned i,count=g->guild_count;
    if(count>XEEN_CLASS_SPELL_LIST)count=XEEN_CLASS_SPELL_LIST;
    for(i=0;i<count;++i)spell[i]=(uint8_t)sls_line_spell(g,i);
    return xeen_sls_rows(spell,count,rows,XEEN_SLS_ROWS);
}

/**
 * Draws the sort-order button (raised, or sunk when PRESSED) with its current label onto the
 * framebuffer.
 */
static void sls_draw_button(void *user,unsigned i,int pressed)
{
    xeen_game *g=user;xeen_presentation *p=g->presentation;uint8_t *fb;unsigned x,y;
    uint16_t inner[4]={SLS_X+1,SLS_Y+2,SLS_W-2,8};char text[16];
    (void)i;
    if(!p || p->framebuffer_size<320u*200u || !g->text_window.loaded)return;
    fb=p->framebuffer;
    for(y=0;y<SLS_H;++y)for(x=0;x<SLS_W;++x){
        uint8_t c=196;
        if(!y || !x)c=pressed?167:193;
        if(y==SLS_H-1u || (x==SLS_W-1u && y))c=pressed?193:167;
        fb[(SLS_Y+y)*320u+SLS_X+x]=c;
    }
    (void)snprintf(text,sizeof text,"\x02\x03" "c\x0c%02u%s",33u,sls_type(g)?"Type":"A-Z");
    (void)xeen_text_write(&g->text_window,fb,p->framebuffer_size,inner,text,NULL);
    for(y=1;y<SLS_H-1u;++y)for(x=1;x<SLS_W-1u;++x)if(fb[(SLS_Y+y)*320u+SLS_X+x]==189u)fb[(SLS_Y+y)*320u+SLS_X+x]=187;
}
/* A long name's centred title ("Spells for <name>", 7282) reaches under the
 * button: it is written again centred in the rest of the title row. */
static void sls_title(xeen_game *g)
{
    xeen_presentation *p=g->presentation;uint8_t *fb=p->framebuffer;unsigned x,y;int under=0;char text[80];
    uint16_t row[4]={SLS_X+SLS_W+2,SLS_Y+2,199u-(SLS_X+SLS_W+2),9};
    for(y=SLS_Y;y<SLS_Y+SLS_H;++y)for(x=35;x<SLS_X+SLS_W+1u;++x)if(fb[y*320u+x]!=153u)under=1;
    if(!under)return;
    for(y=SLS_Y;y<SLS_Y+SLS_H;++y)memset(fb+y*320u+35u,153,199u-35u);
    (void)snprintf(text,sizeof text,"\x02\x03" "c\x0c" "dSpells for %s",ds_text(g,g->guild_record));
    (void)xeen_text_write(&g->text_window,fb,p->framebuffer_size,row,text,NULL);
}
/* The page's list area (window 25h's text inside 19h): the digit column (x 35, 6 wide) and the lines (x 42, 145
 * wide) from y 28 to 117, cleared to the window background (153); a group heading line is 9 pixels high at
 * (44, 29 + 9 k), 142 wide. */
enum { SLS_LIST_TOP=28, SLS_LIST_END=118, SLS_DIGITS_X=35, SLS_DIGITS_W=6, SLS_LINES_X=42, SLS_LINES_W=145,
       SLS_BACKGROUND=153, SLS_HEAD_X=44, SLS_HEAD_Y=29, SLS_HEAD_W=142, SLS_LINE_H=9 };
/** OUT = R inset by its 8-pixel border (a window's text rectangle). */
static void sls_inset(const uint16_t r[4],uint16_t out[4])
{
    out[0]=(uint16_t)(r[0]+8u);out[1]=(uint16_t)(r[1]+8u);out[2]=(uint16_t)(r[2]-16u);out[3]=(uint16_t)(r[3]-16u);
}
/* Hook (guild_page's end, before its 50BF6(19h)): the button, and in Type the
 * ten lines from the add-on's top over the original's: the digits (window
 * 19h's text, 7263's form; none on a group line), the lines (window 25h's,
 * 72A0's form, each in its list colour) and the "- Group -" lines. An empty
 * list (its message line) stays the original's. */
void spell_list_sort_page(xeen_game *g)
{
    xeen_spell_list_sort *s=sls_state(g);xeen_presentation *p=g->presentation;
    int16_t rows[XEEN_SLS_ROWS];unsigned n,k,y;uint16_t r25[4],r19[4],b25[4],b19[4];
    char text[1600],digits[64],line[128];size_t at=0,dt=0;
    if(!s || !p || p->framebuffer_size<320u*200u || !g->text_window.loaded || sls_roster(g)<0)return;
    if(s->record!=g->guild_record){s->record=g->guild_record;s->top=0;}
    sls_title(g);
    sls_draw_button(g,0,0);
    if(!sls_type(g) || !g->guild_count)return;
    if(!xeen_presentation_window(p,XEEN_WINDOW_VIEW_DIALOG_TEXT,r25) || !xeen_presentation_window(p,XEEN_WINDOW_VIEW_DIALOG,r19))return;
    sls_inset(r25,b25);
    sls_inset(r19,b19);
    n=sls_rows(g,rows);
    if(s->top+SLS_LINES>n)s->top=n>SLS_LINES?n-SLS_LINES:0u;
    for(y=SLS_LIST_TOP;y<SLS_LIST_END;++y){
        memset(p->framebuffer+y*320u+SLS_DIGITS_X,SLS_BACKGROUND,SLS_DIGITS_W);
        memset(p->framebuffer+y*320u+SLS_LINES_X,SLS_BACKGROUND,SLS_LINES_W);
    }
    at=(size_t)snprintf(text,sizeof text,"\x02\x03l\x0b" "015");
    dt=(size_t)snprintf(digits,sizeof digits,"\x02\x03l\x0b" "015\t001");
    for(k=0;k<SLS_LINES;++k){
        unsigned r=s->top+k;const char *tab=k?"\t010":"\t011";
        if(r<n && rows[r]>=0){
            unsigned i=(unsigned)rows[r],spell=sls_line_spell(g,i);
            unsigned colour=i<sizeof g->list_colour?g->list_colour[i]:9u;
            /* the original line I: 50 bytes each at 9C6F (guild_list_spell, 3ECDF) */
            if(!xeen_sls_line(ds_text(g,(uint16_t)(XEEN_DS_SCRATCH+50u*i)),spell,colour,line,sizeof line))line[0]=0;
            at+=(size_t)snprintf(text+at,sizeof text-at,"%s\x0c%02u%s\x0c" "d\x03l\n",tab,colour,line);
            digits[dt++]=(char)('0'+(k+1u)%10u);
        }else if(r<n){
            char head[48];uint16_t hb[4]={SLS_HEAD_X,(uint16_t)(SLS_HEAD_Y+SLS_LINE_H*k),SLS_HEAD_W,SLS_LINE_H};
            at+=(size_t)snprintf(text+at,sizeof text-at,"%s\x0c" "d\x03l\n",tab);
            (void)snprintf(head,sizeof head,"\x02\x03" "c\x0c%02u- %s -\x0c" "d",33u,xeen_sls_group_name((unsigned)(-1-rows[r])));
            (void)xeen_text_write(&g->text_window,p->framebuffer,p->framebuffer_size,hb,head,NULL);
        }else {
            at+=(size_t)snprintf(text+at,sizeof text-at,"%s\x0c" "d\x03l\n",tab);
            digits[dt++]=(char)('0'+(k+1u)%10u);
        }
        if(k+1u<SLS_LINES)digits[dt++]='\n';
        if(at>=sizeof text)return;
    }
    digits[dt]=0;
    (void)xeen_text_write(&g->text_window,p->framebuffer,p->framebuffer_size,b25,text,NULL);
    (void)xeen_text_write(&g->text_window,p->framebuffer,p->framebuffer_size,b19,digits,NULL);
}
/* Hook (guild_key and cast_list_key, first): 1 with *out when the add-on took
 * the key. O switches the member's list; in Type Up/Down move the add-on's
 * top by a line, PgUp/PgDn by ten, and a digit takes the line at that page
 * position: a spell's line goes to the original handler as its own digit
 * would (the original's top set to that line for the call, then restored),
 * a group line or an empty one does nothing. Everything else is the
 * original's. */
int spell_list_sort_key(xeen_game *g,uint16_t key,xeen_status *out)
{
    xeen_spell_list_sort *s=sls_state(g);int roster;int16_t rows[XEEN_SLS_ROWS];unsigned n,r;
    if(!s || s->busy || (roster=sls_roster(g))<0)return 0;
    if(key=='O'){
        xeen_sls_set_mode(s,(unsigned)roster,ds_text(g,g->guild_record),!sls_type(g));
        s->top=0;guild_page(g);
        *out=panel_set(g,g->list_caster_spells?CAST_LIST:GUILD_POLL,GUILD_POLL_BOUNDARY);return 1;
    }
    if(!sls_type(g) || !g->guild_count)return 0;
    n=sls_rows(g,rows);
    if(key==XEEN_KEY_UP || key==XEEN_KEY_DOWN || key==XEEN_KEY_PAGE_UP || key==XEEN_KEY_PAGE_DOWN){
        int top=(int)s->top+(key==XEEN_KEY_UP?-1:key==XEEN_KEY_DOWN?1:key==XEEN_KEY_PAGE_UP?-SLS_LINES:SLS_LINES);
        if(top>(int)n-SLS_LINES)top=(int)n-SLS_LINES;
        s->top=top<0?0u:(unsigned)top;
        guild_page(g);
        *out=panel_set(g,g->list_caster_spells?CAST_LIST:GUILD_POLL,GUILD_POLL_BOUNDARY);return 1;
    }
    if(key<'0' || key>'9')return 0;
    r=s->top+(key=='0'?9u:(unsigned)(key-'1'));
    if(r<n && rows[r]>=0){
        unsigned top=g->guild_top;
        g->guild_top=(unsigned)rows[r];s->busy=1;
        *out=g->list_caster_spells?cast_list_key(g,'1'):guild_key(g,'1');
        s->busy=0;g->guild_top=top;
        /* The Cast list's digit composed its page (AC0F) from that line: the
         * page again from the original's own top, as the original leaves it.
         * (The guild's digit opens its confirm without a page.) */
        if(g->list_caster_spells && g->guild_active && g->panel_stage==CAST_LIST)guild_page(g);
        return 1;
    }
    *out=panel_set(g,g->list_caster_spells?CAST_LIST:GUILD_POLL,GUILD_POLL_BOUNDARY);return 1;
}
/* Hook (the GUILD_POLL and CAST_LIST polls, 189E3): the button answers a
 * click and shows its key press, beside the list's own buttons. */
uint16_t spell_list_sort_poll(xeen_game *g)
{
    xeen_poll_buttons b;uint16_t key;
    if(!sls_state(g) || sls_roster(g)<0)return panel_poll(g);
    memset(&b,0,sizeof b);b.count=1;b.draw=sls_draw_button;b.user=g;
    b.x[0]=SLS_X;b.y[0]=SLS_Y;b.w[0]=SLS_W;b.h[0]=SLS_H;b.key[0]='O';b.enabled[0]=1;
    g->input_poll.addon_extra=&b;key=panel_poll(g);g->input_poll.addon_extra=NULL;
    return key;
}
