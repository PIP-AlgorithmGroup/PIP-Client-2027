"""Exercise DEB metadata and filesystem behavior without installing into the host system."""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "package_linux", Path(__file__).resolve().parents[2] / "scripts/package_linux.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class LinuxPackageTest(unittest.TestCase):
    """Use disposable payloads; never touch /opt, /usr, user settings or dpkg's database."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.payload = self.root / "payload"
        (self.payload / "bin").mkdir(parents=True)
        (self.payload / "bin/PIPClient").write_text("fixture\n", encoding="utf-8")
        (self.payload / "bin/qt.conf").write_text("[Paths]\nPrefix=..\n", encoding="utf-8")

    def test_rejects_version_before_creating_output(self):
        with self.assertRaises(ValueError):
            PACKAGE.package_deb(self.payload, self.root / "output", "../invalid")
        self.assertFalse((self.root / "output").exists())

    def test_rejects_output_inside_payload(self):
        with self.assertRaises(ValueError):
            PACKAGE.package_deb(self.payload, self.payload / "output", "0.1.0-alpha.1")
        self.assertFalse((self.payload / "output").exists())

    @unittest.skipUnless(shutil.which("dpkg-deb"), "Requires Debian packaging tool")
    def test_archive_registers_desktop_and_preserves_payload(self):
        applications = self.payload / "share/applications"
        applications.mkdir(parents=True)
        (applications / "org.pip.PIPClient.desktop").write_text(
            "Exec=PIPClient\nTryExec=PIPClient\n", encoding="utf-8")
        (self.payload / "share/icons").mkdir()
        system_libraries = self.payload / "lib"
        system_libraries.mkdir()
        (system_libraries / "libva.so.2").write_text("system-library-fixture", encoding="utf-8")
        output = self.root / "output"
        PACKAGE.package_deb(self.payload, output, "0.1.0-alpha.1")
        archive = output / "PIPClient-0.1.0-alpha.1-Ubuntu-amd64.deb"
        version = subprocess.check_output(
            ["dpkg-deb", "-f", str(archive), "Version"], text=True).strip()
        self.assertEqual(version, "0.1.0~alpha.1")
        extracted = self.root / "extracted"
        subprocess.run(["dpkg-deb", "-x", str(archive), str(extracted)], check=True)
        self.assertEqual((extracted / "opt/pip-client/bin/PIPClient").read_text(), "fixture\n")
        launcher = (extracted / "usr/bin/pip-client").read_text()
        self.assertIn("exec /opt/pip-client/bin/PIPClient", launcher)
        self.assertIn("Exec=/opt/pip-client/bin/PIPClient", (
            extracted / "usr/share/applications/org.pip.PIPClient.desktop").read_text())
        self.assertTrue((applications / "org.pip.PIPClient.desktop").exists())
        self.assertFalse((extracted / "home").exists())
        self.assertFalse((extracted / "opt/pip-client/lib/libva.so.2").exists())
        self.assertTrue((system_libraries / "libva.so.2").exists())
        with self.assertRaises(FileExistsError):
            PACKAGE.package_deb(self.payload, output, "0.1.0-alpha.1")


if __name__ == "__main__":
    unittest.main()
