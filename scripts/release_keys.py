#!/usr/bin/env python3
"""Provision and validate private NX809J release keys without exposing them."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile


DEVICE = Path(__file__).resolve().parents[1]
MANIFEST = DEVICE / 'signing/manifest.json'
PUBLIC_KEYS = DEVICE / 'signing/public-keys.json'
SUBJECT = '/O=Nattsu39/OU=NX809J/CN=NX809J Release'


def command(*arguments: str) -> bytes:
    result = subprocess.run(arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise ValueError(f'{Path(arguments[0]).name} failed: {result.stderr.decode(errors="replace").strip()}')
    return result.stdout


def load_manifest() -> dict:
    manifest = json.loads(MANIFEST.read_text())
    if manifest['version'] != 1:
        raise ValueError('Unsupported signing manifest version')
    return manifest


def certificate_names(manifest: dict) -> list[str]:
    return sorted(set(manifest['apk_keys']) | {
        entry['container_key'] for entry in manifest['apexes'] + manifest.get('apk_overrides', [])
    })


def payload_names(manifest: dict) -> list[str]:
    return sorted({entry['payload_key'] for entry in manifest['apexes']})


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require_private(path: Path) -> None:
    status = path.stat()
    if status.st_uid != os.getuid() or stat.S_IMODE(status.st_mode) & 0o077:
        raise ValueError(f'Private key permissions must restrict access to the current user: {path}')


def check_pair_exists(paths: list[Path]) -> bool:
    present = [path.exists() for path in paths]
    if any(present) and not all(present):
        raise ValueError(f'Incomplete key pair preserved; restore from backup: {paths[0]}')
    return all(present)


def generate_certificate(directory: Path, name: str) -> None:
    destinations = [directory / f'{name}.pk8', directory / f'{name}.x509.pem']
    if check_pair_exists(destinations):
        return
    with tempfile.TemporaryDirectory(prefix='.new-certificate-', dir=directory) as temporary:
        work = Path(temporary)
        key = work / 'key.pem'
        command('openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:4096',
                '-out', str(key))
        command('openssl', 'req', '-new', '-x509', '-sha256', '-key', str(key),
                '-out', str(work / 'certificate.pem'), '-days', '10000', '-subj', SUBJECT)
        command('openssl', 'pkcs8', '-topk8', '-inform', 'PEM', '-outform', 'DER',
                '-in', str(key), '-out', str(work / 'key.pk8'), '-nocrypt')
        for source, destination in zip((work / 'key.pk8', work / 'certificate.pem'), destinations):
            source.chmod(0o600)
            source.replace(destination)


def generate_payload(directory: Path, name: str, bits: int, avbtool: Path) -> None:
    destinations = [directory / f'{name}.pem', directory / f'{name}.avbpubkey']
    if check_pair_exists(destinations):
        return
    with tempfile.TemporaryDirectory(prefix='.new-payload-', dir=directory) as temporary:
        work = Path(temporary)
        command('openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', f'rsa_keygen_bits:{bits}',
                '-out', str(work / 'key.pem'))
        command(str(avbtool), 'extract_public_key', '--key', str(work / 'key.pem'),
                '--output', str(work / 'key.avbpubkey'))
        for source, destination in zip((work / 'key.pem', work / 'key.avbpubkey'), destinations):
            source.chmod(0o600)
            source.replace(destination)


def generate(tree: Path, directory: Path, manifest: dict, group: str) -> None:
    os.umask(0o077)
    if directory.exists():
        require_private(directory)
    else:
        directory.mkdir(parents=True, mode=0o700)
    names = manifest['apk_keys'] if group == 'apk' else certificate_names(manifest)
    if group in ('apk', 'all'):
        for name in names:
            generate_certificate(directory, name)
    if group == 'all':
        avbtool = tree / 'external/avb/avbtool.py'
        for name in payload_names(manifest):
            generate_payload(directory, name, 4096, avbtool)
        for name, bits in manifest['avb_keys'].items():
            generate_payload(directory, name, bits, avbtool)
        # A few upstream modules explicitly select "testkey" by name. It is an
        # alias of our private default, never a copy of the public AOSP test key.
        for suffix in ('.pk8', '.x509.pem'):
            alias = directory / f'testkey{suffix}'
            expected = directory / f'releasekey{suffix}'
            if alias.exists() and alias.read_bytes() != expected.read_bytes():
                raise ValueError(f'Existing testkey alias is not the private release key: {alias}')
            if not alias.exists():
                shutil.copyfile(expected, alias)
                alias.chmod(0o600)
        configuration = {
            'Android.bp': (DEVICE / 'signing/keys.Android.bp').read_bytes(),
            'keys.mk': b'# Local key configuration; private files stay outside Git.\n'
                       b'ifeq ($(TARGET_BUILD_VARIANT),user)\n'
                       b'PRODUCT_DEFAULT_DEV_CERTIFICATE := vendor/lineage-priv/keys/releasekey\n'
                       b'endif\n',
            '.gitignore': b'*\n',
        }
        for name, content in configuration.items():
            target = directory / name
            if target.exists() and target.read_bytes() != content:
                raise ValueError(f'Existing key configuration differs; review it before replacing: {target}')
            if not target.exists():
                target.write_bytes(content)
                target.chmod(0o600)
    print(f'Provisioned {group} keys; existing key pairs were preserved')


def public_inventory(tree: Path, directory: Path, manifest: dict) -> dict:
    require_private(directory)
    result = {'version': 1, 'certificates': {}, 'payloads': {}, 'avb': {}}
    for name in certificate_names(manifest):
        key = directory / f'{name}.pk8'
        certificate = directory / f'{name}.x509.pem'
        require_private(key)
        public = command('openssl', 'pkey', '-inform', 'DER', '-in', str(key), '-pubout')
        certified = command('openssl', 'x509', '-in', str(certificate), '-pubkey', '-noout')
        if public != certified:
            raise ValueError(f'Certificate and private key do not match: {name}')
        der = command('openssl', 'x509', '-in', str(certificate), '-outform', 'DER')
        result['certificates'][name] = sha256(der)
    if len(set(result['certificates'].values())) != len(result['certificates']):
        raise ValueError('Independent signing roles unexpectedly reuse a certificate')
    for group, names in (('payloads', payload_names(manifest)), ('avb', manifest['avb_keys'])):
        for name in names:
            key = directory / f'{name}.pem'
            require_private(key)
            public = command(str(tree / 'external/avb/avbtool.py'), 'extract_public_key',
                             '--key', str(key), '--output', '-')
            if public != (directory / f'{name}.avbpubkey').read_bytes():
                raise ValueError(f'AVB public and private key do not match: {name}')
            result[group][name] = sha256(public)
    for suffix in ('.pk8', '.x509.pem'):
        if (directory / f'testkey{suffix}').read_bytes() != (directory / f'releasekey{suffix}').read_bytes():
            raise ValueError('testkey alias does not use the private release key')
    if (directory / 'Android.bp').read_bytes() != (DEVICE / 'signing/keys.Android.bp').read_bytes():
        raise ValueError('Private certificate module declarations are stale')
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('generate', 'enroll', 'verify'))
    parser.add_argument('--source-tree', type=Path, required=True)
    parser.add_argument('--keys-dir', type=Path)
    parser.add_argument('--group', choices=('apk', 'all'), default='all')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    manifest = load_manifest()
    tree = args.source_tree.resolve()
    directory = args.keys_dir or tree / manifest['key_directory']
    try:
        if args.action == 'generate':
            generate(tree, directory, manifest, args.group)
            return
        inventory = public_inventory(tree, directory, manifest)
        if args.action == 'enroll':
            if args.report is None or args.report.exists():
                raise ValueError('Enrollment requires a new --report path; existing trust records are never overwritten')
            args.report.write_text(json.dumps(inventory, indent=2) + '\n')
        elif inventory != json.loads(PUBLIC_KEYS.read_text()):
            raise ValueError('Release keys differ from the committed public fingerprints')
        print(f'Verified {len(inventory["certificates"])} certificate pairs, '
              f'{len(inventory["payloads"])} APEX payload pairs and {len(inventory["avb"])} AVB pairs')
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Release signing: {error}\n')


if __name__ == '__main__':
    main()
