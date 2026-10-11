# NX809J source build

The self-hosted workflow uses the portable build entry point and its fixed source
lock. Port fixes are committed in their owning repositories. It does not apply
device-tree mailbox patches or run mechanical source substitutions.

Install the portable build files on the runner, including `tools/nx809j`,
`build-inputs`, and `README.build.txt`. Configure `NX809J_PORT_SCRIPT` and
`NX809J_RELEASE_DIR` if their paths differ from the workflow defaults.
The Linux runner must have access to the private personal source imports.
Keep credentials in the runner's Git credential setup, outside the build package.

The optional synchronization step checks local changes, writes the fixed
manifest, synchronizes sources, and checks every locked revision. The build step
verifies committed features and prepares fixed binary dependencies before
building the system and boot images. Additional `dist` or custom targets run
after that verified base build. A source mismatch stops the workflow.

Android system partitions and boot packaging are built from the locked sources.
The GKI image, vendor, ODM base, and kernel drivers remain fixed binary inputs.
The ODM assembler installs the committed manifest and matching source-built
control and camera components, preserving OEM metadata.

The legacy signing and release packaging steps remain optional. Their static
vendor/ODM/DLKM paths must point to the reviewed inputs. The portable `super`
command prepares those inputs from the official 0823 package.

This workflow is dispatched manually; committing source fixes does not launch a
build or publish a firmware release. Deployment requires the verified
SuperFastboot mode and the project's image, slot, capacity, and backup checks.
