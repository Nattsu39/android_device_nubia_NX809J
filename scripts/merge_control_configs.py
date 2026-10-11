#!/usr/bin/env python3
"""Merge source-built control and camera compatibility assets into OEM ODM."""

import argparse
import hashlib
import os
from pathlib import Path
import runpy
import stat
import struct
import sys


CONFIGS = {
    "redmagic_hw_arm.rc": "hwlevels/redmagic_hw_arm.rc",
    "fp_uewake_arm.rc": "fp_uewake/fp_uewake_arm.rc",
    "dt2w_uewake_arm.rc": "dt2w/dt2w_uewake_arm.rc",
    "nx809j-camera-provider.rc": "camera/nx809j-camera-provider.rc",
}
LABEL = "security.selinux"
CAMERA_LIBRARY = "libnx809j_camera_provider.so"
CAMERA_EXECUTABLE = "nx809j-camera-provider"
CAMERA_EXECUTABLE_CONTEXT = b"u:object_r:hal_camera_default_exec:s0"


def regular_file(path: Path) -> Path:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Missing or redirected control configuration: {path}")
    return path


def checked_directory(path: Path) -> Path:
    if path.is_symlink() or not path.is_dir():
        raise ValueError(f"Missing or redirected ODM directory: {path}")
    return path


def merge_configs(device_tree: Path, compiled_odm: Path, odm_root: Path,
                  verify_only: bool = False) -> None:
    init_dir = checked_directory(checked_directory(odm_root / "etc") / "init")
    reference = regular_file(init_dir / "init.NX809J.rc")
    directory_metadata = init_dir.stat()
    reference_metadata = reference.stat()
    context = os.getxattr(reference, LABEL)
    if context.rstrip(b"\0") != b"u:object_r:vendor_configs_file:s0":
        raise ValueError(f"Unexpected OEM init configuration label: {context!r}")

    # Validate every compiled input before modifying the staging tree. A stale
    # build output must not silently reintroduce actions from an older checkout.
    inputs = {}
    for name, source in CONFIGS.items():
        expected = regular_file(device_tree / source).read_bytes()
        compiled = regular_file(compiled_odm / "etc/init" / name).read_bytes()
        if compiled != expected:
            raise ValueError(f"Stale compiled control configuration: {name}")
        destination = init_dir / name
        if destination.is_symlink():
            raise ValueError(f"Redirected destination preserved: {destination}")
        if destination.exists() and not destination.is_file():
            raise ValueError(f"Non-file destination preserved: {destination}")
        inputs[name] = compiled

    library_dir = checked_directory(odm_root / "lib64")
    library_directory_metadata = library_dir.stat()
    library_context = os.getxattr(library_dir, LABEL)
    if library_context.rstrip(b"\0") != b"u:object_r:vendor_file:s0":
        raise ValueError(f"Unexpected ODM library label: {library_context!r}")
    library = regular_file(compiled_odm / "lib64" / CAMERA_LIBRARY).read_bytes()
    if (len(library) < 64 or library[:6] != b"\x7fELF\x02\x01"
            or struct.unpack_from("<HH", library, 16) != (3, 183)
            or b"AServiceManager_addService\0" not in library
            or b"NX809JCameraCompat\0" not in library):
        raise ValueError("Invalid compiled camera adapter: expected the AArch64 shared library")
    library_destination = library_dir / CAMERA_LIBRARY
    if library_destination.is_symlink():
        raise ValueError(f"Redirected destination preserved: {library_destination}")
    if library_destination.exists() and not library_destination.is_file():
        raise ValueError(f"Non-file destination preserved: {library_destination}")

    transformer = regular_file(device_tree / "camera/patch_provider.py")
    original_provider = regular_file(
        device_tree / "camera/provider/vendor.qti.camera.provider-service_64").read_bytes()
    expected_provider = runpy.run_path(str(transformer))["patch_provider"](original_provider)
    provider = regular_file(compiled_odm / "bin/hw" / CAMERA_EXECUTABLE).read_bytes()
    if provider != expected_provider:
        raise ValueError("Compiled ODM provider differs from the deterministic OEM transformation")
    provider_context = regular_file(device_tree / "camera/provider_file_contexts").read_bytes()
    if provider_context != b"/odm/bin/hw/nx809j-camera-provider " + CAMERA_EXECUTABLE_CONTEXT + b"\n":
        raise ValueError("Unexpected camera provider context mapping")
    contexts_file = regular_file(odm_root / "etc/selinux/odm_file_contexts")
    contexts_metadata = contexts_file.stat()
    contexts_label = os.getxattr(contexts_file, LABEL)
    if contexts_label.rstrip(b"\0") != b"u:object_r:file_contexts_file:s0":
        raise ValueError("Unexpected ODM file contexts label")
    contexts = contexts_file.read_bytes()
    if provider_context.rstrip(b"\n") not in contexts.splitlines():
        if b"nx809j-camera-provider" in contexts:
            raise ValueError("Conflicting camera provider context mapping")
        contexts = contexts + (b"\n" if contexts and not contexts.endswith(b"\n") else b"")
        contexts += provider_context
    for path in (odm_root / "bin", odm_root / "bin/hw"):
        if path.exists():
            checked_directory(path)
        elif verify_only:
            raise ValueError(f"Missing ODM executable directory: {path}")
    provider_destination = odm_root / "bin/hw" / CAMERA_EXECUTABLE
    if provider_destination.is_symlink() or (provider_destination.exists()
                                             and not provider_destination.is_file()):
        raise ValueError("Redirected ODM provider executable")

    for name, contents in inputs.items():
        destination = init_dir / name
        if not verify_only and (not destination.exists() or destination.read_bytes() != contents):
            destination.write_bytes(contents)
            os.chmod(destination, 0o644)
            os.chown(destination, 0, 0)
            os.setxattr(destination, LABEL, context)
            os.utime(destination, ns=(reference_metadata.st_atime_ns,
                                     reference_metadata.st_mtime_ns))
        regular_file(destination)
        metadata = destination.stat()
        if (destination.read_bytes() != contents
                or stat.S_IMODE(metadata.st_mode) != 0o644
                or metadata.st_uid != 0 or metadata.st_gid != 0
                or os.getxattr(destination, LABEL) != context):
            raise ValueError(f"ODM control configuration verification failed: {name}")
        print(f"Verified {hashlib.sha256(contents).hexdigest()}  etc/init/{name}")

    if not verify_only and (not library_destination.exists()
                            or library_destination.read_bytes() != library):
        library_destination.write_bytes(library)
        os.chmod(library_destination, 0o644)
        os.chown(library_destination, 0, 0)
        os.setxattr(library_destination, LABEL, library_context)
        os.utime(library_destination, ns=(library_directory_metadata.st_atime_ns,
                                         library_directory_metadata.st_mtime_ns))
    regular_file(library_destination)
    metadata = library_destination.stat()
    if (library_destination.read_bytes() != library
            or stat.S_IMODE(metadata.st_mode) != 0o644
            or metadata.st_uid != 0 or metadata.st_gid != 0
            or os.getxattr(library_destination, LABEL) != library_context):
        raise ValueError("ODM camera library verification failed")
    print(f"Verified {hashlib.sha256(library).hexdigest()}  lib64/{CAMERA_LIBRARY}")
    if not verify_only:
        root_metadata = odm_root.stat()
        for path in (odm_root / "bin", odm_root / "bin/hw"):
            if not path.exists():
                path.mkdir()
                os.chmod(path, 0o755)
                os.chown(path, 0, 2000)
                os.setxattr(path, LABEL, library_context)
                os.utime(path, ns=(reference_metadata.st_atime_ns, reference_metadata.st_mtime_ns))
        if not provider_destination.exists() or provider_destination.read_bytes() != provider:
            provider_destination.write_bytes(provider)
            os.chmod(provider_destination, 0o755)
            os.chown(provider_destination, 0, 2000)
            os.setxattr(provider_destination, LABEL, CAMERA_EXECUTABLE_CONTEXT)
            os.utime(provider_destination,
                     ns=(reference_metadata.st_atime_ns, reference_metadata.st_mtime_ns))
        if contexts_file.read_bytes() != contexts:
            contexts_file.write_bytes(contexts)
            os.utime(contexts_file, ns=(contexts_metadata.st_atime_ns, contexts_metadata.st_mtime_ns))
        os.utime(odm_root, ns=(root_metadata.st_atime_ns, root_metadata.st_mtime_ns))
    metadata = regular_file(provider_destination).stat()
    if (provider_destination.read_bytes() != provider or stat.S_IMODE(metadata.st_mode) != 0o755
            or metadata.st_uid != 0 or metadata.st_gid != 2000
            or os.getxattr(provider_destination, LABEL).rstrip(b"\0") != CAMERA_EXECUTABLE_CONTEXT):
        raise ValueError("ODM camera executable verification failed")
    if contexts_file.read_bytes() != contexts or os.getxattr(contexts_file, LABEL) != contexts_label:
        raise ValueError("ODM camera file context verification failed")
    print(f"Verified {hashlib.sha256(provider).hexdigest()}  bin/hw/{CAMERA_EXECUTABLE}")
    if not verify_only:
        os.utime(init_dir, ns=(directory_metadata.st_atime_ns, directory_metadata.st_mtime_ns))
        os.utime(library_dir, ns=(library_directory_metadata.st_atime_ns,
                                 library_directory_metadata.st_mtime_ns))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device-tree", type=Path, required=True)
    parser.add_argument("--compiled-odm", type=Path, required=True)
    parser.add_argument("--odm-root", type=Path, required=True)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    try:
        merge_configs(args.device_tree.resolve(strict=True),
                      args.compiled_odm.resolve(strict=True),
                      args.odm_root.resolve(strict=True), args.verify_only)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
