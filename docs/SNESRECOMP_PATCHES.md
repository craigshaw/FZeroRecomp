# snesrecomp integration

| Revision | Commit |
| --- | --- |
| Integration on [craigshaw/snesrecomp](https://github.com/craigshaw/snesrecomp/tree/codex/fzero-upstream-20260914) | `da4a541713fd28702dd8c11c87a2c0fd69fcce45` |
| Upstream base | `4d42cab33d02a8ce3dd5626ca2f2a7b91cedb628` |

The eight ordered files under `patches/snesrecomp/` reproduce the integration
from that base. Keep generic changes in the dependency and game-specific host
code here.

Recovery patches use LF line endings, enforced by `.gitattributes`, so
`git apply --cached` sees the same payload on Windows and POSIX hosts.

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
| 0008 | Preserve eight-byte PPU priority-buffer alignment with MSVC, GCC and Clang; test C and C++ on Windows, Linux and macOS. |

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
applying patches. At the documented base, it applies all eight in order and
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
not a claim of complete game coverage. Existing release downloads remain
unchanged.

### Windows alignment follow-up

Patch 0008 retains the required eight-byte alignment and unchanged priority
buffer layout, using `__declspec(align(8))` for MSVC (including clang-cl) and
the existing GNU alignment attribute for GCC and Clang. Both the struct tag
and typedef carry the alignment. The synthetic test checks nested buffers,
array strides, and stack, static and heap addresses in C and C++; removing
the alignment makes the regression test fail at compile time.

The [PPU alignment workflow](https://github.com/craigshaw/snesrecomp/actions/runs/34834925823)
passed C and C++ tests on Windows/MSVC, Linux/GCC, Linux/Clang and
macOS/Apple Clang. Local Windows checks also passed with MinGW GCC and LLVM
Clang in C99, C11 and C++17 modes.

The final pinned integration rebuilt successfully with MSVC x64 Release and
SDL 3.4.16 after regeneration. All four host CTest tests and the Direct3D 12
GPU presentation checks passed. Separate headless and Direct3D 12 boot/attract
runs each completed 1,800 frames with RGB validation: all headless full frames
and all GPU protected-HUD frames matched. These are automated smoke tests,
not a new manual Grand Prix playthrough.

The dependency v2 runner reported 420/420, including two C contract entries
that explicitly skip on Windows. The separate exact-width regression passed.
All eight recovery patches reconstruct the integration from the documented
base and accept a repeated run. The seven recovery/classifier tests pass with
normal Windows Git settings, including the dirty-checkout refusal cases.
The full Linux/macOS game was not rebuilt for this alignment-only follow-up;
the cross-platform CI checks exercise the shared PPU layout contract.
