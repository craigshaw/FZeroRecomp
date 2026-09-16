# Agent instructions

## Project workflow

- Read `README.md` and `docs/ARCHITECTURE.md` before changing behaviour.
- The host uses C11, C++17, SDL3, CMake, and Ninja. Keep changes focused and
  preserve unrelated work.
- Treat `snesrecomp/` and `recomp-ui/` as pinned dependencies. Follow
  `docs/SNESRECOMP_PATCHES.md` and `patches/recomp-ui/README.md` for changes.
- Use the build instructions in `README.md`. On a configured macOS checkout,
  run `sh build.sh` and `ctest --test-dir build --output-on-failure` for host
  changes. Run checks appropriate to the files changed and report any gaps.
- Run `python3 tools/check_publication.py` before publication. Update relevant
  documentation when behaviour or build requirements change.
- Use Simplified Technical English. Do not use em dashes.

## Beads workflow

Beads (`bd`) is the work tracker for this project. Its local `.beads/` store
uses stealth mode and syncs through a separate private Dolt remote. Never add
`.beads/`, database files, or Beads exports to application Git. Do not install
Beads Git hooks or create parallel TODO, progress, or handoff files.

1. At session start or after context loss, run `bd prime` and `bd context`.
   Confirm this checkout is selected, then run `bd dolt pull` before editing
   issues. If the store or remote is unavailable, report the problem; do not
   initialise a replacement store or overwrite remote history.
2. Run `bd ready` and `bd list --limit 0`. Read relevant work with
   `bd show <id>`; include deferred issues when revisiting earlier work.
3. Reuse an issue or create one with `bd create --title '...' --type task
   --description '...'`. Mark active work with
   `bd update <id> --status in_progress` before implementation.
4. Record progress, blockers, verification results, and the next action in
   issue notes. Use `bd dep add <id> <dependency-id>` for dependencies and
   create linked issues for discovered work.
5. Close completed work with `bd close <id> --reason '...'`. Leave unfinished
   work in the appropriate status with enough context for the next session.
6. Before handoff, run `bd dolt commit`, `bd dolt pull`, and `bd dolt push` to
   sync work state with the private remote. Resolve conflicts without force
   pushing. Report any failed or pending sync. Finish with `git status --short`
   and a concise account of changes, checks, and remaining work. Application
   Git commits and pushes are separate and follow the user's requested scope.
