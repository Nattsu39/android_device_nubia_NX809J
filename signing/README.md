# NX809J user release signing

The release entrypoint builds `lineage_NX809J-bp4a-user` and signs during the
build. It does not require target-files, an OTA package or a post-build re-sign.
The ROM uses the pinned personal 6.12.38/ReSukiSU Image from the kernel prebuilt
repository. Android's user variant does not remove ReSukiSU from that Image.

## Key provisioning

Private files live in `vendor/lineage-priv/keys`, outside the managed source
projects. Restrict the directory to the build account (`0700`), and private
files to `0600`. Back it up separately. Neither Git nor the portable build ZIP
may contain the private directory or its backup.

On another build machine, restore the original keys and run:

```sh
python3 device/nubia/NX809J/scripts/release_keys.py verify --source-tree "$PWD"
```

The public certificate fingerprints and AVB public-key digests are committed in
`public-keys.json`. This binds subsequent builds to the same signing identity.
Generating unrelated keys on a new machine will not pass that check.

For an explicitly authorized new signing identity, `release_keys.py generate`
creates missing pairs without replacing existing ones. `enroll --report PATH`
writes public fingerprints to a new file; review and commit that public record
before using the new identity. Build-time signing needs unencrypted private
keys, so access controls and a separate private backup are required.

## Coverage

`manifest.json` is the reviewed signing inventory:

- Eight base APK roles, 36 APEX container roles and 11 APEX-internal APK roles
  use independent certificates (55 certificate/private-key pairs).
- The 36 source APEX modules also have independent payload keys. Both halves of
  APEX signing are checked. The CAS APEX is covered in the build graph, although
  the hybrid ROM continues to retain its fixed vendor partition.
- `avb.pem` signs the top-level vbmeta; `vbmeta_system.pem` signs the system chain.
  The system chain keeps rollback location 2 and its existing rollback policy.
- Three original IMS APKs, Via, Starmoment and the upstream CTS shim retain their
  original signatures. Their exact file hashes and signer fingerprints are
  allowlisted; a changed file stops packaging pending review. The CTS shim is a
  deliberate upstream test fixture, not a private-key-signed product component.

`testkey.pk8` and `testkey.x509.pem` in the private directory are aliases of the
new release key for modules selecting that name explicitly. They are not the
public AOSP test key. The product's default certificate is `releasekey`.

`release.mk` rejects missing key files before Soong can fall back to module-local
APEX test keys. Explicit certificate modules are redirected through
`PRODUCT_CERTIFICATE_OVERRIDES`; the declarations in `keys.Android.bp` are
provisioned beside the private files. The common Lineage product excludes its
extra public recovery certificate in user builds. Mainline and Bluetooth
SELinux certificate paths also use the private directory, so platform,
SDK sandbox, Bluetooth, media, network stack and NFC retain their security
domain mappings after the certificate change.

## Verification and build boundaries

The portable entrypoint verifies private/public pairs, permissions and committed
fingerprints before compilation. After compilation it inspects installed APKs,
APEX containers, compressed inner containers, payloads and embedded APKs. Shared
system/phone UIDs must use the private platform certificate.

Before Super assembly, the same checker mounts the actual system/system_ext/
product EROFS images read-only and repeats the package checks. It verifies
user/release properties and both vbmeta signatures, then records image hashes
in `release-signing-report.json`. This phase requires root and the host EROFS
tools. New verification keys or image contents require another check.

The hybrid boot policy remains `vbmeta flags=3`. Custom signatures do not restore
an enforced OEM verified-boot trust chain and do not authorize relocking the
bootloader. Vendor/ODM/DLKM input ownership is unchanged. A clean user-data
installation and hardware regression testing are separate deployment steps.

Validation commands:

```sh
python3 device/nubia/NX809J/scripts/tests/test_release_keys.py
python3 device/nubia/NX809J/scripts/verify_release_graph.py --source-tree "$PWD"
```

The self-hosted workflow uses the same build and packaging entrypoint. Provision
keys directly on its trusted runner; private keys are never workflow artifacts.
