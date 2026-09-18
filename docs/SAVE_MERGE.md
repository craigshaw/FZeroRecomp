# Command-line save merge

Run from the project directory with Python 3.11 or newer. The tool uses only
the Python standard library. No ROM, compiler, or game build is required.

```powershell
python tools/merge_saves.py primary.srm secondary.srm merged.srm
```

On macOS or Linux, use `python3` if necessary. Quote paths that contain spaces.
The output must be a new file. Both inputs remain unchanged, and existing
output files are never overwritten. Close any game using these files first.
Inspect the result by importing `merged.srm` in the launcher. Import replaces
the active save; this tool does not change the launcher's Import behaviour.

## Merge rules

- Primary supplies all non-record data. Secondary supplies additional records.
- For each track and car, take the distinct race times from both saves and
  retain the fastest ten. Separately take the distinct lap times and retain
  the fastest five. Equal times within a list become one entry, even if they
  originally came from different races. Equal times in different cars remain
  separate records. Empty places are not records.
- Read both original and extended record tables. Include valid records visible
  in the original tables, including changes made by the original game.
- Rebuild the original mixed top-ten race table and single best lap per track
  from the merged lists. Equal times use car order to break ties. Preserve the
  original record's mode flag from Primary when available, otherwise Secondary.
  For extension-only entries, use a zero mode flag: the extension does not
  store this information. This flag does not change time or car ownership.
- Recalculate the original checksums and extension checksums. The output stays
  2,048 bytes and uses the current version-1 records extension.

Merging the output with the same Secondary again produces identical bytes.
Merging a save with itself removes its duplicate times and normalises its
record tables. It need not reproduce the original bytes on that first merge.
Swapping Primary and Secondary produces the same extended times, but can
change non-record data and original record mode flags.

Only the best times stored in the inputs can be recovered. A merge cannot
recover older results that were already displaced. It can restore a record
that was cleared in one input but still exists in the other.

## Validation and limits

Inputs must be complete 2 KB F-Zero battery saves with valid original signatures
and checksums. The tool accepts legacy saves with an all-zero or all-255 spare
area, and saves with a valid current records extension. It rejects damaged,
unknown, and newer extension formats instead of attempting recovery. Save
states and copier headers are not supported.

The current format allows only one inherited lap longer than two minutes per
track, across all cars. If more than one such lap survives the merged top-five
lists, the tool refuses the merge and names the track number (1 through 15 in
league order). It does not discard a qualifying time to fit this limit.

Validation completes before output creation. Ordinary output write errors
remove the partial output. After a crash or loss of power, discard any partial
output and rerun with a new output filename. The inputs are never modified.

## Tests

```powershell
python -m unittest discover -s tests -p test_merge_saves.py -v
```

Tests use synthetic saves and the reference vector from the game's C codec.
They cover distinct unions, capacities, Primary metadata, original table flags,
legacy files, long laps, invalid inputs, command-line file protection, and
repeated-merge idempotence. An optional `FZERO_RECORDS_LIBRARY` environment
variable can name a shared build of `src/fzero_records.c`, exporting
`FZeroRecordsDecode`, `FZeroRecordsEncode`, and `FZeroRecordsLegacyValid`, to
check the output directly against the runtime codec.
