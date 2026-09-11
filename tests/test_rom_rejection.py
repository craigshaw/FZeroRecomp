"""Reject an invented ROM without replacing the executable's ROM cache."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="fzero-rom-rejection-") as directory:
        root = Path(directory)
        exe = root / binary.name
        shutil.copy2(binary, exe)
        rom = root / "invented.sfc"
        rom.write_bytes(bytes(4096))
        cache = root / "rom.cfg"
        cache.write_text("previously-selected-rom.sfc\n", encoding="utf-8")
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   SNESRECOMP_MAX_FRAMES="1")
        result = subprocess.run([str(exe), str(rom)], cwd=root, env=env,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 1, result.stderr
        assert "ROM verification failed" in result.stderr, result.stderr
        assert "rom loaded:" not in result.stderr, result.stderr
        assert cache.read_text(encoding="utf-8") == "previously-selected-rom.sfc\n"
    print("ROM rejection and cache preservation: passed")


if __name__ == "__main__":
    main()
