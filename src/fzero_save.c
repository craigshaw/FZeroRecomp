#include "fzero_save.h"
#include "fzero_records.h"
#include "save_location.h"
#include <SDL3/SDL.h>
#include <string.h>

static bool EmptyTail(const uint8_t *save) {
    bool zero=true,ff=true;
    for(unsigned i=512;i<2048;++i) {zero&=save[i]==0;ff&=save[i]==255;}
    return zero || ff;
}
static bool Failure(char *message,size_t capacity,const char *action) {
    SDL_snprintf(message,capacity,"%s: %s",action,SDL_GetError());return false;
}
/* Recovery files are never overwritten. Preserve the exact input, including
 * malformed length, before allowing the guest or extension to rebuild it. */
static bool Recovery(const void *data,size_t size,char *message,size_t capacity) {
    char recovery[FZERO_SAVE_PATH_CAPACITY], name[64];
    for(unsigned i=0;i<10000;++i) {
        SDL_snprintf(name,sizeof(name),"save.srm.recovery-%u",i);
        if(!FZeroSavePath(recovery,sizeof(recovery),name)) {
            SDL_SetError("Save recovery path is too long");return Failure(message,capacity,"Cannot create recovery copy");
        }
        FZeroSavePathState state=FZeroSavePathStatus(recovery);
        if(state==FZERO_SAVE_PATH_EXISTS)continue;
        if(state==FZERO_SAVE_PATH_ERROR)return Failure(message,capacity,"Cannot inspect recovery path");
        SDL_IOStream *out=SDL_IOFromFile(recovery,"wbx");
        if(!out)return Failure(message,capacity,"Cannot create recovery copy");
        bool ok=SDL_WriteIO(out,data,size)==size && SDL_FlushIO(out);
        if(!SDL_CloseIO(out))ok=false;
        if(!ok) {SDL_RemovePath(recovery);return Failure(message,capacity,"Cannot finish recovery copy");}
        SDL_snprintf(message,capacity,"Save data was repaired. The original is in %s.",recovery);return true;
    }
    SDL_SetError("No free recovery filename");return Failure(message,capacity,"Cannot preserve save");
}
bool FZeroSaveLoad(uint8_t save[2048],char *message,size_t capacity) {
    message[0]=0;memset(save,0,2048);
    if(!FZeroSaveDirectoryAvailable()) {
        SDL_snprintf(message,capacity,"The selected save folder is unavailable; saving is disabled.");return false;
    }
    const char *path=FZeroSaveFile();
    FZeroSavePathState state=FZeroSavePathStatus(path);
    if(state==FZERO_SAVE_PATH_MISSING)return true;
    if(state==FZERO_SAVE_PATH_ERROR)return Failure(message,capacity,"Cannot inspect save; saving disabled");
    size_t size=0;void *data=SDL_LoadFile(path,&size);
    if(!data)return Failure(message,capacity,"Cannot load save; saving disabled");
    memcpy(save,data,size<2048?size:2048);
    FZeroRecords records;bool changed;
    FZeroRecordsStatus status=FZeroRecordsDecode(&records,save,&changed);
    bool unknown_tail=status==FZERO_RECORDS_ABSENT && !EmptyTail(save);
    bool ok=true;
    if(status==FZERO_RECORDS_UNSUPPORTED) {
        SDL_snprintf(message,capacity,"This save uses a newer records format. Saving is disabled to preserve it.");ok=false;
    } else if(size!=2048 || status==FZERO_RECORDS_DAMAGED || unknown_tail || !FZeroRecordsLegacyValid(save)) {
        ok=Recovery(data,size,message,capacity);
    }
    SDL_free(data);return ok;
}
bool FZeroSaveWrite(const uint8_t save[2048],char *message,size_t capacity) {
    message[0]=0;
    if(!FZeroSaveEnsureDirectory())return Failure(message,capacity,"Cannot open save folder");
    const char *path=FZeroSaveFile();
    char temporary[FZERO_SAVE_PATH_CAPACITY],backup[FZERO_SAVE_PATH_CAPACITY];
    if(!FZeroSavePath(temporary,sizeof(temporary),"save.srm.writing") ||
       !FZeroSavePath(backup,sizeof(backup),"save.srm.bak")) {
        SDL_SetError("Save path is too long");return Failure(message,capacity,"Cannot write save");
    }
    SDL_IOStream *out=SDL_IOFromFile(temporary,"wbx");
    if(!out)return Failure(message,capacity,"Cannot create temporary save");
    bool ok=SDL_WriteIO(out,save,2048)==2048 && SDL_FlushIO(out);
    if(!ok)Failure(message,capacity,"Cannot write save");
    if(!SDL_CloseIO(out) && ok)ok=Failure(message,capacity,"Cannot finish save");
    /* Keep a previous valid file. A damaged input already has its own recovery
     * copy and must not replace a useful backup. */
    if(ok) {
        FZeroSavePathState state=FZeroSavePathStatus(path);
        if(state==FZERO_SAVE_PATH_EXISTS) {
            size_t size=0;uint8_t *old=SDL_LoadFile(path,&size);
            if(!old)ok=Failure(message,capacity,"Cannot read previous save");
            else if(size==2048 && FZeroRecordsLegacyValid(old)) {
                FZeroRecords records;bool changed;
                FZeroRecordsStatus status=FZeroRecordsDecode(&records,old,&changed);
                if(memcmp(old,save,2048) &&
                   (status==FZERO_RECORDS_VALID || (status==FZERO_RECORDS_ABSENT && EmptyTail(old))) &&
                   !SDL_CopyFile(path,backup))ok=Failure(message,capacity,"Cannot back up previous save");
            }
            SDL_free(old);
        } else if(state==FZERO_SAVE_PATH_ERROR)ok=Failure(message,capacity,"Cannot inspect previous save");
    }
    if(ok && !SDL_RenamePath(temporary,path))ok=Failure(message,capacity,"Cannot replace save");
    if(!ok)SDL_RemovePath(temporary);
    return ok;
}
