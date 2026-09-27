#include "launcher_saves.h"
#include "save_location.h"
#include <SDL3/SDL.h>
#include <string.h>

static int Failure(char *error, size_t capacity, const char *action) {
    SDL_snprintf(error, capacity, "%s: %s", action, SDL_GetError());
    return 0;
}

static int BackupSave(char *error, size_t capacity) {
    const char *save_path = FZeroSaveFile();
    char backup_path[FZERO_SAVE_PATH_CAPACITY];
    if (!FZeroSavePath(backup_path, sizeof(backup_path), "save.srm.bak")) {
        SDL_snprintf(error, capacity, "The save backup path is too long.");
        return 0;
    }
    FZeroSavePathState state = FZeroSavePathStatus(save_path);
    if (state != FZERO_SAVE_PATH_EXISTS) {
        if (state == FZERO_SAVE_PATH_MISSING) return 1;
        return Failure(error, capacity, "Cannot inspect the current save");
    }
    if (!SDL_CopyFile(save_path, backup_path))
        return Failure(error, capacity, "Cannot back up the current save");
    return 1;
}

int FZeroImportSave(const char *source, char *error, size_t capacity) {
    error[0] = '\0';
    const char *save_path = FZeroSaveFile();
    char temp_path[FZERO_SAVE_PATH_CAPACITY];
    if (!FZeroSavePath(temp_path, sizeof(temp_path), "save.srm.importing")) {
        SDL_snprintf(error, capacity, "The save import path is too long.");
        return 0;
    }
    size_t size = 0;
    /* Read before touching the destination or backup: selecting either of
     * those files for import must also preserve the selected bytes. SDL's
     * file APIs accept the native picker's UTF-8 paths on Windows. */
    void *data = SDL_LoadFile(source, &size);
    if (!data) return Failure(error, capacity, "Cannot read the selected save");
    if (!size) {
        SDL_free(data);
        SDL_snprintf(error, capacity, "The selected save is empty.");
        return 0;
    }
    if (!FZeroSaveEnsureDirectory()) {
        SDL_free(data);
        return Failure(error, capacity, "Cannot open the save folder");
    }
    /* Exclusive creation avoids overwriting a leftover import or another
     * process's temporary file. Stage fully before backing up/replacing SRAM. */
    SDL_IOStream *out = SDL_IOFromFile(temp_path, "wbx");
    if (!out) {
        SDL_free(data);
        return Failure(error, capacity, "Cannot create the temporary save");
    }
    int ok = SDL_WriteIO(out, data, size) == size;
    SDL_free(data);
    if (!ok) Failure(error, capacity, "Cannot write the imported save");
    if (!SDL_CloseIO(out) && ok) {
        ok = 0;
        Failure(error, capacity, "Cannot finish writing the imported save");
    }
    if (ok) ok = BackupSave(error, capacity);
    if (ok && !SDL_RenamePath(temp_path, save_path)) {
        ok = 0;
        Failure(error, capacity, "Cannot replace the current save");
    }
    if (!ok) SDL_RemovePath(temp_path);
    return ok;
}

int FZeroClearSave(char *error, size_t capacity) {
    error[0] = '\0';
    const char *save_path = FZeroSaveFile();
    if (!BackupSave(error, capacity)) return 0;
    if (!SDL_RemovePath(save_path))
        return Failure(error, capacity, "Cannot clear the current save");
    return 1;
}
