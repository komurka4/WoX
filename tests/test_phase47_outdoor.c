#include "combat_fixture.h"
#include <stdlib.h>

/* Phase 47 rule contracts on the synthetic fixture (not an authentic session):
 * outdoor (F608) contact admission through 2126C, the 1B503 effects-stage
 * 96EF countdown indoors and outdoors, and the owner contract for the 2AAB1
 * queued ranged delivery that ends combat's direct 2A701 call at 3906E. The
 * genuine Ogre encounter is covered by test_phase47_connected.py. */
typedef struct {unsigned calls;xeen_status status;uint32_t boundary;} owner;

static xeen_status deliver(void *user,xeen_movement *m,uint32_t *boundary)
{
    owner *o=user;++o->calls;
    if(o->status!=XEEN_OK){*boundary=o->boundary;return o->status;}
    m->ds[0x97f6]=0;m->ds[0x97f8]=0;*boundary=0; /* 41519 */
    return XEEN_OK;
}

static void prepare(xeen_movement *m,uint8_t *db,int outdoor)
{
    static const uint8_t fast[7]={15,10,10,10,25,10,10};
    fixture_world(m,db);
    if(outdoor)xeen_write16(m,0xf608,0x8000);
    fixture_member(m,0,"Zippo",5,5,fast,24,0,60);
    fixture_member(m,1,"Other",0,5,fast,24,0,60);
    fixture_weapon(m,24,2,4);
    fixture_monster(m,db,3,11,90,10,5,1,1,8,10,200);
}

static int contact(xeen_movement *m,const uint8_t *db)
{
    xeen_status s=xeen_read16(m,0xf608)?xeen_scene_outdoor_monsters(m,db,FIXTURE_DB_ROWS*60u):
        xeen_scene_monsters(m,db,FIXTURE_DB_ROWS*60u);
    return s==XEEN_OK && m->ds[0x98b4]==3u && m->ds[0x98b5]==0xffu;
}

/* One 'A' against record 3 that hits and does not kill: 96EF counts down
 * from 3 on each following in-combat scene call. */
static int hit_countdown(xeen_movement *m,uint8_t *db,int outdoor)
{
    static const uint8_t results[3]={3,3,15},lows[3]={1,1,1};
    fixture_rng rng={results,lows,3,0,{0}};xeen_combat c;uint32_t boundary=0;
    prepare(m,db,outdoor);
    c=(xeen_combat){0};
    CHECK(contact(m,db));
    CHECK(xeen_combat_initialize(&c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(c.phase==XC_AWAITING_COMMAND && m->ds[0x97ff]==0);
    CHECK(xeen_combat_command(&c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912cu && c.last_hit==1 && xeen_read16(m,0xe530+3*20+18)<90u);
    CHECK(m->ds[0x97b2]==(xeen_read16(m,0x972e)?0u:1u) && m->ds[0x96ef]==0 && c.scenes>=4u);
    return 0;
}

int main(void)
{
    xeen_movement *m=malloc(sizeof *m);uint8_t db[FIXTURE_DB_ROWS*60u];xeen_combat c;
    owner o;fixture_rng rng={NULL,NULL,0,0,{0}};
    CHECK(m);
    /* Outdoor contact is admitted (2126C selection). Phase73: 38F5F has no
     * world refusal, so Darkside (DS 94 = 1) initializes the same way. */
    prepare(m,db,1);CHECK(contact(m,db));
    c=(xeen_combat){0};
    CHECK(xeen_combat_initialize(&c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(c.phase==XC_AWAITING_COMMAND && m->ds[0x9a1c]==2 && c.attackers[0]==3 && m->ds[0x306a]==3);
    CHECK(m->ds[0x9713]==0 && m->ds[0x9714]==1 && m->ds[0x9715]==2 && m->ds[0x9716]==0xff);
    prepare(m,db,1);CHECK(contact(m,db));m->ds[0x94]=1;c=(xeen_combat){0};
    CHECK(xeen_combat_initialize(&c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(c.phase==XC_AWAITING_COMMAND && m->ds[0x9a1c]==2 && c.attackers[0]==3 && m->ds[0x306a]==3);
    CHECK(m->ds[0x9713]==0 && m->ds[0x9714]==1 && m->ds[0x9715]==2 && m->ds[0x9716]==0xff);
    /* Effects stage 1BBDF/1B7F1 counts down the hit-slot flag in combat. */
    CHECK(hit_countdown(m,db,0)==0);
    CHECK(hit_countdown(m,db,1)==0);
    /* 3906E: with 96C0 set, init's 2A701 ends at 2AAB1 when a ranged queue
     * is pending (97F6; the record's cleared visibility byte keeps the
     * synthetic update off the map). The owner runs exactly once, before the
     * 1B503 scene and the first 3912C poll, and survives the reset of c. */
    prepare(m,db,1);CHECK(contact(m,db));
    m->ds[0x96c0]=1;m->ds[0x96c4]=1;m->ds[0x97f6]=1;m->ds[0xe530+3*20+4]=0;
    o=(owner){0,XEEN_OK,0};c=(xeen_combat){0};c.ranged_delivery=deliver;c.ranged_user=&o;
    CHECK(xeen_combat_initialize(&c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(o.calls==1 && c.phase==XC_AWAITING_COMMAND && m->ds[0x97f6]==0 && m->ds[0x96c0]==0);
    CHECK(c.ranged_delivery==deliver && c.ranged_user==&o && m->ds[0xe530+3*20+4]==1);
    /* An unsupported delivery keeps its exact boundary; no retry. */
    prepare(m,db,1);CHECK(contact(m,db));
    m->ds[0x96c0]=1;m->ds[0x96c4]=1;m->ds[0x97f6]=1;m->ds[0xe530+3*20+4]=0;
    o=(owner){0,XEEN_SERVICE_REQUIRED,0x38163u};c=(xeen_combat){0};c.ranged_delivery=deliver;c.ranged_user=&o;
    CHECK(xeen_combat_initialize(&c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(o.calls==1 && c.phase==XC_UNSUPPORTED_CONTINUATION && c.last_boundary==0x38163u && m->ds[0x97f6]==1);
    /* Without an owner the original 2AAB1 stop stays explicit. */
    prepare(m,db,1);CHECK(contact(m,db));
    m->ds[0x96c0]=1;m->ds[0x96c4]=1;m->ds[0x97f6]=1;m->ds[0xe530+3*20+4]=0;c=(xeen_combat){0};
    CHECK(xeen_combat_initialize(&c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(c.phase==XC_UNSUPPORTED_CONTINUATION && c.last_boundary==0x2aab1u && m->ds[0x97f6]==1);
    free(m);
    puts("phase47 outdoor admission, effects countdown and 2AAB1 delivery owner passed");
    return 0;
}
