#include "combat_fixture.h"
#include "xeen/bash.h"
#include <stdlib.h>

static void setup(xeen_movement *m,uint8_t *db,uint8_t might,int16_t hp)
{
    uint8_t stats[7]={might,10,10,10,10,10,10};
    fixture_world(m,db);
    fixture_member(m,0,"Basher One",0,1,stats,0,0,hp);
    fixture_member(m,1,"Basher Two",0,1,stats,0,0,hp);
    xeen_navigation_set(m,(xeen_navigation){0,8,10,41});
    xeen_write16(m,0xf46a,41);xeen_write16(m,0xf608,0);
    m->ds[0xc0fc]=1;m->ds[0xc101]=60;m->ds[0xc102]=50;
}

static int set_ahead(xeen_movement *m,uint8_t wall)
{
    uint16_t found=0;
    if(xeen_wall_set(m,8,10,0,wall)!=XEEN_OK)return 0;
    return xeen_navigation_query(m,2,&found)==XEEN_OK && found==wall;
}

static uint16_t high_raw(void *user,uint16_t incoming)
{
    *(uint16_t *)user=incoming;return 0xffffu;
}

int main(void)
{
    xeen_movement *m=malloc(sizeof *m);uint8_t db[FIXTURE_DB_ROWS*60u];
    xeen_bash_result result;uint8_t before[65536];
    static const uint8_t one[]={1};static const uint8_t low[]={1};
    fixture_rng rng={one,low,1,0,{0}};
    CHECK(m);

    /* 3D689: two able members each lose two HP; Might+1 reaches the normal
     * threshold and 2DE1D opens both faces of the wall. */
    setup(m,db,30,100);CHECK(set_ahead(m,8));
    CHECK(xeen_bash_wall(m,fixture_draw,&rng,NULL,&result)==XEEN_OK);
    CHECK(result.first==0 && result.second==1 && result.wall==8);
    CHECK(rng.incoming[0]==30);
    CHECK(result.threshold==50 && result.roll==1 && result.might==61 && result.opened);
    CHECK(xeen_read16(m,0xb41e + 0x156)==98 && xeen_read16(m,0xb41e + 0x162 + 0x156)==98);
    {uint16_t wall=0;CHECK(xeen_navigation_query(m,2,&wall)==XEEN_OK && wall==3);}
    xeen_navigation_set(m,(xeen_navigation){2,8,11,41});
    {uint16_t wall=0;CHECK(xeen_navigation_query(m,2,&wall)==XEEN_OK && wall==3);}

    /* A failed strength check still charges both HP and consumes exactly one
     * 1..30 draw, but leaves the wall unchanged. */
    rng=(fixture_rng){one,low,1,0,{0}};setup(m,db,10,100);CHECK(set_ahead(m,8));
    CHECK(xeen_bash_wall(m,fixture_draw,&rng,NULL,&result)==XEEN_OK);
    CHECK(!result.opened && result.might==21 && rng.at==1);
    CHECK(xeen_read16(m,0xb41e + 0x156)==98 && xeen_read16(m,0xb41e + 0x162 + 0x156)==98);
    {uint16_t wall=0;CHECK(xeen_navigation_query(m,2,&wall)==XEEN_OK && wall==8);}

    /* 3D806 retains second Might; 504E0 masks bit 15 before 1..30. */
    {uint16_t incoming=0;
     setup(m,db,30,100);CHECK(set_ahead(m,8));
     CHECK(xeen_bash_wall(m,high_raw,&incoming,NULL,&result)==XEEN_OK);
     CHECK(incoming==30 && result.roll==8 && result.might==68);
    }

    /* Wall kinds 7/14/15 take their original counter paths without RNG. */
    rng=(fixture_rng){one,low,1,0,{0}};setup(m,db,30,100);CHECK(set_ahead(m,7));
    CHECK(xeen_bash_wall(m,fixture_draw,&rng,NULL,&result)==XEEN_OK);
    CHECK(rng.at==0 && m->ds[0xf57a]==1 && m->ds[0xf5b6]==1 && m->ds[0xf5ca]==1);

    /* Phase149 (replaces the Phase41B boundary, which stopped here): a
     * basher at 2 HP takes 3D6FD/3D75D's 2A152(member, 2): HP 0, then
     * unconscious (+14Fh) when HP + max HP >= 1, else dead (+150h); the wall's
     * roll follows (one draw). Compared in original bytes by family phase149
     * (p149-bash-lowhp-*, p149-bash-death-clouds). */
    setup(m,db,30,2);CHECK(set_ahead(m,8));memcpy(before,m->ds,sizeof before);
    rng=(fixture_rng){one,low,1,0,{0}};
    CHECK(xeen_bash_wall(m,fixture_draw,&rng,NULL,&result)==XEEN_OK);
    {unsigned k;for(k=0;k<2u;++k){const uint8_t *ch=m->ds+0xb41eu+k*0x162u;
        int alive=(int16_t)xeen_combat_max_hp(m,ch)>=1;
        CHECK(xeen_read16(m,(uint16_t)(0xb41eu+k*0x162u+0x156u))==0);
        CHECK(alive?(ch[0x14f]==1 && !ch[0x150]):(ch[0x150]==1 && !ch[0x14f]));}}
    CHECK(rng.at==1 && memcmp(before,m->ds,sizeof before));

    free(m);puts("phase41B original bash HP, RNG, threshold, wall and 2A152 contracts passed");return 0;
}
