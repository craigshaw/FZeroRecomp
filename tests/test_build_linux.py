"""Failure and cache checks for the Linux build entry point. No ROM required."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "build-linux.py"
ABC_SHA256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"


class LinuxBuildTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.is_file(), "Linux build entry point is missing")
        spec = importlib.util.spec_from_file_location("build_linux", SCRIPT)
        self.build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.build)
        temporary = tempfile.TemporaryDirectory(prefix="fzero build test ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def run_helper(self, *args):
        checkout = self.root / "checkout with spaces"
        checkout.mkdir()
        shutil.copyfile(SCRIPT, checkout / SCRIPT.name)
        env = {k: v for k, v in os.environ.items() if k != "FZERO_ROM"}
        result = subprocess.run([sys.executable, checkout / SCRIPT.name, *args],
                                cwd=self.root, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse((checkout / ".tools").exists(), result.stdout)
        return result.stdout

    def test_wrong_rom_rejected_before_setup(self):
        rom = self.root / "Wrong ROM.sfc"
        rom.write_bytes(bytes(524288))
        output = self.run_helper("--rom", str(rom))
        self.assertIn("Unsupported ROM", output)
        self.assertNotIn("Traceback", output)

    def test_missing_rom_explains_how_to_supply_it(self):
        output = self.run_helper()
        self.assertIn("--rom", output)
        self.assertIn("FZERO_ROM", output)

    def test_skip_generate_requires_existing_output(self):
        output = self.run_helper("--skip-generate")
        self.assertIn("generated", output)
        self.assertIn("--rom", output)

    def test_zero_jobs_rejected(self):
        self.assertIn("positive", self.run_helper("--jobs", "0"))

    def test_old_python_is_reported_before_rom_hashing(self):
        rom = self.root / "Wrong ROM.sfc"
        rom.write_bytes(bytes(524288))
        with patch.object(sys, "version_info", (3, 10, 0)), \
                patch.object(sys, "argv", [str(SCRIPT), "--rom", str(rom)]):
            try:
                self.build.main()
            except (RuntimeError, SystemExit) as error:
                self.assertIsInstance(error, RuntimeError)
                self.assertIn("Python 3.11", str(error))
            else:
                self.fail("Unsupported Python version was accepted")

    def test_incompatible_libc_is_reported_before_setup(self):
        for version in (("glibc", "2.37"), ("musl", "1.2.5")):
            with self.subTest(version=version), \
                    patch.object(self.build.platform, "libc_ver", return_value=version):
                with self.assertRaisesRegex(RuntimeError, "glibc 2.38"):
                    self.build.check_prerequisites()

    def test_verified_download_is_reused_without_source(self):
        source = self.root / "download source"
        source.write_bytes(b"abc")
        target = self.root / "cache" / "archive.tar.gz"
        self.build.download(source.as_uri(), target, ABC_SHA256)
        self.assertEqual(target.read_bytes(), b"abc")
        source.unlink()
        self.build.download(source.as_uri(), target, ABC_SHA256)
        self.assertEqual(target.read_bytes(), b"abc")

    def test_corrupt_download_never_enters_cache(self):
        source = self.root / "source"
        source.write_bytes(b"wrong data")
        target = self.root / "cache" / "archive.tar.gz"
        with self.assertRaisesRegex(RuntimeError, "SHA-256"):
            self.build.download(source.as_uri(), target, ABC_SHA256)
        self.assertFalse(target.exists())
        self.assertEqual(list(target.parent.iterdir()), [])

    def test_corrupt_cache_is_refused_without_overwriting_it(self):
        target = self.root / "archive.tar.gz"
        target.write_bytes(b"corrupt cache")
        with self.assertRaisesRegex(RuntimeError, "SHA-256"):
            self.build.download((self.root / "missing").as_uri(), target, ABC_SHA256)
        self.assertEqual(target.read_bytes(), b"corrupt cache")

    def test_failed_download_leaves_no_cache_entry(self):
        target = self.root / "cache" / "archive.tar.gz"
        with self.assertRaises(OSError):
            self.build.download((self.root / "missing").as_uri(), target, ABC_SHA256)
        self.assertFalse(target.exists())
        self.assertEqual(list(target.parent.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
