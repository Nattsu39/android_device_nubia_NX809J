#!/usr/bin/env python3
"""Check the retained-ODM merge seam, including stale and missing build outputs."""

import importlib.util
import os
from pathlib import Path
import tempfile
import struct
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "merge_control_configs.py"
SPEC = importlib.util.spec_from_file_location("control_configs", SCRIPT)
CONTROL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONTROL)
CONTEXT = b"u:object_r:vendor_configs_file:s0\0"
LIBRARY_CONTEXT = b"u:object_r:vendor_file:s0\0"


class ControlConfigTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.device = root / "device"
        self.compiled = root / "compiled"
        self.odm = root / "odm"
        for name, source in CONTROL.CONFIGS.items():
            content = f"on property:fixture.{name}=1\n    write /fixture 1\n".encode()
            for path in (self.device / source, self.compiled / "etc/init" / name):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
        self.reference = self.odm / "etc/init/init.NX809J.rc"
        self.reference.parent.mkdir(parents=True)
        self.reference.write_bytes(b"OEM init configuration\n")
        os.utime(self.reference, (0, 0))
        self.oem = self.odm / "lib64/camera.bin"
        self.oem.parent.mkdir()
        self.oem.write_bytes(b"retained OEM camera payload")
        self.library = self.compiled / "lib64" / CONTROL.CAMERA_LIBRARY
        self.library.parent.mkdir()
        library = bytearray(64)
        library[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<HH", library, 16, 3, 183)
        library.extend(b"AServiceManager_addService\0NX809JCameraCompat\0")
        self.library.write_bytes(library)
        # Host fixture boundaries cannot set Android security.selinux labels.
        # Production integration additionally verifies real mounted EROFS xattrs.
        self.get_label = patch.object(os, "getxattr", side_effect=lambda path, name:
                                      LIBRARY_CONTEXT if "lib64" in Path(path).parts else CONTEXT,
                                      create=True)
        self.set_label = patch.object(os, "setxattr", create=True)
        self.chown = patch.object(os, "chown", create=True)
        for mocked in (self.get_label, self.set_label, self.chown):
            mocked.start()
            self.addCleanup(mocked.stop)
        if os.name == "nt":
            self.skipTest("UID and Unix file mode validation requires Linux")

    def merge(self, verify_only=False):
        CONTROL.merge_configs(self.device, self.compiled, self.odm, verify_only)

    def test_missing_backend_is_rejected_by_final_verification(self):
        with self.assertRaisesRegex(ValueError, "Missing or redirected"):
            self.merge(verify_only=True)

    def test_merge_preserves_oem_payload_and_repeated_output(self):
        before = (self.reference.read_bytes(), self.oem.read_bytes())
        self.merge()
        self.merge(verify_only=True)
        self.merge()
        self.assertEqual(before, (self.reference.read_bytes(), self.oem.read_bytes()))
        self.assertEqual((self.odm / "lib64" / CONTROL.CAMERA_LIBRARY).read_bytes(),
                         self.library.read_bytes())
        for name in CONTROL.CONFIGS:
            path = self.odm / "etc/init" / name
            self.assertEqual(path.read_bytes(), (self.compiled / "etc/init" / name).read_bytes())
            self.assertEqual(path.stat().st_mtime, 0)

    def test_missing_compiled_file_stops_before_any_merge(self):
        (self.compiled / "etc/init/fp_uewake_arm.rc").unlink()
        with self.assertRaisesRegex(ValueError, "Missing or redirected"):
            self.merge()
        self.assertFalse((self.odm / "etc/init/redmagic_hw_arm.rc").exists())

    def test_stale_compiled_file_stops_before_any_merge(self):
        (self.compiled / "etc/init/fp_uewake_arm.rc").write_bytes(b"old build")
        with self.assertRaisesRegex(ValueError, "Stale compiled"):
            self.merge()
        self.assertFalse((self.odm / "etc/init/redmagic_hw_arm.rc").exists())

    def test_redirected_destination_is_preserved(self):
        path = self.odm / "etc/init/fp_uewake_arm.rc"
        path.symlink_to(self.oem)
        with self.assertRaisesRegex(ValueError, "Redirected destination"):
            self.merge()
        self.assertEqual(self.oem.read_bytes(), b"retained OEM camera payload")

    def test_corrupted_final_configuration_is_rejected(self):
        self.merge()
        (self.odm / "etc/init/fp_uewake_arm.rc").write_bytes(b"truncated")
        with self.assertRaisesRegex(ValueError, "verification failed"):
            self.merge(verify_only=True)

    def test_missing_camera_library_stops_before_any_merge(self):
        self.library.unlink()
        with self.assertRaisesRegex(ValueError, "Missing or redirected"):
            self.merge()
        self.assertFalse((self.odm / "etc/init/redmagic_hw_arm.rc").exists())

    def test_invalid_camera_library_stops_before_any_merge(self):
        self.library.write_bytes(b"not an AArch64 library")
        with self.assertRaisesRegex(ValueError, "Invalid compiled camera adapter"):
            self.merge()
        self.assertFalse((self.odm / "etc/init/redmagic_hw_arm.rc").exists())

    def test_redirected_camera_library_is_preserved(self):
        destination = self.odm / "lib64" / CONTROL.CAMERA_LIBRARY
        destination.symlink_to(self.oem)
        with self.assertRaisesRegex(ValueError, "Redirected destination"):
            self.merge()
        self.assertEqual(self.oem.read_bytes(), b"retained OEM camera payload")

    def test_corrupted_camera_library_is_rejected(self):
        self.merge()
        (self.odm / "lib64" / CONTROL.CAMERA_LIBRARY).write_bytes(b"truncated")
        with self.assertRaisesRegex(ValueError, "camera library verification failed"):
            self.merge(verify_only=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
