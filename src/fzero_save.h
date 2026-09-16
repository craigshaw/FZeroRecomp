#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Paths are relative to the host's selected data directory. Failed loads
 * disable writes for this session so an unreadable save cannot be replaced. */
bool FZeroSaveLoad(uint8_t save[2048], char *message, size_t capacity);
bool FZeroSaveWrite(const uint8_t save[2048], char *message, size_t capacity);
