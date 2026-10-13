# NX809J self-hosted release build

The manually dispatched workflow uses the portable fixed-source entrypoint for
`lineage_NX809J-bp4a-user`, with the personal 6.12.38/ReSukiSU prebuilt kernel.
It signs during compilation and packages the verified partition images directly.
Target-files and OTA generation are not part of this workflow.

Before dispatch, provision the matching private keys on the trusted build account
and run the verifier documented in [signing/README.md](../signing/README.md).
The public trust fingerprints and signing policy are committed. Private keys are
neither GitHub secrets managed by this workflow nor release artifacts.

The runner needs the synchronized portable script, tools, binary inputs, the
reviewed 0823 firmware inputs and sufficient build/package space. Repository
variables can select those paths; all source revisions remain pinned by the
portable source lock. The runner needs root or configured non-interactive sudo
for read-only image verification and Super assembly.

The build rejects public test private-key dependencies, missing or mismatched
key pairs and changed pre-signed exceptions. Before publication it checks
APK/APEX signatures inside the actual system images and records image hashes.
The common Lineage public recovery test key is excluded from user builds.

Vendor, ODM and driver inputs retain the project's validated hybrid arrangement.
Custom AVB signatures retain `flags=3`; no claim of OEM verified-boot enforcement
or bootloader relocking is made. The workflow does not connect to or flash phones.
