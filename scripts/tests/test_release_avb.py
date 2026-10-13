#!/usr/bin/env python3
"""Exercise hybrid release verification using small, genuinely signed AVB images."""
import contextlib
import io
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release_keys import command, generate_payload
from verify_release_build import verify_avb

BOOT = ('boot', 'dtbo', 'init_boot', 'recovery', 'vendor_boot')
SYSTEM = ('system', 'system_ext', 'product')
RETAINED = ('odm', 'system_dlkm', 'vendor', 'vendor_dlkm')


class ReleaseAvbTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tree = Path(os.environ.get('NX809_TEST_SOURCE_TREE', '/root/avium'))
        cls.temporary = tempfile.TemporaryDirectory(prefix='nx809j-avb-tests-')
        cls.directory = Path(cls.temporary.name)
        cls.keys = cls.directory / 'keys'
        cls.keys.mkdir(mode=0o700)
        cls.tool = cls.tree / 'external/avb/avbtool.py'
        generate_payload(cls.keys, 'avb', 4096, cls.tool)
        generate_payload(cls.keys, 'vbmeta_system', 2048, cls.tool)
        cls.fixture = cls.directory / 'fixture'
        cls.fixture.mkdir()
        for name in BOOT + SYSTEM + RETAINED:
            image = cls.fixture / f'{name}.img'
            image.write_bytes((name.encode() + bytes(4096))[:4096])
            action = 'add_hash_footer' if name in BOOT else 'add_hashtree_footer'
            options = [] if name in BOOT else ['--do_not_generate_fec']
            command(str(cls.tool), action, '--image', str(image), '--partition_name', name,
                    '--partition_size', '131072', *options)
        system_arguments = []
        for name in SYSTEM:
            system_arguments.extend(['--include_descriptors_from_image',
                                     str(cls.fixture / f'{name}.img')])
        command(str(cls.tool), 'make_vbmeta_image', '--output', str(cls.fixture / 'vbmeta_system.img'),
                '--algorithm', 'SHA256_RSA2048', '--key', str(cls.keys / 'vbmeta_system.pem'),
                *system_arguments)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.product = self.directory / self.id().split('.')[-1]
        shutil.copytree(self.fixture, self.product)
        self.make_root()
        # These images come from fixed external firmware during Super assembly.
        # They are deliberately absent from the new source product output.
        for name in RETAINED:
            (self.product / f'{name}.img').unlink()

    def make_root(self, flags=3, location=2, root_key='avb', extra_partition=None):
        arguments = []
        for name in BOOT + RETAINED:
            arguments.extend(['--include_descriptors_from_image', str(self.fixture / f'{name}.img')])
        if extra_partition:
            image = self.product / 'unexpected.img'
            image.write_bytes(bytes(4096))
            command(str(self.tool), 'add_hashtree_footer', '--image', str(image),
                    '--partition_name', extra_partition, '--partition_size', '131072',
                    '--do_not_generate_fec')
            arguments.extend(['--include_descriptors_from_image', str(image)])
        command(str(self.tool), 'make_vbmeta_image', '--output', str(self.product / 'vbmeta.img'),
                '--algorithm', 'SHA256_RSA4096' if root_key == 'avb' else 'SHA256_RSA2048',
                '--key', str(self.keys / f'{root_key}.pem'), '--flags', str(flags),
                '--chain_partition', f'vbmeta_system:{location}:{self.keys / "vbmeta_system.avbpubkey"}',
                *arguments)

    def verify(self):
        with contextlib.redirect_stdout(io.StringIO()):
            verify_avb(self.tree, self.product, self.keys)

    def test_fixed_vendor_images_are_not_required_in_source_output(self):
        self.verify()

    def test_changed_boot_bytes_are_rejected(self):
        path = self.product / 'boot.img'
        with path.open('r+b') as stream:
            stream.write(b'bad!')
        with self.assertRaises(ValueError):
            self.verify()

    def test_changed_system_bytes_are_rejected(self):
        path = self.product / 'system.img'
        with path.open('r+b') as stream:
            stream.write(b'bad!')
        with self.assertRaises(ValueError):
            self.verify()

    def test_wrong_root_signing_key_is_rejected(self):
        self.make_root(root_key='vbmeta_system')
        with self.assertRaises(ValueError):
            self.verify()

    def test_tampered_root_signature_is_rejected(self):
        path = self.product / 'vbmeta.img'
        data = bytearray(path.read_bytes())
        data[256] ^= 1
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'signature verification failed'):
            self.verify()

    def test_unexpected_root_public_key_is_rejected(self):
        wrong_keys = self.product / 'wrong-keys'
        shutil.copytree(self.keys, wrong_keys)
        shutil.copyfile(wrong_keys / 'vbmeta_system.avbpubkey', wrong_keys / 'avb.avbpubkey')
        with self.assertRaisesRegex(ValueError, 'private release key'):
            verify_avb(self.tree, self.product, wrong_keys)

    def test_wrong_chain_location_is_rejected(self):
        self.make_root(location=3)
        with self.assertRaises(ValueError):
            self.verify()

    def test_wrong_hybrid_flags_are_rejected(self):
        self.make_root(flags=0)
        with self.assertRaises(ValueError):
            self.verify()

    def test_unreviewed_retained_partition_is_rejected(self):
        self.make_root(extra_partition='unexpected_vendor')
        with self.assertRaises(ValueError):
            self.verify()


if __name__ == '__main__':
    unittest.main(verbosity=2)
