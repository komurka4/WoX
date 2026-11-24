#include "combat_fixture.h"
#include <stdlib.h>

/* Phase 33 ownership contracts on the general Phase 40 combat state machine.
 * Synthetic fixture: two party members and one monster in the party cell. */
int main(void)
{
    static const uint8_t fast[7]={15,10,10,10,25,15,10},slow[7]={15,10,10,10,12,15,10};
    xeen_movement *m=malloc(sizeof *m),*before=malloc(sizeof *before);
    xeen_combat c={0};uint32_t boundary=0;uint8_t db[FIXTURE_DB_ROWS*60u];
    xeen_sprite_cache cache={0};xeen_sprite_archives archives={0};
    static uint8_t pixels[XEEN_PRESENT_PIXELS];xeen_presentation presentation={0};
    fixture_rng rng={NULL,NULL,0,0,{0}};
    CHECK(m && before);
    fixture_world(m,db);
    fixture_member(m,0,"Fast",0,5,fast,1,0,50);fixture_member(m,1,"Slow",0,5,slow,1,0,50);
    fixture_monster(m,db,3,7,30,4,20,1,1,4,8,100);
    CHECK(fixture_contact(m,db) && m->ds[0x98b4]==3 && m->ds[0x98b5]==0xff);
    presentation.framebuffer=pixels;presentation.framebuffer_size=sizeof pixels;
    xeen_write16(m,0x98d4,1);*before=*m;

    /* Resource ownership: no combat.icn archive rejects without mutation. */
    CHECK(xeen_combat_begin(&c,m,db,sizeof db,&cache,&archives,&presentation,fixture_draw,&rng,NULL)==XEEN_BAD_INPUT);
    CHECK(!memcmp(m,before,sizeof *m) && c.phase==XC_INACTIVE && cache.last==0);
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,NULL,NULL,NULL)==XEEN_BAD_INPUT); /* sole RNG owner required */
    CHECK(xeen_combat_initialize(&c,m,db,7u*60u,fixture_draw,&rng,NULL)==XEEN_BAD_INPUT); /* missing DB row */
    CHECK(!memcmp(m,before,sizeof *m));
    m->ds[0x98b4]=40;CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_BAD_INPUT);
    *m=*before;

    /* 38F6D..3912C: party speeds 25 and 12, monster 20 -> order [0,2,1]. */
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(c.phase==XC_AWAITING_COMMAND && rng.at==0 && c.rng_draws==0);
    CHECK(!memcmp(m->ds+0x9713,(uint8_t[]){0,2,1,255},4));
    CHECK(m->ds[0x97fe]==0 && m->ds[0x97ff]==0 && m->ds[0x306a]==3 && m->ds[0x9804]==0);
    CHECK(m->ds[0x9a1c]==2 && m->ds[0xfaf8]==2 && m->ds[0x971f]==2 && m->ds[0x96c3]==0);
    CHECK(!memcmp(m->ds+0xac0f,"\r\f00\003c\v000\t000\002Combat\n\v020\f15Fixture Beast\fd\001",44));
    CHECK(c.scenes==1); /* 96C0 zero: only the 390D9 loop-top scene */

    /* An unlisted key falls through 39161 to 3934F: no action, no draw. */
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,0xf2,&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && c.phase==XC_AWAITING_COMMAND && !c.commands && !c.actions && !rng.at);
    CHECK(m->ds[0x306a]==3 && m->ds[0x9804]==0 && m->ds[0x97ff]==0);
    /* Numeric 2 with an empty slot keeps the target; numeric 1 selects it. */
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'2',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && c.commands==1 && m->ds[0x306a]==3 && m->ds[0x9804]==0);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'1',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && c.commands==2 && !c.actions && !c.rng_draws);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'1',NULL)==XEEN_BAD_INPUT);
    /* Phase63: Cast (391E5) hands 3D8C1 to the game owner with DS as it
     * was; an unpaid dialog (AX 0) returns at 391ED: 2A1F6(9720[97FF]),
     * 3934F, the same actor's poll, no action and no draw. */
    *before=*m;
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'C',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x391e5 && c.phase==XC_AWAITING_CAST && !memcmp(m,before,sizeof *m) && c.commands==3);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'1',&boundary)==XEEN_BAD_INPUT);
    m->ds[0x96c3]=0xff;
    CHECK(xeen_combat_after_cast(&c,m,db,sizeof db,fixture_draw,&rng,NULL,0,&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && c.phase==XC_AWAITING_COMMAND && !c.actions && !c.rng_draws && m->ds[0x97ff]==0);
    CHECK(m->ds[0x96c3]==0 && m->ds[0x9720]==0);
    CHECK(xeen_combat_after_cast(&c,m,db,sizeof db,fixture_draw,&rng,NULL,0,&boundary)==XEEN_BAD_INPUT);
    /* Phase100 (until then 'F' stopped here at 3914E): Options (3929C)
     * hands 3E9B6 to the game owner with DS as it was; its return (392D9:
     * 2A1F6(97FF), 3934F) keeps the actor's turn without a draw, and only
     * the site the command named is accepted. */
    *before=*m;
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'O',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3929c && c.phase==XC_AWAITING_CAST && c.modal_site==0x3929c && !memcmp(m,before,sizeof *m));
    CHECK(xeen_combat_after_modal(&c,m,db,sizeof db,fixture_draw,&rng,NULL,0x392bb,0,&boundary)==XEEN_BAD_INPUT);
    CHECK(xeen_combat_after_modal(&c,m,db,sizeof db,fixture_draw,&rng,NULL,0x3929c,0,&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && c.phase==XC_AWAITING_COMMAND && !c.modal_site && !c.actions && !c.rng_draws && m->ds[0x97ff]==0);
    CHECK(xeen_combat_after_modal(&c,m,db,sizeof db,fixture_draw,&rng,NULL,0x3929c,0,&boundary)==XEEN_BAD_INPUT);
    /* A turn (39188) keeps the actor too: ED96 + 1, F5E6 toggled (9732 0),
     * no draw (96C0 0: no 2A701); its 3067 = 1 is consumed by the loop
     * top's 1B503. */
    m->ds[0xed96]=3;m->ds[0x9732]=0;xeen_write16(m,0xf5e6,0);m->ds[0x96c0]=0;m->ds[0x3067]=0;
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,0xf1,&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && m->ds[0xed96]==0 && xeen_read16(m,0xf5e6)==1 && !c.actions && !c.rng_draws);
    free(m);free(before);
    puts("phase33 combat ownership, admission and command boundary tests passed");return 0;
}
