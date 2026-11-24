/* Add-on spell_list_sort (owner-approved in Phase 211, NOT original
 * behavior; include/xeen/addon/spell_list_sort.h): the groups, the rows,
 * the line text and the per-character setting. */
#include "xeen/addon/spell_list_sort.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const groups[XEEN_SLS_GROUPS]={"Healing","Attack","Control","Protection","Travel","Detection","Utility"};
static const struct { int8_t group, rank, target; } order[76]={
#include "order.inc"
};

const char *xeen_sls_group_name(unsigned group){return group<XEEN_SLS_GROUPS?groups[group]:"";}


unsigned xeen_sls_rows(const uint8_t *spell,unsigned count,int16_t *rows,unsigned cap)
{
    unsigned n=0,g,i;
    for(g=0;g<XEEN_SLS_GROUPS;++g){
        unsigned in=0,r;
        for(i=0;i<count;++i)if(spell[i]<76u && (unsigned)order[spell[i]].group==g)++in;
        if(!in)continue;
        if(n<cap)rows[n++]=(int16_t)(-1-(int)g);
        for(r=0;r<76u;++r)                       /* the ranks in order; a spell listed twice keeps both lines */
            for(i=0;i<count;++i)
                if(spell[i]<76u && (unsigned)order[spell[i]].group==g && (unsigned)order[spell[i]].rank==r && n<cap)
                    rows[n++]=(int16_t)i;
    }
    for(i=0;i<count;++i)if(spell[i]>=76u && n<cap)rows[n++]=(int16_t)i;   /* none in the shipped lists */
    return n;
}

/* The dots' ink: 0x7C draws a 2-pixel dot with a 2-pixel lead and a 5-pixel
 * advance (measured with the game's reduced font, Phase 211), so one dot is
 * 2 pixels of ink, two 7, three 12. Their right edge is x 144: 3 pixels
 * before the widest cost ("100/20" starts at 148) and 4 after the longest
 * Attack name with three dots (Elemental Storm ends at 128). */
int xeen_sls_line(const char *line,unsigned spell,unsigned colour,char *out,size_t n)
{
    static const unsigned ink[3]={2,7,12};
    const char *cost=strstr(line,"\x03r");int target=-1,w;
    if(spell<76u)target=order[spell].target;
    if(target<0 || target>2 || !cost){w=snprintf(out,n,"%s",line);return w>=0 && (size_t)w<n;}
    w=snprintf(out,n,"%.*s\t%03u\x0c%02u%.*s\x0c%02u%s",(int)(cost-line),line,144u-ink[target]+1u-35u-2u,33u,
        target+1,"|||",colour,cost);
    return w>=0 && (size_t)w<n;
}

/** Returns 1 when two stored character names are equal (the stored length). */
static int same_name(const char *a,const char *b){return !strncmp(a,b,XEEN_SLS_NAME-1);}
int xeen_sls_mode(const xeen_spell_list_sort *s,unsigned roster,const char *name)
{
    if(!s || roster>=XEEN_SLS_ROSTER || !name || !s->mode[roster])return 0;
    return same_name(s->name[roster],name)?s->mode[roster]:0;
}
void xeen_sls_set_mode(xeen_spell_list_sort *s,unsigned roster,const char *name,int mode)
{
    if(!s || roster>=XEEN_SLS_ROSTER || !name)return;
    s->mode[roster]=(uint8_t)(mode?1:0);
    memset(s->name[roster],0,XEEN_SLS_NAME);
    (void)snprintf(s->name[roster],XEEN_SLS_NAME,"%s",name);
}
void xeen_sls_clear(xeen_spell_list_sort *s)
{
    if(!s)return;
    memset(s->mode,0,sizeof s->mode);memset(s->name,0,sizeof s->name);
}

uint8_t *xeen_sls_serialize(const xeen_spell_list_sort *s,size_t *size)
{
    unsigned i,count=0;uint8_t *b,*p;
    if(size)*size=0;
    if(!s || !size)return NULL;
    for(i=0;i<XEEN_SLS_ROSTER;++i)if(s->mode[i])++count;
    if(!count || !(b=malloc(2u+18u*count)))return NULL;
    b[0]=1;b[1]=(uint8_t)count;p=b+2;
    for(i=0;i<XEEN_SLS_ROSTER;++i)if(s->mode[i]){
        p[0]=(uint8_t)i;p[1]=s->mode[i];memcpy(p+2,s->name[i],XEEN_SLS_NAME);p+=18;
    }
    *size=2u+18u*count;return b;
}
int xeen_sls_deserialize(xeen_spell_list_sort *s,const uint8_t *b,size_t n)
{
    unsigned i,count;
    if(!s)return 0;
    xeen_sls_clear(s);
    if(!b || n<2u || b[0]!=1u)return 0;
    count=b[1];
    if(n!=2u+18u*count)return 0;
    for(i=0;i<count;++i){
        const uint8_t *p=b+2u+18u*i;char name[XEEN_SLS_NAME];
        if(p[0]>=XEEN_SLS_ROSTER || p[1]>1u){xeen_sls_clear(s);return 0;}
        memcpy(name,p+2,XEEN_SLS_NAME);name[XEEN_SLS_NAME-1]=0;
        xeen_sls_set_mode(s,p[0],name,p[1]);
    }
    return 1;
}
