# NX809J camera compatibility

The OEM provider exposes rear logical camera `0` with physical IDs `2`, `1`,
and front camera `3`. Android's Camera1 logical-camera filtering consequently
hides the front camera. Sense uses its front Camera2 ID both as a Camera1 index
and in Camera2 availability callbacks. Filtering the group and overriding the
index alone fixes camera opening but leaves authentication waiting for a
different Camera2 ID.

The device tree builds an ODM library and an init override for the retained
OEM camera provider. The scoped preload wraps only registration of the standard
`vendor_qti/0` provider. The executable, service identity, scheduling, four AIDL
registrations, proprietary extensions, and vendor inputs are retained.
Provider, device, session, callback, offline, and injection adapters use the
frozen AIDL V3 interfaces implemented by this firmware.

The standard public IDs `1` and `3` are swapped. Public ID `1` becomes the front
camera and matches Camera1 index `1`; public ID `3` is the former rear auxiliary
camera `1`. The rear logical group's physical-ID list excludes the opposite
facing front camera and becomes public IDs `2`, `3`. Hidden physical devices
whose facing cannot be queried are retained; a logical group must keep at least
two physical devices. Malformed metadata is preserved.

The mapping covers device names, availability and torch callbacks, concurrent
queries, resource conflicts, physical characteristics, stream configurations,
physical capture settings and results, and standard active physical-ID metadata.
Input request queues and opaque vendor metadata are retained. Result callbacks
consume the HAL FMQ and write translated packets to a separate framework FMQ
of the same capacity. A full output queue uses standard inline metadata fallback.
Binder parcel copying preserves file descriptor ownership. No shared packet is
edited while a client can read it. Binder extensions and dumps are forwarded.

The SystemUI device overlay uses public ID `1` for the existing screen-under
camera protection; its position and shape are unchanged. CameraService, Sense,
and proprietary HAL source require no changes. Sense uses ordinary front-camera
discovery, without a camera-index property or an additional SELinux grant.
Applications should select cameras by their advertised facing.

The Super assembler merges the compiled library and source-matched init file
into the retained OEM ODM image, preserving existing files and labels. Missing,
stale, redirected, or malformed inputs are rejected. Revalidate the mapping
and AIDL ABI when changing the retained vendor firmware.

Build `nx809j-camera-metadata-test` and `nx809j-camera-provider-test`. The latter
reads real NX809J provider metadata and exercises local callback/session
delegators and actual FMQs; it never registers a service or opens a live camera.
For execution, place only the retained camera AIDL dependency libraries in a
dedicated test directory. The whole vendor library directory can shadow the
test's platform Binder library when used in `LD_LIBRARY_PATH`.

Deployment acceptance includes Camera1 indices and Camera2 facing, all four
camera previews, physical stream/result IDs, stock camera front/rear use, Sense
enrollment and lock-screen authentication, camera protection, and a normal
restart. Full application workflows using offline or injection sessions require
separate device validation in addition to focused adapter tests.
