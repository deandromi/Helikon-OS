# Disposable HFS+ image test

This directory contains the macOS-only integration test for the native HFS+
writer. It is **not** a device test and it never touches a real volume.

`run-macos.sh` creates a new 16 MiB journaled HFS+ image under `mktemp -d`,
runs the host writer on it, mounts it read-only through macOS to verify the file
content, deletes the file, and runs `fsck_hfs -fn` after each stage. `fsck_hfs`
must report that the volume is OK and must not attempt a repair.

The script accepts no image or device argument. Do not add one, and do not
point the harness at a library, a mounted volume or a `/dev` node.

Run it with:

```sh
make -C tests/image test
```

On non-macOS hosts the driver prints `SKIP` and exits successfully; the
portable host unit tests are in `tests/hfsplus`.

The image is disposable and is deleted by the exit trap. It is not committed:
the repository does not store disk images.
