#include "fzero_save.h"
#include "fzero_records.h"
#include "save_location.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"records save %d: %s (%s)\n",__LINE__,#x,SDL_GetError());exit(1);} } while(0)
static void CheckFile(const char *path,const uint8_t *expected,size_t length) {
    size_t size;void *data=SDL_LoadFile(path,&size);
    CHECK(data && size==length && !memcmp(data,expected,size));SDL_free(data);
}
int main(void) {
    SDL_RemovePath("config.ini");
    SDL_RemovePath("config.ini.save-folder.tmp");
    char location_error[256];
    CHECK(FZeroSaveLocationLoad(location_error,sizeof(location_error)) && FZeroSaveIsDefault());
    const char *paths[]={"saves/save.srm","saves/save.srm.bak","saves/save.srm.writing",
        "saves/save.srm.recovery-0","saves/save.srm.recovery-1","saves/save.srm.recovery-2","saves"};
    for(unsigned i=0;i<sizeof(paths)/sizeof(*paths);++i)SDL_RemovePath(paths[i]);
    uint8_t a[2048]={0},b[2048],loaded[2048];char message[384];FZeroRecords records;
    CHECK(FZeroSaveLoad(loaded,message,sizeof(message)) && !message[0]);
    memcpy(a,"FZERO",5);memcpy(a+507,"FZERO",5); /* Valid empty invented legacy block. */
    FZeroRecordsInit(&records);CHECK(FZeroRecordsEncode(&records,a));
    CHECK(FZeroSaveWrite(a,message,sizeof(message)));
    CHECK(FZeroSaveLoad(loaded,message,sizeof(message)) && !memcmp(a,loaded,2048));
    memcpy(b,a,2048);FZeroRecordsInsert(&records,3,2,10000,2000);CHECK(FZeroRecordsEncode(&records,b));
    CHECK(FZeroSaveWrite(b,message,sizeof(message)));CheckFile(paths[0],b,2048);CheckFile(paths[1],a,2048);
    CHECK(FZeroSaveWrite(b,message,sizeof(message)));CheckFile(paths[1],a,2048);
    CHECK(SDL_SaveFile(paths[2],a,2048));CHECK(!FZeroSaveWrite(a,message,sizeof(message)) && message[0]);
    CheckFile(paths[0],b,2048);CheckFile(paths[2],a,2048);CHECK(SDL_RemovePath(paths[2]));
    CHECK(SDL_RemovePath(paths[1]));CHECK(SDL_CreateDirectory(paths[1]));
    CHECK(!FZeroSaveWrite(a,message,sizeof(message)));CheckFile(paths[0],b,2048);
    CHECK(SDL_RemovePath(paths[1]));CHECK(SDL_SaveFile(paths[1],a,2048));
    /* Checksum corruption is copied verbatim before repair. */
    b[700]^=1;CHECK(SDL_SaveFile(paths[0],b,2048));
    CHECK(FZeroSaveLoad(loaded,message,sizeof(message)) && message[0]);CheckFile(paths[3],b,2048);
    CHECK(FZeroSaveWrite(a,message,sizeof(message)));CheckFile(paths[1],a,2048);CheckFile(paths[3],b,2048);
    /* A short import also gets a recovery copy, without overwriting the first. */
    CHECK(SDL_SaveFile(paths[0],a,512));CHECK(FZeroSaveLoad(loaded,message,sizeof(message)) && message[0]);
    CheckFile(paths[4],a,512);CheckFile(paths[3],b,2048);
    b[516]=2;CHECK(SDL_SaveFile(paths[0],b,2048));
    CHECK(!FZeroSaveLoad(loaded,message,sizeof(message)) && message[0]);CheckFile(paths[0],b,2048);
    /* Existing directories cannot be silently replaced by a fresh save. */
    CHECK(SDL_RemovePath(paths[0]));CHECK(SDL_CreateDirectory(paths[0]));
    CHECK(!FZeroSaveLoad(loaded,message,sizeof(message)));
    CHECK(!FZeroSaveWrite(a,message,sizeof(message)));CHECK(SDL_RemovePath(paths[0]));
    CHECK(FZeroSaveWrite(a,message,sizeof(message)));
    char *cwd=SDL_GetCurrentDirectory();CHECK(cwd);
    char selected[FZERO_SAVE_PATH_CAPACITY],custom_save[FZERO_SAVE_PATH_CAPACITY],custom_backup[FZERO_SAVE_PATH_CAPACITY];
    SDL_snprintf(selected,sizeof(selected),"%s/custom-records",cwd);SDL_free(cwd);
    CHECK(SDL_CreateDirectory(selected));
    CHECK(FZeroSaveLocationSelect(selected,location_error,sizeof(location_error)));
    CHECK(FZeroSaveLocationLoad(location_error,sizeof(location_error)));
    CHECK(FZeroSavePath(custom_save,sizeof(custom_save),"save.srm"));
    CHECK(FZeroSavePath(custom_backup,sizeof(custom_backup),"save.srm.bak"));
    SDL_RemovePath(custom_save);SDL_RemovePath(custom_backup);
    CHECK(FZeroSaveLoad(loaded,message,sizeof(message)) && !message[0]);
    CHECK(FZeroSaveWrite(a,message,sizeof(message)));
    CHECK(FZeroSaveLoad(loaded,message,sizeof(message)) && !memcmp(a,loaded,2048));
    FZeroRecordsInit(&records);FZeroRecordsInsert(&records,3,2,10000,2000);
    memcpy(b,a,2048);CHECK(FZeroRecordsEncode(&records,b));
    CHECK(FZeroSaveWrite(b,message,sizeof(message)));
    CheckFile(custom_save,b,2048);CheckFile(custom_backup,a,2048);
    CHECK(SDL_RemovePath(custom_save));CHECK(SDL_RemovePath(custom_backup));
    CHECK(SDL_RemovePath(selected));
    CHECK(!FZeroSaveLoad(loaded,message,sizeof(message)) && message[0]);
    CHECK(!FZeroSaveWrite(a,message,sizeof(message)));
    CHECK(FZeroSaveLocationSelect("",location_error,sizeof(location_error)));
    CheckFile(paths[0],a,2048);
    puts("records file replacement, backups, recovery and write failures: passed");return 0;
}
