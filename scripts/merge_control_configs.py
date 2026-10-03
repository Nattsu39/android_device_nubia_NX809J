#!/usr/bin/env python3
"""Merge source-built hardware control actions into the retained OEM ODM tree."""

import argparse
import hashlib
import os
from pathlib import Path
import stat
import sys


CONFIGS = {
    "redmagic_hw_arm.rc": "hwlevels/redmagic_hw_arm.rc",
    "fp_uewake_arm.rc": "fp_uewake/fp_uewake_arm.rc",
    "dt2w_uewake_arm.rc": "dt2w/dt2w_uewake_arm.rc",
}
LABEL = "security.selinux"


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
    if not verify_only:
        os.utime(init_dir, ns=(directory_metadata.st_atime_ns, directory_metadata.st_mtime_ns))


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
