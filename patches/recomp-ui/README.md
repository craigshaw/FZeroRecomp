# F-Zero launcher adapter

The launcher remains pinned to `773155ae7d3be80a21d40851b58f99c79e003de1`.
The patch selects project-owned Display, Hotkeys and input-source renderers,
allows the Display card to fit seven rows, adds the host save-folder picker,
and hides unsupported pad remapping.
The host Aspect Ratio selector stages Original, 16:9, and 32:9 without changing
the pinned boolean widescreen ABI.
It changes no shared ABI, emulator behavior or other game.

CMake normalises the backend's line endings, copies it into the build directory,
and applies the patch there with Git outside the source checkout's repository
scope. It checks for adapter markers because Git can otherwise report success
while skipping an ignored build-local file. The original submodule stays clean.
The generated copy is disposable and must not be committed. This adapter and
`src/launcher_controls.inc` belong to F-Zero; they are not a generic upstream
feature. The patched backend retains recomp-ui's MIT licence and notices.

When changing the dependency pin, review and refresh the adapter, configure
from a fresh build directory, run the host/UI tests, and inspect the launcher
at its default and minimum sizes. Verify that Play transfers and saves settings,
that the runtime reads them, and that closing the launcher discards staged
Display edits. FPS and keyboard binding editors persist edits immediately,
matching the pinned launcher's existing binding behavior.
