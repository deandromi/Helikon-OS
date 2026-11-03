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

## Hard preconditions

Do not begin until all are true:

- [ ] A Helikon-OS `bootloader-ipod6g.ipod` was built from a recorded commit
      with `--thumb`.
- [ ] `_moveend` is within `MOVE_AREA` with recorded headroom.
- [ ] The image SHA-256 is recorded here and in the release notes.
- [ ] D-01 (manifest/signature) and D-02 (boot state/fallback) are recorded.
- [ ] A disposable 6G test iPod is available; it is not the only copy of any
      library.
- [ ] The library and `/.rockpod-rw` are backed up and read back.
- [ ] Disk Mode, DFU, Apple restore and the DFU installer are confirmed.
- [ ] Recovery has been rehearsed on the test device.

If any box is unchecked, stop.

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

## 2. Back up and eject

1. Connect in Apple Disk Mode.
2. Copy the music library, `.rockbox`, `/.rockpod-rw` (migration material),
   settings, playlists and database files.
3. Hash the backup and read one file back.
4. Run `fsck_hfs -n` / `diskutil verifyVolume` and record the result.
5. Eject the volume. Keep the USB cable connected.

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

It must not write the volume. If it does, stop and recover.

## 6. Recovery

- DFU install/uninstall and Apple restore are the recovery paths.
- Never use `--single`.
- Never format, erase, repartition or delete the journal as a repair.
- If the device does not enter DFU or the scanner cannot see it, stop; do not
  retry with `--single`.

## Current blocker

The host diagnostic core and splash are ready and host-tested. The missing
pieces are the hardware HAL/ATA/LCD entry point, the build integration, the
D-01/D-02 owner decisions and a recorded recovery rehearsal. Until those exist,
this runbook is intentionally non-executable.
