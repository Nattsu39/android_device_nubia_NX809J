#!/usr/bin/env python3
"""Exercise release key safeguards using disposable real RSA key pairs."""

import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import release_keys
from verify_release_build import check_mac_permissions, check_properties, Verifier


class ReleaseKeyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tree = Path(os.environ.get('NX809_TEST_SOURCE_TREE', '/root/avium'))
        cls.temporary = tempfile.TemporaryDirectory(prefix='nx809j-key-tests-')
        cls.directory = Path(cls.temporary.name)
        cls.fixture = cls.directory / 'fixture'
        cls.manifest = {
            'apk_keys': ['releasekey', 'platform'],
            'apexes': [{'container_key': 'example', 'payload_key': 'example'}],
            'avb_keys': {'avb': 2048},
        }
        with contextlib.redirect_stdout(io.StringIO()):
            release_keys.generate(cls.tree, cls.fixture, cls.manifest, 'all')
        cls.public = release_keys.public_inventory(cls.tree, cls.fixture, cls.manifest)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.work = self.directory / self.id().split('.')[-1]
        shutil.copytree(self.fixture, self.work)
        self.work.chmod(0o700)

    def test_real_pairs_pass(self):
        self.assertEqual(release_keys.public_inventory(self.tree, self.work, self.manifest), self.public)

    def test_rerun_does_not_rotate_private_keys(self):
        before = {path.name: release_keys.sha256(path.read_bytes()) for path in self.work.iterdir()}
        with contextlib.redirect_stdout(io.StringIO()):
            release_keys.generate(self.tree, self.work, self.manifest, 'all')
        after = {path.name: release_keys.sha256(path.read_bytes()) for path in self.work.iterdir()}
        self.assertEqual(before, after)

    def test_partial_pair_is_preserved_and_rejected(self):
        (self.work / 'platform.x509.pem').unlink()
        original = (self.work / 'platform.pk8').read_bytes()
        with self.assertRaisesRegex(ValueError, 'Incomplete key pair'):
            release_keys.generate(self.tree, self.work, self.manifest, 'all')
        self.assertEqual((self.work / 'platform.pk8').read_bytes(), original)

    def test_mismatched_certificate_is_rejected(self):
        shutil.copyfile(self.work / 'releasekey.x509.pem', self.work / 'platform.x509.pem')
        with self.assertRaisesRegex(ValueError, 'do not match'):
            release_keys.public_inventory(self.tree, self.work, self.manifest)

    def test_readable_private_key_is_rejected(self):
        (self.work / 'platform.pk8').chmod(0o644)
        with self.assertRaisesRegex(ValueError, 'permissions'):
            release_keys.public_inventory(self.tree, self.work, self.manifest)

    def test_mismatched_apex_payload_is_rejected(self):
        shutil.copyfile(self.work / 'avb.avbpubkey', self.work / 'example.avbpubkey')
        with self.assertRaisesRegex(ValueError, 'public and private key do not match'):
            release_keys.public_inventory(self.tree, self.work, self.manifest)

    def test_changed_default_alias_is_rejected(self):
        shutil.copyfile(self.work / 'platform.pk8', self.work / 'testkey.pk8')
        with self.assertRaisesRegex(ValueError, 'testkey alias'):
            release_keys.public_inventory(self.tree, self.work, self.manifest)

    def test_unexpected_presigned_replacement_is_rejected(self):
        package = self.work / 'presigned.apk'
        package.write_bytes(b'changed upstream app')
        manifest = {'apexes': [], 'presigned': [{
            'path': 'product/app/example.apk', 'sha256': '0' * 64, 'signer_sha256': ['approved'],
        }]}
        verifier = Verifier(self.tree, self.work, manifest, self.public)
        with self.assertRaisesRegex(ValueError, 'exception changed'):
            verifier.preserved(package, 'product/app/example.apk', ['approved'])

    def test_userdebug_and_misleading_release_tags_are_rejected(self):
        roots = {}
        for name in ('system', 'system_ext', 'product'):
            root = self.work / name
            root.mkdir()
            (root / 'build.prop').write_text(f'ro.{name}.build.type=user\nro.{name}.build.tags=release-keys\n')
            roots[name] = root
        with (roots['system'] / 'build.prop').open('a') as stream:
            stream.write('ro.build.type=userdebug\nro.build.tags=release-keys\n'
                         'ro.debuggable=0\nro.secure=1\nro.adb.secure=1\n')
        with self.assertRaisesRegex(ValueError, 'ro.build.type'):
            check_properties(roots)

    def test_selinux_requires_each_release_signer(self):
        policy = self.work / 'etc/selinux'
        policy.mkdir(parents=True)
        roles = {'platform': 'platform', 'sdk_sandbox': 'sdk_sandbox',
                 'bluetooth': 'bluetooth', 'media': 'media',
                 'networkstack': 'network_stack', 'nfc': 'nfc'}
        certificates = {key: key.encode() for key in roles}
        public = {'certificates': {key: release_keys.sha256(value)
                                   for key, value in certificates.items()}}
        signers = ''.join(f'<signer signature="{certificates[key].hex()}">'
                          f'<seinfo value="{label}"/></signer>'
                          for key, label in roles.items())
        target = policy / 'plat_mac_permissions.xml'
        target.write_text(f'<policy>{signers}</policy>')
        check_mac_permissions({'system': self.work}, public)
        target.write_text(target.read_text().replace(
            certificates['bluetooth'].hex(), b'old public certificate'.hex()))
        with self.assertRaisesRegex(ValueError, 'bluetooth'):
            check_mac_permissions({'system': self.work}, public)


if __name__ == '__main__':
    unittest.main(verbosity=2)
