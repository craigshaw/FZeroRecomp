"""Exercise recovery with overlapping patches and clean integration pins."""

from pathlib import Path
import re
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]


def git(repo, *args):
    if args[0] == "commit":
        args += ("--trailer", "Co-authored-by: Codex <codex@openai.com>")
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


@pytest.fixture
def recovery(tmp_path):
    dependency = tmp_path / "snesrecomp"
    dependency.mkdir()
    git(dependency, "init", "-q")
    git(dependency, "config", "user.name", "Synthetic Test")
    git(dependency, "config", "user.email", "test@example.invalid")
    source = dependency / "value.txt"
    source.write_text("base\n")
    git(dependency, "add", "value.txt")
    git(dependency, "commit", "-qm", "Synthetic base")
    base = git(dependency, "rev-parse", "HEAD")
    patches = tmp_path / "patches" / "snesrecomp"
    patches.mkdir(parents=True)
    for number, value in enumerate(("first", "second"), 1):
        source.write_text(value + "\n")
        patch = git(dependency, "diff") + "\n"
        (patches / f"{number:04d}.patch").write_text(patch, newline="\n")
        git(dependency, "commit", "-qam", f"Synthetic patch {number}")
    integrated = git(dependency, "rev-parse", "HEAD")
    script = (ROOT / "tools/apply_snesrecomp_patches.sh").read_text()
    script = re.sub(r"^EXPECTED_BASE=.*$", f"EXPECTED_BASE={base}", script, flags=re.M)
    script = re.sub(r"^INTEGRATED_REVISION=.*$", f"INTEGRATED_REVISION={integrated}",
                    script, flags=re.M)
    target = tmp_path / "tools" / "apply_snesrecomp_patches.sh"
    target.parent.mkdir()
    target.write_text(script)
    return dependency, base, target


def run(script):
    return subprocess.run(["sh", str(script)], text=True, capture_output=True)


def test_recovery_and_repeated_run_with_overlapping_hunks(recovery):
    dependency, base, script = recovery
    git(dependency, "checkout", "--detach", base)
    index_before = git(dependency, "write-tree")
    first = run(script)
    assert first.returncode == 0, first.stderr
    assert (dependency / "value.txt").read_text() == "second\n"
    second = run(script)
    assert second.returncode == 0, second.stderr
    assert "already applied" in second.stdout
    assert git(dependency, "write-tree") == index_before


def test_integrated_checkout_requires_clean_state(recovery):
    dependency, _, script = recovery
    assert run(script).returncode == 0
    (dependency / "value.txt").write_text("unexpected edit\n")
    result = run(script)
    assert result.returncode != 0
    assert "local edits" in result.stderr


def test_recovery_rejects_unrelated_tracked_edits(recovery):
    dependency, base, script = recovery
    git(dependency, "checkout", "--detach", base)
    (dependency / "value.txt").write_text("unexpected edit\n")
    result = run(script)
    assert result.returncode != 0
    assert (dependency / "value.txt").read_text() == "unexpected edit\n"


def test_unexpected_revision_is_rejected(recovery):
    dependency, _, script = recovery
    git(dependency, "commit", "--allow-empty", "-qm", "Unexpected revision")
    result = run(script)
    assert result.returncode != 0
    assert "unexpected snesrecomp base" in result.stderr
