# Loader test image — DFU-only runbook

> Purpose: install and run the read-only firmware-loader test image on an iPod
> Classic 6G, record the result, and recover.
> Status: final-splash `6bc6927c…` owner-reported pass (one 6G/iFlash SD, 6.5 s startup, `fsck_hfs -fn` clean).
> Owner: boot lead
> Last updated: 2026-11-04
> Source: D-01, D-02, T-049; [`LOADER.md`](LOADER.md), [`DFU.md`](DFU.md)

This is a **development test image**, not a production bootloader and not a
firmware release. It verifies a signed manifest, reads the image directly to its
load address, verifies SHA-256, selects a boot-state slot, and executes a tiny
signed ARM payload that returns the marker `HELO`. Production firmware handoff
is deliberately disabled. The image never writes the filesystem.

A successful run proves only that the read-only loader, Ed25519 verification,
HFS+ read path, DRAM copy, cache flush and jump execute on the device. It does
**not** prove a firmware boot, a kernel, audio, UI or writable HFS+.

## 1. Preconditions

- [ ] A verified backup of the iPod library exists and a restore path is known
      (D-25 / [`../docs/outer-volume-test.md`](../docs/outer-volume-test.md)).
- [ ] You accept that installing a bootloader is DFU-only and can be recovered
      through DFU or Apple restore. The Apple restore can erase the library.
- [ ] The outer-volume diagnostic is no longer needed on the device, or you have
      its run recorded. This test image replaces the installed bootloader.
- [ ] `mks5lboot` is built from the pinned donor checkout
      (`utils/mks5lboot`) and `--dfuscan` works.
- [ ] You have read [`DFU.md`](DFU.md) sections 1–6 and 9.
- [ ] Never use `--single`. It destroys the Apple NOR boot path.

## 2. Build and host gates

From the repository root:

```sh
./tools/build-loader-bootloader.sh
./tools/check-loader-artifact.sh
```

The build script:

- reuses `build/loader-bootloader/test-key.pem` if present, otherwise generates
  a new test key there (mode 0600, never committed);
- creates `build/loader-bootloader/out/firmware.img` (12-byte ARM payload),
  `firmware.manifest` (Ed25519-signed) and `bootstate` (GOOD slot);
- builds `build/loader-bootloader/out/bootloader-ipod6g-loader-test.ipod`;
- checks `MOVE_AREA` headroom and the `HELIKON-OS LOADER TEST` marker;
- verifies the exact generated files through the host HFS+ read core;
- validates the image with `mks5lboot --mkdfu-inst`.

Record from `build/loader-bootloader/out/SHA256SUMS`:

```text
image SHA-256, size, MOVE_AREA headroom, public key, key ID
```

Do not continue if any host gate fails. A successful link is not a device run.

## 3. Copy the device files

Mount the iPod's HFS+ volume on macOS. Create `/.helikon/` if it does not exist
and copy the three generated files:

```text
firmware.img
firmware.manifest
bootstate
```

Use `ditto` or Finder; then verify each SHA-256 against
`build/loader-bootloader/out/SHA256SUMS`, eject cleanly, and run `fsck_hfs -fn`
on the volume from macOS. Do not delete or modify library files. The bootloader
itself does not write these files; this is a host-side copy.

If `/.helikon/` already contains a different manifest or image, stop and record
it. Do not overwrite an unknown test image without a backup.

## 4. Install through DFU

Follow [`DFU.md`](DFU.md) sections 5–6, substituting the loader test image:

```sh
./utils/mks5lboot/mks5lboot --bl-inst \
  /path/to/Helikon-OS/build/loader-bootloader/out/bootloader-ipod6g-loader-test.ipod
```

Do **not** add `--single`. Expect the normal `Classic 6G` / `ipod6g` / `ip6g` /
`71` report. If the install fails, stop and recover through DFU; do not retry
with different flags.

## 5. Expected first boot

The screen should show, in order:

```text
HELIKON-OS splash, VERSION ALPHA
HFS+ DETAILS turns green after the volume/catalog open
HELIKON-OS LOADER TEST
Firmware verified.
Version: 1
Load: 0x08000000
Size: 12 bytes
Boot state: GOOD
Executing signed test payload...
JUMP OK
Signed test payload executed.
Press any button to power off.
```

The splash starts with a neutral `HFS+ DETAILS`, then turns green when the HFS+
volume and catalog open. A red label or a failure screen shows the HFS+ or
loader error name and code. Record the full screen before pressing a button.

The final-splash build does not print the temporary bottom stage line. The
earlier `3979de6f…` build used that line during bring-up. The final-splash
`6bc6927c…` image is device-checked.

The splash is a 16-colour RGB565 representation of the owner artwork; the
illustration is not redrawn. `VERSION ALPHA` is baked into the current build.
The exact image version/size values in the text screen come from the generated
manifest.

Pass criteria:

- the splash appears and the loader reads the manifest/image/bootstate;
- `Firmware verified.` and `Boot state: GOOD` appear;
- `JUMP OK` appears after the signed payload executes;
- the device powers off normally and the volume still passes `fsck_hfs -fn`
  from macOS;
- no bootloader write activity is observed, and the filesystem differs only by
  the pre-installed `/.helikon` files (compare a before/after listing or hash).

Any crash, hang, wrong screen, `JUMP FAILED`, signature/hash error, or dirty
journal is a FAIL. Do not retry writes. Preserve the screen and record the exact
state.

## 6. Recovery

- Hold **Menu + Select** to return to DFU.
- Remove the test bootloader with the donor uninstaller:

  ```sh
  ./utils/mks5lboot/mks5lboot --dfusend \
    /path/to/Helikon-OS/bootloader/artifacts/bootloader-ipod6g-uninstall.dfu
  ```

  Or run `tools/uninstall-bootloader.sh`, or use `--bl-uninst ipod6g`.
- To return to the donor Rockbox bootloader, reinstall the donor V3.2 image from
  its own release after removing this bootloader.
- Apple restore through Finder/iTunes is the last resort and can erase the
  library.
- Never format, erase, repartition, delete the journal or delete `/.helikon`
  as a repair.

## 7. Record the run

Record in [`../docs/device-matrix.md`](../docs/device-matrix.md):

- date, board, storage model, adapter/cable, battery/USB state;
- image SHA-256 and size, manifest/key ID, `MOVE_AREA` headroom;
- `fsck_hfs -fn` output before and after;
- screen text and pass/fail for every stage;
- whether the payload returned `HELO`;
- recovery action and final device state.

Until that record exists, T-049 is **in progress** and the image is **unproven**.
No firmware boot, release or support claim follows from this test.

## 8. Open items

- D-20 signing-key ownership is accepted (ADR-0002), but the test key is
  generated locally and is not a release key. No release key has been generated
  and certificate verification is T-053; production signing remains future
  work.
- Production firmware handoff is disabled in this image. The entry-mode
  encoding (ARM/Thumb) remains open for production handoff.
- The A/B image-path question is closed: single-image fail-closed is accepted
  (T-050). A failed or mismatched bootstate fails closed to recovery; there is
  no previous-image boot and no update rollback writer.
