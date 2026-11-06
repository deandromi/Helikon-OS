# Release notes archive

> Purpose: archive the release notes and quick starts that were published as
> GitHub releases.
> Status: living
> Owner: release lead
> Last updated: 2026-11-06

The canonical release process is [`../release.md`](../release.md). This folder
holds the notes that were attached to each GitHub release so the text remains
reviewable in the source tree. A release is not evidence of support; read the
limits in each note.

| Release | Tag | Date | Type | Notes |
|---|---|---|---|---|
| Alpha 2.0.0 — Launchers and Interfaces | `alpha-2.0.0` | 2026-11-06 | Firmware + bootloader + companion preview | [`alpha-2.0.0/RELEASE_NOTES.md`](alpha-2.0.0/RELEASE_NOTES.md) · [`QUICKSTART.md`](alpha-2.0.0/QUICKSTART.md) |
| Alpha 1.0.1 — Bootloader Only — A Quick Start | `alpha-1.0.1` | 2026-11-04 | Bootloader only | GitHub release; notes not archived here |

## Rules

- Do not rewrite a published release note. If a correction is needed, add a
  dated note to the release body or publish a new patch release.
- Every note must list what is included, what is not, the device evidence, the
  signing state and the recovery path.
- The release asset hashes are recorded in the GitHub release `SHA256SUMS`; the
  archive links to the release rather than duplicating mutable data.
