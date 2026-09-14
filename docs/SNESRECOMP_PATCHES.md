# snesrecomp integration

| Revision | Commit |
| --- | --- |
| Integration on [craigshaw/snesrecomp](https://github.com/craigshaw/snesrecomp/tree/codex/fzero-upstream-20260914) | `cfc70ad071e385db2d779d39f2cf04a684cff77a` |
| Upstream base | `4d42cab33d02a8ce3dd5626ca2f2a7b91cedb628` |

The seven ordered files under `patches/snesrecomp/` reproduce the integration
from that base. Keep generic changes in the dependency and game-specific host
code here.

## Patch series

| Patch | Purpose |
| --- | --- |
| 0001 | Recognise long-call trampolines in Python and Rust analysis and emission. |
| 0002 | Preserve RDNMI open-bus bits and resume parked polls at the hardware read. |
| 0003 | Give the active interpreter ownership of mixed-tier return continuations. |
| 0004 | Classify dispatch helpers at exact entry M/X widths and retract disagreements. |
| 0005 | Classify tier-2 evidence and honour final per-address entry-width overrides. |
| 0006 | Support the macOS system Bash, linker and context API in the C test harness. |
| 0007 | Restrict the low-WRAM dynamic polling policy to S-DD1 cartridges. |

The previous HDMA ownership patch is retired. F-Zero now uses upstream's
`snes_set_hdma_beam_enabled` interface around its per-line HDMA/render walk
and restores the prior setting. The runtime patches preserve upstream's
interpreter stack-pop behavior and explicit tier-2 entry widths.

Patch 0005's constructed regression fixture includes a real ROM call pair.
It was reviewed and accepted unchanged. The publication audit covers these
added patches and commits, not inherited upstream content or history.

## Verify or recover

```sh
git submodule update --init
sh tools/apply_snesrecomp_patches.sh
```

At the clean integrated revision, the script verifies the checkout without
applying patches. At the documented base, it applies all seven in order and
accepts a repeated run. It refuses an unexpected revision or a dirty integrated
checkout. Recovery mode leaves local changes; use the integrated pin normally.

## Update the dependency

1. Start from a clean base in an isolated checkout and apply the ordered stack.
2. Preserve logical commits and synthetic tests. Run from the dependency root:

   ```sh
   python3 tests/v2/run_tests.py
   python3 -m pytest -p no:cacheprovider tests/test_tier2_ingest.py
   bash tests/run_c_tests.sh
   python3 - <<'PYTEST'
   import runpy, sys
   sys.path.insert(0, "recompiler")
   test = runpy.run_path("tests/v2/test_dispatch_helper_entry_width.py")
   test["test_x16_call_does_not_accept_x8_misalignment_as_dispatch_helper"]()
   PYTEST
   cargo test --manifest-path recompiler-rs/Cargo.toml
   ```

3. Follow the [build instructions](../README.md#build-from-source) to regenerate and
   rebuild the game. Run CTest and exercise the affected route. Verify recovery,
   repeated application, and dirty-checkout refusal.
4. Publish the integration before updating the parent pin. Update the recovery
   patches, verifier, and this document together.

When upstream accepts a change, remove only its superseded recovery patch and
revalidate the remaining stack. Never edit the pinned dependency ad hoc.

## September 2026 upgrade validation

The integration uses the fixed upstream target above, with the retained fixes
ported and tested before publication. Python generation from the unchanged
cfg with `--cfg-roots` produces 678 exact AOT variants and 9 LLE variants,
matching the previous baseline manifest structure. The Python and Rust
analyzers each match their respective previous coverage after the trampoline
patch. The current game build uses Python generation.

On macOS arm64, the dependency Python suite passed 420 tests, the tier-2
classifier passed three tests, and the separate exact-width regression passed.
Rust passed 86 library tests and two analyzer tests. The full C suite passed,
including 107 bridge tests. The host passed all four CTest tests and synthetic
Metal GPU checks. Headless and Metal boot/attract runs each completed 1,800
frames; the Metal run matched all protected-HUD RGB comparisons. The headless
tier-2 capture and SRAM matched the previous baseline run.

On 14 September 2026, the owner reported completing a full GP with no issues,
including working saves, filters and modes. The league, difficulty and vehicle
were not specified. This is the tested candidate's manual gameplay result,
not a claim of complete game coverage. This dependency upgrade has not yet
been built or exercised on Windows or Linux. Existing release downloads
remain unchanged.
