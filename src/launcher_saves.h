#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Paths share the runtime's data-directory working directory. Return nonzero
 * on success; otherwise supply a user-visible error without replacing SRAM. */
int FZeroImportSave(const char *source, char *error, size_t capacity);
int FZeroClearSave(char *error, size_t capacity);

#ifdef __cplusplus
}
#endif
