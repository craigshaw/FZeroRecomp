#!/usr/bin/env python3
"""Build an Apple Silicon release ZIP. Run after normal ROM code generation.

Usage: python3 tools/package_macos.py --version 0.1.0
Requires macOS, Python 3.11+, CMake, Ninja, Apple Command Line Tools and network
access for the checksum-pinned SDL source. Output stays under ignored build*/.
An existing output directory is never replaced. No ROM or user data is staged.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SDL_VERSION = "3.4.14"
SDL_SHA256 = "30d4aa2b3037718142b32dffd4e72f917ebb6cc5227150e7bb9c45efb2153aeb"
MIN_MACOS = "13.0"
ASSETS = (
    "fonts/LatoLatin-Regular.ttf", "fonts/LatoLatin-Bold.ttf",
    "fonts/NotoSansSymbols2-Regular.ttf", "fonts/OpenMoji-black-glyf.ttf",
    "img/boxart.png", "img/brand_mark.tga", "img/pad.tga",
    "img/verdict_ok.tga", "img/verdict_bad.tga", "img/verdict_warn.tga",
    "img/verdict_none.tga",
)


def run(*args: str | Path) -> None:
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True)


def output(*args: str | Path) -> str:
    return subprocess.check_output([str(a) for a in args], cwd=ROOT, text=True).strip()


def copy(source: Path, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, dest)


def dependencies(binary: Path) -> list[str]:
    return [line.strip().split(" (", 1)[0]
            for line in output("otool", "-L", binary).splitlines()[1:]]


def system_library(path: str) -> bool:
    return path.startswith(("/usr/lib/", "/System/Library/"))


def check_binary(binary: Path, bundled: str) -> None:
    if output("lipo", "-archs", binary) != "arm64":
        raise RuntimeError(f"Expected arm64: {binary}")
    commands = output("otool", "-l", binary)
    minimum = re.findall(r"\bminos (\d+(?:\.\d+)+)", commands)
    if not minimum or any(tuple(map(int, v.split('.'))) > (13, 0) for v in minimum):
        raise RuntimeError(f"Unexpected macOS deployment target: {minimum}")
    for dep in dependencies(binary):
        if not system_library(dep) and dep != bundled:
            raise RuntimeError(f"Unbundled dependency: {dep}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--sdl-archive", type=Path,
                        help="Use an already downloaded official SDL archive")
    parser.add_argument("--output", type=Path,
                        help="New output directory; defaults to build-release/vVERSION")
    args = parser.parse_args()
    if sys.platform != "darwin" or not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        parser.error("Run on macOS with a numeric version such as 0.1.0")
    if not list((ROOT / "generated").glob("bank*_v2.c")):
        parser.error("Generate game C first using the README instructions")
    release = (args.output or ROOT / "build-release" / f"v{args.version}").resolve()
    if release.exists():
        parser.error(f"Output already exists: {release}")
    work = ROOT / "build-macos-deps"
    work.mkdir(exist_ok=True)
    archive = args.sdl_archive or work / f"SDL3-{SDL_VERSION}.tar.gz"
    if not archive.exists():
        url = (f"https://github.com/libsdl-org/SDL/releases/download/"
               f"release-{SDL_VERSION}/SDL3-{SDL_VERSION}.tar.gz")
        urllib.request.urlretrieve(url, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SDL_SHA256:
        raise RuntimeError("SDL source checksum mismatch")
    source = work / f"SDL3-{SDL_VERSION}"
    if not source.exists():
        with tarfile.open(archive) as tar:
            # This archive is pinned above; still refuse paths outside work.
            for member in tar.getmembers():
                if not (work / member.name).resolve().is_relative_to(work):
                    raise RuntimeError("Unsafe archive member")
                if member.issym() or member.islnk():
                    raise RuntimeError("Unexpected archive link")
            tar.extractall(work)
    prefix = work / "install"
    common = ("-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
              f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MIN_MACOS}",
              "-DCMAKE_OSX_ARCHITECTURES=arm64")
    run("cmake", "-S", source, "-B", work / "sdl", *common,
        "-DSDL_STATIC=OFF", "-DSDL_TESTS=OFF", "-DSDL_TEST_LIBRARY=OFF",
        f"-DCMAKE_INSTALL_PREFIX={prefix}", f"-DCMAKE_C_FLAGS=-ffile-prefix-map={ROOT}=.")
    run("cmake", "--build", work / "sdl", "--parallel", "8")
    run("cmake", "--install", work / "sdl")
    build = ROOT / "build-macos"
    run("cmake", "-S", ROOT, "-B", build, *common,
        f"-DSDL3_DIR={prefix / 'lib/cmake/SDL3'}", "-DFZERO_MACOS_APP=ON",
        f"-DCMAKE_C_FLAGS=-ffile-prefix-map={ROOT}=.",
        f"-DCMAKE_CXX_FLAGS=-ffile-prefix-map={ROOT}=.")
    run("cmake", "--build", build, "--parallel", "8")
    run("ctest", "--test-dir", build, "--output-on-failure")

    app = release / "F-Zero Recomp.app"
    contents = app / "Contents"
    resources = contents / "Resources"
    executable = contents / "MacOS/fzero_recomp"
    library = contents / "Frameworks/libSDL3.0.dylib"
    copy(build / "fzero_recomp", executable)
    copy(prefix / "lib/libSDL3.0.dylib", library)
    for asset in ASSETS:
        copy(build / "assets" / asset, resources / "assets" / asset)
    notices = resources / "Licenses"
    for source_path, name in (
        (ROOT / "LICENSE", "FZeroRecomp.txt"),
        (ROOT / "docs/THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
        (ROOT / "snesrecomp/LICENSE", "snesrecomp.txt"),
        (ROOT / "snesrecomp/THIRD_PARTY_ATTRIBUTION.md", "snesrecomp-attribution.md"),
        (ROOT / "recomp-ui/LICENSE", "recomp-ui.txt"),
        (ROOT / "recomp-ui/src/third_party/imgui/LICENSE.txt", "Dear-ImGui.txt"),
        (ROOT / "assets/licenses/fonts.txt", "fonts.txt"),
        (prefix / "share/licenses/SDL3/LICENSE.txt", "SDL3.txt"),
    ):
        copy(source_path, notices / name)
    mit = (ROOT / "recomp-ui/src/third_party/imgui/LICENSE.txt").read_text()
    (notices / "Khronos.txt").write_text(
        "Copyright 2013-2020 The Khronos Group Inc.\n\n" +
        mit[mit.index("Permission is hereby granted"):])
    tiny = (ROOT / "recomp-ui/src/third_party/tinyfiledialogs.c").read_text()
    (notices / "tinyfiledialogs.txt").write_text(
        "Copyright (c) 2014 - 2025 Guillaume Vareille http://ysengrin.com\n\n" +
        tiny.split("- License -\n", 1)[1].split("\n\n", 1)[0] + "\n")
    stb = (ROOT / "recomp-ui/src/third_party/stb_image.h").read_text()
    (notices / "stb_image.txt").write_text(stb.split("ALTERNATIVE A - ", 1)[1].split("-----", 1)[0])
    (resources / "BuildInfo.json").write_text(json.dumps({
        "version": args.version, "commit": output("git", "rev-parse", "HEAD"),
        "dirty": bool(output("git", "status", "--porcelain")),
        "sdl_version": SDL_VERSION, "sdl_source_sha256": SDL_SHA256,
        "minimum_macos": MIN_MACOS, "architecture": "arm64",
    }, indent=2) + "\n")
    (resources / "Readme.txt").write_text(
        f"F-Zero Recomp {args.version}\n\n"
        "Move this app to Applications, open it, choose your F-Zero (USA) ROM, and press Play.\n"
        "Requires Apple Silicon and macOS 13 or newer. No ROM is included.\n"
        "If blocked, use System Settings > Privacy & Security > Open Anyway.\n"
        "https://support.apple.com/en-gb/102445\n\n"
        "Saves and settings: ~/Library/Application Support/FZeroRecomp/\n"
        "Quit before replacing the app to update. Your data stays in that folder.\n"
        "Instructions and source: https://github.com/craigshaw/FZeroRecomp\n"
        "Third-party licences are in the Licenses folder beside this file.\n"
        "Required Notice: Copyright (c) 2026 Craig Shaw\n"
        "Game artwork belongs to its respective rights holders and is not covered by the project licence.\n")
    with (contents / "Info.plist").open("wb") as f:
        plistlib.dump({
            "CFBundleExecutable": "fzero_recomp", "CFBundleName": "F-Zero Recomp",
            "CFBundleDisplayName": "F-Zero Recomp", "CFBundleIdentifier": "com.craigshaw.fzerorecomp",
            "CFBundlePackageType": "APPL", "CFBundleInfoDictionaryVersion": "6.0",
            "CFBundleShortVersionString": args.version, "CFBundleVersion": args.version,
            "LSMinimumSystemVersion": MIN_MACOS, "NSHighResolutionCapable": True,
            "LSApplicationCategoryType": "public.app-category.racing-games",
        }, f)
    bundled = "@executable_path/../Frameworks/libSDL3.0.dylib"
    for dep in dependencies(executable):
        if Path(dep).name.startswith("libSDL3"):
            run("install_name_tool", "-change", dep, bundled, executable)
    run("install_name_tool", "-id", bundled, library)
    commands = output("otool", "-l", executable)
    for rpath in re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", commands):
        run("install_name_tool", "-delete_rpath", rpath, executable)
    for binary in (library, executable):
        run("strip", "-x", binary)
        binary.chmod(0o755)
        check_binary(binary, bundled)
        run("codesign", "--force", "--sign", "-", binary)
    run("codesign", "--force", "--sign", "-", app)
    run("codesign", "--verify", "--deep", "--strict", app)
    # The pinned launcher's compatibility sidecars also probe the executable
    # directory. Keep it read-only so all persistent writes stay in user data.
    (contents / "MacOS").chmod(0o555)
    zip_path = release / f"FZeroRecomp-v{args.version}-macOS-arm64.zip"
    run("ditto", "-c", "-k", "--keepParent", "--norsrc", app, zip_path)
    digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    checksum = zip_path.with_suffix(".zip.sha256")
    checksum.write_text(f"{digest}  {zip_path.name}\n")
    print(f"\nRelease: {zip_path}\nSHA-256: {digest}")


if __name__ == "__main__":
    main()
