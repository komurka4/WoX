#include "xeen/entities.h"
#include "combat_fixture.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#undef CHECK
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int trace_capacity(void)
{
    static const uint8_t stats[7]={19,13,13,19,28,25,14};
    static const uint8_t results[5]={1,3,5,3,3},lows[5]={1,1,1,1,1};
    xeen_movement *a=malloc(sizeof *a),*b=malloc(sizeof *b);
    xeen_combat ca={0},cb;uint8_t db[FIXTURE_DB_ROWS*60u];uint32_t ba=0,bb=0;
    fixture_rng ra={results,lows,5,0,{0}},rb=ra;
    CHECK(a && b);fixture_world(a,db);
    fixture_member(a,0,"Attacker",1,5,stats,22,0,85);
    fixture_member(a,1,"Next",1,5,stats,22,0,85);
    fixture_weapon(a,22,4,5);fixture_monster(a,db,18,11,30,10,20,2,2,12,10,100);
    CHECK(fixture_contact(a,db));
    CHECK(xeen_combat_initialize(&ca,a,db,sizeof db,fixture_draw,&ra,NULL)==XEEN_SERVICE_REQUIRED);
    *b=*a;cb=ca;cb.rng_draws=XEEN_COMBAT_RNG_CAPACITY;
    CHECK(xeen_combat_command(&ca,a,db,sizeof db,fixture_draw,&ra,NULL,'A',&ba)==XEEN_SERVICE_REQUIRED);
    CHECK(xeen_combat_command(&cb,b,db,sizeof db,fixture_draw,&rb,NULL,'A',&bb)==XEEN_SERVICE_REQUIRED);
    CHECK(ba==bb && !memcmp(a->ds,b->ds,65536) && ra.at==rb.at && ra.at==5);
    CHECK(ca.rng_trace_complete && !cb.rng_trace_complete);
    free(a);free(b);return 0;
}

int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--probe")){
        unsigned mode,a,b,c,ax,cx,dx,low,high;
        while(scanf("%u %u %u %u %u %u %u %u %u",&mode,&a,&b,&c,&ax,&cx,&dx,&low,&high)==9){
            xeen_entity_rng r={{(uint16_t)a,(uint16_t)b,(uint16_t)c}};uint16_t raw,result=0;int ok;
            if(mode)raw=xeen_entity_rng_draw(&r,(uint16_t)ax,
                (xeen_rng_time){(uint8_t)(cx>>8),(uint8_t)cx,(uint8_t)(dx>>8),(uint8_t)dx},NULL);
            else raw=xeen_entity_rng_mix(&r,(uint16_t)ax,(uint16_t)cx,(uint16_t)dx);
            ok=xeen_entity_rng_range(raw,(uint16_t)low,(uint16_t)high,&result);
            printf("%u %u %u %u %d %u\n",raw,r.words[0],r.words[1],r.words[2],ok,result);
        }
        return 0;
    }
    CHECK(trace_capacity()==0);
    {
        xeen_entity_rng a={{12309,32602,15439}},b=a;
        xeen_rng_transition t;uint16_t v=999,raw;
        raw=xeen_entity_rng_draw(&a,10,(xeen_rng_time){12,34,56,78},&t);
        CHECK(raw==xeen_entity_rng_draw(&b,0xff01,(xeen_rng_time){12,34,56,78},NULL));
        CHECK(!memcmp(&a,&b,sizeof a));CHECK(t.incoming_ax==10 && t.post_time_ax==0x2c00);
        CHECK(t.cx==0x0c22 && t.dx==0x384e && t.raw==raw);
        CHECK(t.before.words[0]==12309 && !memcmp(&t.after,&a,sizeof a));
        CHECK(xeen_entity_rng_range(0xffff,1,100,&v) && v==68);
        CHECK(xeen_entity_rng_range(0xffff,10,8,&v) && v==10); /* signed width -1 */
        CHECK(xeen_entity_rng_range(1,0xffff,1,&v) && v==0); /* wrapped addition */
        CHECK(!xeen_entity_rng_range(1,1,0,&v) && v==0); /* original divide error */
        CHECK(!xeen_entity_rng_range(1,0,0xffff,&v));
    }
    puts("phase44 explicit time returns, single RNG owner, transition evidence and signed ranges passed");
    return 0;
}
