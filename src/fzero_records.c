#include "fzero_records.h"
#include <string.h>

/* Five base-2^32 limbs keep the codec portable to C11/MSVC. All intermediates
 * in C(n,k), k <= 10, fit in 160 bits for this format. */
typedef struct Number { uint32_t word[5]; } Number;
static int Compare(Number a, Number b) {
    for (int i=4;i>=0;--i) if (a.word[i]!=b.word[i]) return a.word[i]>b.word[i]?1:-1;
    return 0;
}
static void Add(Number *a, Number b) {
    uint64_t carry=0;
    for (unsigned i=0;i<5;++i) { carry+=(uint64_t)a->word[i]+b.word[i]; a->word[i]=(uint32_t)carry; carry>>=32; }
}
static void Subtract(Number *a, Number b) {
    uint64_t borrow=0;
    for (unsigned i=0;i<5;++i) { uint64_t v=(uint64_t)b.word[i]+borrow;
        borrow=(uint64_t)a->word[i]<v; a->word[i]-=(uint32_t)v; }
}
static Number Choose(unsigned n, unsigned k) {
    Number a={{0}};
    if (n<k) return a;
    a.word[0]=1;
    for (unsigned j=1;j<=k;++j) {
        uint64_t carry=0;
        for (unsigned i=0;i<5;++i) { carry+=(uint64_t)a.word[i]*(n-j+1); a.word[i]=(uint32_t)carry; carry>>=32; }
        uint64_t rem=0;
        for (int i=4;i>=0;--i) { uint64_t v=(rem<<32)|a.word[i]; a.word[i]=(uint32_t)(v/j); rem=v%j; }
    }
    return a;
}
static Number ReadBits(const uint8_t *data, unsigned *offset, unsigned count) {
    Number a={{0}};
    for (unsigned i=0;i<count;++i,++*offset)
        if (data[*offset/8]&(1u<<(*offset%8))) a.word[i/32]|=1u<<(i%32);
    return a;
}
static void WriteBits(uint8_t *data, unsigned *offset, unsigned count, Number a) {
    for (unsigned i=0;i<count;++i,++*offset)
        if (a.word[i/32]&(1u<<(i%32))) data[*offset/8]|=(uint8_t)(1u<<(*offset%8));
}
static bool ReadList(uint16_t *out, unsigned k, unsigned empty, Number rank) {
    if (Compare(rank,Choose(empty+k,k))>=0) return false;
    unsigned hi=empty+k-1;
    for (unsigned i=k;i>0;--i) {
        unsigned lo=i-1;
        while (lo<hi) { unsigned mid=lo+(hi-lo+1)/2;
            if (Compare(Choose(mid,i),rank)<=0) lo=mid; else hi=mid-1; }
        unsigned value=lo-(i-1);
        out[i-1]=value==empty?FZERO_RECORD_EMPTY:(uint16_t)value;
        Subtract(&rank,Choose(lo,i));
        hi=lo?lo-1:0;
    }
    return true;
}
static bool WriteList(const uint16_t *values, unsigned k, unsigned empty, Number *rank) {
    memset(rank,0,sizeof(*rank)); unsigned previous=0;
    for (unsigned i=0;i<k;++i) {
        unsigned v=values[i]==FZERO_RECORD_EMPTY?empty:values[i];
        if (v>empty || (v==empty && values[i]!=FZERO_RECORD_EMPTY) || v<previous) return false;
        Add(rank,Choose(v+i,i+1)); previous=v;
    }
    return true;
}
static uint32_t Read32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void Write32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8)); }
uint32_t FZeroRecordsCrc(const void *data, unsigned size) {
    const uint8_t *p=data; uint32_t c=0xffffffffu;
    for(unsigned i=0;i<size;++i) { c^=p[i]; for(unsigned b=0;b<8;++b)c=(c>>1)^((0u-(c&1u))&0xedb88320u); }
    return ~c;
}
void FZeroRecordsInit(FZeroRecords *r) { memset(r,0xff,sizeof(*r)); }
void FZeroRecordsClearTrack(FZeroRecords *r,unsigned track) {
    if(track<FZERO_RECORD_TRACKS)memset(r->track[track],0xff,sizeof(r->track[track]));
}
static bool Bcd(uint8_t b,unsigned *v) {
    if((b&15)>9 || (b>>4)>9)return false;
    *v=(b>>4)*10+(b&15);return true;
}
bool FZeroRecordsTime(const uint8_t *p,uint16_t *time) {
    unsigned sec,cent;
    if(p[0]>9 || !Bcd(p[1],&sec) || sec>=60 || !Bcd(p[2],&cent))return false;
    *time=(uint16_t)(p[0]*6000+sec*100+cent);return true;
}
bool FZeroRecordsLegacyValid(const uint8_t *save) {
    if(memcmp(save,"FZERO",5) || memcmp(save+507,"FZERO",5))return false;
    for(unsigned g=0;g<3;++g) {
        unsigned base=5+g*167,sum=0;
        for(unsigned j=0;j<165;++j)sum+=save[base+j];
        if(sum!=((unsigned)save[base+165]|((unsigned)save[base+166]<<8)))return false;
    }
    return true;
}
FZeroRecordsStatus FZeroRecordsDecode(FZeroRecords *r,const uint8_t *save,bool *changed) {
    uint8_t ext[1536]; memcpy(ext,save+512,sizeof(ext));
    FZeroRecordsInit(r); if(changed)*changed=false;
    if(memcmp(ext,"FZRX",4))return FZERO_RECORDS_ABSENT;
    if(ext[4]!=1 || ext[5])return FZERO_RECORDS_UNSUPPORTED;
    uint32_t crc=Read32(ext+12); memset(ext+12,0,4);
    if(ext[6]!=0xcd || ext[7]!=5 || FZeroRecordsCrc(ext,sizeof(ext))!=crc || ext[1535] || (ext[1534]&0xc0))return FZERO_RECORDS_DAMAGED;
    FZeroRecords decoded; FZeroRecordsInit(&decoded); unsigned bit=0;
    for(unsigned t=0;t<15;++t)for(unsigned c=0;c<4;++c) {
        FZeroCarRecords *v=&decoded.track[t][c];
        if(!ReadList(v->races,10,60000,ReadBits(ext+16,&bit,137)) ||
           !ReadList(v->laps,5,12001,ReadBits(ext+16,&bit,61)))return FZERO_RECORDS_DAMAGED;
    }
    bit=0;
    for(unsigned t=0;t<15;++t) {
        unsigned entry=ReadBits(ext+1501,&bit,18).word[0];
        if(entry==0xffff)continue;
        unsigned time=entry&65535,car=entry>>16;
        if(time<=12000 || time>59999)return FZERO_RECORDS_DAMAGED;
        uint16_t *laps=decoded.track[t][car].laps;
        unsigned pos=0;while(pos<5 && laps[pos]!=FZERO_RECORD_EMPTY)++pos;
        if(pos==5)return FZERO_RECORDS_DAMAGED;
        laps[pos]=(uint16_t)time;
    }
    *r=decoded;if(changed)*changed=Read32(ext+8)!=FZeroRecordsCrc(save,512);
    return FZERO_RECORDS_VALID;
}
bool FZeroRecordsEncode(const FZeroRecords *r,uint8_t *save) {
    uint8_t ext[1536]={0}; unsigned bit=0; uint32_t inherited[15];
    for(unsigned t=0;t<15;++t) {
        inherited[t]=0xffff;
        for(unsigned c=0;c<4;++c) {
            const FZeroCarRecords *v=&r->track[t][c]; Number rank;
            if(!WriteList(v->races,10,60000,&rank))return false;
            WriteBits(ext+16,&bit,137,rank);
            uint16_t laps[5];memcpy(laps,v->laps,sizeof(laps));
            unsigned previous=0;
            for(unsigned i=0;i<5;++i) {
                unsigned time=laps[i];if(time<previous)return false;previous=time;
                if(time!=FZERO_RECORD_EMPTY && time>12000) {
                    if(time>59999 || inherited[t]!=0xffff)return false;
                    inherited[t]=(c<<16)|time;laps[i]=FZERO_RECORD_EMPTY;
                }
            }
            if(!WriteList(laps,5,12001,&rank))return false;
            WriteBits(ext+16,&bit,61,rank);
        }
    }
    bit=0;
    for(unsigned t=0;t<15;++t) { Number value={{inherited[t],0,0,0,0}};WriteBits(ext+1501,&bit,18,value); }
    memcpy(ext,"FZRX",4);ext[4]=1;ext[6]=0xcd;ext[7]=5;
    Write32(ext+8,FZeroRecordsCrc(save,512));Write32(ext+12,FZeroRecordsCrc(ext,sizeof(ext)));
    memcpy(save+512,ext,sizeof(ext));return true;
}
static bool Insert(uint16_t *v,unsigned n,uint16_t value) {
    unsigned i=0;while(i<n && v[i]<=value)++i;
    if(i==n)return false;
    memmove(v+i+1,v+i,(n-i-1)*sizeof(*v));v[i]=value;return true;
}
static void InsertOwned(uint16_t *times,uint8_t *cars,unsigned n,uint16_t time,unsigned car) {
    unsigned i=0;while(i<n && times[i]<=time)++i;
    if(i==n)return;
    memmove(times+i+1,times+i,(n-i-1)*sizeof(*times));
    memmove(cars+i+1,cars+i,n-i-1);
    times[i]=time;cars[i]=(uint8_t)car;
}
void FZeroRecordsMixed(const FZeroRecords *r,unsigned track,FZeroMixedRecords *mixed) {
    memset(mixed,0xff,sizeof(*mixed));
    if(track>=FZERO_RECORD_TRACKS)return;
    for(unsigned car=0;car<FZERO_RECORD_CARS;++car) {
        const FZeroCarRecords *v=&r->track[track][car];
        /* Car order provides a stable tie break without dropping equal times. */
        for(unsigned i=0;i<FZERO_RECORD_RACES;++i)
            InsertOwned(mixed->times.races,mixed->race_cars,FZERO_RECORD_RACES,v->races[i],car);
        for(unsigned i=0;i<FZERO_RECORD_LAPS;++i)
            InsertOwned(mixed->times.laps,mixed->lap_cars,FZERO_RECORD_LAPS,v->laps[i],car);
    }
}
bool FZeroRecordsInsert(FZeroRecords *r,unsigned t,unsigned c,uint16_t race,uint16_t lap) {
    if(t>=15 || c>=4 || race>59999 || lap>11999 || (unsigned)lap*5>race)return false;
    bool a=Insert(r->track[t][c].races,10,race);
    bool b=Insert(r->track[t][c].laps,5,lap);return a||b;
}
unsigned FZeroRecordsMergeLegacy(FZeroRecords *r,const uint8_t *save) {
    if(!FZeroRecordsLegacyValid(save))return 0;
    unsigned skipped=0;
    for(unsigned t=0;t<15;++t) {
        unsigned base=5+(t/5)*167+(t%5)*33;
        for(unsigned j=0;j<11;++j) {
            const uint8_t *p=save+base+j*3;if(!(p[0]&0x80))continue;
            uint8_t plain[3]={p[0]&15,p[1],p[2]};uint16_t time;
            if(!FZeroRecordsTime(plain,&time))continue;
            unsigned car=(p[0]>>4)&3;
            uint16_t *list=j<10?r->track[t][car].races:r->track[t][car].laps;
            unsigned n=j<10?10:5,have=0,want=1;
            for(unsigned k=0;k<n;++k)if(list[k]==time)++have;
            if(j<10)for(unsigned k=0;k<j;++k) {
                const uint8_t *q=save+base+k*3;
                if((q[0]&0xbf)==(p[0]&0xbf) && q[1]==p[1] && q[2]==p[2])++want;
            }
            if(have>=want)continue;
            if(j==10 && time>12000) {
                bool occupied=false;
                for(unsigned c=0;c<4;++c)for(unsigned k=0;k<5;++k)
                    if(r->track[t][c].laps[k]>12000 && r->track[t][c].laps[k]!=FZERO_RECORD_EMPTY)occupied=true;
                if(occupied && time<list[4]) { ++skipped;continue; }
            }
            Insert(list,n,time);
        }
    }
    return skipped;
}
