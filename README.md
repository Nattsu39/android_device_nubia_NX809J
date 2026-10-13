# NX809J / AviumUI 16.2 device tree

Device configuration for the Nubia RedMagic 11 Pro (`NX809J`, `canoe`, SM8850).
The personal branch builds AviumUI Android 16 userspace and its boot images,
then combines them with the project's fixed vendor, ODM and driver inputs.

## Current build configuration

- Product: `lineage_NX809J-bp4a-user`.
- Kernel: the verified personal 6.12.38/ReSukiSU Image from
  [android_kernel_nubia_NX809J](https://github.com/Nattsu39/android_kernel_nubia_NX809J/tree/nx809j-avium-16.2).
  The prebuilt repository records the source commits and successful build run.
- Signing: separately provisioned private APK/APEX/AVB keys. See
  [signing/README.md](signing/README.md) for the pinned public fingerprints,
  coverage, preserved pre-signed packages and verification commands.
- SELinux starts enforcing. The hybrid AVB policy retains `vbmeta flags=3`.

The user/release configuration is a build migration. Previous userdebug hardware
results do not constitute validation of a newly built user image or a clean-data
installation. Fingerprint/face enrollment and other hardware regressions require
their own deployment checks.

## Building

Use the NX809J portable build entrypoint and its fixed source lock. It verifies
source revisions, private-key fingerprints, the kernel/DTBO pairing and the
image signing dependency graph before producing release artifacts. It also
preserves the stock first-stage modules and SELinux labels when assembling Super.

For configuration-only validation from an already prepared source tree:

```sh
source build/envsetup.sh
lunch lineage_NX809J-bp4a-user
m nothing
python3 device/nubia/NX809J/scripts/verify_release_graph.py --source-tree "$PWD"
```

`bp4a` is the required release configuration. The release entrypoint generates
system/system_ext/product and a matching boot chain, verifies the signatures,
and assembles the hybrid Super. It does not need an OTA package.

Restore the existing private signing identity when moving to another machine.
Private keys do not belong in this repository or in the portable build archive.
The self-hosted workflow follows the same entrypoint and requires locally
provisioned keys; see [.github/README-CI.md](.github/README-CI.md).

## Deployment boundaries

Use the installed **SuperFastboot** and the project's partition, slot and image
checks. The original ABL fastboot cannot flash this device. Do not replace the
validated vendor/firmware baseline, flash ABL/EFISP to enter a mode, erase factory
calibration partitions or relock the bootloader as part of this migration.

A clean user-data installation avoids migrating the previous platform signing
identity. The original per-device calibration on `persist` must be retained.

## Credits

Initial NX809J device bring-up by IronShing, using ZTE/Nubia vendor components.
The Nattsu39 branch maintains the AviumUI integration, committed device fixes,
fixed personal kernel input and reproducible release build configuration.
