#include "launcher_saves.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

static const char *save_path = "saves/save.srm";
static const char *backup_path = "saves/save.srm.bak";
static const char *temp_path = "saves/save.srm.importing";

static int Failure(char *error, size_t capacity, const char *action) {
    SDL_snprintf(error, capacity, "%s: %s", action, SDL_GetError());
    return 0;
}

static int BackupSave(char *error, size_t capacity) {
    struct stat info;
    if (stat(save_path, &info) != 0) {
        if (errno == ENOENT) return 1;
        SDL_SetError("%s", strerror(errno));
        return Failure(error, capacity, "Cannot inspect the current save");
    }
    if (!SDL_CopyFile(save_path, backup_path))
        return Failure(error, capacity, "Cannot back up the current save");
    return 1;
}

int FZeroImportSave(const char *source, char *error, size_t capacity) {
    error[0] = '\0';
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
    if (!SDL_CreateDirectory("saves")) {
        SDL_free(data);
        return Failure(error, capacity, "Cannot create the saves folder");
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
    if (!BackupSave(error, capacity)) return 0;
    if (!SDL_RemovePath(save_path))
        return Failure(error, capacity, "Cannot clear the current save");
    return 1;
}
