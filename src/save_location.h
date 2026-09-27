#pragma once
#include <stdbool.h>
#include <stddef.h>

#define FZERO_SAVE_PATH_CAPACITY 4160

#ifdef __cplusplus
extern "C" {
#endif

/* The optional folder is stored in config.ini [Save]. Empty means the
 * existing platform default under the selected data root. */
bool FZeroSaveLocationLoad(char *error, size_t capacity);
bool FZeroSaveLocationSelect(const char *folder, char *error, size_t capacity);
const char *FZeroSaveDirectory(void);
const char *FZeroSaveFile(void);
bool FZeroSavePath(char *out, size_t capacity, const char *name);
bool FZeroSaveEnsureDirectory(void);
bool FZeroSaveDirectoryAvailable(void);
bool FZeroSaveIsDefault(void);

typedef enum FZeroSavePathState {
    FZERO_SAVE_PATH_ERROR = -1,
    FZERO_SAVE_PATH_MISSING = 0,
    FZERO_SAVE_PATH_EXISTS = 1
} FZeroSavePathState;
/* A UTF-8 path probe, including custom Windows folders with Unicode names. */
FZeroSavePathState FZeroSavePathStatus(const char *path);

#ifdef __cplusplus
}
#endif
