"""Check project publication files without requiring private reference inputs.

Default: existing tracked files plus non-ignored untracked files in the working
tree. --index: exact staged blobs, including files deleted only from disk.
Submodule contents and Git history need a separate review.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_ROOTS = {
    "build",
    "captures",
    "evidence",
    "generated",
    "recordings",
    "reference",
    "replays",
    "saves",
    "screenshots",
    "traces",
    "workspaces",
}
FORBIDDEN_NAMES = {".DS_Store", "project.txt", "fzero_usa_reference.sfc"}
FORBIDDEN_SUFFIXES = {
    ".fig",
    ".jsonl",
    ".mmo",
    ".sfc",
    ".smc",
    ".srm",
    ".state",
    ".sqlite",
    ".asm",
    ".s",
    ".labels",
    ".map",
    ".dis",
    ".lst",
    ".bin",
    ".bundle",
    ".zip",
    ".tar",
    ".gz",
    ".7z",
}

TEXT_SUFFIXES = {".c", ".h", ".cpp", ".py", ".md", ".cfg", ".sh", ".ps1",
                 ".txt", ".json", ".toml", ".patch"}
# These two policy files contain invented examples of the generic signatures.
# Path checks, private-symbol checks, and encoding checks still apply to them.
POLICY_FILES = {"tools/check_publication.py", "tests/test_check_publication.py"}
CONTENT_RULES = (
    (re.compile(r"/Users/[^/\s]+/|[A-Za-z]:\\Users\\"), "local user path"),
    (re.compile(r"\b(?:ca65|ld65|as65c)\b|\b[\w-]+\.(?:labels|asm)\b", re.I),
     "reference asset or assembly tool reference"),
    (re.compile(r"\b(?:ingest_[\w]+\.py|auto-ingested|source-derived|source-aware)\b", re.I),
     "obsolete configuration workflow"),
    (re.compile(r"\breference/(?:generated\b|[\w.-]+\.(?:map|labels)\b)", re.I),
     "private reference input path"),
    (re.compile(r"(?:^|\s)\$[0-9a-f]{4,6}:\s*(?:lda|sta|ldx|ldy|jmp|jsr|jsl|jml|"
                r"rts|rtl|rti|sep|rep|stz|beq|bne|bra)\b", re.I),
     "addressed assembly excerpt"),
)


def git(root: Path, *args: str) -> bytes:
    return subprocess.check_output(["git", "--no-optional-locks", "-C", str(root), *args])


def violation(path: PurePosixPath) -> str | None:
    if path.name in FORBIDDEN_NAMES:
        return "private/generated filename"
    first = path.parts[0] if path.parts else ""
    if path.parts and (
        first in FORBIDDEN_ROOTS
        or (len(path.parts) > 1 and first.startswith("build"))
    ):
        return "private/generated directory"
    if any(suffix.lower() in FORBIDDEN_SUFFIXES for suffix in path.suffixes):
        return "private cartridge, state, or trace file type"
    if path.name.startswith("tier2_") and path.suffix == ".json":
        return "commercial-title tier-2 evidence"
    return None


def cfg_line_violation(path: PurePosixPath, line: str) -> str | None:
    match = re.fullmatch(r"bank([0-9a-fA-F]{2})\.cfg", path.name)
    if path.parent != PurePosixPath("config") or not match:
        return None
    tokens = line.split("#", 1)[0].split()
    if not tokens:
        return None
    if tokens[0] == "bank":
        try:
            if len(tokens) != 3 or tokens[1] != "=" or int(tokens[2], 16) != int(match[1], 16):
                return "cfg bank must match its filename"
        except ValueError:
            return "malformed cfg bank declaration"
        return None
    if tokens[0] == "symbol":
        return "optional symbol overlay is outside the compact cfg policy"
    if tokens[0] not in {"func", "name"}:
        return None
    try:
        if tokens[0] == "func":
            bank, pc, name = int(match[1], 16), int(tokens[2], 16), tokens[1]
            if not 0 <= pc <= 0xFFFF:
                raise ValueError
        else:
            address, name = int(tokens[1], 16), tokens[2]
            if not 0 <= address <= 0xFFFFFF:
                raise ValueError
            bank, pc = address >> 16, address & 0xFFFF
    except (ValueError, IndexError):
        return "malformed cfg name declaration"
    if name != f"bank_{bank:02X}_{pc:04X}":
        return "cfg name must match its bank and address"
    return None


def content_violations(path: PurePosixPath, data: bytes,
                       private_symbols: set[str]) -> list[tuple[int, str]]:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return [(0, "invalid UTF-8 in text file")] if path.suffix.lower() in TEXT_SUFFIXES else []
    if "\0" in text:
        return [(0, "binary content in text file")] if path.suffix.lower() in TEXT_SUFFIXES else []
    failures = []
    for number, line in enumerate(text.splitlines(), 1):
        reason = cfg_line_violation(path, line)
        if reason:
            failures.append((number, reason))
        if path == PurePosixPath("config/funcs.h"):
            declaration = re.search(r"\b(?:void|RecompReturn)\s+(\w+)\s*\(\s*CpuState\s*\*", line)
            if declaration and not re.fullmatch(r"bank_[0-9A-F]{2}_[0-9A-F]{4}(?:_M[01]X[01])?",
                                                declaration[1]):
                failures.append((number, "ROM declaration must use an address-based name"))
        if str(path) not in POLICY_FILES:
            for pattern, reason in CONTENT_RULES:
                if pattern.search(line):
                    failures.append((number, reason))
        if private_symbols:
            tokens = re.findall(r"\b[A-Za-z_][A-Za-z_0-9]*\b", line)
            if any(re.sub(r"_M[01]X[01]$", "", token) in private_symbols for token in tokens):
                # Report the location without copying private names into logs.
                failures.append((number, "private symbol match"))
    return failures


def scan_repository(root: Path, *, index: bool = False,
                    private_symbols: set[str] | None = None,
                    project_only: bool = False) -> list[tuple[str, int, str]]:
    entries = {}
    failures = []
    for record in git(root, "ls-files", "--stage", "-z").split(b"\0"):
        if not record:
            continue
        metadata, raw_path = record.split(b"\t", 1)
        mode, oid, stage = metadata.decode().split()
        name = raw_path.decode()
        if stage != "0":
            failures.append((name, 0, "unmerged index entry"))
        entries[name] = (mode, oid)
    if not index:
        for raw_path in git(root, "ls-files", "--others", "--exclude-standard", "-z").split(b"\0"):
            if raw_path:
                entries.setdefault(raw_path.decode(), ("", ""))
    for name, (mode, oid) in sorted(entries.items()):
        path = PurePosixPath(name)
        if mode == "160000":
            continue  # Dependency contents and history are a separate audit.
        if project_only and path.parts[:2] == ("patches", "snesrecomp"):
            continue
        file = root / name
        linked = not index and any((root.joinpath(*path.parts[:n])).is_symlink()
                                   for n in range(1, len(path.parts) + 1))
        if not index and not linked and not file.exists():
            continue  # A worktree deletion is not part of the proposed snapshot.
        reason = violation(path)
        if reason:
            failures.append((name, 0, reason))
            continue
        if (index and mode == "120000") or linked:
            failures.append((name, 0, "symlink requires separate publication review"))
            continue  # Never follow a link into private content.
        data = git(root, "cat-file", "blob", oid) if index else file.read_bytes()
        failures.extend((name, line, why) for line, why in
                        content_violations(path, data, private_symbols or set()))
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", action="store_true", help="inspect exact staged blobs")
    parser.add_argument("--project-only", action="store_true",
                        help="exclude dependency recovery patches for a scoped review")
    parser.add_argument("--private-symbols", type=Path,
                        help="private UTF-8 file with one reviewed identifier per line")
    args = parser.parse_args()
    symbols = set()
    if args.private_symbols:
        try:
            symbols = {line.strip() for line in args.private_symbols.read_text(encoding="utf-8").splitlines()
                       if line.strip() and not line.lstrip().startswith("#")}
        except (OSError, UnicodeError) as exc:
            parser.error(str(exc))
        if any(not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", s) for s in symbols):
            parser.error("private symbols must be identifiers, one per line")
    failures = scan_repository(ROOT, index=args.index, private_symbols=symbols,
                               project_only=args.project_only)
    scope = "project files, excluding dependency patches" if args.project_only else "repository files"
    print(f"Checked {scope} in {'index' if args.index else 'working tree'}; "
          "submodule contents and history excluded.")
    if failures:
        print("publication check failed:", file=sys.stderr)
        for path, line, reason in failures:
            print(f"  {path}{':' + str(line) if line else ''}: {reason}", file=sys.stderr)
        return 1
    print("publication check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
