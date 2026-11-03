# Prompt — run one device matrix row

> Purpose: reusable device-run prompt.
> Status: stub
> Owner: docs lead
> Last updated: 2026-11-03

Read `agents.md`, `docs/verification.md`, `docs/device-matrix.md`,
`docs/validation.md`, `docs/failures.md` and the relevant phase in
`docs/roadmap.md`.

Before testing, confirm:

- the device is not the only copy of the library;
- a backup was hashed and read back;
- Disk Mode, DFU and Apple restore are confirmed;
- the test volume is disposable or backed up;
- the firmware/bootloader image hash is recorded;
- the test does not use `--single` or format the library.

Run exactly one test from the matrix. Record:

- model/revision, capacity, adapter/card, firmware hash;
- test method and expected result;
- actual result, `fsck_hfs -n` output, logs/screenshots;
- whether the volume was written and whether it was disposable;
- rollback/recovery performed.

Update `docs/device-matrix.md` with a result (`pass`, `fail`, `blocked`,
`not run`) and evidence link. Update `docs/state.md` if the result changes a
blocker. Do not claim support for the device or adapter beyond this row.
