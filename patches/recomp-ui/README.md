# F-Zero launcher adapter

The launcher remains pinned to `773155ae7d3be80a21d40851b58f99c79e003de1`.
The patch selects project-owned Display, Hotkeys and input-source renderers,
allows the Display card to fit seven rows, and hides unsupported pad remapping.
It changes no shared ABI, persistence format, emulator behavior or other game.

CMake normalises the backend's line endings, copies it into the build directory,
and applies the patch there with Git. The original submodule stays clean.
The generated copy is disposable and must not be committed. This adapter and
`src/launcher_controls.inc` belong to F-Zero; they are not a generic upstream
feature. The patched backend retains recomp-ui's MIT licence and notices.

When changing the dependency pin, review and refresh the adapter, configure
from a fresh build directory, run the host/UI tests, and inspect the launcher
at its default and minimum sizes. Verify that Play transfers and saves settings,
that the runtime reads them, and that closing the launcher discards staged
Display edits. FPS and keyboard binding editors persist edits immediately,
matching the pinned launcher's existing binding behavior.
