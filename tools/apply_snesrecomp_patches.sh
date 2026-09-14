#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEPENDENCY="$ROOT/snesrecomp"
PATCH_DIR="$ROOT/patches/snesrecomp"
EXPECTED_BASE=4d42cab33d02a8ce3dd5626ca2f2a7b91cedb628
INTEGRATED_REVISION=da4a541713fd28702dd8c11c87a2c0fd69fcce45

if [ ! -d "$DEPENDENCY/.git" ] && [ ! -f "$DEPENDENCY/.git" ]; then
  printf '%s\n' 'snesrecomp submodule is not initialised.' >&2
  printf '%s\n' 'Run: git submodule update --init' >&2
  exit 1
fi

actual_base=$(git -C "$DEPENDENCY" rev-parse HEAD)
if [ "$actual_base" = "$INTEGRATED_REVISION" ]; then
  if [ -n "$(git -C "$DEPENDENCY" status --porcelain)" ]; then
    printf '%s\n' 'The integrated snesrecomp checkout has local edits; review them first.' >&2
    exit 1
  fi
  printf '%s\n' 'All F-Zero patches are included in the pinned integration commit.'
  exit 0
fi

if [ "$actual_base" != "$EXPECTED_BASE" ]; then
  printf 'unexpected snesrecomp base: %s\nexpected integrated revision: %s\nrecovery base: %s\n' \
    "$actual_base" "$INTEGRATED_REVISION" "$EXPECTED_BASE" >&2
  exit 1
fi

# Build the expected final index without changing the checkout's real index.
# Later patches can revise earlier hunks, so reversing each patch separately
# cannot reliably identify a fully recovered checkout.
RECOVERY_INDEX=$(mktemp "${TMPDIR:-/tmp}/fzero-recovery.XXXXXX")
trap 'rm -f "$RECOVERY_INDEX"' EXIT HUP INT TERM
rm -f "$RECOVERY_INDEX"
GIT_INDEX_FILE="$RECOVERY_INDEX" git -C "$DEPENDENCY" read-tree "$EXPECTED_BASE"
for patch in "$PATCH_DIR"/*.patch; do
  GIT_INDEX_FILE="$RECOVERY_INDEX" git -C "$DEPENDENCY" apply --cached "$patch"
done
if GIT_INDEX_FILE="$RECOVERY_INDEX" git -C "$DEPENDENCY" diff --quiet; then
  printf '%s\n' 'The full F-Zero recovery stack is already applied.'
  exit 0
fi

for patch in "$PATCH_DIR"/*.patch; do
  if git -C "$DEPENDENCY" apply --reverse --check "$patch" 2>/dev/null; then
    printf 'already applied: %s\n' "$(basename "$patch")"
  elif git -C "$DEPENDENCY" apply --check "$patch"; then
    git -C "$DEPENDENCY" apply "$patch"
    printf 'applied: %s\n' "$(basename "$patch")"
  else
    printf 'cannot apply cleanly: %s\n' "$patch" >&2
    exit 1
  fi
done

if ! GIT_INDEX_FILE="$RECOVERY_INDEX" git -C "$DEPENDENCY" diff --quiet; then
  printf '%s\n' 'Recovered checkout differs from the expected patch stack.' >&2
  exit 1
fi
