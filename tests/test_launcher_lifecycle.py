"""Linux desktop check. Requires a display, launcher assets, and a user ROM."""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    rom = args.rom.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="fzero-launcher-lifecycle-") as directory:
        root = Path(directory)
        exe = root / binary.name
        shutil.copy2(binary, exe)
        (root / "assets").symlink_to(binary.parent / "assets", target_is_directory=True)
        if (binary.parent / "lib").is_dir():
            (root / "lib").symlink_to(binary.parent / "lib", target_is_directory=True)
        (root / "rom.cfg").write_text(str(rom) + "\n", encoding="utf-8")
        # Fixed launcher dimensions match the pinned UI's Play button. All
        # settings and saves stay in this disposable executable directory.
        play = "size:1100x880;wait:15;click:970,819;wait:30;quit"
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(("SNESRECOMP_", "LNG_"))}
        env.update(G_DEBUG="fatal-warnings", SDL_AUDIODRIVER="dummy",
                   SNESRECOMP_MAX_PRESENTATIONS="30", SNESRECOMP_AUDIO_MUTE="1",
                   SNESRECOMP_VALIDATE_PRESENTATION="1")
        cases = (
            ("play", play, "dummy", 0, "[Video]"),
            ("cancel", "wait:15;quit", "dummy", 0, "launcher: action=1"),
            ("startup failure", play, "fzero-invalid-audio-driver", 1, "SDL_Init failed:"),
        )
        for name, script, audio, expected_exit, expected_output in cases:
            (root / "config.ini").write_text(
                "[Settings]\nFullscreen = 0\nVolume = 0\nWindowScale = 2\n"
                "Player1Source = 0\nSkipLauncher = 0\n", encoding="utf-8")
            result = subprocess.run(
                [str(exe)], cwd=root, capture_output=True, text=True, timeout=30,
                env=dict(env, LNG_SCRIPT=script, SDL_AUDIODRIVER=audio))
            output = result.stdout + result.stderr
            assert result.returncode == expected_exit, output
            assert expected_output in output, output
            assert "Gtk-WARNING" not in output, output
            if name != "cancel":
                assert "launcher: action=0" in output, output
                assert "rom loaded:" in output, output
            else:
                assert "rom loaded:" not in output, output
            if name == "play":
                frames = re.search(r"RGB validation: (\d+) full frames, (\d+) HUD-only frames matched", output)
                assert frames and sum(map(int, frames.groups())) == 30, output
            print(f"Launcher lifecycle ({name}): passed", flush=True)


if __name__ == "__main__":
    main()
