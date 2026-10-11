# Fixed Avium source build

The NX809J Avium port uses committed Nattsu39 source revisions. The portable
build package supplies `port_nx809j_avium.sh`, `tools/nx809j/build-sources.json`,
the source manifest generator, and input verification tools. Consult its
`README.build.txt` for setup and the prepare, sync, build, and super phases.

The lock fixes the Avium manifest, every base project, and each personal source
project to a full commit. Sync checks tracked changes before proceeding, retains
manifest file operations, and verifies every checkout afterward. Local source
changes must be committed and published before updating a fixed revision.

Persistent compatibility fixes, device resources, and product changes live in
the corresponding source repositories. The old patch directory and post-sync
replacement script have been removed after a successful ROM build and Super
assembly. The build entry no longer applies patches or rewrites source files.

Build with `SKIP_MINSDK=1`, as set by the portable entry and device CI. Keep the
upstream minimum SDK declarations and upstream prebuilt revisions. The Lineage
protobuf compatibility modules use distinct build names; their runtime library
names and 16 KiB binary variants remain unchanged.

MagicDesk and Shizuku are absent from product packages and RedMagicControl's
special application grants. The generic desktop RRO control remains available.

Android system images and the boot chain are built from source with the verified
portable GKI 6.12.23 input and committed camera GPIO DTBO. The official 0823
vendor/ODM/DLKM images remain required binary inputs. ODM assembly validates the
committed VINTF manifest, control configuration, compiled camera payload, and
filesystem metadata. This workflow does not deploy images to a device.
