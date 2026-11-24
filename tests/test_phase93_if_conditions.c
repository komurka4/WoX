#include "xeen/event_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Phase93: the If operations (31A6D) and 2BF6E's conditions, the new
 * take/give modes (2C94C: 12, 13, 18, 37..65, 103, 107) and operation 46,
 * through the interactive executor on synthetic DS images (one record).
 * Original facts: xeen_dat_evidence/phase93/ifproc.asm, ifproc2.asm. */
#define CHECK(c) do{if(!(c)){fprintf(stderr,"phase93 if line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static uint8_t ds[65536];
static uint16_t host_ax;static unsigned host_calls,host_cond,host_member;
static int service(void *u,const xeen_event_request *r,uint8_t *d,size_t n,uint16_t *ax)
{
    (void)u;(void)n;
    if(r->address==0x15616u){d[r->destination]=0;return 1;}
    if(r->address==0x4a1b7u || r->address==0x4a24cu)return 1;
    if(r->address==0x2bf6eu){++host_calls;host_cond=r->argument;host_member=r->second_argument;*ax=host_ax;return 1;}
    return 0;
}
static uint16_t rec(unsigned m){return (uint16_t)(0xb41eu+m*0x162u);}
/* One record at (14,5) facing 3 (index offset 0 or the padding's). */
static xeen_exec_status run(const uint8_t *body,size_t n,unsigned sel,xeen_event_execution *e,size_t pad)
{
    static uint8_t buf[256];xeen_event_index idx;size_t at=0;
    memset(buf,0,sizeof buf);
    if(pad){buf[0]=(uint8_t)(pad-1u);buf[1]=99;buf[2]=99;buf[3]=0;buf[4]=0;buf[5]=0;at=pad;}  /* an unmatched record before */
    buf[at]=(uint8_t)(n+4u);buf[at+1]=14;buf[at+2]=5;buf[at+3]=3;buf[at+4]=0;memcpy(buf+at+5,body,n);
    CHECK(xeen_events_decode(buf,at+n+5u,NULL,0,&idx)==XE_OK);
    ds[0xed97]=14;ds[0xed98]=5;ds[0xed96]=3;ds[0x98b4]=255;
    CHECK(xeen_event_execution_begin(e,ds,sizeof ds)==XEE_DONE);
    e->selected_character=(uint8_t)sel;
    return xeen_event_execute_interactive(buf,at+n+5u,&idx,ds,sizeof ds,e,1,service,NULL,NULL,NULL);
}
static void party(unsigned count){memset(ds,0,sizeof ds);ds[0xed8c]=(uint8_t)count;host_calls=0;}
/* If op (8, 9, 10), condition, byte value, line: the branch taken? */
static int taken(unsigned op,unsigned cond,unsigned value,unsigned sel)
{
    uint8_t body[4]={(uint8_t)op,(uint8_t)cond,(uint8_t)value,40};xeen_event_execution e;
    xeen_exec_status s=run(body,4,sel,&e,0);
    CHECK(s==XEE_EXTERNAL || s==XEE_BUDGET);
    CHECK(e.state.event_flag && e.state.steps==1);
    return e.state.sequence==40u;
}
int main(void)
{
    xeen_event_execution e;
    /* Condition 13: the member's own skill byte (+27h + n), any nonzero. */
    party(6);ds[rec(2)+0x27u+7u]=1;
    CHECK(taken(9,13,7,0));                 /* BP-C 0: member 2 has it, ORed */
    CHECK(!taken(9,13,7,1));                /* BP-C 1: member 0 alone lacks it */
    CHECK(taken(9,13,7,3));                 /* BP-C 3: member 2 */
    ds[0x98d7]=2;CHECK(!taken(9,13,7,8));   /* BP-C 8 skips 98D7 */
    ds[rec(0)+0x27u+7u]=0xff;CHECK(taken(9,13,7,1));
    /* The false path compares the 15616 residue: DI = the record offset,
     * an unknown SI. With the offset differing from the value '=' is
     * false whatever SI is; equal it depends on SI: an explicit stop. */
    party(6);
    {uint8_t body[4]={9,13,7,40};xeen_exec_status st=run(body,4,1,&e,0);
     CHECK((st==XEE_EXTERNAL || st==XEE_BUDGET) && e.state.steps==1 && e.state.sequence!=40u);
     party(6);CHECK(run(body,4,1,&e,7)==XEE_UNSUPPORTED && e.state.boundary==0x2c4c4u);}
    /* '>=' false paths: residue offset 0, value 7: SI 0 false, SI > 0 true. */
    party(6);{uint8_t body[4]={8,13,7,40};CHECK(run(body,4,1,&e,0)==XEE_UNSUPPORTED && e.state.boundary==0x2c4c4u);}
    /* ... and value 0: any residue >= 0. */
    party(6);CHECK(taken(8,13,0,1));
    /* Condition 9: SP at +158h, CDQ, unsigned (op 8 '>=' 0 always true; a
     * negative SP is FFFFxxxxh: above any byte). */
    party(6);ds[rec(0)+0x158u]=0;ds[rec(0)+0x159u]=0x80;
    CHECK(taken(8,9,200,1));CHECK(!taken(10,9,200,1));
    ds[rec(0)+0x158u]=5;ds[rec(0)+0x159u]=0;CHECK(taken(10,9,5,1) && taken(9,9,5,1) && !taken(8,9,6,1));
    /* Condition 21: FFFFFFFFh first, so no residue: absent item '=' false. */
    party(6);CHECK(!taken(9,21,9,1));
    ds[rec(0)+0xa7u]=9;CHECK(taken(9,21,9,1));
    /* Condition 18: a condition byte; 16 with every byte clear passes. */
    party(6);ds[rec(0)+0x143u+7u]=1;CHECK(taken(9,18,7,1));CHECK(taken(9,18,16,1));
    /* Condition 102: the host's 3CF5B, CDQ. */
    party(6);host_ax=104;CHECK(taken(8,102,100,4));CHECK(host_calls==1 && host_cond==102 && host_member==3);
    host_ax=0;CHECK(taken(10,102,0,1));
    /* Value widths: condition 16 (experience) a dword, the line after it. */
    party(6);ds[rec(0)+0x15cu]=0x10;ds[rec(0)+0x15eu]=0x01;   /* 00010010h */
    {uint8_t body[7]={8,16,0x10,0,0x01,0,40};xeen_exec_status st=run(body,7,1,&e,0);
     CHECK((st==XEE_EXTERNAL || st==XEE_BUDGET) && e.state.sequence==40u);
     ds[rec(0)+0x15eu]=0;st=run(body,7,1,&e,0);
     CHECK((st==XEE_EXTERNAL || st==XEE_BUDGET) && e.state.sequence!=40u);}
    /* Take/give 13 (a skill) and 12 (the temporary age), op 12 for BP-C 1. */
    party(6);
    {uint8_t body[5]={12,0,0,13,4};CHECK(run(body,5,1,&e,0)!=XEE_UNSUPPORTED);CHECK(ds[rec(0)+0x27u+4u]==1 && !ds[rec(1)+0x27u+4u]);}
    {uint8_t body[5]={12,0,0,12,25};CHECK(run(body,5,1,&e,0)!=XEE_UNSUPPORTED);CHECK(ds[rec(0)+0x26u]==25);}
    {uint8_t body[5]={12,12,5,0,0};CHECK(run(body,5,1,&e,0)!=XEE_UNSUPPORTED);CHECK(ds[rec(0)+0x26u]==20);}
    {uint8_t body[5]={12,13,4,0,0};CHECK(run(body,5,1,&e,0)!=XEE_UNSUPPORTED);CHECK(!ds[rec(0)+0x27u+4u]);}
    /* Give 46 (permanent Intellect +16h) saturates at FFh. */
    party(6);ds[rec(0)+0x16u]=253;
    {uint8_t body[5]={12,0,0,46,5};CHECK(run(body,5,1,&e,0)!=XEE_UNSUPPORTED);CHECK(ds[rec(0)+0x16u]==0xff);}
    ds[rec(0)+0x16u]=10;{uint8_t body[5]={12,0,0,46,5};(void)run(body,5,1,&e,0);CHECK(ds[rec(0)+0x16u]==15);}
    /* Give 18: 6 sets In Love to 1, others increment, 16 clears all, 13
     * (Dead) zeroes a positive HP. */
    party(6);ds[rec(0)+0x143u+6u]=4;ds[rec(0)+0x143u+7u]=2;ds[rec(0)+0x156u]=50;
    {uint8_t b1[5]={12,0,0,18,6},b2[5]={12,0,0,18,7},b3[5]={12,0,0,18,13},b4[5]={12,0,0,18,16};
     (void)run(b1,5,1,&e,0);CHECK(ds[rec(0)+0x143u+6u]==1);
     (void)run(b2,5,1,&e,0);CHECK(ds[rec(0)+0x143u+7u]==3);
     (void)run(b3,5,1,&e,0);CHECK(ds[rec(0)+0x143u+13u]==1 && !ds[rec(0)+0x156u]);
     (void)run(b4,5,1,&e,0);for(unsigned k=0;k<16u;++k)CHECK(!ds[rec(0)+0x143u+k]);}
    /* Give 107: bit n of the member's roster array (F30C + 4 x ED8E[m]). */
    party(6);ds[0xed8e]=3;
    {uint8_t body[5]={12,0,0,107,9};(void)run(body,5,1,&e,0);CHECK(ds[0xf30cu+12u+1u]==0x40);}
    puts("Phase93 If conditions: 13 per member/ORed/98D7, the residue rule and its stop, 9 CDQ unsigned, 21, 18, "
         "102 by the host, dword widths, take/give 12/13/18/46/107 passed");
    return 0;
}
