"""Check an extracted Linux package without a ROM or development library path."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


def main() -> None:
    package = Path(sys.argv[1]).resolve()
    assert (package / "lib/libSDL3.so.0").is_file(), "Packaged SDL is missing"
    allowed_roots = {"fzero_recomp", "assets", "lib", "Licenses", "Readme.txt", "BuildInfo.json"}
    assert {p.name for p in package.iterdir()} == allowed_roots
    for path in package.rglob("*"):
        assert not path.is_symlink(), f"Unexpected symlink: {path}"
        assert path.suffix not in {".sfc", ".smc", ".srm", ".ini", ".cfg", ".c", ".h"}, path
    assert [p.name for p in (package / "lib").iterdir()] == ["libSDL3.so.0"]
    dynamic = subprocess.check_output(["readelf", "-d", str(package / "fzero_recomp")], text=True)
    paths = [line for line in dynamic.splitlines() if "(RUNPATH)" in line or "(RPATH)" in line]
    assert len(paths) == 1 and "[$ORIGIN/lib]" in paths[0], paths

    # A different path, spaces, and an unrelated cwd must not require the checkout.
    with tempfile.TemporaryDirectory(prefix="fzero-linux-package-") as directory:
        root = Path(directory)
        installed = root / "Folder with spaces" / "F-Zero Recomp"
        shutil.copytree(package, installed)
        cwd = root / "unrelated"
        cwd.mkdir()
        env = dict(os.environ, PATH="/usr/bin:/bin", SDL_VIDEODRIVER="dummy",
                   SDL_AUDIODRIVER="dummy", SNESRECOMP_MAX_FRAMES="1")
        for key in ("LD_LIBRARY_PATH", "LD_PRELOAD"):
            env.pop(key, None)
        binary = installed / "fzero_recomp"
        libraries = subprocess.check_output(["ldd", str(binary)], env=env, text=True)
        assert "not found" not in libraries, libraries
        assert str(installed / "lib/libSDL3.so.0") in libraries, libraries
        assert "/.tools/" not in libraries, libraries
        rom = cwd / "invented.sfc"
        rom.write_bytes(bytes(4096))
        cache = installed / "rom.cfg"
        cache.write_text("previously-selected-rom.sfc\n", encoding="utf-8")
        result = subprocess.run([str(binary), str(rom)], cwd=cwd, env=env,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 1, result.stderr
        assert "ROM verification failed" in result.stderr, result.stderr
        assert "rom loaded:" not in result.stderr, result.stderr
        assert cache.read_text() == "previously-selected-rom.sfc\n"
        assert sorted(p.name for p in cwd.iterdir()) == ["invented.sfc"]
    print("Linux package relocation, dependencies, ROM rejection and cache preservation: passed")


if __name__ == "__main__":
    main()
