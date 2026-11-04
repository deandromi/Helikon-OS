# Architecture decision records (ADRs)

> Purpose: long-form decisions that are too large for `docs/decisions.md`.
> Status: living document
> Owner: architecture lead
> Last updated: 2026-11-04

## When to create an ADR

Use an ADR when a decision:

- changes a module boundary or data format;
- has multiple serious alternatives and long consequences;
- needs a design review record that would overwhelm `decisions.md`;
- is expected to be revisited with measurements (e.g. journal ownership,
  writer architecture, app runtime, update transport).

Do **not** use an ADR for a small style choice, a task status, or a decision that
`docs/decisions.md` already records concisely. One canonical record per decision.

## Naming and status

- `NNNN-short-title.md`, e.g. `0002-journal-ownership.md`.
- Status: `proposed`, `accepted`, `rejected`, `superseded`.
- Superseded ADRs stay in the repository; link the replacement.
- `0001-template.md` is the template and is not a project decision.

## Process

1. Copy `0001-template.md` to the next free number.
2. Fill context, options, decision, consequences and follow-up.
3. Link the paper section and affected docs.
4. Open a docs PR; do not merge without owner review.
5. Update `docs/decisions.md` with a one-line index entry.

## Index

| ADR | Title | Status |
|---|---|---|
| [`0002-signing-key-ownership.md`](0002-signing-key-ownership.md) | Signing-key ownership and release signing | accepted |
| [`0003-firmware-memory-map.md`](0003-firmware-memory-map.md) | Firmware memory map and image layout | accepted |
| [`0004-firmware-entry-abi.md`](0004-firmware-entry-abi.md) | Firmware entry ABI and boot information | accepted |
| [`0001-template.md`](0001-template.md) | Template (not a decision) | template |

Add real ADRs above this line when created. Do not create both an ADR and a
duplicate long entry in `decisions.md`; the short entry links here.
