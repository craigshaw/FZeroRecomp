#include "fzero_records_view.h"
#include <string.h>

static void TimeTiles(uint16_t *row,uint16_t time) {
    if(time==FZERO_RECORD_EMPTY) {
        /* A small host-owned dash marks absent history. */
        for(unsigned i=0;i<7;++i)row[i]=0x09ff;
        return;
    }
    unsigned minutes=time/6000,seconds=(time/100)%60,cent=time%100;
    row[0]=(uint16_t)(0x0800+minutes);row[1]=0x082a;
    row[2]=(uint16_t)(0x0800+seconds/10);row[3]=(uint16_t)(0x0800+seconds%10);
    row[4]=0x082b;row[5]=(uint16_t)(0x0800+cent/10);row[6]=(uint16_t)(0x0800+cent%10);
}
static void Text(uint16_t *row,const char *text) {
    for(;*text;++text,++row)
        *row=*text==' '?0x0100:(*text=='?'?0x0c5b:(uint16_t)(0x0c40+*text-'A'));
}
static void RecordRow(uint16_t *map,unsigned row,unsigned column,unsigned rank,uint16_t time) {
    uint16_t *r=map+row*32;
    if(rank==10)r[column-1]=1;
    r[column]=(uint16_t)(rank%10);r[column+1]=0x0c5a;
    TimeTiles(r+column+2,time);
    r+=32;r[column-1]=0x000d;
    for(unsigned i=column;i<column+8;++i)r[i]=0x000e;
    r[column+8]=0x000f;
}
static void CarIcon(Ppu *ppu,unsigned slot,unsigned car,unsigned x,unsigned y) {
    ppu->oam[slot*2]=(uint16_t)((y<<8)|x);
    ppu->oam[slot*2+1]=(uint16_t)(0x3900+car*0x202);
    unsigned shift=(slot%4)*2;
    ppu->highOam[slot/4]=(ppu->highOam[slot/4]&~(3u<<shift))|(2u<<shift);
}
static void CarTabs(Ppu *ppu,unsigned selected) {
    for(unsigned car=0;car<FZERO_RECORD_CARS;++car) {
        /* Sixteen-pixel sprites, with one native pixel between each tab. */
        CarIcon(ppu,car,car,4+car*17,56);
        if(selected==FZERO_RECORD_MIXED_PAGE || selected==car)continue;
        /* These four OBJ palettes belong to the car artwork. Change only the
         * copied PPU, retaining the title, map, text and backdrop palettes. */
        for(unsigned i=1;i<16;++i) {
            unsigned index=192+car*16+i,color=ppu->cgram[index];
            unsigned grey=((color&31)*77+((color>>5)&31)*150+((color>>10)&31)*29)>>8;
            grey=grey*3/4;
            ppu->cgram[index]=(uint16_t)(grey|(grey<<5)|(grey<<10));
        }
    }
}
static unsigned MixedRank(const uint8_t *cars,unsigned count,unsigned car,unsigned rank) {
    if(!rank)return 0;
    /* A mixed list contains a prefix of each car's list, in source order.
     * Counting that car's entries preserves identity even when times tie. */
    for(unsigned i=0;i<count;++i)if(cars[i]==car && --rank==0)return i+1;
    return 0;
}
static void Marker(Ppu *ppu,unsigned slot,unsigned x,unsigned y,uint16_t attributes) {
    ppu->oam[slot*2]=(uint16_t)((y<<8)|x);
    ppu->oam[slot*2+1]=attributes;
    ppu->highOam[slot/4]&=~(3u<<((slot%4)*2)); /* Eight-pixel sprite. */
}
void FZeroRecordsViewLine(FZeroRecordsView *v,const FZeroRecordsRuntime *r,const Ppu *ppu,int line) {
    if(line==1)v->active=false;
    if(line==57) {
        /* The Records menu and page share display mode. Check the loaded
         * page header as well, so fade transitions keep the correct content. */
        unsigned map=((unsigned)ppu->bgXsc[2]&0xfc)<<8;
        if(!r || !r->initialized || !r->view_active || PPU_mode(ppu)!=1 ||
           ppu->bgXsc[2]!=0x23 || ppu->bgTileAdr!=0x4433 ||
           ppu->vram[map+8*32+9]!=0x0c27 || ppu->vram[map+8*32+12]!=0x0c51)return;
        memcpy(&v->scratch,ppu,sizeof(*ppu));Ppu *copy=&v->scratch;
        uint16_t *tiles=copy->vram+map;
        /* This unused glyph exists only in the copied PPU. */
        memset(copy->vram+0x4ff8,0,16);copy->vram[0x4ffb]=0x3c3c;
        for(unsigned y=8;y<28;++y)for(unsigned x=0;x<32;++x) {
            bool keep=(y==8 && x>=9 && x<18) || ((y==12 || y==13) && x>=2 && x<14);
            if(!keep)tiles[y*32+x]=0x0100;
        }
        for(unsigned x=0;x<6;++x)tiles[16*32+9+x]=ppu->vram[map+15*32+2+x];
        FZeroMixedRecords mixed;
        const FZeroCarRecords *records;
        if(r->view_car==FZERO_RECORD_MIXED_PAGE) {
            FZeroRecordsMixed(&r->records,r->view_track,&mixed);records=&mixed.times;
        } else records=&r->records.track[r->view_track][r->view_car];
        /* Leave an eight-pixel marker slot before the two-digit tenth rank,
         * clear of the mixed lap car icons, which end at x=143. */
        for(unsigned i=0;i<10;++i)RecordRow(tiles,8+i*2,20,i+1,records->races[i]);
        for(unsigned i=0;i<5;++i)RecordRow(tiles,18+i*2,7,i+1,records->laps[i]);
        if(r->confirmation) {
            for(unsigned y=22;y<28;++y)for(unsigned x=18;x<32;++x)
                tiles[y*32+x]=(y==24 || y==26) && x>=25?ppu->vram[map+y*32+x]:0x0100;
            Text(tiles+22*32+22,"ERASE?");
            Text(tiles+23*32+20,"ALL CARS");
        }
        for(unsigned i=0;i<11;++i)copy->oam[i*2]=0xf000;
        /* The guest places its two legacy markers in slots 64 and 65.
         * Rebuild this range for row ownership and our own markers instead. */
        for(unsigned i=64;i<=80;++i)copy->oam[i*2]=0xf000;
        CarTabs(copy,r->view_car);
        if(r->view_car==FZERO_RECORD_MIXED_PAGE) {
            /* Empty rows and rows covered by the dialog receive no icon.
             * All four palettes remain coloured here. */
            for(unsigned i=0;i<10;++i)
                if(mixed.race_cars[i]<4 && (!r->confirmation || i<7))
                    CarIcon(copy,64+i,mixed.race_cars[i],236,56+i*16);
            for(unsigned i=0;i<5;++i)
                if(mixed.lap_cars[i]<4)CarIcon(copy,74+i,mixed.lap_cars[i],128,136+i*16);
        }
        FZeroRecordHighlight highlight=r->highlights[r->view_track];
        if(r->view_car==FZERO_RECORD_MIXED_PAGE) {
            highlight.race_rank=MixedRank(mixed.race_cars,10,highlight.car,highlight.race_rank);
            highlight.lap_rank=MixedRank(mixed.lap_cars,5,highlight.car,highlight.lap_rank);
        } else if(r->view_car!=highlight.car)highlight.race_rank=highlight.lap_rank=0;
        /* Keep the resident marker art and the guest's alternating palette.
         * Separate slots also work when neither candidate made a legacy list. */
        uint16_t attributes=(uint16_t)(0x3163|(ppu->oam[64*2+1]&0x0200));
        if(highlight.race_rank && (!r->confirmation || highlight.race_rank<=7))
            Marker(copy,79,highlight.race_rank==10?144:152,63+(highlight.race_rank-1)*16,attributes);
        if(highlight.lap_rank)
            Marker(copy,80,48,143+(highlight.lap_rank-1)*16,attributes);
        v->active=true;
    }
    if(v->active && line>=57 && line<=224)ppu_runLine(&v->scratch,line);
}
