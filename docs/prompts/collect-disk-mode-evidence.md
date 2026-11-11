# Runbook — collect Apple Disk Mode baseline evidence (6G)

> Purpose: capture a read-only host baseline of Apple Disk Mode on the 6G for
> the Disk Mode P0 evidence task and the T-109 probe comparison.
> Status: living — owner-run procedure
> Owner: boot/test lead; device owner
> Last updated: 2026-11-11
> Source: `docs/disk-mode.md` §4, §6 and §13 (T-108 branch);
> [`../verification.md`](../verification.md), [`../device-matrix.md`](../device-matrix.md),
> [`../security.md`](../security.md), [`../conventions.md`](../conventions.md)

This is host evidence from **Apple Disk Mode**, not Helikon Disk Mode. It does
not prove a Helikon USB stack, a LUN, LBA48 or a writable mode. It gives the
host-side values that the T-109 firmware probe and later T-110/T-111 work must
match: USB descriptors, LUN block size, APM/MBR layout, HFS+ geometry and a
read-only `fsck_hfs` result.

## 1. Safety rules — read before touching the iPod

- **No writes.** Never use `of=/dev/...`, `dd` to a device, `newfs_hfs`,
  `diskutil eraseDisk`, `diskutil partitionDisk`, `diskutil repairVolume` or any
  restore/format action.
- **`fsck_hfs -n` only.** `-n` checks without repairing. Never run `fsck_hfs`
  with `-y`, `-p` or without `-n`; those can repair or modify the volume.
- **Never run `fsck_hfs` on a mounted volume.** Unmount first with
  `diskutil unmountDisk /dev/diskN`.
- **Cancel macOS dialogs.** If macOS or Music offers to initialize, erase,
  restore or set up the iPod, cancel. Do not click Initialize or Erase.
- **Identify the disk.** Confirm the size and product name before entering the
  disk number. If you are unsure, stop and ask.
- **One device.** Disconnect other external disks so the iPod cannot be confused
  with another volume.

The collector refuses non-Darwin hosts, internal disks, virtual/synthesized
disks and the boot disk. It does not unmount, format, repair or write.

## 2. Enter Apple Disk Mode on the 6G

1. Eject and disconnect the iPod from macOS.
2. Reset it: hold **Menu + Select** until the Apple logo appears.
3. Immediately hold **Select + Play/Pause** until the screen shows
   **Disk Mode** (and usually **Do not disconnect**). If the main menu appears,
   repeat from step 2.
4. Connect the 30-pin cable directly to the Mac, without a hub, while the iPod
   is in Disk Mode. A data cable is required; a charge-only cable will not
   enumerate.
5. Confirm the iPod screen still says **Disk Mode**. On the Mac, Finder may show
   an **IPOD** volume. Do not open Music or iTunes.
6. If Finder says the disk is not readable, do not initialize it. Eject, check
   the cable and retry.

Record the iPod model, capacity, adapter/card model and cable before starting.

## 3. Run the collector

From the repository root:

```sh
./tools/collect-ipod-evidence.sh
```

The script prints `diskutil list` and the candidate whole disks, then asks for
the iPod whole disk, for example `/dev/disk4`. It validates `Whole: Yes`, refuses
the boot disk and internal/virtual disks, and writes a timestamped directory:

```text
./ipod-evidence-YYYY-MM-DD_HHMMSS/
```

For a read-only filesystem check, unmount first and use `--fsck`:

```sh
diskutil unmountDisk /dev/disk4
./tools/collect-ipod-evidence.sh --fsck
```

`--fsck` prompts, re-checks that every volume is unmounted, and runs only
`fsck_hfs -n` on each HFS+ volume device. If any volume is mounted it refuses;
it does not unmount for you.

Show the options with:

```sh
./tools/collect-ipod-evidence.sh --help
```

## 4. What is collected and why

| File | Source | Why it matters |
|---|---|---|
| `diskutil-list.txt`, `diskutil-list-diskN.txt` | `diskutil list` | Partition scheme (APM/MBR/GPT), slice identifiers, sizes and block size as macOS sees the LUN. |
| `diskutil-info-diskN.txt` | `diskutil info /dev/diskN` | Whole-disk view: `Whole: Yes`, `Device Block Size`, `Disk Size`, `Device Location`, `Content (IOContent)`. |
| `diskutil-info-diskNsM.txt` | `diskutil info /dev/diskNsM` | Each volume: `Mounted`, `Partition Type`, `File System Personality`, `Type (Bundle)`, total/free space, `Allocation Block Size`, read-only state. |
| `system_profiler-SPUSBDataType.txt` | `system_profiler SPUSBDataType` | USB descriptor view: vendor/product strings, VID/PID, USB version, speed, BSD name; serial fields redacted. |
| `ioreg-IOUSB.txt` | `ioreg -p IOUSB -l -w 0` | IORegistry properties: `idVendor`, `idProduct`, product/vendor names, USB version, speed; serial fields redacted. |
| `sw_vers.txt`, `uname-a.txt` | `sw_vers`, `uname -a` | Host baseline, so the capture can be repeated on the same macOS build. |
| `geometry-summary.txt` | Extracted from the files above | The short values to paste into the device matrix and the T-109 comparison. |
| `fsck-diskNsM.txt`, `fsck-summary.txt` | `fsck_hfs -n` | Read-only HFS+ consistency result and exit code. Skipped unless `--fsck` is used and volumes are unmounted. |
| `block0.bin`, `block0-hexdump.txt` | `dd` from `/dev/rdiskN` | Optional MBR/APM header: APM signature `0x4552`, `sbBlkSize` at offset 2, `sbBlkCount` at offset 4; MBR signature `0x55AA` at 510 and entries at 446. |
| `README.txt`, `commands.log`, `SHA256SUMS` | Collector | Exact commands, date, disk identifier, no-write statement, redaction note and file hashes. |

The **LUN block size** is the `Device Block Size` in `diskutil info`, cross-checked
with the block size in `diskutil list`. The **USB descriptors** are the
`system_profiler` and `ioreg` views; they show what the Apple firmware advertises,
which the Helikon descriptors in `docs/disk-mode.md` §7 must replace. **APM/MBR**
comes from `diskutil list` and, if requested, block 0. **HFS+ geometry** is the
allocation block size, volume size and free space in `diskutil info`, checked by
`fsck_hfs -n`.

## 5. Optional read-only block 0 (explicit warning)

Only use this when the APM/MBR header bytes are needed for the T-109 comparison.
It is a raw read and `dd` is unforgiving: a wrong `of=` can destroy a disk.

```sh
diskutil unmountDisk /dev/disk4
./tools/collect-ipod-evidence.sh --read-block0 --yes
```

The script requires `--yes`, asks you to type `READ-BLOCK0` when run
interactively, checks again that all volumes are unmounted, refuses any output
path under `/dev`, and runs only:

```sh
sudo dd if=/dev/rdiskN of="$OUT/block0.bin" bs=512 count=1
```

It then writes a local `block0-hexdump.txt`. Do not run your own `dd`. If the
read fails, stop and record the error; do not retry with a larger count or a
different device name.

`block0.bin` is binary and is not redacted. Keep it out of the repository; attach
the hexdump or the decoded fields only after review. The repository policy still
forbids device backups and disk images.

## 6. No-write rule

The collector never writes to the iPod. There is no command in it that uses
`of=/dev/...`, formats, repairs, repartitions or deletes anything. The only
device read outside `diskutil`, `system_profiler`, `ioreg` and `fsck_hfs -n` is
the optional 512-byte `dd` read.

Do not "help" a dirty volume by repairing it. A dirty journal or a failed
`fsck_hfs -n` is evidence: record it and stop. Repair, format and journal
deletion are owner decisions outside this runbook.

When finished, eject and disconnect safely:

```sh
diskutil eject /dev/disk4
```

Wait for the iPod to leave Disk Mode before unplugging.

## 7. Attach the evidence

Keep the raw output directory local and private. Do not commit it. Record only
redacted text and hashes in the repository.

1. **Device matrix.** Add or update the **P0 Apple Disk Mode evidence** row in
   [`../device-matrix.md`](../device-matrix.md). Keep the row's result to `pass`,
   `fail` or `blocked`; name the date, macOS version, USB VID/PID, LUN block size,
   partition scheme, HFS+ allocation block size and the `fsck_hfs -n` result.
   Evidence is the local directory plus `SHA256SUMS`, for example
   `local capture 2026-11-11, SHA256SUMS <hash>`.
2. **T-109 probe report.** T-109 is the on-device read-only ATA/LBA48/APM probe.
   Add a short "Host baseline (Apple Disk Mode)" table to the T-109 report or PR
   description and compare:
   - IDENTIFY capacity vs `Disk Size`;
   - IDENTIFY logical/physical sector size vs `Device Block Size`;
   - APM/MBR signature and partition start/size vs `diskutil list` and block 0;
   - HFS+ block size vs `Allocation Block Size`;
   - selected LUN candidate vs the largest HFS+ partition;
   - the host `fsck_hfs -n` result.
   State any mismatch; do not silently reconcile it.
3. **Hash.** Record `shasum -a 256 SHA256SUMS` in the report. Attach redacted
   text files only if the issue/PR supports attachments; otherwise keep them
   local and link the hash.
4. **Do not edit** `docs/disk-mode.md`, `docs/decisions.md` or the task table from
   this run. The T-108 branch owns those files.

### Capture summary template

Record these values, without serial numbers, in the device-matrix row and the
T-109 report:

```text
Date:
Host macOS:
iPod model/capacity:
Adapter/card:
USB VID/PID:
USB product/manufacturer:
USB version/speed:
LUN block size:
Disk size:
Partition scheme:
HFS+ allocation block size:
HFS+ volume size/free:
fsck_hfs -n:
SHA256SUMS:
```

## 8. Redaction

The collector pipes every text capture through `sed` and replaces the value of
`Serial Number`, `USB Serial Number`, `Serial:`, `"Serial Number" =`,
`"USB Serial Number" =` and `"iSerial Number" =` with `[REDACTED]`. It keeps
model, capacity, product ID, vendor ID, USB version and firmware version.

Before sharing, check locally:

```sh
grep -RIn -i 'serial' ipod-evidence-YYYY-MM-DD_HHMMSS/
```

All values should be `[REDACTED]`. If a raw serial remains, delete that file,
fix the pattern and rerun. The script does **not** redact volume names, the host
name inside `uname -a`, USB location IDs or `block0.bin`; review or remove those
manually.

Never commit serial numbers, UDIDs, Wi-Fi credentials, library contents, device
backups or disk images.

## 9. Stop conditions

Stop and report if:

- the iPod is not in **Disk Mode** or the screen is ambiguous;
- macOS offers to initialize, erase or restore the iPod;
- more than one external disk could be the target;
- a volume is mounted when `--fsck` or `--read-block0` is requested;
- `fsck_hfs -n` reports damage (record it; do not repair);
- the USB device disconnects during capture;
- any command would write to a device.

## 10. What this proves

It records what Apple's 6G firmware exposes in Disk Mode on one device, adapter
and macOS host. It is a baseline and a comparison point for T-109 and T-110. It
is not Helikon Disk Mode, not a Helikon USB/LUN test, not a write test and not a
support claim.
