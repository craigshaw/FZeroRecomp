#!/usr/bin/env python3
"""Build F-Zero Recomp on Linux x86_64. See README.md for system packages."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parent
ROM_SHA256 = "bf16c3c867c58e2ab061c70de9295b6930d63f29f81cc986f5ecae03e0ad18d2"
SDL_VERSION = "3.4.16"
SDL_ARCHIVE = f"SDL3-{SDL_VERSION}.tar.gz"
SDL_URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/{SDL_ARCHIVE}"
SDL_SHA256 = "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68"
DXC_VERSION = "1.9.2609"
DXC_ARCHIVE = "linux_dxc_2026_09_28.x86_x64.tar.gz"
DXC_URL = f"https://github.com/microsoft/DirectXShaderCompiler/releases/download/v{DXC_VERSION}/{DXC_ARCHIVE}"
DXC_SHA256 = "96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1"
# Check the development interfaces, not distribution-specific package names.
PKG_CONFIG_MODULES = (
    "alsa", "libpulse", "libpipewire-0.3", "wayland-client", "wayland-egl",
    "wayland-cursor", "wayland-protocols", "libdecor-0", "xkbcommon",
    "x11", "xext", "xrandr", "xcursor", "xi", "xfixes", "xscrnsaver", "xtst",
    "libdrm", "gbm", "gl", "egl", "glesv2", "dbus-1", "libudev",
    "libusb-1.0", "vulkan",
)


def run(*args: str | Path) -> None:
    command = [str(arg) for arg in args]
    print("+ " + shlex.join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def sha256(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def verify_digest(path: Path, expected: str) -> None:
    actual = sha256(path)
    if actual != expected:
        raise RuntimeError(f"SHA-256 mismatch: {path}\nExpected {expected}\n"
                           f"Received {actual}\nRemove the invalid download and retry.")


def download(url: str, target: Path, expected: str) -> None:
    if target.exists():
        verify_digest(target, expected)
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {target.name}", flush=True)
    # Only verified, complete downloads enter the reusable cache.
    with tempfile.TemporaryDirectory(prefix=".download-", dir=target.parent) as directory:
        pending = Path(directory) / target.name
        with urllib.request.urlopen(url, timeout=60) as response, pending.open("wb") as output:
            shutil.copyfileobj(response, output)
        verify_digest(pending, expected)
        pending.replace(target)


def check_prerequisites() -> None:
    if sys.platform != "linux" or platform.machine() != "x86_64":
        raise RuntimeError("This helper requires Linux x86_64 (the pinned DXC platform).")
    libc, version = platform.libc_ver()
    match = re.match(r"(\d+)\.(\d+)", version)
    if libc != "glibc" or not match or tuple(map(int, match.groups())) < (2, 38):
        raise RuntimeError("The pinned DXC requires glibc 2.38 or newer; "
                           f"detected {libc or 'unknown libc'} {version}.")
    missing = [name for name in ("git", "cmake", "ninja", "pkg-config", "cc", "c++", "tar")
               if shutil.which(name) is None]
    if missing:
        raise RuntimeError("Missing build tools: " + ", ".join(missing) +
                           ". Install the Linux prerequisites listed in README.md.")
    version = subprocess.check_output(["cmake", "--version"], text=True)
    match = re.search(r"cmake version (\d+)\.(\d+)", version)
    if not match or tuple(map(int, match.groups())) < (3, 20):
        raise RuntimeError("CMake 3.20 or newer is required.")
    missing = [name for name in PKG_CONFIG_MODULES
               if subprocess.run(["pkg-config", "--exists", name],
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL).returncode != 0]
    if missing:
        raise RuntimeError("Missing development libraries (pkg-config): " + ", ".join(missing) +
                           ". Install the Linux prerequisites listed in README.md.")


def prepare_submodules(skip_update: bool) -> None:
    for name in ("snesrecomp", "recomp-ui"):
        if (ROOT / name / ".git").exists():
            changes = subprocess.check_output(
                ["git", "-C", str(ROOT / name), "status", "--porcelain", "--untracked-files=no"],
                text=True)
            if changes:
                raise RuntimeError(f"{name} has local changes. Preserve them before building.")
    if not skip_update:
        run("git", "submodule", "update", "--init", "--recursive")
    status = subprocess.check_output(["git", "submodule", "status", "--recursive"],
                                     cwd=ROOT, text=True)
    if not status.strip() or any(line[0] != " " for line in status.splitlines()):
        raise RuntimeError("Submodules are missing or differ from the pinned revisions. "
                           "Run git submodule update --init --recursive.")
    run("sh", ROOT / "tools/apply_snesrecomp_patches.sh")


def installed(prefix: Path, stamp: str, files: tuple[str, ...]) -> bool:
    marker = prefix / ".fzero-bootstrap"
    return (marker.is_file() and marker.read_text() == stamp and
            all((prefix / name).is_file() for name in files))


def prepare_dependencies(jobs: int) -> tuple[Path, Path]:
    tools = ROOT / ".tools"
    # Check the prebuilt compiler before spending time compiling SDL.
    dxc = tools / f"dxc-{DXC_VERSION}"
    if not installed(dxc, DXC_SHA256, ("bin/dxc", "lib/libdxcompiler.so")):
        archive = tools / "downloads" / DXC_ARCHIVE
        download(DXC_URL, archive, DXC_SHA256)
        dxc.mkdir(parents=True, exist_ok=True)
        run("tar", "-xzf", archive, "-C", dxc)
        run(dxc / "bin/dxc", "--version")
        (dxc / ".fzero-bootstrap").write_text(DXC_SHA256)
    else:
        print(f"Using cached DXC {DXC_VERSION}", flush=True)

    sdl = tools / f"sdl3-{SDL_VERSION}"
    sdl_files = ("lib/libSDL3.so.0", "lib/cmake/SDL3/SDL3Config.cmake")
    # Include the setup version so a future change to build flags rebuilds SDL.
    sdl_stamp = f"linux-sdl-setup-1\n{SDL_SHA256}\n"
    if not installed(sdl, sdl_stamp, sdl_files):
        archive = tools / "downloads" / SDL_ARCHIVE
        download(SDL_URL, archive, SDL_SHA256)
        source = tools / "src"
        source.mkdir(parents=True, exist_ok=True)
        run("tar", "-xzf", archive, "-C", source)
        build = tools / "build" / f"sdl3-{SDL_VERSION}"
        run("cmake", "-S", source / f"SDL3-{SDL_VERSION}", "-B", build, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={sdl}",
            "-DCMAKE_INSTALL_LIBDIR=lib", "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF",
            "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF",
            "-DSDL_WAYLAND=ON", "-DSDL_X11=ON", "-DSDL_OPENGL=ON", "-DSDL_VULKAN=ON",
            "-DSDL_ALSA=ON", "-DSDL_PULSEAUDIO=ON", "-DSDL_PIPEWIRE=ON")
        run("cmake", "--build", build, "--parallel", str(jobs))
        run("cmake", "--install", build)
        if not all((sdl / name).is_file() for name in sdl_files):
            raise RuntimeError(f"SDL installation is incomplete: {sdl}")
        (sdl / ".fzero-bootstrap").write_text(sdl_stamp)
    else:
        print(f"Using cached SDL {SDL_VERSION}", flush=True)

    return sdl, dxc / "bin/dxc"


def positive_int(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=os.environ.get("FZERO_ROM"),
                        help="Headerless F-Zero (USA) ROM; defaults to FZERO_ROM")
    parser.add_argument("--skip-generate", action="store_true",
                        help="Reuse generated code for host/shader edits; no ROM needed")
    parser.add_argument("--skip-submodule-update", action="store_true",
                        help="Verify existing pinned submodules without updating them")
    parser.add_argument("--jobs", type=positive_int, default=min(os.cpu_count() or 1, 8),
                        help="Parallel compile jobs (default: CPU count, capped at 8)")
    args = parser.parse_args()
    if sys.version_info < (3, 11):
        raise RuntimeError("Python 3.11 or newer is required.")
    if args.skip_generate:
        if not (ROOT / "generated/dispatch_v2.c").is_file():
            parser.error("Missing generated code. Run with --rom before using --skip-generate.")
    else:
        if args.rom is None:
            parser.error("Supply --rom '/path/to/F-Zero (USA).sfc' or set FZERO_ROM.")
        args.rom = args.rom.expanduser().resolve()
        if not args.rom.is_file():
            parser.error(f"ROM not found: {args.rom}")
        if args.rom.stat().st_size != 524288 or sha256(args.rom) != ROM_SHA256:
            parser.error("Unsupported ROM. Use the headerless F-Zero (USA) dump described in README.md.")
    check_prerequisites()
    prepare_submodules(args.skip_submodule_update)
    sdl, dxc = prepare_dependencies(args.jobs)
    if not args.skip_generate:
        run(sys.executable, ROOT / "snesrecomp/tools/v2_sync_funcs_h.py",
            "--cfg-dir", ROOT / "config", "--out", ROOT / "config/funcs.h")
        run(sys.executable, ROOT / "snesrecomp/tools/v2_emit.py", "--rom", args.rom,
            "--cfg-dir", ROOT / "config", "--out-dir", ROOT / "generated", "--cfg-roots",
            "--analysis-backend", "python")
    build = ROOT / "build"
    run("cmake", "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_PREFIX_PATH={sdl}", f"-DSDL3_DIR={sdl / 'lib/cmake/SDL3'}",
        f"-DFZERO_DXC={dxc}", "-DFZERO_LINUX_VULKAN=ON")
    run("cmake", "--build", build, "--parallel", str(args.jobs))
    print(f"\nBuilt: {build / 'fzero_recomp'}\n"
          f"Run: {shlex.quote(str(build / 'fzero_recomp'))}\n"
          f"Tests: ctest --test-dir {shlex.quote(str(build))} --output-on-failure")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
