"""Stage, verify and archive a Linux x86_64 Release build without user data.

Run sh build.sh first. SDL is bundled; glibc, graphics drivers and desktop/audio
libraries remain system dependencies. No Git operations or ROM are required.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ASSETS = (
    "fonts/LatoLatin-Regular.ttf", "fonts/LatoLatin-Bold.ttf",
    "fonts/NotoSansSymbols2-Regular.ttf", "fonts/OpenMoji-black-glyf.ttf",
    "img/boxart.png", "img/brand_mark.tga", "img/pad.tga",
    "img/verdict_ok.tga", "img/verdict_bad.tga", "img/verdict_warn.tga",
    "img/verdict_none.tga",
)
SYSTEM_LIBS = {
    "libc.so.6", "libm.so.6", "libstdc++.so.6", "libgcc_s.so.1", "libGL.so.1",
    "libpthread.so.0", "libdl.so.2", "librt.so.1", "ld-linux-x86-64.so.2",
}


def output(*args: str | Path) -> str:
    return subprocess.check_output([str(a) for a in args], text=True,
                                   env=dict(os.environ, LC_ALL="C")).strip()


def cache_value(cache: str, name: str) -> str:
    match = re.search(rf"^{re.escape(name)}:[^=]+=(.+)$", cache, re.M)
    if not match:
        raise RuntimeError(f"Build cache is missing {name}")
    return match[1].strip()


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def inspect_elf(binary: Path, expected_runpath: str | None) -> dict:
    header = output("readelf", "-h", binary)
    if "ELF64" not in header or "Advanced Micro Devices X86-64" not in header:
        raise RuntimeError(f"Not a Linux x86_64 ELF binary: {binary}")
    dynamic = output("readelf", "-d", binary)
    paths = re.findall(r"\((?:RUNPATH|RPATH)\).*\[(.*?)\]", dynamic)
    if paths != ([expected_runpath] if expected_runpath else []):
        raise RuntimeError(f"Unexpected runtime library path in {binary.name}: {paths}")
    needed = re.findall(r"\(NEEDED\).*\[(.*?)\]", dynamic)
    unknown = set(needed) - SYSTEM_LIBS - {"libSDL3.so.0"}
    if unknown:
        raise RuntimeError(f"Unreviewed system dependencies in {binary.name}: {sorted(unknown)}")
    versions = sorted(set(re.findall(r"Name: ((?:GLIBCXX|GLIBC|CXXABI|GCC)_[0-9.]+)",
                                    output("readelf", "--version-info", binary))))
    return {"needed": needed, "symbol_versions": versions, "sha256": digest(binary)}


def notices(package: Path, sdl: Path) -> None:
    folder = package / "Licenses"
    for source, name in (
        (ROOT / "LICENSE", "FZeroRecomp.txt"),
        (ROOT / "docs/THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
        (ROOT / "snesrecomp/LICENSE", "snesrecomp.txt"),
        (ROOT / "snesrecomp/THIRD_PARTY_ATTRIBUTION.md", "snesrecomp-attribution.md"),
        (ROOT / "recomp-ui/LICENSE", "recomp-ui.txt"),
        (ROOT / "recomp-ui/src/third_party/imgui/LICENSE.txt", "Dear-ImGui.txt"),
        (ROOT / "assets/licenses/fonts.txt", "fonts.txt"),
        (sdl / "share/licenses/SDL3/LICENSE.txt", "SDL3.txt"),
    ):
        copy(source, folder / name)
    mit = (folder / "Dear-ImGui.txt").read_text()
    (folder / "Khronos.txt").write_text("Copyright 2013-2020 The Khronos Group Inc.\n\n" +
        mit[mit.index("Permission is hereby granted"):], encoding="utf-8")
    tiny = (ROOT / "recomp-ui/src/third_party/tinyfiledialogs.c").read_text()
    (folder / "tinyfiledialogs.txt").write_text(
        "Copyright (c) 2014 - 2025 Guillaume Vareille http://ysengrin.com\n\n" +
        tiny.split("- License -\n", 1)[1].split("\n\n", 1)[0] + "\n", encoding="utf-8")
    stb = (ROOT / "recomp-ui/src/third_party/stb_image.h").read_text()
    (folder / "stb_image.txt").write_text(
        stb.split("ALTERNATIVE A - ", 1)[1].split("-----", 1)[0], encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="For example 0.6.0")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--sdl-prefix", type=Path, required=True, help="SDL installation used by the build")
    parser.add_argument("--source-revision", default="unrecorded", help="Source commit or WIP label supplied by the builder")
    parser.add_argument("--output", type=Path, help="New output directory; existing paths are refused")
    args = parser.parse_args()
    if sys.platform != "linux" or platform.machine() != "x86_64":
        parser.error("Run on Linux x86_64")
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[A-Za-z0-9][A-Za-z0-9.-]*)?", args.version):
        parser.error("Use a version such as 0.6.0 or 0.6.0-rc.1")
    build, sdl = args.build_dir.resolve(), args.sdl_prefix.resolve()
    release = (args.output or ROOT / "build-release" / f"v{args.version}-linux-x64").resolve()
    if release.exists():
        parser.error(f"Output already exists: {release}")
    cache = (build / "CMakeCache.txt").read_text()
    if cache_value(cache, "CMAKE_BUILD_TYPE") != "Release":
        parser.error("Only Release builds can be packaged")
    if cache_value(cache, "FZERO_LINUX_VULKAN") != "ON":
        parser.error("The Linux package requires FZERO_LINUX_VULKAN=ON")
    # Match the SDL that the build actually loads, including lib/lib64 layouts.
    linked = re.search(r"libSDL3\.so\.0 => (.+?) \(", output("ldd", build / "fzero_recomp"))
    if not linked or not Path(linked[1]).resolve().is_relative_to(sdl):
        parser.error("--sdl-prefix must contain the SDL library used by this build")
    sdl_library = Path(linked[1]).resolve()
    subprocess.run([cache_value(cache, "CMAKE_CTEST_COMMAND"), "--test-dir", str(build),
                    "--output-on-failure"], check=True)

    release.parent.mkdir(parents=True, exist_ok=True)
    # Assemble only named runtime files in a fresh directory, then publish the
    # completed output directory atomically. Never copy a user's build folder.
    with tempfile.TemporaryDirectory(prefix=".fzero-package-", dir=release.parent) as directory:
        stage = Path(directory)
        package = stage / "F-Zero Recomp"
        subprocess.run(["cmake", "--install", str(build), "--config", "Release",
                        "--component", "FZeroRuntime", "--prefix", str(package)], check=True)
        copy(sdl_library, package / "lib/libSDL3.so.0")
        binaries = {
            "fzero_recomp": inspect_elf(package / "fzero_recomp", "$ORIGIN/lib"),
            "lib/libSDL3.so.0": inspect_elf(package / "lib/libSDL3.so.0", None),
        }
        requirements = {}
        for binary in binaries.values():
            for name in binary["symbol_versions"]:
                family, version = name.split("_", 1)
                if tuple(map(int, version.split("."))) > tuple(map(int, requirements.get(family, "0").split("."))):
                    requirements[family] = version
        for asset in ASSETS:
            copy(build / "assets" / asset, package / "assets" / asset)
        notices(package, sdl)
        (package / "Readme.txt").write_text(
            f"F-Zero Recomp {args.version} - Linux x86_64\n\n"
            "Extract the entire archive into a writable folder. Open a terminal there\n"
            "and run ./fzero_recomp, choose your F-Zero (USA) ROM, and press Play.\n"
            "No ROM is included. Keep lib/ and assets/ beside the executable.\n\n"
            f"Requires glibc {requirements.get('GLIBC', 'unknown')} or newer and a libstdc++ providing\n"
            f"GLIBCXX_{requirements.get('GLIBCXX', 'unknown')}. Tested on Ubuntu 24.04 x86_64.\n"
            "SDL is bundled. Use the system's graphics drivers, Vulkan loader, OpenGL,\n"
            "Wayland or X11, and audio libraries. A desktop session is required.\n"
            "Vulkan visual filters fall back to Original colours if unavailable.\n"
            "For Ubuntu, runtime packages include libstdc++6, libgl1, libvulkan1,\n"
            "libwayland-client0, libxkbcommon0, libasound2t64 and libpulse0, plus\n"
            "the graphics driver for your GPU. zenity supplies native file dialogs.\n"
            "No compiler, shader compiler or SDL development files are needed.\n\n"
            "F1 opens settings; arrow keys steer; Z accelerates; X uses a Super Jet.\n"
            "Enter is Start.\n"
            "Settings and saves are beside the executable: config.ini, keybinds.ini,\n"
            "rom.cfg, saves/save.srm and screenshots/. A custom save folder may be\n"
            "selected in the launcher and is recorded in config.ini.\n"
            "Quit before updating. Back up saves/ and settings, replace program files,\n"
            "and retain your personal files. Extract somewhere writable, not /usr.\n\n"
            "This package has not been validated on other distributions.\n"
            "Source: https://github.com/craigshaw/FZeroRecomp\n"
            "Third-party licences are in Licenses/.\n"
            "Required Notice: Copyright (c) 2026 Craig Shaw\n"
            "Game artwork belongs to its respective rights holders.\n", encoding="utf-8")
        sdl_header = (sdl / "include/SDL3/SDL_version.h").read_text()
        info = {
            "version": args.version, "architecture": "x86_64",
            "source_revision": args.source_revision,
            "compiler": output(cache_value(cache, "CMAKE_C_COMPILER"), "--version").splitlines()[0],
            "build_system": platform.freedesktop_os_release().get("PRETTY_NAME", "Linux"),
            "sdl_version": ".".join(re.search(rf"#define SDL_{part}_VERSION\s+(\d+)", sdl_header)[1]
                                     for part in ("MAJOR", "MINOR", "MICRO")),
            "minimum_symbol_versions": requirements, "elf": binaries,
            "shader_source_sha256": digest(ROOT / "src/shaders/scene.hlsl"),
            "shader_spirv_sha256": digest(build / "shaders/scene.spv"),
            "files_sha256": {str(p.relative_to(package)): digest(p)
                             for p in sorted(package.rglob("*")) if p.is_file()},
        }
        (package / "BuildInfo.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        subprocess.run([sys.executable, str(ROOT / "tests/test_package_linux.py"), str(package)], check=True)
        archive = stage / f"FZeroRecomp-v{args.version}-Linux-x64.tar.gz"
        with tarfile.open(archive, "w:gz", compresslevel=9) as tar:
            tar.add(package, arcname=package.name)
        checksum = digest(archive)
        archive.with_suffix(".gz.sha256").write_text(f"{checksum}  {archive.name}\n", encoding="ascii")
        stage.rename(release)
    print(f"\nLinux package: {release / archive.name}\nSHA-256: {checksum}")


if __name__ == "__main__":
    main()
