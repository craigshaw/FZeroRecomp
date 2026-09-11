"""Publication checks use invented identifiers and temporary Git repositories."""

from pathlib import PurePosixPath

import pytest

from tools.check_publication import content_violations, git, scan_repository, violation


@pytest.fixture
def repo(tmp_path):
    git(tmp_path, "init", "--quiet")
    return tmp_path


def write(repo, name, text):
    path = repo / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def test_private_paths_and_compound_extensions():
    for name in ("reference/input.txt", "build-debug/log.txt", "file.srm.bak",
                 "file.labels", "file.asm", "archive.tar.gz", "history.bundle"):
        assert violation(PurePosixPath(name)), name
    assert violation(PurePosixPath("src/main.c")) is None


def test_untracked_content_and_ignored_evidence(repo):
    write(repo, ".gitignore", "/evidence/\n")
    write(repo, "evidence/input.labels", "invented private input")
    write(repo, "docs/new.md", "Use reference/fixture.map for generation.\n")
    failures = scan_repository(repo)
    assert failures and all(f[0] == "docs/new.md" for f in failures)


def test_index_inspection_does_not_trust_clean_worktree(repo):
    path = write(repo, "docs/guide.md", "Run ingest_fixture.py.\n")
    git(repo, "add", "docs/guide.md")
    path.write_text("Build with the checked-in configuration.\n")
    assert not scan_repository(repo)
    assert scan_repository(repo, index=True)


def test_worktree_deletion_still_checked_in_index(repo):
    path = write(repo, "private.labels", "invented")
    git(repo, "add", "private.labels")
    path.unlink()
    assert not scan_repository(repo)
    assert scan_repository(repo, index=True)


def test_cfg_names_match_addresses_and_variants_are_optional(repo):
    path = write(repo, "config/bank04.cfg",
                 "bank = 04\nfunc bank_04_9234 9234 entry_mx:1,0\n"
                 "name 049300 bank_04_9300\n")
    assert not scan_repository(repo)
    for line in ("func InventedRoutine 9234", "func bank_04_9235 9234",
                 "name 049300 bank_05_9300", "symbol 049300 InventedData",
                 "func incomplete", "bank = 05"):
        path.write_text("bank = 04\n" + line + "\n")
        assert scan_repository(repo), line


def test_private_symbols_detect_variants_in_all_policy_files(repo):
    write(repo, "src/main.c", "void InventedPrivateEntry_M1X0(void);\n")
    write(repo, "tests/test_check_publication.py", "# InventedPrivateEntry\n")
    failures = scan_repository(repo, private_symbols={"InventedPrivateEntry"})
    assert {f[0] for f in failures} == {"src/main.c", "tests/test_check_publication.py"}
    assert all(f[2] == "private symbol match" for f in failures)
    assert "InventedPrivateEntry" not in str(failures)


def test_dependency_patch_exclusion_is_explicit(repo):
    write(repo, "patches/snesrecomp/0001-test.patch", "+# InventedPrivateEntry\n")
    symbols = {"InventedPrivateEntry"}
    assert scan_repository(repo, private_symbols=symbols)
    assert not scan_repository(repo, private_symbols=symbols, project_only=True)


def test_symlinks_are_rejected_without_following_them(repo):
    (repo / "link.c").symlink_to(repo / "missing-private-target")
    assert "symlink" in scan_repository(repo)[0][2]
    git(repo, "add", "link.c")
    assert "symlink" in scan_repository(repo, index=True)[0][2]


def test_symlink_replacing_a_tracked_parent_is_not_followed(repo, tmp_path):
    path = write(repo, "docs/guide.md", "safe\n")
    git(repo, "add", "docs/guide.md")
    path.unlink()
    path.parent.rmdir()
    path.parent.symlink_to(tmp_path / "missing-private-directory")
    assert "symlink" in scan_repository(repo)[0][2]


def test_stale_header_is_rejected_without_a_private_list(repo):
    path = write(repo, "config/funcs.h", "void InventedOldEntry(CpuState *cpu);\n")
    assert scan_repository(repo)
    path.write_text("void bank_04_9234(CpuState *cpu);\n"
                    "RecompReturn bank_04_9234_M1X0(CpuState *cpu);\n")
    assert not scan_repository(repo)


def test_text_rules_do_not_reject_ordinary_source_terminology():
    path = PurePosixPath("src/main.c")
    assert not content_violations(path, b"/* Source pixels retain their original colours. */", set())
    assert content_violations(path, b"/* $9234: lda #$12 */", set())
    assert content_violations(path, b"/* /Users/example/project */", set())
    assert content_violations(path, b"/* ld65 input */", set())
    assert content_violations(path, b"\xff", set())
    assert content_violations(path, b"prefix\0hidden", set())
