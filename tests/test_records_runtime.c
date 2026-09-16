#include "fzero_records_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"records runtime %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static uint8_t ram[0x20000],save[2048];
static void Time(uint8_t *p,unsigned value) {
    p[0]=(uint8_t)(value/6000);value%=6000;
    p[1]=(uint8_t)((value/100/10)*16+value/100%10);
    p[2]=(uint8_t)((value%100/10)*16+value%10);
}
static void Finish(FZeroRecordsRuntime *r,unsigned total) {
    for(unsigned i=0;i<5;++i)Time(ram+0xe90+i*3,total*(i+1)/5);
    ram[0xf53]=5;ram[0xc3]=1;
    FZeroRecordsAfterFrame(r,ram,save);
}
static void Start(FZeroRecordsRuntime *r,unsigned track,unsigned car,unsigned mode,unsigned demo) {
    memset(ram,0,sizeof(ram));ram[0x54]=2;ram[0x55]=1;
    FZeroRecordsBeforeFrame(r,ram,save);
    ram[0x55]=3;ram[0xeeb]=(uint8_t)(track/5);ram[0xeec]=(uint8_t)(track%5);
    ram[0x52]=(uint8_t)car;ram[0x58]=(uint8_t)mode;ram[0x5b]=(uint8_t)demo;
    FZeroRecordsBeforeFrame(r,ram,save);
}
static void Highlights(void) {
    FZeroRecordsRuntime r;FZeroRecords empty;FZeroRecordsInit(&empty);
    CHECK(FZeroRecordsEncode(&empty,save));FZeroRecordsStart(&r,save,true);
    /* Five completed GP races retain their own exact positions, including ties. */
    for(unsigned t=0;t<5;++t) {
        CHECK(FZeroRecordsInsert(&r.records,t,0,10000,2000));
        CHECK(FZeroRecordsInsert(&r.records,t,3,12000,2400));
        Start(&r,t,3,0,0);Finish(&r,12000);
        CHECK(r.highlights[t].car==3 && r.highlights[t].race_rank==2 && r.highlights[t].lap_rank==2);
        for(unsigned frame=0;frame<20;++frame) {
            FZeroRecordsBeforeFrame(&r,ram,save);FZeroRecordsAfterFrame(&r,ram,save);
            CHECK(r.highlights[t].race_rank==2 && r.highlights[t].lap_rank==2);
        }
        ram[0x54]=3;ram[0x55]=5;FZeroRecordsBeforeFrame(&r,ram,save);
    }
    ram[0x54]=0;ram[0x55]=4;FZeroRecordsBeforeFrame(&r,ram,save);
    for(unsigned t=0;t<5;++t)CHECK(r.highlights[t].race_rank==2 && r.highlights[t].lap_rank==2);
    ram[0x55]=6;ram[0x5a]=0;FZeroRecordsBeforeFrame(&r,ram,save);
    ram[0x55]=4;FZeroRecordsAfterFrame(&r,ram,save);
    CHECK(!r.highlights[4].race_rank && !r.highlights[4].lap_rank);
    CHECK(r.highlights[3].race_rank==2);
    /* A retry removes that track's previous markers even if it never finishes. */
    Start(&r,3,3,0,0);CHECK(!r.highlights[3].race_rank && !r.highlights[3].lap_rank);
    CHECK(r.highlights[2].race_rank==2);
    ram[0x54]=1;FZeroRecordsBeforeFrame(&r,ram,save);
    for(unsigned t=0;t<15;++t)CHECK(!r.highlights[t].race_rank && !r.highlights[t].lap_rank);
    /* Race and lap candidates qualify separately, including the final places. */
    FZeroRecordsInit(&r.records);
    for(unsigned i=0;i<5;++i)r.records.track[0][0].laps[i]=1000;
    Start(&r,0,0,1,0);Finish(&r,10000);
    CHECK(r.highlights[0].race_rank==1 && !r.highlights[0].lap_rank);
    for(unsigned i=0;i<10;++i)r.records.track[1][0].races[i]=9000;
    Start(&r,1,0,1,0);Finish(&r,10000);
    CHECK(!r.highlights[1].race_rank && r.highlights[1].lap_rank==1);
    for(unsigned i=0;i<9;++i)r.records.track[2][0].races[i]=(uint16_t)(9000+i);
    for(unsigned i=0;i<4;++i)r.records.track[2][0].laps[i]=(uint16_t)(1800+i);
    Start(&r,2,0,1,0);Finish(&r,10000);
    CHECK(r.highlights[2].race_rank==10 && r.highlights[2].lap_rank==5);
    Start(&r,2,0,1,0);Finish(&r,10000);
    CHECK(!r.highlights[2].race_rank && !r.highlights[2].lap_rank);
    /* Highlights are transient and do not make loaded records look new. */
    CHECK(FZeroRecordsFlush(&r,save));FZeroRecordsStart(&r,save,true);
    for(unsigned t=0;t<15;++t)CHECK(!r.highlights[t].race_rank && !r.highlights[t].lap_rank);
}
int main(void) {
    FZeroRecordsRuntime r;FZeroRecords records;FZeroRecordsInit(&records);
    CHECK(FZeroRecordsEncode(&records,save));FZeroRecordsStart(&r,save,true);
    for(unsigned t=0;t<15;++t)for(unsigned c=0;c<4;++c)for(unsigned m=0;m<2;++m) {
        Start(&r,t,c,m,0);CHECK(r.race_armed);Finish(&r,10001);
        for(unsigned i=0;i<120;++i){FZeroRecordsBeforeFrame(&r,ram,save);FZeroRecordsAfterFrame(&r,ram,save);}
        CHECK(r.records.track[t][c].races[m]==10001);
        CHECK(r.records.track[t][c].races[m+1]==65535);
        CHECK(r.records.track[t][c].laps[m]==2000);
    }
    records=r.records;
    Start(&r,0,0,1,2);CHECK(!r.race_armed);Finish(&r,5000);
    CHECK(!memcmp(&records,&r.records,sizeof(records)));
    Start(&r,0,0,0,0);ram[0xf53]=4;ram[0xc3]=0x40;
    FZeroRecordsAfterFrame(&r,ram,save);CHECK(!memcmp(&records,&r.records,sizeof(records)));
    Start(&r,0,0,0,0);Finish(&r,10000);CHECK(r.records.track[0][0].races[0]==10000);
    Start(&r,0,0,0,0);ram[0xf53]=5;memset(ram+0xe90,0xff,15);
    FZeroRecordsAfterFrame(&r,ram,save);CHECK(!r.race_done);
    CHECK(r.records.track[0][0].races[1]==10001);
    Start(&r,0,0,0,0);ram[0xf53]=5;for(unsigned i=0;i<5;++i)Time(ram+0xe90+i*3,10000-i);
    FZeroRecordsAfterFrame(&r,ram,save);CHECK(!r.race_done);
    /* A confirmation clears extension-only history, even with no legacy rows. */
    memset(ram,0,sizeof(ram));ram[0x5b]=1;ram[0x55]=5;ram[0x52]=2;
    FZeroRecordsBeforeFrame(&r,ram,save);CHECK(r.view_car==2);
    CHECK(FZeroRecordsInput(&r,ram,0x80,true)==0 && r.selected_car==3);
    CHECK(FZeroRecordsInput(&r,ram,0x80,true)==0 && r.selected_car==3);
    FZeroRecordsInput(&r,ram,0,true);FZeroRecordsInput(&r,ram,0x80,true);CHECK(r.selected_car==4);
    FZeroRecordsInput(&r,ram,0,true);FZeroRecordsInput(&r,ram,0x80,true);CHECK(r.selected_car==0);
    FZeroRecordsInput(&r,ram,0,true);FZeroRecordsInput(&r,ram,0xc0,true);CHECK(r.selected_car==0);
    FZeroRecordsInput(&r,ram,0,true);FZeroRecordsInput(&r,ram,0x40,false);CHECK(r.selected_car==0);
    FZeroRecordsInput(&r,ram,0,true);FZeroRecordsInput(&r,ram,0x40,true);CHECK(r.selected_car==4);
    ram[0x55]=4;ram[0xeec]=1;FZeroRecordsBeforeFrame(&r,ram,save);CHECK(r.view_track==1 && r.view_car==4);
    ram[0x55]=6;ram[0x5a]=1;FZeroRecordsBeforeFrame(&r,ram,save);ram[0x55]=5;
    FZeroRecordsAfterFrame(&r,ram,save);CHECK(r.records.track[1][0].races[0]==10001);
    ram[0x55]=6;ram[0x5a]=0;FZeroRecordsBeforeFrame(&r,ram,save);ram[0x55]=4;
    FZeroRecordsAfterFrame(&r,ram,save);
    for(unsigned c=0;c<4;++c)CHECK(r.records.track[1][c].races[0]==65535 && r.records.track[1][c].laps[0]==65535);
    CHECK(r.records.track[2][0].races[0]==10001);
    /* Keep the selected page and track through every exit-fade frame. */
    ram[0x55]=5;FZeroRecordsBeforeFrame(&r,ram,save);
    ram[0x55]=2;
    for(unsigned i=0;i<32;++i) {
        FZeroRecordsBeforeFrame(&r,ram,save);
        CHECK(r.view_active && r.view_car==4 && r.view_track==1);
        CHECK(FZeroRecordsInput(&r,ram,0x80,false)==0x80);
    }
    ram[0x55]=3;FZeroRecordsBeforeFrame(&r,ram,save);CHECK(!r.view_active);
    /* Post-GP entry has a different demo selector and starts on the raced car. */
    for(unsigned car=0;car<4;++car) {
        ram[0x54]=3;ram[0x55]=5;ram[0x5b]=0;ram[0x52]=(uint8_t)car;
        FZeroRecordsBeforeFrame(&r,ram,save);CHECK(!r.view_active);
        ram[0x54]=0;ram[0x55]=4;ram[0xeeb]=2;ram[0xeec]=4;
        FZeroRecordsBeforeFrame(&r,ram,save);
        CHECK(r.view_active && r.view_car==car && r.view_track==14);
        ram[0x55]=5;FZeroRecordsInput(&r,ram,0,true);FZeroRecordsInput(&r,ram,0x80,true);
        FZeroRecordsBeforeFrame(&r,ram,save);CHECK(r.view_car==car+1);
    }
    CHECK(FZeroRecordsFlush(&r,save));FZeroRecordsStart(&r,save,true);
    CHECK(r.records.track[2][0].races[0]==10001 && !r.dirty);
    save[516]=2;uint8_t before[2048];memcpy(before,save,2048);
    FZeroRecordsStart(&r,save,true);Start(&r,0,0,0,0);Finish(&r,10000);
    CHECK(!r.writable && FZeroRecordsFlush(&r,save) && !memcmp(before,save,2048));
    Highlights();
    puts("records events, highlights, controls, clear and restart: passed");return 0;
}
