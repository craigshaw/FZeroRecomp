"""Stage, verify, and ZIP a native Windows Release build (no ROM or user data).

Run build.ps1 first, then python tools/package_windows.py --version 0.1.0.
Requires the build's Visual Studio tools and a GPU supporting the shader path.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ASSETS = (
    "fonts/LatoLatin-Regular.ttf", "fonts/LatoLatin-Bold.ttf",
    "fonts/NotoSansSymbols2-Regular.ttf", "fonts/OpenMoji-black-glyf.ttf",
    "img/boxart.png", "img/brand_mark.tga", "img/pad.tga",
    "img/verdict_ok.tga", "img/verdict_bad.tga", "img/verdict_warn.tga",
    "img/verdict_none.tga",
)
# Windows 10/11 components; never obtain these from the development toolchain.
SYSTEM_DLLS = {
    "kernel32.dll", "user32.dll", "gdi32.dll", "shell32.dll", "ole32.dll",
    "oleaut32.dll", "advapi32.dll", "comdlg32.dll", "comctl32.dll", "imm32.dll",
    "version.dll", "winmm.dll", "setupapi.dll", "cfgmgr32.dll", "hid.dll",
    "shlwapi.dll", "ws2_32.dll", "bcrypt.dll", "ntdll.dll", "ucrtbase.dll",
    "opengl32.dll", "dwmapi.dll", "d3d12.dll", "dxgi.dll", "dxguid.dll",
    "dinput8.dll", "powrprof.dll", "msvcrt.dll", "wintrust.dll", "crypt32.dll",
}


def output(*args: str | Path) -> str:
    return subprocess.check_output([str(a) for a in args], cwd=ROOT, text=True).strip()


def copy(source: Path, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, dest)


def cache_value(cache: str, name: str) -> str:
    match = re.search(rf"^{re.escape(name)}:[^=]+=(.+)$", cache, re.M)
    if not match:
        raise RuntimeError(f"Build cache is missing {name}")
    return match[1].strip()


def dependencies(dumpbin: Path, binary: Path) -> list[str]:
    return re.findall(r"^\s+([\w.-]+\.dll)\s*$", output(dumpbin, "/dependents", binary), re.M | re.I)


def system_dll(name: str) -> bool:
    return name.lower() in SYSTEM_DLLS or name.lower().startswith(("api-ms-win-", "ext-ms-win-"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/windows-msvc-x64-release")
    parser.add_argument("--output", type=Path, help="New release output directory")
    args = parser.parse_args()
    if sys.platform != "win32" or not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        parser.error("Run on Windows with a numeric version such as 0.1.0")
    build = args.build_dir.resolve()
    release = (args.output or ROOT / "build-release" / f"v{args.version}-windows-x64").resolve()
    if release.exists():
        parser.error(f"Output already exists: {release}")
    cache = (build / "CMakeCache.txt").read_text()
    if cache_value(cache, "CMAKE_BUILD_TYPE") != "Release":
        parser.error("Only Release builds can be packaged")
    compiler = Path(cache_value(cache, "CMAKE_C_COMPILER"))
    dumpbin = compiler.with_name("dumpbin.exe")
    vc = next(p for p in compiler.parents if p.name == "VC")
    redist_version = (vc / "Auxiliary/Build/Microsoft.VCRedistVersion.default.txt").read_text().strip()
    crt_dirs = list((vc / "Redist/MSVC" / redist_version / "x64").glob("Microsoft.VC*.CRT"))
    if len(crt_dirs) != 1:
        raise RuntimeError("Could not identify the installed x64 MSVC redistributable")
    vcpkg = Path(cache_value(cache, "VCPKG_INSTALLED_DIR")) / "x64-windows"
    ctest = cache_value(cache, "CMAKE_CTEST_COMMAND")
    subprocess.run([ctest, "--test-dir", str(build), "--output-on-failure"], check=True)
    gpu_dir = build / "host-ui-gpu-test-data"
    gpu_dir.mkdir(exist_ok=True)
    gpu_env = dict(os.environ, FZERO_TEST_GPU="1", SDL_AUDIODRIVER="dummy")
    gpu_env.pop("SDL_VIDEODRIVER", None)
    subprocess.run([str(build / "fzero_host_ui_tests.exe")], cwd=gpu_dir,
                   env=gpu_env, check=True, timeout=120)

    package = release / "F-Zero Recomp"
    package.mkdir(parents=True)
    copy(build / "fzero_recomp.exe", package / "fzero_recomp.exe")
    # Resolve the complete import graph from release dependencies and the official CRT.
    pending = [package / "fzero_recomp.exe"]
    visited = set()
    bundled = {}
    while pending:
        binary = pending.pop()
        if binary.name.lower() in visited:
            continue
        visited.add(binary.name.lower())
        if not re.search(r"\b8664 machine \(x64\)", output(dumpbin, "/headers", binary), re.I):
            raise RuntimeError(f"Not an x64 binary: {binary}")
        for name in dependencies(dumpbin, binary):
            if system_dll(name):
                continue
            source = next((folder / name for folder in (vcpkg / "bin", crt_dirs[0])
                           if (folder / name).is_file()), None)
            if source is None:
                raise RuntimeError(f"Unresolved distributable dependency: {binary.name}: {name}")
            destination = package / name
            if not destination.exists():
                copy(source, destination)
            bundled[name] = hashlib.sha256(destination.read_bytes()).hexdigest()
            pending.append(destination)
    for asset in ASSETS:
        copy(build / "assets" / asset, package / "assets" / asset)
    notices = package / "Licenses"
    for source, name in (
        (ROOT / "LICENSE", "FZeroRecomp.txt"),
        (ROOT / "docs/THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
        (ROOT / "snesrecomp/LICENSE", "snesrecomp.txt"),
        (ROOT / "snesrecomp/THIRD_PARTY_ATTRIBUTION.md", "snesrecomp-attribution.md"),
        (ROOT / "recomp-ui/LICENSE", "recomp-ui.txt"),
        (ROOT / "recomp-ui/src/third_party/imgui/LICENSE.txt", "Dear-ImGui.txt"),
        (ROOT / "assets/licenses/fonts.txt", "fonts.txt"),
        (vcpkg / "share/sdl3/copyright", "SDL3.txt"),
        (vc.parent / "Licenses/1033/Redist.txt", "Microsoft-Redist.txt"),
    ):
        copy(source, notices / name)
    mit = (ROOT / "recomp-ui/src/third_party/imgui/LICENSE.txt").read_text()
    (notices / "Khronos.txt").write_text("Copyright 2013-2020 The Khronos Group Inc.\n\n" +
        mit[mit.index("Permission is hereby granted"):], encoding="utf-8")
    tiny = (ROOT / "recomp-ui/src/third_party/tinyfiledialogs.c").read_text()
    (notices / "tinyfiledialogs.txt").write_text(
        "Copyright (c) 2014 - 2025 Guillaume Vareille http://ysengrin.com\n\n" +
        tiny.split("- License -\n", 1)[1].split("\n\n", 1)[0] + "\n", encoding="utf-8")
    stb = (ROOT / "recomp-ui/src/third_party/stb_image.h").read_text()
    (notices / "stb_image.txt").write_text(
        stb.split("ALTERNATIVE A - ", 1)[1].split("-----", 1)[0], encoding="utf-8")
    (package / "Readme.txt").write_text(
        f"F-Zero Recomp {args.version} - Windows x64\n\n"
        "Extract the entire ZIP into a writable folder, open fzero_recomp.exe,\n"
        "choose your F-Zero (USA) ROM, and press Play. No ROM is included.\n"
        "Requires Windows 10/11 x64. Visual filters require a Direct3D 12 GPU\n"
        "with Shader Model 6.0 support. Other renderers use Original colours.\n"
        "SDL and the Visual C++ runtime are included. No development tools are needed.\n\n"
        "F1 opens settings; arrow keys steer; X accelerates; Enter is Start.\n"
        "Settings and saves are beside the executable: config.ini, keybinds.ini,\n"
        "rom.cfg, and saves/save.srm. Keep the whole folder writable.\n"
        "Quit before updating. Back up saves/ and your settings, then replace\n"
        "program files with the new release, preserving those personal files.\n\n"
        "Source and instructions: https://github.com/craigshaw/FZeroRecomp\n"
        "Third-party licences are in Licenses/.\n"
        "Required Notice: Copyright (c) 2026 Craig Shaw\n"
        "Game artwork belongs to its respective rights holders.\n", encoding="utf-8")
    (package / "BuildInfo.json").write_text(json.dumps({
        "version": args.version, "architecture": "x64", "minimum_windows": "10",
        "commit": output("git", "rev-parse", "HEAD"),
        "dirty": bool(output("git", "status", "--porcelain")),
        "snesrecomp": output("git", "-C", ROOT / "snesrecomp", "rev-parse", "HEAD"),
        "recomp_ui": output("git", "-C", ROOT / "recomp-ui", "rev-parse", "HEAD"),
        "vcpkg_baseline": json.loads((ROOT / "vcpkg.json").read_text())["builtin-baseline"],
        "sdl_version": ".".join(re.search(rf"#define SDL_{part}_VERSION\s+(\d+)",
            (vcpkg / "include/SDL3/SDL_version.h").read_text())[1]
            for part in ("MAJOR", "MINOR", "MICRO")),
        "shader_source_sha256": hashlib.sha256((ROOT / "src/shaders/scene.hlsl").read_bytes()).hexdigest(),
        "shader_dxil_sha256": hashlib.sha256((build / "shaders/scene.dxil").read_bytes()).hexdigest(),
        "bundled_dll_sha256": bundled,
    }, indent=2) + "\n", encoding="utf-8")
    # Exercise the staged program with only Windows on PATH. The rejection test
    # copies its DLLs to a separate temp directory and requires the expected error.
    clean_env = dict(os.environ, PATH=str(Path(os.environ["SystemRoot"]) / "System32"))
    subprocess.run([sys.executable, str(ROOT / "tests/test_rom_rejection.py"),
                    str(package / "fzero_recomp.exe")], env=clean_env, check=True)
    archive = release / f"FZeroRecomp-v{args.version}-Windows-x64.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(package.rglob("*")):
            if path.is_file():
                z.write(path, path.relative_to(release))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
    print(f"\nRelease: {archive}\nSHA-256: {digest}")


if __name__ == "__main__":
    main()
