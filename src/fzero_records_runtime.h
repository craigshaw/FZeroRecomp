#pragma once
#include "fzero_records.h"

enum { FZERO_RECORD_MIXED_PAGE = FZERO_RECORD_CARS,
       FZERO_RECORD_PAGES = FZERO_RECORD_CARS + 1 };

/* Transient identity of the latest race on each track. Ranks are one-based;
 * zero means that candidate did not enter its car's list. */
typedef struct FZeroRecordHighlight {
    unsigned car, race_rank, lap_rank;
} FZeroRecordHighlight;

typedef struct FZeroRecordsRuntime {
    FZeroRecords records;
    FZeroRecordHighlight highlights[FZERO_RECORD_TRACKS];
    FZeroRecordsStatus status;
    bool initialized, writable, dirty, race_armed, race_done, view_active, clear_pending, confirmation;
    unsigned selected_car, view_track, view_car, race_track, race_car;
    unsigned skipped_laps;
    uint32_t previous_input;
    uint8_t legacy_before[512];
} FZeroRecordsRuntime;

void FZeroRecordsStart(FZeroRecordsRuntime *r, const uint8_t *save, bool writable);
uint32_t FZeroRecordsInput(FZeroRecordsRuntime *r, const uint8_t *ram,
                          uint32_t input, bool fully_visible);
void FZeroRecordsBeforeFrame(FZeroRecordsRuntime *r, const uint8_t *ram,
                             const uint8_t *save);
void FZeroRecordsAfterFrame(FZeroRecordsRuntime *r, const uint8_t *ram,
                            uint8_t *save);
bool FZeroRecordsFlush(FZeroRecordsRuntime *r, uint8_t *save);
