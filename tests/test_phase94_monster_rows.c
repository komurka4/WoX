#include "xeen/entities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Phase94: 1A3A9 reads row id x 60 of the 50D4 table for every record of
 * the map, including the off-grid placeholder of an FFh sprite slot that
 * Clouds maps 49, 71, 73 and 74 keep. Past the loaded file the table shares
 * memory with the event buffer (2C6CE: 80h fill from 1C7E, one 10-byte
 * entry per event record), so row FFh reads 80h: HP 8080h is the incoming
 * AX of the 1A3F0 draw, +31h = 80h makes the 1A47D draw. Rows between the
 * file and the end of the entries stay refused. Original facts:
 * xeen_dat_evidence/phase94/ORIGINAL_BYTE_EVIDENCE.md (row255_probe.py). */
#define CHECK(c) do{if(!(c)){fprintf(stderr,"phase94 monster rows line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static xeen_movement m;
static uint8_t table[15360];
static unsigned draws;static uint16_t incoming[8];
static uint16_t draw(void *u,uint16_t ax){(void)u;if(draws<8)incoming[draws]=ax;++draws;return 0;}
/* A mob of no objects (9C5A 0: the first partition is the sentinel), the
 * given monster records (x, y, sprite slot) and no wall items; sprite slots
 * 16..31: slot 0 is id 5, the rest FFh. */
static size_t mob(uint8_t *b,const uint8_t (*recs)[3],unsigned n)
{
    size_t p=48;unsigned i;
    memset(b,0xff,512);memset(b,0,48);memset(b+16,0xff,16);b[16]=5;
    p+=4;                                                             /* objects: the sentinel */
    for(i=0;i<n;++i){b[p]=recs[i][0];b[p+1]=recs[i][1];b[p+2]=recs[i][2];b[p+3]=0;p+=4;}
    p+=4;                                                             /* monsters' sentinel */
    b[p]=1;b[p+1]=1;b[p+2]=0;b[p+3]=0;p+=4;                           /* one wall item */
    return p+4;
}
static void fill(size_t file,size_t alias)
{
    memset(table,0,sizeof table);
    {size_t i;for(i=0;i<file;++i)table[i]=(uint8_t)(i*7u);}
    table[5u*60u+0x14u]=0x34;table[5u*60u+0x15u]=0x12;table[5u*60u+0x31u]=0;
    if(alias<sizeof table)memset(table+alias,0x80,sizeof table-alias);
}
int main(void)
{
    uint8_t b[512];size_t n;
    static const uint8_t one[1][3]={{0x64,0x64,1}};                   /* slot 1: FFh */
    static const uint8_t two[2][3]={{3,4,0},{0x64,0x64,1}};
    /* the native's earlier rule (file rows only) refuses the placeholder */
    n=mob(b,one,1);fill(5400,0x1c7eu+10u*131u);
    CHECK(xeen_entities_monsters_walls(b,n,table,5400,&m,draw,NULL)==XR_NOT_FOUND);
    /* the original's table: row FFh in the 80h fill */
    memset(&m,0,sizeof m);draws=0;
    CHECK(xeen_entities_monsters_walls_aliased(b,n,table,5400,0x1c7eu+10u*131u,sizeof table,&m,draw,NULL)==XR_OK);
    CHECK(m.ds[0x9c59]==1 && m.ds[0xe530]==0x64 && m.ds[0xe536]==0xff);
    CHECK(m.ds[0xe530u+18u]==0x80 && m.ds[0xe530u+19u]==0x80);       /* HP 8080h */
    CHECK(m.ds[0xe530u+14u]==0x80 && m.ds[0xe530u+12u]==0x80);       /* +31h */
    CHECK(draws==2 && incoming[0]==0x8080u);                         /* 1A3F0 with AX = HP, then 1A47D */
    /* an in-file record next to it keeps the file's row */
    memset(&m,0,sizeof m);draws=0;n=mob(b,two,2);
    CHECK(xeen_entities_monsters_walls_aliased(b,n,table,5400,0x1c7eu+10u*131u,sizeof table,&m,draw,NULL)==XR_OK);
    CHECK(m.ds[0xe536]==5 && m.ds[0xe530u+18u]==0x34 && m.ds[0xe530u+19u]==0x12 && incoming[0]==0x1234u);
    CHECK(m.ds[0xe54a]==0xff && m.ds[0xe530u+20u+18u]==0x80);
    /* a map with event entries reaching row FFh (more than 800 records):
     * the row is the entries, not the fill: refused */
    memset(&m,0,sizeof m);n=mob(b,one,1);fill(5400,0x1c7eu+10u*806u);
    CHECK(xeen_entities_monsters_walls_aliased(b,n,table,5400,0x1c7eu+10u*806u,sizeof table,&m,draw,NULL)==XR_NOT_FOUND);
    puts("phase94 monster rows: ok");
    return 0;
}
