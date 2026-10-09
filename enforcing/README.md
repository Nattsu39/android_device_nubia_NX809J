# NX809J SELinux boot policy

## Current behavior

Since 2026-10-09, BoardConfig leaves SELinux enforcing from early init. It no
longer adds `androidboot.selinux=permissive` to the vendor boot command line.
Android init defaults to enforcing when neither the command line nor bootconfig
requests permissive mode. Check both inputs when changing the boot images.

Zygote caches the enforcement state during initialization. The former scheme
started Zygote while permissive and restored enforcement at `boot_completed`.
That left the cache false and caused ordinary applications to skip seccomp
installation even though `getenforce` later reported Enforcing.

`nx809j-enforce.rc` is retained as a compatibility assertion. Its final write of
`1` is redundant during normal startup and is not the mechanism that enables
early enforcement. No framework seccomp bypass or new policy allow rule is part
of this change.

## Validation on the current firmware

The 2026-10-09 trial changed only the active vendor boot command line and its
existing unsigned AVB hash. Kernel, ramdisk, DTB, bootconfig and installed policy
were preserved. A real `vendorbootimage` build produced the same complete boot
payload; its AVB salt differs from the minimal trial image.

- Enforcement became active at approximately 0.87 seconds, before Zygote started
  at approximately 4.44 seconds. The Zygote cache changed from 0 to 1.
- Launcher, Settings and Duck processes had seccomp mode 2 with one filter.
  Duck's ordinary-app reboot probe was blocked by seccomp.
- KeyMint, Gatekeeper and QMI services started in their dedicated vendor domains.
  The installed SIM returned to NR service; actual calls were not automated.
- Four Camera2 previews passed without provider restarts or sensor NACKs.
- Health telemetry, charging bypass transitions, fan control and fingerprint
  wake arming worked; the user confirmed fingerprint unlock.

This validates the existing firmware and user data, not every stock vendor
version, a factory reset, all camera algorithms or long-term stability. Keep
precise rollback images before testing another combination.

## Remaining boot diagnostics

The old `fp_cal_purge` shell service has no domain transition from init and is
rejected under enforcing. This device already has `.rom_cal_purged`; the old
one-shot migration was therefore redundant here. Migration from pre-August ROMs
without that marker needs a properly scoped implementation and separate testing.
Do not grant init general shell execution just to silence this diagnostic.

Health property reads, camera directory searches, vendor-init control property
reads and optional unsigned Hexagon service discovery still produce denials.
The tested functions above work despite those specific denied accesses. Review
the actual caller, label and required behavior before adding any policy rule;
do not turn application property enumeration failures into blanket permissions.

## Vendor policy and historical context

The project retains the compatible stock vendor image. The files in this
directory record the earlier vendor-policy integration:

- `native_enforcing_rules.cil`: CIL additions used by the vendor repack.
- `enforcing_rules.magiskpolicy`: historical runtime form of those additions.
- `build_native_enforcing_vendor.sh`: vendor image preparation helper.

June 2026 bring-up attempts reported QMI data-directory denials and security HALs
running in the init domain. Deferred enforcement was used to obtain a working
boot. Those observations do not describe the current dedicated HAL domains;
the installed vendor policy also includes the former QMI directory permission.

The runtime policy may additionally be modified by root modules. A successful
test with those modules does not prove an unmodified vendor policy has identical
behavior. Inspect the effective policy and boot logs for the image being tested.
