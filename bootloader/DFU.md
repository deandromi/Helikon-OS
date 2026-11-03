# Helikon-OS diagnostic bootloader — DFU install runbook

> Status: **BLOCKED — no flashable Helikon-OS bootloader image exists.**
> Do not flash anything from this document yet.
> Owner: boot/release lead
> Last updated: 2026-11-03
> Source: design paper §5, §16, §24; donor DFU procedure

This runbook describes the procedure for a future **Helikon-OS diagnostic
bootloader** image. The source core exists, but the S5L8702 HAL, ATA/LCD
entry point and image build are not integrated. There is no image and no
Helikon-OS SHA-256 to verify. Do not substitute the donor V3.0/V3.2 image or
hash.

## Owner decisions for this image

Recorded 2026-11-03:

- **Read-only diagnostic only.** No firmware loading, no HFS+ writes.
- **Manual SHA-256 verification.** No signed manifest for this diagnostic image;
  the hash is compared by hand. A signed manifest is still required before any
  firmware-loading bootloader or release.
- **No write test.** The report shows `HFS+ write: NOT IMPLEMENTED`. Write
  diagnostics will be built later on a disposable volume, with the real writer.
- **Wait for a button, then power off.** No automatic reboot or write loop.
- **Backup scope:** current `.rockbox` build and `/.rockpod-rw` only. Music
  backup is deliberately omitted by the owner. A failed flash may require Apple
  restore and can erase the music library.

## Hard preconditions

Do not begin until all are true:

- [ ] A Helikon-OS `bootloader-ipod6g.ipod` was built from a recorded commit
      with `--thumb`.
- [ ] `_moveend` is within `MOVE_AREA` with recorded headroom.
- [ ] The image SHA-256 is recorded here and in the build log.
- [ ] `.rockbox` and `/.rockpod-rw` are backed up and read back.
- [ ] Disk Mode, DFU, Apple restore and the DFU installer are confirmed.
- [ ] Recovery has been rehearsed on the test device.

D-01/D-02 are **not** required for this read-only diagnostic image. They become
required before any firmware-loading bootloader.

## 1. Build and verify

The toolchain is `arm-elf-eabi-gcc` 9.5.0 / binutils 2.38. A generic
`arm-none-eabi` toolchain is not automatically equivalent.

Build the bootloader with Thumb, then verify:

```sh
file bootloader-ipod6g.ipod
strings -a bootloader-ipod6g.ipod | grep -E 'HELIKON-OS|HFS\\+|read-only'
shasum -a 256 bootloader-ipod6g.ipod
```

Expected Helikon-OS hash:

```text
<HELIKON_BOOTLOADER_SHA256 — NOT YET AVAILABLE>
```

**Do not use the donor hash**
`78cf...` — it belongs to the donor V3.0 bootloader, not Helikon-OS.

## 2. Back up the Rockbox state and eject

Connect in Apple Disk Mode. Music is **not** copied in this backup; if the flash
fails and Apple restore is required, the music library may be erased.

```sh
VOL="/Volumes/iPod"          # confirm the actual name with: ls /Volumes
ls -la "$VOL"                # .rockbox and .rockpod-rw must be present
STAMP="$(date +%Y-%m-%d_%H%M%S)"
DEST="$HOME/Helikon-Backup/$STAMP"
mkdir -p "$DEST"

ditto "$VOL/.rockbox" "$DEST/.rockbox"
ditto "$VOL/.rockpod-rw" "$DEST/.rockpod-rw"
```

Verify the copies by reading them back:

```sh
shasum -a 256 "$VOL/.rockpod-rw" "$DEST/.rockpod-rw"
cmp "$VOL/.rockpod-rw" "$DEST/.rockpod-rw" && echo "overlay backup OK"
diff -qr "$VOL/.rockbox" "$DEST/.rockbox" && echo "rockbox backup OK"
diskutil verifyVolume "$VOL"
```

Compare the two overlay SHA-256 lines; they must match. Then eject:

```sh
diskutil eject "$VOL"
```

Keep the USB cable connected. Do not format, erase or delete `/.rockpod-rw` at
any point.

## 3. Apple-device monitor

Use the Apple-device monitor from the donor `shipping.md` procedure. It pauses
`AMPDevicesAgent`, `AMPDeviceDiscoveryAgent`, `com.apple.amp.devicesui` and
`iTunesHelper` while DFU is active, and resumes them on Control-C. It is not
Helikon-OS-specific.

Do not close the monitor window without stopping it with Control-C.

## 4. DFU scan and install

Put the iPod into DFU: hold **Menu + Select** for about 12 seconds and release
at the black screen.

```sh
# From a separate donor tools checkout; mks5lboot is the DFU installer.
./mks5lboot --dfuscan --loop 1
```

The scanner must report a DFU device. Stop the scanner with Control-C, then
install the **verified Helikon-OS image**:

```sh
./mks5lboot --bl-inst bootloader-ipod6g.ipod
```

Do **not** add `--single`. It destroys the Apple NOR boot path.

After the success tone and reboot, stop the Apple-device monitor with Control-C.

## 5. Expected first boot

The diagnostic bootloader should show:

- the Helikon-OS splash (320×240, 1-bit);
- a read-only HFS+ report;
- `HFS+ write: NOT IMPLEMENTED` — there is no write path.

Press a button, then the diagnostic should power off. It must not write the
volume or reboot automatically. If it writes or fails to power off, stop and
recover.

The hardware button/power behavior is part of the future bootloader entry
point; today this is a host-tested core only.

## 6. Recovery

- DFU install/uninstall and Apple restore are the recovery paths.
- Never use `--single`.
- Never format, erase, repartition or delete the journal as a repair.
- If the device does not enter DFU or the scanner cannot see it, stop; do not
  retry with `--single`.
- Apple restore may erase the music library. Only `.rockbox` and
  `/.rockpod-rw` are in this backup.

## Current blocker

The host diagnostic core and splash are ready and host-tested. The missing
pieces are the hardware HAL/ATA/LCD entry point, the build integration, the
D-01/D-02 owner decisions and a recorded recovery rehearsal. Until those exist,
this runbook is intentionally non-executable.
