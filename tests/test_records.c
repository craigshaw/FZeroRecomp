#include "fzero_records.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"records %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void Checksums(uint8_t *s) {
    for(unsigned g=0;g<3;++g){unsigned base=5+g*167,sum=0;for(unsigned i=0;i<165;++i)sum+=s[base+i];s[base+165]=(uint8_t)sum;s[base+166]=(uint8_t)(sum>>8);}
}
static void Legacy(uint8_t *s) {
    memset(s,0,2048);memcpy(s,"FZERO",5);memcpy(s+507,"FZERO",5);
    for(unsigned g=0;g<3;++g)for(unsigned i=0;i<55;++i){unsigned p=5+g*167+i*3;s[p]=9;s[p+1]=0x59;s[p+2]=0x99;}Checksums(s);
}
static uint32_t rng=91823;
static unsigned Random(unsigned n){rng=rng*1664525u+1013904223u;return rng%n;}
int main(void) {
    FZeroRecords a,b;uint8_t save[2048],before[2048];bool changed;
    Legacy(save);CHECK(FZeroRecordsLegacyValid(save));
    CHECK(FZeroRecordsDecode(&a,save,&changed)==FZERO_RECORDS_ABSENT);
    FZeroRecordsInit(&a);
    for(unsigned t=0;t<15;++t)for(unsigned c=0;c<4;++c){
        for(unsigned j=0;j<10;++j)a.track[t][c].races[j]=(uint16_t)(10000+t*100+c*10+j);
        for(unsigned j=0;j<5;++j)a.track[t][c].laps[j]=(uint16_t)(1000+t*10+c+j);
    }
    memcpy(before,save,2048);CHECK(FZeroRecordsEncode(&a,save));CHECK(!memcmp(save,before,512));
    /* Independent Python combinatorial reference vector, including header. */
    CHECK(FZeroRecordsCrc(save,2048)==0x3602d52fu);
    CHECK(FZeroRecordsDecode(&b,save,&changed)==FZERO_RECORDS_VALID && !changed);CHECK(!memcmp(&a,&b,sizeof(a)));
    save[510]^=1;CHECK(FZeroRecordsDecode(&b,save,&changed)==FZERO_RECORDS_VALID && changed);save[510]^=1;
    for(unsigned bit=0;bit<1536*8;++bit){
        unsigned p=512+bit/8;save[p]^=(uint8_t)(1u<<(bit%8));
        CHECK(FZeroRecordsDecode(&b,save,&changed)!=FZERO_RECORDS_VALID);save[p]^=(uint8_t)(1u<<(bit%8));
    }
    for(unsigned trial=0;trial<100;++trial){
        FZeroRecordsInit(&a);
        for(unsigned t=0;t<15;++t)for(unsigned c=0;c<4;++c)for(unsigned j=0,n=Random(30);j<n;++j){
            uint16_t time=(uint16_t)Random(60000),lap=(uint16_t)Random(time/5+1);
            FZeroRecordsInsert(&a,t,c,time,lap);
        }
        CHECK(FZeroRecordsEncode(&a,save));CHECK(FZeroRecordsDecode(&b,save,&changed)==FZERO_RECORDS_VALID);CHECK(!memcmp(&a,&b,sizeof(a)));
    }
    FZeroRecordsInit(&a);
    for(unsigned t=0;t<15;++t)a.track[t][t%4].laps[0]=59999;
    CHECK(FZeroRecordsEncode(&a,save));CHECK(FZeroRecordsDecode(&b,save,&changed)==FZERO_RECORDS_VALID);CHECK(!memcmp(&a,&b,sizeof(a)));
    a.track[0][1].laps[0]=15000;memcpy(before,save,2048);CHECK(!FZeroRecordsEncode(&a,save));CHECK(!memcmp(save,before,2048));
    Legacy(save);FZeroRecordsInit(&a);
    /* Three equal observed races, including one from the other mode. */
    for(unsigned i=0;i<3;++i){save[5+i*3]=(uint8_t)(0x91|(i==1?0x40:0));save[6+i*3]=0x23;save[7+i*3]=0x45;}
    save[35]=0xa3;save[36]=0x10;save[37]=0x12;Checksums(save);
    CHECK(!FZeroRecordsMergeLegacy(&a,save));b=a;CHECK(!FZeroRecordsMergeLegacy(&a,save));CHECK(!memcmp(&a,&b,sizeof(a)));
    CHECK(a.track[0][1].races[0]==8345 && a.track[0][1].races[2]==8345 && a.track[0][1].races[3]==65535);
    CHECK(a.track[0][2].laps[0]==19012);CHECK(FZeroRecordsEncode(&a,save));
    CHECK(FZeroRecordsDecode(&b,save,&changed)==FZERO_RECORDS_VALID);CHECK(!memcmp(&a,&b,sizeof(a)));
    /* A different long historical lap cannot evict the first imported one. */
    save[35]=0xb2;save[36]=0x30;save[37]=0;Checksums(save);CHECK(FZeroRecordsMergeLegacy(&a,save)==1);CHECK(a.track[0][2].laps[0]==19012);
    for(unsigned i=0;i<5;++i)CHECK(FZeroRecordsInsert(&a,0,2,10000,1000));
    CHECK(a.track[0][2].laps[4]==1000);CHECK(FZeroRecordsEncode(&a,save));
    CHECK(!FZeroRecordsInsert(&a,0,0,100,21));CHECK(!FZeroRecordsInsert(&a,15,0,10000,1000));
    FZeroRecordsClearTrack(&a,0);for(unsigned c=0;c<4;++c)CHECK(a.track[0][c].laps[0]==65535 && a.track[0][c].races[0]==65535);
    save[516]=2;memcpy(before,save,2048);CHECK(FZeroRecordsDecode(&b,save,&changed)==FZERO_RECORDS_UNSUPPORTED);CHECK(!memcmp(save,before,2048));
    /* Mixed lists are the exact top N across cars, including ties and long laps. */
    FZeroRecordsInit(&a);FZeroMixedRecords mixed;
    FZeroRecordsMixed(&a,0,&mixed);CHECK(mixed.times.races[0]==65535 && mixed.times.laps[0]==65535);
    CHECK(mixed.race_cars[0]==255 && mixed.lap_cars[0]==255);
    a.track[0][2].laps[0]=59999;
    FZeroRecordsMixed(&a,0,&mixed);CHECK(mixed.times.laps[0]==59999 && mixed.times.laps[1]==65535);
    CHECK(mixed.lap_cars[0]==2 && mixed.lap_cars[1]==255);
    for(unsigned car=0;car<4;++car)for(unsigned i=0;i<10;++i) {
        a.track[0][car].races[i]=(uint16_t)(1000+i*4+car);
        if(i<5)a.track[0][car].laps[i]=(uint16_t)(100+i/2);
    }
    b=a;FZeroRecordsMixed(&a,0,&mixed);CHECK(!memcmp(&a,&b,sizeof(a)));
    for(unsigned i=0;i<10;++i)CHECK(mixed.times.races[i]==1000+i && mixed.race_cars[i]==i%4);
    for(unsigned i=0;i<5;++i)CHECK(mixed.times.laps[i]==100 && mixed.lap_cars[i]==i/2);
    FZeroRecordsClearTrack(&a,0);FZeroRecordsMixed(&a,0,&mixed);
    CHECK(mixed.times.races[0]==65535 && mixed.times.laps[0]==65535);
    CHECK(mixed.race_cars[0]==255 && mixed.lap_cars[0]==255);
    puts("records codec, migration and ranking: passed");return 0;
}
