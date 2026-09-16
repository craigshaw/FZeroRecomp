# Per-car records extension

The host implements this extension without changing the original save block.

## Behaviour

Each of the 15 tracks has four car pages and a fifth mixed page. Each car
page holds ten race times and five lap times. GP and Practice share these lists. A completed five-lap race
supplies one race time and one fastest-lap candidate. Each candidate qualifies
independently. Incomplete races supply neither candidate.

Equal times from separate completed races can occupy separate places. A race
must be processed once, even if its result screen remains open for many frames.

Left and right cycle through the four cars and then the mixed page, with
wrapping in both directions. Up and down retain track navigation and keep the
selected page. All four car images stay in fixed positions at the upper
left, with one native pixel between their 16-pixel sprite boxes. On a car
page, the other three images are greyed out. On the mixed page, all four
remain coloured. Ten race times form an aligned column on the right. Five
lap times appear beside the track map. Populated mixed rows show their car
beside the time; empty rows and individual-car rows have no car image.
Preserve the existing backdrop, track name, map, lettering, colours, and
exit controls.

The mixed page takes the fastest ten race times and fastest five lap times
across the saved car lists, preserving equal times and each entry's car.
Equal times use car order for a stable display order. Ownership comes from
the source list and remains paired with the time as rows are ranked. This
is calculated for display and requires no extra SRAM or format change. It follows the same
completed-race and inherited-history rules as the car pages.

Both menu and post-GP entry use this presentation. A fresh entry selects the
car used for racing. The selected page stays visible through the exit fade,
until the game loads the next menu.

Flashing markers identify the latest completed race and its fastest lap at
their actual positions on each page, immediately to the left of their ranks.
Qualification is independent for the
two lists and for the car and mixed pages. Equal times identify the newly
inserted entry, not an older entry with the same time. Highlights survive
between the five races of a GP. Retrying a track or clearing it removes its
old highlights. A new selection, title reset, or save reload removes all
highlights. These are transient display details and do not change the save
format.

The original combined records continue to update through the existing game
logic. Their storage layout and validation rules remain unchanged.

## Storage

The file remains exactly 2,048 bytes. Only the spare area contains the new
format. All offsets below are file offsets.

| Offset | Bytes | Purpose |
| --- | ---: | --- |
| `0x000` | 512 | Original save block |
| `0x200` | 16 | Extension header |
| `0x210` | 1,485 | Packed race and lap lists |
| `0x7DD` | 34 | Inherited long-lap entries |
| `0x7FF` | 1 | Reserved, zero |

The header contains four magic bytes (`FZRX`), a one-byte version (`1`),
one byte of flags (`0`), a two-byte payload length (`1485`), a four-byte
CRC-32 of the original block at the last extension save, and a four-byte
CRC-32 of the entire extension. Calculate the extension CRC with its own
field zeroed. Use little-endian integers and standard IEEE CRC-32.

The original-block CRC detects changes made outside the extension. A mismatch
does not invalidate independently stored extended records.

### Lossless list encoding

Times are integer hundredths. Each list is sorted, with empty places at the
end. A race list has ten positions, values from 0 to 59,999, and empty value
60,000. A normal lap list has five positions, values from 0 to 12,000, and
empty value 12,001.

For a sorted list of length `k` with `N` possible values including empty,
encode its rank among combinations with repetition:

```text
rank = sum(C(value[i] + i, i + 1)), for i = 0 through k - 1
valid ranks = 0 through C(N + k - 1, k) - 1
```

Ten race positions require 137 bits. Five lap positions require 61 bits.
Store 60 groups in track order, then car order. Each group contains its race
rank followed by its lap rank. Store each rank least-significant bit first,
without byte padding between groups. This requires exactly 1,485 bytes.

The rank mapping preserves equal times and empty places. Reject out-of-range
ranks and values. Use exact integer arithmetic. A portable implementation can
use fixed multiword integers for the 137-bit race rank; floating point is not
suitable. The lap rank fits in a 64-bit unsigned integer.

The timing bound follows from five lap durations summing exactly to a race
time below ten minutes. Their minimum cannot exceed 11,999 hundredths.
The normal lap encoding leaves one additional hundredth of headroom.

### Inherited long laps

The original save may contain a best lap from an incomplete race. Accept that
history on migration even though new entries require a completed race.

For each track, reserve one 18-bit entry containing a 16-bit time followed by
a two-bit car index. `0x0FFFF` means no entry. Other entries hold a valid
inherited time above 12,000 and at most 59,999 hundredths. These 15 entries
require 34 bytes; the two unused high bits are zero.

An inherited long lap occupies one of its car's five places. Store its other
lap entries in the normal list, with an empty position for the inherited
entry. Remove the inherited entry when five faster laps displace it. Reject
an encoded long entry if its car already has five normal lap entries.

This guarantees room for every track's single best lap on initial migration.
It does not promise storage for arbitrary edited saves with multiple long
laps per track.

## Migration, round trips, and recovery

- On first migration, validate the original records and place each existing
  race and best lap in its recorded car's list. Leave missing history empty.
  Do not promote default placeholders or infer missing results.
- On later loads, use a valid extension directly. If the original block has
  changed, merge recoverable records conservatively. Use the maximum observed
  count of each equal race time, then retain the fastest ten. Do not sum the
  counts: repeated imports must not invent additional races. Merge an observed
  legacy best lap without repeatedly adding the same inherited observation.
- A changed legacy table cannot identify every race played in the original
  game, or distinguish all equal-time results. Only visible legacy history can
  be recovered. Existing extension data remains independently readable.
- When a later external best lap exceeds the normal range, use a free long-lap
  entry if available. Preserve an occupied entry. If both cannot be represented,
  leave the new value in the original block and report that it was not imported.
  Never clamp a time or discard stored extension history to conceal this limit.
- Clearing a track in the recomp clears that track for all four cars and clears
  its original records. Clearing the whole save clears both formats. Clearing
  records in the original game affects only the original block; do not infer
  permission to delete independent extension history from that change.
- A bad extension checksum must not damage the original records. Retain a
  recovery copy before rebuilding from validated legacy history. An unsupported
  extension version must be preserved without rewriting it as an older version.
- Save through a temporary file and replace the destination only after a
  complete write. Keep a previous valid backup. A failed write must leave a
  recoverable full save. The host saves changed records and saves again on normal exit.

## Integration boundaries

Keep storage, migration, and ranking in a host-owned module. Capture final
race and lap values at a verified completion boundary. The original game
continues to own its record updates.

Add a records presentation path at native resolution before final scaling.
Read graphics from the user's loaded ROM and preserve the existing screen
style. Intercept left/right only while the records page is active. Keep the
page state and display upload from the same frame. Verify settings, held
buttons, page transitions, screenshots, native aspect, and widescreen.

No generated game source or pinned dependency needs to be edited for this
design. Any dependency change found necessary during implementation needs
separate review under the project's dependency workflow.

## Validation evidence

Isolated probes used the supported USA ROM, the existing interpreter, and a
copy of the built application. The user's save was read but never written.

| Check | Result |
| --- | --- |
| Save initialization, checksum repair, new records in both modes for every car and track, and record clearing | 142 cases; no spare-area reads or writes |
| Full application startup and exit with valid, blank, and invalid legacy blocks | All three preserved the marked spare area and 2,048-byte file size |
| Timer boundaries across all 60,000 displayed values and both increment phases | 120,000 cases passed |
| Lap subtraction, including second and minute boundaries | 10,005 cases passed with exact hundredths |
| Exhaustive small-domain rank bijection | 11,424 lists passed |
| Complete packed-save encode/decode, including full lists, ties, empty places, and maximum values | 1,005 cases passed |
| Independent decoding after legacy changes | 1,005 cases passed |
| Single-bit corruption anywhere in the extension | All 12,288 cases rejected |
| Current save migration | 39 race entries and all 15 best laps preserved |
| Long-lap migration capacity | One entry on all 15 tracks, tested through 9:59.99 |
| Packed data through original record updates and course clears | 30 cases preserved the extension |
| Packed data through full application startup and save | Passed |
| Repeat-import race merge | 10,000 idempotence cases passed |

These checks establish format capacity and preservation by the tested game
paths. They do not cover physical cartridge transfers, every emulator's save
export, or loss of power during a filesystem write. Transfer tools must retain the
full 2 KB.

Production tests cover codec round trips and corruption, migration, event
capture, held input, track clearing, copied-PPU isolation, file replacement,
backup failures, recovery, and unknown-version preservation. ROM-backed
completion checks cover 120 combinations of mode, track and car. Scripted
page checks cover car wrap, retained car selection across track changes, and
accepted clearing. The real user save is never changed by these checks.
