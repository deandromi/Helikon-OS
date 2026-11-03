# Helikon-OS diagnostic bootloader — backup and DFU install

> Status: **development image; unproven on hardware.**
> This is not a release, not a support claim and not a firmware loader.
> Owner: boot/release lead
> Last updated: 2026-11-03
> Source: design paper §5, §16, §24; D-21; donor `mks5lboot` procedure

This runbook installs the first Helikon-OS image on an iPod Classic 6G. The
image shows the owner-supplied splash, runs a **read-only** HFS+ diagnostic and
then waits for a button and powers off. It never loads firmware and never writes
the volume.

## Which file is installed

The installed file is the **Helikon-OS** image committed in this repository:

```text
bootloader/artifacts/bootloader-ipod6g.ipod
```

It is **not** the donor `bootloader-ipod6g.ipod` from
`hfsplus-rockboxed-ipod`, and it is not the donor V3.0/V3.2 release. Do not
flash the donor image or compare against the donor hash `78cf...`.

The donor checkout is used for two things only:

1. `mks5lboot`, the DFU installer tool;
2. build-time S5L8702 clocking/PMU/ATA/LCD bring-up and the read-only partition
   scanner.

The installed image contains the Helikon diagnostic core, HFS+ reader, report
and splash. It contains no donor overlay, FAT mount, firmware-loading path or
HFS+ write code.

| Field | Value |
|---|---|
| Image | [`artifacts/bootloader-ipod6g.ipod`](artifacts/bootloader-ipod6g.ipod) |
| Size | 67,144 bytes |
| SHA-256 | `280ed7aee5dcf38d1a30470704f91740b299004692c2aebbb21d41744c05f43c` |
| Donor base | `hfsplus-rockboxed-ipod` `55603bd4bc2524ded900fa17feae99a223b04349` |
| `MOVE_AREA` | `0x22020800`–`0x22040000`; `_moveend` `0x22030e40`; 61,888 bytes headroom |

## Owner decisions for this image

Recorded 2026-11-03 as D-21:

- **Read-only diagnostic only.** No firmware loading, no HFS+ writes.
- **Manual SHA-256 verification.** No signed manifest for this diagnostic image;
  a signed manifest is still required before any firmware-loading bootloader or
  release.
- **No write test.** The report shows `HFS+ write: NOT IMPLEMENTED`. Write
  diagnostics will be built later on a disposable volume, with the real writer.
- **Wait for a button, then power off.** No automatic reboot or write loop.
- **Backup scope:** the current `.rockbox` build and `/.rockpod-rw` only. Music
  backup is deliberately omitted by the owner. A failed flash may require Apple
  restore and can erase the music library.

## Hard preconditions

Do not begin until all are true:

- [ ] `.rockbox` and `/.rockpod-rw` are backed up and read back successfully.
- [ ] `diskutil verifyVolume` passes on the iPod volume.
- [ ] The volume is ejected cleanly; the HFS+ journal is not dirty.
- [ ] The image SHA-256 matches the value above.
- [ ] `mks5lboot` runs and `--dfuscan` starts without an error.
- [ ] You can put the iPod into DFU and can use Apple restore if needed.
- [ ] You accept that the fixed image is unproven; the first build booted but
      had report-formatting and read-probe failures.

If any box is unchecked, stop.

## 0. Separate write diagnostic image

The read-only image above is the production diagnostic. A separate DFU-only
write diagnostic exists for the native writer:

| Field | Value |
|---|---|
| Image | [`artifacts/bootloader-ipod6g-write-diag.ipod`](artifacts/bootloader-ipod6g-write-diag.ipod) |
| Image size | 110,120 bytes |
| Image SHA-256 | `7896878ce1586cdc36ba03455588762f0a8e42acae821cfff978416683443e8c` |
| DFU installer | [`artifacts/bootloader-ipod6g-write-diag.dfu`](artifacts/bootloader-ipod6g-write-diag.dfu) |
| DFU size | 118,256 bytes |
| DFU SHA-256 | `06ac01a07a25e9bb413419132b1072f39807ce5188b8168c678051009aecc76f` |
| Uninstaller DFU | `artifacts/bootloader-ipod6g-uninstall.dfu` |
| Uninstaller SHA-256 | `203e836412b71d3a16b3b57a5bf7ef034a0f5dcae1ca193685b8bfee33868a3a` |
| Marker | `HELIKON-OS WRITE DIAG` |
| `MOVE_AREA` | `0x22020800`–`0x22040000`; `_moveend` `0x2203b620`; 18,912 bytes headroom |

Extra preconditions for this image:

- [ ] `helikon-test.img` (16 MiB, journaled HFS+) is at the iPod root and was
      created by [`../tools/make-write-diag-image.sh`](../tools/make-write-diag-image.sh).
- [ ] The outer volume has at least 16 MiB free and its journal is clean.
- [ ] You accept that only the test image's data blocks are written; outer
      metadata is not.
- [ ] You have read [`WRITE_DIAG.md`](WRITE_DIAG.md) and can copy the inner
      image back to macOS for `fsck_hfs -fn`.

The backup, DFU and recovery rules are otherwise identical. The write image has
a one-command sender:

```sh
tools/install-write-diagnostic.sh
```

It verifies the DFU hash, scans for the device and sends the installer. Never
use `--single`. If the diagnostic reports FAIL, preserve the inner image and do
not retry writes until it has been recreated and checked on macOS. No hardware
run of this image is recorded.

## 1. Back up the Rockbox state

Connect the iPod in **Apple Disk Mode**. Music is deliberately not copied. If
the flash fails and Apple restore is required, the music library may be erased.

The donor overlay is `/.rockpod-rw`; this is the directory the owner referred to
as `rockbox.rw`. Confirm the actual volume name and contents first:

```sh
ls /Volumes
VOL="/Volumes/iPod"          # use the real name from ls /Volumes
ls -la "$VOL"                # .rockbox and .rockpod-rw must be visible
```

Copy both trees to a timestamped backup:

```sh
STAMP="$(date +%Y-%m-%d_%H%M%S)"
DEST="$HOME/Helikon-Backup/$STAMP"
mkdir -p "$DEST"

ditto "$VOL/.rockbox" "$DEST/.rockbox"
ditto "$VOL/.rockpod-rw" "$DEST/.rockpod-rw"
```

Verify by reading the copies back:

```sh
shasum -a 256 "$VOL/.rockpod-rw" "$DEST/.rockpod-rw"
cmp "$VOL/.rockpod-rw" "$DEST/.rockpod-rw" && echo "overlay backup OK"
diff -qr "$VOL/.rockbox" "$DEST/.rockbox" && echo "rockbox backup OK"
diskutil verifyVolume "$VOL"
```

The two overlay SHA-256 lines must match. `diff -qr` must print nothing. Then
eject:

```sh
diskutil eject "$VOL"
```

Keep the USB cable connected. Do not format, erase, repartition or delete
`/.rockpod-rw` at any point.

## 2. Verify the image

From this repository:

```sh
cd /path/to/Helikon-OS
shasum -a 256 bootloader/artifacts/bootloader-ipod6g.ipod
```

Expected:

```text
280ed7aee5dcf38d1a30470704f91740b299004692c2aebbb21d41744c05f43c  bootloader/artifacts/bootloader-ipod6g.ipod
```

Also confirm the diagnostic marker:

```sh
strings -a bootloader/artifacts/bootloader-ipod6g.ipod |
  grep 'HELIKON-OS DIAGNOSTIC'
```

**Do not use the donor V3.0/V3.2 hash.** The donor hash `78cf...` belongs to a
different image.

## 3. Get `mks5lboot`

`mks5lboot` is the DFU installer from the donor project. Use a donor checkout at
the pinned commit:

```sh
cd /path/to/hfsplus-rockboxed-ipod
git checkout 55603bd4bc2524ded900fa17feae99a223b04349
make -C utils/mks5lboot -j"$(sysctl -n hw.ncpu)" CC=clang \
  LDOPTS="-framework IOKit -framework CoreFoundation"
file utils/mks5lboot/mks5lboot
```

The result is a native Mach-O executable. On macOS, clang may print
`-Wunterminated-string-initialization` warnings from `mkdfu.c`; they are
inherited and non-fatal.

## 4. Start the Apple-device monitor

Open a **separate terminal** and leave this running. It pauses the Apple helpers
that would otherwise claim the iPod during DFU. It resumes them when you press
Control-C.

```sh
python3 - <<'PYMONITOR'
import os
import signal
import subprocess
import time

names = {
    "AMPDevicesAgent", "AMPDeviceDiscoveryAgent",
    "com.apple.amp.devicesui", "iTunesHelper",
}
paused = {}

def processes():
    output = subprocess.check_output(
        ["ps", "-u", str(os.getuid()), "-o", "pid=,stat=,comm="], text=True,
    )
    result = {}
    for line in output.splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) == 3:
            pid, state, command = parts
            result[int(pid)] = (state, os.path.basename(command))
    return result

def stop_monitor(signum, frame):
    raise KeyboardInterrupt

signal.signal(signal.SIGTERM, stop_monitor)
print("Apple-device monitor active. Stop with Control-C.", flush=True)
try:
    while True:
        for pid, (state, name) in processes().items():
            if name in names and "T" not in state:
                paused[pid] = name
                try:
                    os.kill(pid, signal.SIGSTOP)
                    print(f"Paused: {name} ({pid})", flush=True)
                except (ProcessLookupError, PermissionError):
                    paused.pop(pid, None)
        time.sleep(0.25)
except KeyboardInterrupt:
    pass
finally:
    current = processes()
    for pid, name in paused.items():
        if current.get(pid, (None, None))[1] == name:
            try:
                os.kill(pid, signal.SIGCONT)
            except (ProcessLookupError, PermissionError):
                pass
    print("Monitor stopped; paused helpers resumed.")
PYMONITOR
```

Do not close this window without stopping it with Control-C.

## 5. Enter DFU and scan

Put the iPod into DFU:

- hold **Menu + Select** for about 12 seconds;
- release at the black screen.

In the second terminal, from the donor checkout:

```sh
./utils/mks5lboot/mks5lboot --dfuscan --loop 1
```

The scanner must report a DFU device. If it does not, stop and check the cable,
the button timing and the Apple-device monitor. Do not retry with `--single`.

Stop the scanner with Control-C.

## 6. Install the Helikon image

From the donor checkout, using the verified image path:

```sh
./utils/mks5lboot/mks5lboot --bl-inst \
  /path/to/Helikon-OS/bootloader/artifacts/bootloader-ipod6g.ipod
```

`mks5lboot` should report:

```text
model name:   Classic 6G
platform:     ipod6g
RB name:      ip6g
RB num:       71
```

Do **not** add `--single`. It removes the Apple NOR boot path.

After the success tone and reboot, stop the Apple-device monitor with Control-C.

## 7. Expected first boot

The screen should show the Helikon-OS splash, then a report like:

```text
HELIKON-OS DIAGNOSTIC
HFS+ volume: PASS
HFS+ catalog: PASS
HFS+ read: PASS (32 bytes)
HFS+ write: NOT IMPLEMENTED
Journal: CLEAN
Splash: PASS
Partition: <size> MiB
Press any button to power off.
Nothing was written to the disk.
```

Notes:

- `Journal` is `CLEAN` on a journaled volume and `NOT PRESENT` on a
  non-journaled volume. `DIRTY` is a failure; do not delete the journal.
- `HFS+ read: FAIL (-8)` means `/rockbox.ipod` was not found. Volume and catalog
  can still be `PASS`; record the report and do not "fix" the volume.
- `HFS+ volume: FAIL` with `Journal: DIRTY` means the volume needs macOS to
  replay the journal; boot macOS, mount it, eject cleanly, and retry. Do not
  delete the journal.
- The bootloader does not load firmware. It is a diagnostic, not Rockbox.

Press any button. If the USB cable is still connected, the screen asks you to
unplug it; unplug, then press a button. The device powers off.

## 8. After the first boot

The bootloader install does not write the HFS+ volume, but verify it before
trusting the device:

1. Boot the iPod in Apple Disk Mode.
2. Run `diskutil verifyVolume "$VOL"`.
3. Optionally take a second snapshot with the same `ditto` commands and a new
   `STAMP`.
4. Eject cleanly.

If the diagnostic reported a write, a hang, a crash or a volume error, stop and
record the exact screen text. Do not format, erase, restore or delete the
journal as a repair.

## 9. Recovery

- DFU is in the ROM and is not removed by this install. Hold **Menu + Select**
  to return to DFU.
- Remove the Helikon bootloader through DFU with the donor installer:

  ```sh
  ./utils/mks5lboot/mks5lboot --dfusend \
    /path/to/Helikon-OS/bootloader/artifacts/bootloader-ipod6g-uninstall.dfu
  ```

  Or run `tools/uninstall-bootloader.sh`, or use `--bl-uninst ipod6g`.

- To return to the donor Rockbox bootloader, reinstall the donor V3.2
  `bootloader-ipod6g.ipod` from its own release after removing the Helikon
  bootloader. Do not use the donor image or hash for Helikon.
- Apple restore through Finder/iTunes is the last resort. It can erase the music
  library.
- Never use `--single`.
- Never format, erase, repartition or delete `/.rockpod-rw` or the journal as a
  repair.

## 10. Rebuild from source

```sh
DONOR_DIR=/path/to/hfsplus-rockboxed-ipod \
  ./tools/build-diagnostic-bootloader.sh
```

The script exports the pinned donor commit with `git archive`, copies the
Helikon diagnostic core, applies
[`hardware/donor-bootloader.patch`](hardware/donor-bootloader.patch), builds
with `--thumb`, checks `_moveend` against `MOVE_AREA`, copies the image to
`bootloader/artifacts/` and prints the SHA-256. If the toolchain or build
environment changes, use the hash printed by the script rather than the
committed artifact hash.

## Known unknowns

- **The fixed image has not been rerun.** The first build booted and showed the
  splash, but its report formatting and read probe failed. The current artifact
  fixes both; its splash/report/button/power-off path is still unproven.
- The combined image is host-tested plus donor bring-up; it is not a release.
- D-01 (manifest/signature), D-02 (boot state/fallback) and D-20 (signing-key
  ownership) remain open for the later firmware-loading bootloader.
- HFS+ write diagnostics (T-030) are deliberately not in this image.
