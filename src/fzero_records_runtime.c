#include "fzero_records_runtime.h"
#include <string.h>

static bool Page(const uint8_t *ram) {
    /* Both title-menu and post-GP Records use these processes. The demo
     * selector differs between those entry paths and is not a page guard. */
    return ram[0x54]==0 && ram[0x55]>=4 && ram[0x55]<=6;
}
void FZeroRecordsStart(FZeroRecordsRuntime *r,const uint8_t *save,bool writable) {
    memset(r,0,sizeof(*r));bool changed;
    r->status=FZeroRecordsDecode(&r->records,save,&changed);
    r->writable=writable && r->status!=FZERO_RECORDS_UNSUPPORTED;
    if(r->status==FZERO_RECORDS_VALID) {
        r->initialized=true;
        if(changed)r->skipped_laps=FZeroRecordsMergeLegacy(&r->records,save);
        r->dirty=changed;
    }
}
uint32_t FZeroRecordsInput(FZeroRecordsRuntime *r,const uint8_t *ram,uint32_t input,bool visible) {
    uint32_t pressed=input&~r->previous_input;r->previous_input=input;
    if(Page(ram)) {
        if(ram[0x55]==5 && visible && (input&0xc0)!=0xc0) {
            if(pressed&0x40)r->selected_car=(r->selected_car+FZERO_RECORD_PAGES-1)%FZERO_RECORD_PAGES;
            if(pressed&0x80)r->selected_car=(r->selected_car+1)%FZERO_RECORD_PAGES;
        }
        return input&~0xc0u;
    }
    return input;
}
void FZeroRecordsBeforeFrame(FZeroRecordsRuntime *r,const uint8_t *ram,const uint8_t *save) {
    memcpy(r->legacy_before,save,512);
    /* A new selection or title reset ends the previous racing session.
     * Race/result transitions within a GP retain all five track highlights. */
    if(ram[0x54]==1 || (ram[0x54]==0 && ram[0x55]==0))
        memset(r->highlights,0,sizeof(r->highlights));
    r->confirmation=Page(ram) && ram[0x55]==6;
    r->clear_pending=r->confirmation && ram[0x5a]==0;
    bool page=Page(ram);
    if(page && !r->view_active)r->selected_car=ram[0x52]&3;
    /* Exit changes process before the visible page fades. Retain the paired
     * selection until the next menu loads; the renderer also checks its tiles. */
    bool leaving=r->view_active && ram[0x54]==0 && ram[0x55]==2;
    r->view_active=(page || leaving) && ram[0xeeb]<3 && ram[0xeec]<5;
    if(page && r->view_active) { r->view_track=ram[0xeeb]*5+ram[0xeec];r->view_car=r->selected_car; }
    /* The title demonstration uses the same race engine. Its nonzero demo
     * selector excludes it. Arm only in a live, unfinished player race. */
    bool player_race=ram[0x54]==2 && ram[0x5b]==0 && ram[0x58]<=1;
    if(!player_race || ram[0x55]<2) {r->race_armed=false;r->race_done=false;}
    if(player_race && ram[0x55]>=2 && ram[0xf53]<5 && ram[0xc3]==0 &&
       ram[0xeeb]<3 && ram[0xeec]<5 && ram[0x52]<4) {
        if(!r->race_armed)
            memset(&r->highlights[ram[0xeeb]*5+ram[0xeec]],0,sizeof(r->highlights[0]));
        r->race_armed=true;r->race_track=ram[0xeeb]*5+ram[0xeec];r->race_car=ram[0x52];
    }
}
static bool EmptyLegacyTrack(const uint8_t *save,unsigned t) {
    unsigned base=5+(t/5)*167+(t%5)*33;
    for(unsigned i=0;i<11;++i)if(save[base+i*3]&0x80)return false;
    return true;
}
static unsigned CandidateRank(const uint16_t *times,unsigned count,uint16_t time) {
    /* New equal times follow all existing equal times. Remember the actual
     * inserted position, rather than finding the first matching value later. */
    unsigned i=0;while(i<count && times[i]<=time)++i;
    return i<count?i+1:0;
}
static void ClearTrack(FZeroRecordsRuntime *r,unsigned track) {
    FZeroRecordsClearTrack(&r->records,track);
    memset(&r->highlights[track],0,sizeof(r->highlights[track]));
    r->dirty=true;
}
void FZeroRecordsAfterFrame(FZeroRecordsRuntime *r,const uint8_t *ram,uint8_t *save) {
    if(!r->initialized && FZeroRecordsLegacyValid(save)) {
        r->skipped_laps=FZeroRecordsMergeLegacy(&r->records,save);
        r->initialized=true;r->dirty=true;
    }
    if(!r->initialized || !r->writable)return;
    /* Legacy updates can arrive after the finish event. Save that completed
     * update too, without importing incomplete-race laps into the new lists. */
    if(memcmp(r->legacy_before,save,512) && FZeroRecordsLegacyValid(save))r->dirty=true;
    if(r->clear_pending && ram[0x54]==0 && ram[0x55]==4) {
        ClearTrack(r,r->view_track);
    }
    if(r->view_active && FZeroRecordsLegacyValid(save)) {
        for(unsigned t=0;t<15;++t)if(!EmptyLegacyTrack(r->legacy_before,t) && EmptyLegacyTrack(save,t)) {
            ClearTrack(r,t);
        }
    }
    if(r->race_armed && !r->race_done && ram[0xf53]==5 && ram[0x54]==2 && ram[0x5b]==0) {
        uint16_t previous=0,fastest=65535;
        for(unsigned i=0;i<5;++i) {
            uint16_t elapsed;
            if(!FZeroRecordsTime(ram+0xe90+i*3,&elapsed) || elapsed<previous)return;
            uint16_t lap=(uint16_t)(elapsed-previous);if(lap<fastest)fastest=lap;
            previous=elapsed;
        }
        const FZeroCarRecords *list=&r->records.track[r->race_track][r->race_car];
        FZeroRecordHighlight highlight={r->race_car,
            CandidateRank(list->races,FZERO_RECORD_RACES,previous),
            CandidateRank(list->laps,FZERO_RECORD_LAPS,fastest)};
        if(FZeroRecordsInsert(&r->records,r->race_track,r->race_car,previous,fastest)) {
            r->highlights[r->race_track]=highlight;r->dirty=true;
        }
        r->race_done=true;
    }
}
bool FZeroRecordsFlush(FZeroRecordsRuntime *r,uint8_t *save) {
    if(!r->initialized || !r->writable)return true;
    if(!FZeroRecordsEncode(&r->records,save))return false;
    r->dirty=false;return true;
}
