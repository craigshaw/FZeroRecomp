#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEPENDENCY="$ROOT/snesrecomp"
PATCH_DIR="$ROOT/patches/snesrecomp"
EXPECTED_BASE=a64932f1af958f7e71a728ac1235d6cf911f71a0
INTEGRATED_REVISION=54e88618f179eabcd4dfa2279efa4ba492ac682f

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
  printf '%s\n' 'All six F-Zero patches are included in the pinned integration commit.'
  exit 0
fi

if [ "$actual_base" != "$EXPECTED_BASE" ]; then
  printf 'unexpected snesrecomp base: %s\nexpected integrated revision: %s\nrecovery base: %s\n' \
    "$actual_base" "$INTEGRATED_REVISION" "$EXPECTED_BASE" >&2
  exit 1
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
