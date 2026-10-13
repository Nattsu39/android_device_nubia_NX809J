#!/usr/bin/env python3
"""Check the real image dependency graph for fallback signing keys."""

import argparse
import json
from pathlib import Path
import subprocess

from release_keys import certificate_names, load_manifest, payload_names


TARGETS = ('systemimage', 'systemextimage', 'productimage', 'bootimage', 'initbootimage',
           'vendorbootimage', 'dtboimage', 'vbmetaimage', 'recoveryimage')


def verify(tree: Path) -> dict:
    manifest = load_manifest()
    prefix = manifest['key_directory'] + '/'
    variables = json.loads((tree / 'out/soong/soong.lineage_NX809J.variables').read_text())
    if variables['DefaultAppCertificate'] != prefix + 'releasekey':
        raise ValueError('The build graph does not select the private default certificate')
    for name in ('MainlineSepolicyDevCertificates', 'MainlineBluetoothSepolicyDevCertificates'):
        if variables.get(name, '').rstrip('/') != manifest['key_directory']:
            raise ValueError(f'SELinux signing identity is not aligned with the release keys: {name}')
    if variables.get('ExtraOtaRecoveryKeys') or variables.get('ExtraOtaKeys'):
        raise ValueError('Unexpected additional recovery/OTA trust keys')
    overrides = dict(value.split(':', 1) for value in variables['CertificateOverrides'])
    for entry in manifest['apexes'] + manifest.get('apk_overrides', []):
        if overrides.get(entry['module']) != 'nx809j_' + entry['container_key']:
            raise ValueError(f'Certificate override missing from the real graph: {entry["module"]}')

    allowed_private = {prefix + name + '.pk8' for name in certificate_names(manifest)}
    allowed_private.add(prefix + 'testkey.pk8')
    allowed_private.update(prefix + name + '.pem' for name in payload_names(manifest))
    allowed_private.update(prefix + name + '.pem' for name in manifest['avb_keys'])
    result = subprocess.run([
        str(tree / 'prebuilts/build-tools/linux-x86/bin/ninja'),
        '-f', 'out/combined-lineage_NX809J.ninja', '-t', 'inputs', *TARGETS,
    ], cwd=tree, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    inputs = set(result.stdout.splitlines())
    signing = set()
    for relative in inputs:
        if relative.endswith('.pk8'):
            signing.add(relative)
            if relative not in allowed_private:
                raise ValueError(f'Image dependency still uses an unapproved APK private key: {relative}')
        elif relative.endswith('.pem') and not relative.endswith('.x509.pem'):
            if relative in allowed_private:
                signing.add(relative)
            else:
                path = tree / relative
                if path.is_file():
                    with path.open('rb') as stream:
                        header = stream.read(128)
                    if b'PRIVATE KEY' in header:
                        raise ValueError(f'Image dependency still uses an unapproved AVB private key: {relative}')
    required = {prefix + name + '.pem' for name in manifest['avb_keys']}
    required.update(prefix + entry['payload_key'] + '.pem' for entry in manifest['apexes'])
    required.update(prefix + entry['container_key'] + '.pk8'
                    for entry in manifest['apexes'] + manifest.get('apk_overrides', []))
    if not required.issubset(signing):
        raise ValueError(f'Required signing inputs missing: {sorted(required - signing)}')
    return {'targets': TARGETS, 'private_signing_inputs': sorted(signing),
            'certificate_overrides': len(manifest['apexes']) + len(manifest.get('apk_overrides', [])),
            'extra_recovery_keys': variables.get('ExtraOtaRecoveryKeys', [])}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-tree', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        report = verify(args.source_tree.resolve())
        if args.report:
            args.report.write_text(json.dumps(report, indent=2) + '\n')
        print(f'PASS: {len(report["private_signing_inputs"])} private signing inputs, '
              f'{report["certificate_overrides"]} explicit overrides, no public test private keys')
    except (OSError, KeyError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Release signing graph: {error}\n')


if __name__ == '__main__':
    main()
