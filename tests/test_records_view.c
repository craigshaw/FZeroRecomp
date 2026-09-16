/* Invented page graphics verify that rendering changes only copied state. */
#include "fzero_records_view.h"
#include "snes/snes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
uint16_t WsShadowTile(int l,int x,uint32_t y,uint16_t tile){return tile;}
bool WsShadowLayerActive(int l){return false;}
uint32_t WsShadowWorldX(int l){return 0;}
uint32_t WsShadowPresentWorldY(int l,int x){return 0;}
uint32_t WsShadowScrollY(int l){return 0;}
void WsShadowOnVramWrite(uint16_t a,uint16_t v){}
#define CHECK(x) do { if(!(x)){fprintf(stderr,"records view %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static Ppu ppu,before;
static FZeroRecordsView view;
static uint32_t pixels[224][256];
int main(void) {
    FZeroRecordsRuntime records={0};FZeroRecordsInit(&records.records);
    records.initialized=records.view_active=true;
    ppu_reset(&ppu);PpuBeginDrawing(&ppu,(uint8_t *)pixels,sizeof(pixels[0]),kPpuRenderFlags_NewRenderer);
    ppu.inidisp=15;ppu.bgmode=1;ppu.bgXsc[2]=0x23;ppu.bgTileAdr=0x4433;
    ppu.vram[0x2000+8*32+9]=0x0c27;ppu.vram[0x2000+8*32+12]=0x0c51;
    ppu.vram[0x2000+12*32+2]=0x0c19;
    for(unsigned i=0;i<6;++i)ppu.vram[0x2000+15*32+2+i]=(uint16_t)(0xc40+i);
    for(unsigned i=0;i<128;++i)ppu.oam[i*2]=0xf000;
    /* Visible legacy markers must never leak through the copied page. */
    ppu.oam[128]=0x4f87;ppu.oam[130]=0x870f;
    ppu.oam[129]=ppu.oam[131]=0x3363;ppu.highOam[16]=0xf0;
    ppu.oam[240]=0x9810;
    for(unsigned i=0;i<256;++i)ppu.cgram[i]=(uint16_t)(0x0815+i%16);
    ppu_runLine(&ppu,0);before=ppu;
    for(unsigned car=0;car<4;++car) {
        records.view_car=car;records.records.track[0][car].races[0]=12345;
        FZeroRecordsViewLine(&view,&records,&ppu,1);
        CHECK(!view.active);
        for(int line=57;line<=224;++line)FZeroRecordsViewLine(&view,&records,&ppu,line);
        CHECK(view.active && !memcmp(&ppu,&before,sizeof(ppu)));
        uint16_t *map=view.scratch.vram+0x2000;
        CHECK(map[8*32+22]==0x0802 && map[8*32+25]==0x0803 && map[8*32+27]==0x0804);
        CHECK(map[10*32+22]==0x09ff && map[18*32+9]==0x09ff);
        CHECK(map[12*32+2]==0xc19 && map[16*32+9]==0xc40);
        CHECK(view.scratch.oam[240]==ppu.oam[240]);
        for(unsigned i=0;i<4;++i) {
            CHECK(view.scratch.oam[i*2]==0x3804+i*17);
            CHECK(view.scratch.oam[i*2+1]==0x3900+i*0x202);
            for(unsigned shade=1;shade<16;++shade) {
                unsigned index=192+i*16+shade,value=view.scratch.cgram[index];
                if(i==car)CHECK(value==ppu.cgram[index]);
                else CHECK((value&31)==((value>>5)&31) && (value&31)==((value>>10)&31) && value!=ppu.cgram[index]);
            }
        }
        CHECK(!memcmp(view.scratch.cgram,ppu.cgram,192*sizeof(uint16_t)));
        for(unsigned i=4;i<11;++i)CHECK(view.scratch.oam[i*2]==0xf000);
        for(unsigned i=64;i<=80;++i)CHECK(view.scratch.oam[i*2]==0xf000);
    }
    /* The fifth page combines values, retains ties and shows all four cars. */
    records.view_car=FZERO_RECORD_MIXED_PAGE;
    for(unsigned i=1;i<10;++i)records.records.track[0][0].races[i]=(uint16_t)(12345+i);
    for(unsigned car=0;car<4;++car)records.records.track[0][car].laps[0]=2345;
    records.records.track[0][0].laps[1]=3456;
    FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
    for(unsigned car=0;car<4;++car) {
        CHECK(view.scratch.oam[car*2]==0x3804+car*17);
        CHECK(view.scratch.oam[car*2+1]==0x3900+car*0x202);
        CHECK(view.scratch.vram[0x2000+(8+car*2)*32+22]==0x0802);
    }
    CHECK(view.scratch.highOam[0]==0xaa && !memcmp(&ppu,&before,sizeof(ppu)));
    CHECK(!memcmp(view.scratch.cgram,ppu.cgram,sizeof(ppu.cgram)));
    for(unsigned i=0;i<10;++i) {
        CHECK(view.scratch.oam[(64+i)*2]==((56+i*16)<<8)+236);
        CHECK(view.scratch.oam[(64+i)*2+1]==0x3900+(i<4?i:0)*0x202);
    }
    for(unsigned i=0;i<5;++i) {
        CHECK(view.scratch.oam[(74+i)*2]==((136+i*16)<<8)+128);
        CHECK(view.scratch.oam[(74+i)*2+1]==0x3900+(i<4?i:0)*0x202);
        CHECK((view.scratch.oam[(74+i)*2]&255)+16<=18*8); /* Clear of the race marker. */
    }
    /* A track with no history has no row ownership icons. */
    records.view_track=1;
    FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
    for(unsigned i=64;i<=80;++i)CHECK(view.scratch.oam[i*2]==0xf000);
    records.view_track=0;
    /* Retained presentation must also work at every visible fade level. */
    for(unsigned brightness=0;brightness<16;++brightness) {
        ppu.inidisp=(uint8_t)brightness;before=ppu;
        FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
        CHECK(view.active && view.scratch.inidisp==brightness && !memcmp(&ppu,&before,sizeof(ppu)));
    }
    records.confirmation=true;ppu.vram[0x2000+22*32+22]=0xc44;before=ppu;
    FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
    CHECK(view.scratch.vram[0x2000+22*32+22]==0xc44);
    CHECK(view.scratch.vram[0x2000+23*32+20]==0xc40);
    CHECK(view.scratch.vram[0x2000+22*32+19]==0x100);
    CHECK(!memcmp(&ppu,&before,sizeof(ppu)));
    for(unsigned i=7;i<10;++i)CHECK(view.scratch.oam[(64+i)*2]==0xf000);
    CHECK(view.scratch.oam[74*2]==0x8880);
    /* Tied entries identify a source position, not the first matching time.
     * Car zero's second entries rank fifth on both mixed lists. */
    records.confirmation=false;
    records.highlights[0]=(FZeroRecordHighlight){0,2,2};
    for(unsigned page=0;page<FZERO_RECORD_PAGES;++page) {
        records.view_car=page;
        FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
        unsigned rank=page==0?2:5;
        if(page==0 || page==4) {
            CHECK(view.scratch.oam[158]==((63+(rank-1)*16)<<8)+152);
            CHECK(view.scratch.oam[160]==((143+(rank-1)*16)<<8)+48);
            CHECK(view.scratch.oam[159]==0x3363 && view.scratch.oam[161]==0x3363);
            CHECK((view.scratch.highOam[19]&0xc0)==0 && (view.scratch.highOam[20]&3)==0);
        } else CHECK(view.scratch.oam[158]==0xf000 && view.scratch.oam[160]==0xf000);
        CHECK(!memcmp(&ppu,&before,sizeof(ppu)));
    }
    /* The final race and lap positions share a scanline. Both markers must
     * stay clear of the two owner icons and the two-digit race rank. */
    records.view_car=FZERO_RECORD_MIXED_PAGE;
    records.highlights[0]=(FZeroRecordHighlight){0,7,2};
    FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
    CHECK(view.scratch.oam[158]==0xcf90 && view.scratch.oam[160]==0xcf30);
    CHECK(view.scratch.oam[146]==0xc8ec && view.scratch.oam[156]==0xc880);
    CHECK(view.scratch.vram[0x2000+26*32+19]==1 && view.scratch.vram[0x2000+26*32+20]==0);
    /* Two identical times in the same car list still select the second one. */
    records.records.track[0][0].races[1]=12345;records.records.track[0][0].laps[1]=2345;
    records.highlights[0]=(FZeroRecordHighlight){0,2,2};
    FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
    CHECK(view.scratch.oam[158]==0x4f98 && view.scratch.oam[160]==0x9f30);
    /* A car-only result has no mixed marker; one candidate can qualify alone. */
    records.highlights[0]=(FZeroRecordHighlight){0,10,0};
    FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
    CHECK(view.scratch.oam[158]==0xf000 && view.scratch.oam[160]==0xf000);
    records.view_car=0;
    FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
    CHECK(view.scratch.oam[158]==0xcf90 && view.scratch.oam[160]==0xf000);
    records.confirmation=true;
    FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
    CHECK(view.scratch.oam[158]==0xf000);
    records.highlights[0]=(FZeroRecordHighlight){0,0,2};ppu.oam[129]=0x3163;before=ppu;
    FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);
    CHECK(view.scratch.oam[158]==0xf000 && view.scratch.oam[160]==0x9f30);
    CHECK(view.scratch.oam[161]==0x3163 && !memcmp(&ppu,&before,sizeof(ppu)));
    records.view_active=false;FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);CHECK(!view.active);
    records.view_active=true;ppu.vram[0x2000+8*32+9]=0;
    FZeroRecordsViewLine(&view,&records,&ppu,1);FZeroRecordsViewLine(&view,&records,&ppu,57);CHECK(!view.active);
    puts("records presentation isolation, five pages, fading and dialog: passed");return 0;
}
