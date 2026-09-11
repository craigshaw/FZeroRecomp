# snesrecomp integration

| Revision | Commit |
| --- | --- |
| Integration on [craigshaw/snesrecomp](https://github.com/craigshaw/snesrecomp/tree/codex/fzero-runtime) | `54e88618f179eabcd4dfa2279efa4ba492ac682f` |
| Upstream base | `a64932f1af958f7e71a728ac1235d6cf911f71a0` |

The six ordered files under `patches/snesrecomp/` reproduce the integration
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
| 0006 | Let a frame-model host own HDMA during its scanline render walk. |

Patch 0006 exposes `g_host_owns_hdma`. F-Zero sets it only around its per-line
HDMA/render walk, preventing the beam simulator from running a second HDMA
engine. Other hosts retain the default behaviour.

Patch 0005's constructed regression fixture includes a real ROM call pair.
It was reviewed and accepted unchanged. The publication audit covers these
added patches and commits, not inherited upstream content or history.

## Verify or recover

```sh
git submodule update --init
sh tools/apply_snesrecomp_patches.sh
```

At the clean integrated revision, the script verifies the checkout without
applying patches. At the documented base, it applies all six in order and
accepts a repeated run. It refuses an unexpected revision or a dirty integrated
checkout. Recovery mode leaves local changes; use the integrated pin normally.

## Update the dependency

1. Start from a clean base in an isolated checkout and apply the ordered stack.
2. Preserve logical commits and synthetic tests. Run from the dependency root:

   ```sh
   python3 tests/v2/run_tests.py
   python3 -m pytest -p no:cacheprovider tests/test_tier2_ingest.py
   bash tests/run_c_tests.sh
   cargo test --manifest-path recompiler-rs/Cargo.toml
   ```

3. Follow the [build instructions](../README.md#build-and-run) to regenerate and
   rebuild the game. Run CTest and exercise the affected route. Verify recovery,
   repeated application, and dirty-checkout refusal.
4. Publish the integration before updating the parent pin. Update the recovery
   patches, verifier, and this document together.

When upstream accepts a change, remove only its superseded recovery patch and
revalidate the remaining stack. Never edit the pinned dependency ad hoc.
