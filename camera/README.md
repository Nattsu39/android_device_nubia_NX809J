# NX809J camera metadata compatibility

The OEM provider exposes rear logical camera `0` with physical IDs `2`, `1`,
and front camera `3`. Android's unmodified Camera1 logical-camera filtering
therefore hides the front camera. Sense also passes its Camera2 ID to Camera1
unless its existing camera-index property is set.

This device tree builds an ODM library and an init service override. The
override retains the OEM executable, identity, scheduling, and all four AIDL
registrations. Its scoped preload wraps only the Android camera provider
registration. Generated AIDL delegators forward provider/device operations,
sessions, callbacks, errors, dumps, and Binder extensions to the OEM objects.

Only the returned `android.logicalMultiCamera.physicalIds` entry is adapted:
public physical devices with a different facing are excluded. Hidden physical
devices, unrelated metadata, and the OEM provider's internal metadata remain
unchanged. Malformed or unsupported metadata is preserved. At least two
physical devices must remain in a logical group. The adapter requires the
frozen provider/device AIDL V3 interfaces already used by this firmware.

The resulting Camera1 list is `0 -> ID0`, `1 -> ID3`. The system_ext property
`ro.face.sense_service.camera_id=1` selects that front index using Sense's
existing override. No CameraService, Sense, or proprietary HAL source change
is required. Revalidate this index if the retained vendor firmware changes.

The Super assembler merges the compiled library and source-matched init file
into the retained OEM ODM image. It preserves existing ODM content and labels,
and rejects missing, stale, redirected, or malformed inputs. Vendor and its
NDK libraries remain the retained firmware inputs.

Build `nx809j-camera-metadata-test` and `nx809j-camera-provider-test` to check
the adaptation. The provider test requires a running NX809J OEM provider and
reads metadata; it does not register a service or open a capture session.
Run it with the retained vendor libraries in `LD_LIBRARY_PATH` to verify the
actual frozen ABI. Deployment acceptance also requires Camera1 enumeration,
real Sense enrollment, front/rear preview, restart, and lock-screen tests.
