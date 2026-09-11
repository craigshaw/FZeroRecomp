/* Synthetic course trees and artwork only. */
#include "fzero_layers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "ground line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static FZeroLayers layers;
static uint8_t ram[0x20000], saved_ram[0x20000];
static Ppu ppu, saved;
static uint32_t original[224][256];
static void Put(unsigned a, unsigned v) { ram[a] = v; ram[a+1] = v >> 8; }
static unsigned Tile(unsigned x, unsigned y) {
    unsigned room = (((x & 8191) >> 8) * 3 + ((y & 4095) >> 8) * 5) % 8;
    return 1 + (room * 19 + ((x >> 3) & 31) * 7 + ((y >> 3) & 31) * 11) % 254;
}
static void Init(void) {
    memset(&layers, 0, sizeof(layers));
    memset(ram, 0, sizeof(ram));
    ram[0x50]=1; ram[0x54]=2; ram[0x55]=3; ram[0x5c]=1; ram[0x5f]=4;
    Put(0xb0, 0x4c00);
    for (unsigned y=0;y<16;++y) for(unsigned x=0;x<32;++x)
        ram[0x14c00+y*32+x]=(x*3+y*5)%8;
    for(unsigned room=0;room<8;++room) for(unsigned y=0;y<16;++y) {
        Put(0x15000+room*32+y*2, 0x7000+room*512+y*32);
        for(unsigned x=0;x<16;++x) {
            unsigned panel=0x1000+(room*256+y*16+x)*4;
            Put(0x17000+room*512+y*32+x*2,panel);
            for(unsigned dx=0;dx<2;++dx) for(unsigned dy=0;dy<2;++dy)
                ram[0x10000+panel+dx*2+dy]=1+(room*19+(x*2+dx)*7+(y*2+dy)*11)%254;
        }
    }
    ppu_reset(&ppu);
    PpuBeginDrawing(&ppu,(uint8_t*)original,sizeof(original[0]),kPpuRenderFlags_NewRenderer);
    ppu.inidisp=15; ppu.bgmode=7; ppu.screenEnabled[0]=1; ppu.obsel=2;
    for(unsigned i=0;i<0x4000;++i) ppu.vram[i]=((1+(i*13+i/64)%255)<<8)|7;
    for(unsigned i=0;i<256;++i) ppu.cgram[i]=(i&31)|(((i/3)&31)<<5)|(((i/7)&31)<<10);
}
static void Camera(unsigned x, unsigned y) {
    Put(0xa2,x); Put(0xa6,y);
    ppu.m7matrix[4]=((x+384)&1023)+128;
    ppu.m7matrix[5]=((y+336)&1023)+176;
    ppu.m7matrix[6]=ppu.m7matrix[4]-128;
    ppu.m7matrix[7]=ppu.m7matrix[5]-176;
    FZeroGroundPrepare(&layers.ground,ram);
}
static unsigned Rgb(unsigned c) {
    unsigned r=c&31,g=(c>>5)&31,b=(c>>10)&31;
    unsigned brightness=ppu.inidisp&15;
    return (((r*8+r/4)*brightness/15)<<16)|(((g*8+g/4)*brightness/15)<<8)|((b*8+b/4)*brightness/15);
}
static void CheckRender(int line, int angle_case, unsigned wx, unsigned wy) {
    /* Integer transforms with an independently calculated world position. */
    const int matrices[][4]={{256,0,0,256},{0,256,-256,0},{-256,0,0,-256},
                             {0,-256,256,0},{2048,768,-768,2048},
                             {8192,0,0,256},{32767,0,0,256}};
    const int *m=matrices[angle_case];
    for(int i=0;i<4;++i) ppu.m7matrix[i]=m[i];
    Camera(wx,wy);
    memcpy(saved_ram,ram,sizeof(ram));
    ppu_runLine(&ppu,0); ppu_runLine(&ppu,line);
    memcpy(&saved,&ppu,sizeof(ppu));
    unsigned long prior=layers.ground.lines;
    FZeroLayersProcessLine(&layers,&ppu,line,true,true);
    CHECK(layers.ground.lines==prior+1);
    CHECK(!memcmp(&saved,&ppu,sizeof(ppu)));
    CHECK(!memcmp(saved_ram,ram,sizeof(ram)));
    for(int x=0;x<398;++x) {
        if(x>=71&&x<327) {
            CHECK(layers.wide_world[line-1][x]==layers.world[line-1][x-71]);
            CHECK(layers.wide_hud[line-1][x]==layers.hud[line-1][x-71]);
            unsigned result=layers.hud[line-1][x-71]?layers.hud[line-1][x-71]:layers.world[line-1][x-71];
            CHECK((result&0xffffff)==original[line-1][x-71]);
            continue;
        }
        int rx=(ppu.m7sel&1)?255-(x-71):x-71;
        int ry=(ppu.m7sel&2)?255-line:line;
        /* Only the last test has fractional X steps. Its other terms divide
         * exactly; floor the signed sum independently of the implementation. */
        int a=m[0]*(rx-128)+m[1]*(ry-176);
        int b=m[2]*(rx-128)+m[3]*(ry-176);
        int dx=a/256-(a<0&&a%256!=0),dy=b/256-(b<0&&b%256!=0);
        unsigned u=wx+512+dx,v=wy+512+dy;
        unsigned tile=Tile(u,v);
        unsigned colour=ppu.vram[tile*64+(v&7)*8+(u&7)]>>8;
        unsigned expected=Rgb(ppu.cgram[colour]);
        if(layers.wide_world[line-1][x]!=expected)
            fprintf(stderr,"x=%d y=%d matrix=%d flip=%u got=%06x expected=%06x\n",x,line,angle_case,ppu.m7sel,layers.wide_world[line-1][x],expected);
        CHECK(layers.wide_world[line-1][x]==expected);
    }
}
static void CheckComposition(void) {
    static Ppu reference, corrected;
    static uint32_t expected[224][398], actual[224][398];
    Init(); Camera(512,512);
    ppu.m7matrix[0]=ppu.m7matrix[3]=256;
    ppu.screenEnabled[0]=0x11;
    ppu.cgram[193]=0x7c1f;
    for(unsigned i=0;i<0x4000;i+=37) ppu.vram[i]&=255; /* Transparent ground. */
    for(unsigned row=0;row<8;++row) ppu.vram[0x4000+row]=0x0055;
    /* One high-priority and one low-priority sprite in the margins. */
    ppu.oam[136]=(88<<8)|248; ppu.oam[137]=0x3800;
    ppu.oam[138]=(88<<8)|54; ppu.oam[139]=0x0800;
    ppu.highOam[17]=5;
    for(unsigned style=0;style<8;++style) {
        ppu.inidisp=style&1?7:15;
        ppu.cgadsub=style&2?0x91:0x51;
        ppu.fixedColor=0x145f;
        ppu.screenEnabled[1]=style&4?0x11:0;
        ppu.cgwsel=style&4?2:0;
        ppu.screenWindowed[0]=style&1?0x11:0;
        ppu.windowsel=0x000303;
        ppu.window1left=40; ppu.window1right=200;
        memcpy(&reference,&ppu,sizeof(ppu)); memcpy(&corrected,&ppu,sizeof(ppu));
        PpuBeginDrawing(&reference,(uint8_t*)expected,sizeof(expected[0]),kPpuRenderFlags_NewRenderer);
        PpuBeginDrawing(&corrected,(uint8_t*)actual,sizeof(actual[0]),kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpace(&reference,71); PpuSetExtraSpace(&corrected,71);
        /* Independent complete resident rectangle for this camera. */
        for(unsigned y=0;y<128;++y) for(unsigned x=0;x<128;++x) {
            unsigned wx=((x*8-512)&1023)+512,wy=((y*8-512)&1023)+512;
            unsigned address=y*128+x;
            reference.vram[address]=(reference.vram[address]&0xff00)|Tile(wx,wy);
        }
        ppu_runLine(&reference,0); ppu_runLine(&corrected,0);
        ppu_runLine(&reference,93);
        CHECK(FZeroGroundRenderLine(&layers.ground,&corrected,93));
        for(int x=0;x<398;++x) if(x<71||x>=327) CHECK(actual[92][x]==expected[92][x]);
    }
}
void TestGround(void) {
    Init(); Camera(1637,3891);
    uint8_t tile;
    for(unsigned y=0;y<4096;y+=37) for(unsigned x=0;x<8192;x+=41) {
        CHECK(FZeroGroundTile(&layers.ground,x,y,&tile)); CHECK(tile==Tile(x,y));
        CHECK(FZeroGroundTile(&layers.ground,x+8192,y+4096,&tile)); CHECK(tile==Tile(x,y));
    }
    const unsigned cameras[][2]={{0,0},{8191,4095},{895,847},{896,848},{1023,1023},{1637,3891}};
    for(unsigned c=0;c<6;++c) for(unsigned flip=0;flip<4;++flip) {
        ppu.m7sel=flip;
        for(int m=0;m<7;++m) CheckRender(93,m,cameras[c][0],cameras[c][1]);
    }
    CHECK(layers.ground.corrected_pixels>0); CHECK(layers.ground.split_passes>0);
    ppu.inidisp=7; CheckRender(176,4,0,0);
    /* The pending frame owns its map. Later guest edits become visible only
     * after the next snapshot, including changes to a panel's four tiles. */
    Init(); Camera(0,0);
    CHECK(FZeroGroundTile(&layers.ground,0,0,&tile)); unsigned old=tile;
    ram[0x11000]=254;
    CHECK(FZeroGroundTile(&layers.ground,0,0,&tile)&&tile==old);
    FZeroGroundPrepare(&layers.ground,ram);
    CHECK(FZeroGroundTile(&layers.ground,0,0,&tile)&&tile==254);
    Put(0x17000,0xffff); FZeroGroundPrepare(&layers.ground,ram);
    CHECK(!FZeroGroundTile(&layers.ground,0,0,&tile));
    Put(0x15000,0xffff); FZeroGroundPrepare(&layers.ground,ram);
    CHECK(!FZeroGroundTile(&layers.ground,0,0,&tile));
    /* Fail before mutating the copied PPU for unsupported states or timing. */
    Init(); Camera(0,0); PpuSetExtraSpace(&ppu,71);
    ++ppu.m7matrix[4]; memcpy(&saved,&ppu,sizeof(ppu));
    CHECK(!FZeroGroundRenderLine(&layers.ground,&ppu,93));
    CHECK(!memcmp(&saved,&ppu,sizeof(ppu))); --ppu.m7matrix[4];
    ppu.obsel=0; CHECK(!FZeroGroundRenderLine(&layers.ground,&ppu,93)); ppu.obsel=2;
    ppu.m7sel=0x80; CHECK(!FZeroGroundRenderLine(&layers.ground,&ppu,93)); ppu.m7sel=0;
    ppu.mosaic=1; CHECK(!FZeroGroundRenderLine(&layers.ground,&ppu,93)); ppu.mosaic=0;
    /* Course snapshots remain fresh through an explosion and finish camera. */
    ram[0xc3]=0x40; ram[0x50]=0;
    FZeroGroundPrepare(&layers.ground,ram); CHECK(layers.ground.ready);
    ram[0x10000]=73;
    FZeroGroundPrepare(&layers.ground,ram); CHECK(layers.ground.map[0]==73);
    ram[0xc3]=0x22; ram[0x50]=1;
    FZeroGroundPrepare(&layers.ground,ram); CHECK(layers.ground.ready);
    ram[0x54]=3; ram[0x55]=1; ram[0x50]=0; ram[0x5f]=0x80;
    FZeroGroundPrepare(&layers.ground,ram); CHECK(layers.ground.ready);
    ram[0x54]=2; ram[0x55]=0; ram[0x5c]=0; ram[0x5f]=0;
    FZeroGroundPrepare(&layers.ground,ram); CHECK(layers.ground.ready);
    ram[0x54]=3; ram[0x55]=6;
    FZeroGroundPrepare(&layers.ground,ram); CHECK(!layers.ground.ready);
    ram[0x54]=2;
    ram[0x55]=3; ram[0x5c]=1; ram[0x5f]=4; ram[0x50]=1;
    ram[0xc3]=1; FZeroGroundPrepare(&layers.ground,ram); CHECK(!layers.ground.ready);
    ram[0xc3]=0; Put(0xb0,0xffff); FZeroGroundPrepare(&layers.ground,ram); CHECK(!layers.ground.ready);
    FZeroGroundPrepare(&layers.ground,NULL); CHECK(!layers.ground.ready);
    CheckComposition();
    puts("ground checks passed");
}
