#!/usr/bin/env python3
"""Reject user images with stale, unexpected or incomplete release signatures."""

import argparse
from contextlib import contextmanager, ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import xml.etree.ElementTree as ET
import zipfile

from release_keys import command, load_manifest, PUBLIC_KEYS, public_inventory, sha256


PARTITIONS = ('system', 'system_ext', 'product')
IMAGES = (*PARTITIONS, 'boot', 'init_boot', 'vendor_boot', 'dtbo',
          'vbmeta', 'vbmeta_system', 'recovery')


def file_hash(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read_properties(root: Path) -> dict:
    properties = {}
    for relative in ('build.prop', 'etc/build.prop', 'system/build.prop'):
        path = root / relative
        if path.is_file():
            for line in path.read_text().splitlines():
                if '=' in line and not line.startswith('#'):
                    name, value = line.split('=', 1)
                    properties[name] = value
    return properties


def check_properties(roots: dict[str, Path]) -> dict:
    combined = {}
    for partition, root in roots.items():
        properties = read_properties(root)
        for name in (f'ro.{partition}.build.type', f'ro.{partition}.build.tags'):
            expected = 'user' if name.endswith('.type') else 'release-keys'
            if properties.get(name) != expected:
                raise ValueError(f'{partition}: {name} is not {expected}')
        combined.update(properties)
    for name, expected in (('ro.build.type', 'user'), ('ro.build.tags', 'release-keys'),
                           ('ro.debuggable', '0'), ('ro.secure', '1'), ('ro.adb.secure', '1')):
        if combined.get(name) != expected:
            raise ValueError(f'{name}: expected {expected}, got {combined.get(name)!r}')
    return {name: combined[name] for name in ('ro.build.type', 'ro.build.tags',
                                             'ro.debuggable', 'ro.secure', 'ro.adb.secure')}


def check_mac_permissions(roots: dict[str, Path], public: dict) -> None:
    signers = set()
    for root in roots.values():
        for path in (root / 'etc/selinux').glob('*mac_permissions.xml'):
            policy = ET.parse(path)
            for signer in policy.findall('signer'):
                signature = signer.get('signature')
                label = signer.find('seinfo')
                if signature and label is not None:
                    signers.add((sha256(bytes.fromhex(signature)), label.get('value')))
    for key, label in (('platform', 'platform'), ('sdk_sandbox', 'sdk_sandbox'),
                       ('bluetooth', 'bluetooth'), ('media', 'media'),
                       ('networkstack', 'network_stack'), ('nfc', 'nfc')):
        if (public['certificates'][key], label) not in signers:
            raise ValueError(f'SELinux signer mapping is missing the release certificate: {key}')


@contextmanager
def mounted_image(tree: Path, image: Path):
    if os.geteuid() != 0:
        raise ValueError('Image verification requires root for read-only EROFS mounts')
    with tempfile.TemporaryDirectory(prefix='nx809j-release-image-') as temporary:
        directory = Path(temporary)
        mountpoint = directory / 'root'
        mountpoint.mkdir()
        with image.open('rb') as stream:
            sparse = stream.read(4) == b'\x3a\xff\x26\xed'
        if sparse:
            raw = directory / 'image.raw'
            command(str(tree / 'out/host/linux-x86/bin/simg2img'), str(image), str(raw))
        else:
            raw = image
        command('mount', '-t', 'erofs', '-o', 'ro,loop,nosuid,nodev,noexec', str(raw), str(mountpoint))
        try:
            system = mountpoint / 'system'
            yield system if (system / 'build.prop').is_file() and not (mountpoint / 'build.prop').exists() else mountpoint
        finally:
            command('umount', str(mountpoint))


class Verifier:
    def __init__(self, tree: Path, keys: Path, manifest: dict, public: dict):
        self.tree = tree
        self.keys = keys
        self.public = public
        self.apexes = {entry['filename']: entry for entry in manifest['apexes']}
        self.presigned = {entry['path']: entry for entry in manifest['presigned']}
        self.approved_certificates = set(public['certificates'].values())
        self.shared_uids = {}
        self.packages = []
        self.seen_exceptions = set()
        self.host = tree / 'out/host/linux-x86/bin'

    def signer(self, path: Path) -> list[str]:
        output = command(
            str(self.tree / 'prebuilts/jdk/jdk21/linux-x86/bin/java'), '-jar',
            str(self.tree / 'prebuilts/sdk/tools/linux/lib/apksigner.jar'),
            'verify', '--print-certs', str(path)).decode()
        signers = re.findall(r'certificate SHA-256 digest: ([0-9a-f]{64})', output)
        if not signers:
            raise ValueError(f'No verified signer: {path}')
        return signers

    def preserved(self, path: Path, relative: str, signers: list[str]) -> bool:
        exception = self.presigned.get(relative)
        if exception is None:
            return False
        if file_hash(path) != exception['sha256'] or signers != exception['signer_sha256']:
            raise ValueError(f'Pre-signed exception changed and must be reviewed: {relative}')
        self.seen_exceptions.add(relative)
        return True

    def apk(self, path: Path, relative: str) -> None:
        signers = self.signer(path)
        preserved = self.preserved(path, relative, signers)
        if not preserved and (len(signers) != 1 or signers[0] not in self.approved_certificates):
            raise ValueError(f'APK is not signed with a pinned release certificate: {relative}')
        manifest = command(str(self.host / 'aapt2'), 'dump', 'xmltree', '--file',
                           'AndroidManifest.xml', str(path)).decode()
        shared = re.findall(r'sharedUserId[^=]*="([^"]+)"', manifest)
        if shared:
            previous = self.shared_uids.setdefault(shared[0], signers)
            if previous != signers:
                raise ValueError(f'Shared UID certificate mismatch: {shared[0]}: {relative}')
            if shared[0] in ('android.uid.system', 'android.uid.phone'):
                if signers != [self.public['certificates']['platform']]:
                    raise ValueError(f'System/phone shared UID is not platform-signed: {relative}')
        self.packages.append({'path': relative, 'signer_sha256': signers, 'preserved': preserved})

    def apex(self, path: Path, relative: str) -> None:
        signers = self.signer(path)
        if self.preserved(path, relative, signers):
            self.packages.append({'path': relative, 'signer_sha256': signers, 'preserved': True})
            return
        entry = self.apexes.get(path.name.replace('.capex', '.apex'))
        if entry is None:
            raise ValueError(f'APEX has no reviewed release key mapping: {relative}')
        expected = [self.public['certificates'][entry['container_key']]]
        if signers != expected:
            raise ValueError(f'APEX container is not release-signed: {relative}')
        with tempfile.TemporaryDirectory(prefix='nx809j-release-apex-') as temporary:
            work = Path(temporary)
            original = path
            if path.suffix == '.capex':
                original = work / 'original.apex'
                with zipfile.ZipFile(path) as archive:
                    with archive.open('original_apex') as source, original.open('wb') as destination:
                        shutil.copyfileobj(source, destination)
                if self.signer(original) != expected:
                    raise ValueError(f'Compressed APEX inner container signature mismatch: {relative}')
            with zipfile.ZipFile(original) as archive:
                public_key = archive.read('apex_pubkey')
                if sha256(public_key) != self.public['payloads'][entry['payload_key']]:
                    raise ValueError(f'APEX payload still uses an unexpected key: {relative}')
                payload = work / 'apex_payload.img'
                with archive.open('apex_payload.img') as source, payload.open('wb') as destination:
                    shutil.copyfileobj(source, destination)
            avbtool = str(self.tree / 'external/avb/avbtool.py')
            information = command(avbtool, 'info_image', '--image', str(payload)).decode()
            options = ['--accept_zeroed_hashtree'] if re.search(r'Tree Size:\s+0 bytes', information) else []
            command(avbtool, 'verify_image', '--image', str(payload),
                    '--key', str(self.keys / f'{entry["payload_key"]}.pem'), *options)
            extracted = work / 'extracted'
            command(str(self.host / 'deapexer'), '--debugfs_path', str(self.host / 'debugfs_static'),
                    '--fsckerofs_path', str(self.host / 'fsck.erofs'),
                    'extract', str(original), str(extracted))
            for apk in sorted(extracted.rglob('*.apk')):
                self.apk(apk, f'{relative}!/{apk.relative_to(extracted).as_posix()}')
        self.packages.append({'path': relative, 'signer_sha256': signers,
                              'payload_key': entry['payload_key'], 'preserved': False})


def verify_avb(tree: Path, product: Path, keys: Path) -> None:
    tool = str(tree / 'external/avb/avbtool.py')
    command(tool, 'verify_image', '--image', str(product / 'vbmeta_system.img'),
            '--key', str(keys / 'vbmeta_system.pem'))
    command(tool, 'verify_image', '--image', str(product / 'vbmeta.img'),
            '--key', str(keys / 'avb.pem'), '--expected_chain_partition',
            f'vbmeta_system:2:{keys / "vbmeta_system.avbpubkey"}')
    information = command(tool, 'info_image', '--image', str(product / 'vbmeta.img')).decode()
    if re.search(r'^\s*Flags:\s+3\s*$', information, re.MULTILINE) is None:
        raise ValueError('The reviewed hybrid boot policy requires vbmeta flags=3')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-tree', type=Path, required=True)
    parser.add_argument('--product-out', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--verify-images', action='store_true',
                        help='Inspect APK/APEX bytes mounted from the final EROFS images (requires root)')
    args = parser.parse_args()
    try:
        manifest = load_manifest()
        tree = args.source_tree.resolve()
        product = args.product_out.resolve()
        keys = tree / manifest['key_directory']
        public = public_inventory(tree, keys, manifest)
        if public != json.loads(PUBLIC_KEYS.read_text()):
            raise ValueError('Private keys do not match the committed public fingerprints')
        verifier = Verifier(tree, keys, manifest, public)
        with ExitStack() as stack:
            roots = {name: stack.enter_context(mounted_image(tree, product / f'{name}.img'))
                     if args.verify_images else product / name for name in PARTITIONS}
            properties = check_properties(roots)
            check_mac_permissions(roots, public)
            for partition, root in roots.items():
                for path in sorted(root.rglob('*')):
                    if not path.is_file() or path.suffix not in ('.apk', '.apex', '.capex'):
                        continue
                    relative = f'{partition}/{path.relative_to(root).as_posix()}'
                    if path.suffix == '.apk':
                        verifier.apk(path, relative)
                    else:
                        verifier.apex(path, relative)
        if len(verifier.packages) < 100:
            raise ValueError('Incomplete product package inventory')
        missing = set(verifier.presigned) - verifier.seen_exceptions
        if missing:
            raise ValueError(f'Required preserved packages are missing: {sorted(missing)}')
        verify_avb(tree, product, keys)
        report = {
            'version': 1, 'scope': 'images' if args.verify_images else 'staging',
            'properties': properties, 'packages': verifier.packages,
            'images': {f'{name}.img': file_hash(product / f'{name}.img') for name in IMAGES},
            'avb_flags': 3,
            'verified_boot_enforced': False,
        }
        args.report.write_text(json.dumps(report, indent=2) + '\n')
        print(f'PASS: {report["scope"]}, {len(verifier.packages)} packages, user/release properties and AVB signatures')
    except (OSError, ValueError, KeyError, ET.ParseError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Release image verification failed: {error}\n')


if __name__ == '__main__':
    main()
