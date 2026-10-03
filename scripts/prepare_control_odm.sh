#!/usr/bin/env bash
# Keep the OEM ODM payload and add the control actions built from this device tree.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <OEM ODM image> <compiled ODM directory> <output image>" >&2
    exit 2
fi

device_tree=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
input=$(realpath -e "$1")
compiled=$(realpath -e "$2")
output=$(realpath -m "$3")
tmpfs=${REPACK_TMPFS:-/dev/shm}
mkfs=${BIN:?BIN must point to the AOSP host tools}/mkfs.erofs

[ "$EUID" -eq 0 ] || { echo 'ODM staging requires root.' >&2; exit 1; }
[ "$input" != "$output" ] || { echo 'The OEM input must be preserved.' >&2; exit 1; }
[ ! -L "$3" ] || { echo 'A redirected output is not accepted.' >&2; exit 1; }
[ "$(stat -f -c %T "$tmpfs")" = tmpfs ] || {
    echo 'REPACK_TMPFS must support SELinux labels on all inode types.' >&2
    exit 1
}
[ -x "$mkfs" ] || { echo "Missing EROFS host tool: $mkfs" >&2; exit 1; }

required=$(( $(stat -c %s "$input") + 268435456 ))
available=$(( $(stat -f -c %a "$tmpfs") * $(stat -f -c %S "$tmpfs") ))
[ "$available" -ge "$required" ] || { echo 'Insufficient ODM staging space.' >&2; exit 1; }

mkdir -p "$(dirname "$output")"
stage=$(mktemp -d "$tmpfs/nx809j-odm-control.XXXXXX")
mount_dir=$(mktemp -d "$tmpfs/nx809j-odm-mount.XXXXXX")
pending=$(mktemp "$(dirname "$output")/.odm-control.XXXXXX")
mounted=0

cleanup() {
    if [ "$mounted" -eq 1 ]; then
        umount "$mount_dir" || return 1
    fi
    rm -rf -- "$stage" "$mount_dir"
    rm -f -- "$pending"
}
trap cleanup EXIT

mount -t erofs -o loop,ro "$input" "$mount_dir"
mounted=1
cp -a "$mount_dir/." "$stage/"
umount "$mount_dir"
mounted=0

python3 "$device_tree/scripts/merge_control_configs.py" \
    --device-tree "$device_tree" --compiled-odm "$compiled" --odm-root "$stage"

# Pin the filesystem creation time and UUID. --mkfs-time retains OEM inode times.
"$mkfs" -zlz4hc -T "${SOURCE_DATE_EPOCH:-0}" --mkfs-time \
    -U 5dc7ec78-d6f8-44d5-b730-13e2fbdd8090 "$pending" "$stage"

# Read the resulting filesystem through the kernel, including its SELinux xattrs.
mount -t erofs -o loop,ro "$pending" "$mount_dir"
mounted=1
python3 "$device_tree/scripts/merge_control_configs.py" \
    --device-tree "$device_tree" --compiled-odm "$compiled" \
    --odm-root "$mount_dir" --verify-only
umount "$mount_dir"
mounted=0

mv -f -- "$pending" "$output"
echo "Prepared OEM ODM with source-built control configurations: $output"
