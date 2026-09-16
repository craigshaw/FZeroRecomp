#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { FZERO_RECORD_TRACKS = 15, FZERO_RECORD_CARS = 4,
       FZERO_RECORD_RACES = 10, FZERO_RECORD_LAPS = 5,
       FZERO_SAVE_SIZE = 2048, FZERO_RECORD_EMPTY = 65535 };
typedef struct FZeroCarRecords {
    uint16_t races[FZERO_RECORD_RACES];
    uint16_t laps[FZERO_RECORD_LAPS];
} FZeroCarRecords;
typedef struct FZeroRecords {
    FZeroCarRecords track[FZERO_RECORD_TRACKS][FZERO_RECORD_CARS];
} FZeroRecords;
/* Display-only ownership. Empty positions use 0xff; this is not save data. */
typedef struct FZeroMixedRecords {
    FZeroCarRecords times;
    uint8_t race_cars[FZERO_RECORD_RACES], lap_cars[FZERO_RECORD_LAPS];
} FZeroMixedRecords;
typedef enum FZeroRecordsStatus {
    FZERO_RECORDS_ABSENT, FZERO_RECORDS_VALID,
    FZERO_RECORDS_DAMAGED, FZERO_RECORDS_UNSUPPORTED
} FZeroRecordsStatus;

void FZeroRecordsInit(FZeroRecords *records);
uint32_t FZeroRecordsCrc(const void *data, unsigned size);
bool FZeroRecordsLegacyValid(const uint8_t *save);
FZeroRecordsStatus FZeroRecordsDecode(FZeroRecords *records, const uint8_t *save,
                                     bool *legacy_changed);
/* Validate first, then replace only bytes 512..2047. */
bool FZeroRecordsEncode(const FZeroRecords *records, uint8_t *save);
/* Merge observed history without multiplying ties on repeated imports.
 * Returns the number of valid legacy laps that could not be represented. */
unsigned FZeroRecordsMergeLegacy(FZeroRecords *records, const uint8_t *save);
bool FZeroRecordsInsert(FZeroRecords *records, unsigned track, unsigned car,
                        uint16_t race, uint16_t lap);
void FZeroRecordsClearTrack(FZeroRecords *records, unsigned track);
/* Derive the mixed top ten/five without changing saved lists or removing ties. */
void FZeroRecordsMixed(const FZeroRecords *records, unsigned track, FZeroMixedRecords *mixed);
/* Decode a plain three-byte game time, without a car tag. */
bool FZeroRecordsTime(const uint8_t *bytes, uint16_t *time);
