# FAQ — user-facing questions

> Purpose: answer common questions honestly, with the design-stage boundary.
> Status: living document
> Owner: docs lead; project owner
> Last updated: 2026-11-05
> Source: design paper Appendix G, §28; root `README.md`;
> [`competitive-roadmap.md`](competitive-roadmap.md)

## Can I build Helikon-OS today?

No firmware release. Diagnostic bootloader images, a separate loader test image
and the F1/F2 development firmware sources exist; there is no production
firmware, kernel, installer or release. The diagnostic images are not a support
claim. You can build and test the donor project today in a separate checkout and
follow the paper's assembly plan; that is not Helikon-OS firmware.

## Do I need to be a programmer?

To write the kernel, VFS, HFS+ writer, journal or UI, yes — or you need a
developer/AI working with the owner. A non-coder can manage builds, backups,
device tests, acceptance gates and the release checklist.

## Will it run on my iPod?

The first target is the iPod Classic 6G. The 7G is the next named port and
requires a board audit before support is claimed. Other iPods and players are
ports, not launch promises. No device is supported until a recorded run is in
`docs/device-matrix.md`.

## How is Helikon-OS different from Rockbox?

Rockbox is mature and broad; Helikon-OS is early and deliberately narrower. The
main design differences are native HFS+ instead of FAT32, journaled
transactional writes, signed staged updates, a host-built plus resumable library
index, data checksums and scrub, a modern first-party UI, a desktop app and a
sandboxed extension model. The comparison, including what Rockbox does better
today, is in [`competitive-roadmap.md`](competitive-roadmap.md). None of this is
a “better than Rockbox” claim until the differential evidence exists.

## Will it play everything Rockbox plays?

Audio: yes, by v1.x. The plan requires all 37 audio format families documented
in the Rockbox iPod Classic manual, plus Opus. v1 ships eight core families
(MP3, AAC-LC, ALAC, FLAC, WAV, AIFF, Vorbis and Opus); v1.x adds the rest,
including AC3, WMA, APE, DSD, chiptune and tracker families. APE above
`-c1000` is best-effort because the 6G is not fast enough for the highest
profiles. Video, images, native Rockbox plugins and games are separate scopes
and are not promised. The detailed matrix is in
[`audio-formats.md`](audio-formats.md).

## Will it erase my music?

It can, if the write engine has a bug. That is why the plan separates a
non-journaled prototype on a disposable volume from journaled writes on a real
library. Every experimental build needs a verified backup. Do not test on an
only-copy library.

## Can I keep HFS+?

Yes. Native HFS+ is the writable filesystem. Files the device creates are
ordinary HFS+ files visible to macOS, not hidden in an overlay.

## Does Helikon-OS support FAT32?

No. The goal is HFS+ native without an overlay or FAT. A FAT32 iPod is reported
as `NOT_HFS`; there is no FAT mount or hidden fallback. The desktop app should
explain how to back up and convert it, but conversion is a user action, not a
firmware repair.

## Will macOS still read the volume?

That is a release gate. Every crash test must leave a volume that `fsck_hfs`
reports clean and macOS mounts. If macOS disagrees with the device, the build is
wrong.

## Can I still boot Apple firmware?

Yes, if the Apple NOR path is preserved. Never use `--single`. Bootloader
changes are DFU-only and must preserve Apple/DFU recovery.

## Will my Rockbox skins work?

A documented subset will. Helikon-OS is not a full WPS engine; it plans to parse
common layout, tags, images, fonts and progress elements and report unsupported
features. The 20-skin corpus is the evidence, not a blanket promise. Do not copy
proprietary themes/art/fonts without permission.

## Will my Rockbox plugins or games work?

No. Legacy plugins expose raw hardware and are excluded. "Custom game/application
support" means a new sandboxed runtime with permissions and quotas, not binary
compatibility with Rockbox plugins or games.

## Is there a desktop app?

Planned, not built. macOS first, because it can write HFS+ in Disk Mode.
Windows/Linux need either a tested HFS+ path or a Helikon-OS USB service mode;
that transport is an open decision (D-09), not a shipped feature.

## Does it scrobble to Last.fm?

The desktop app is planned to scrobble from play history, opt-in. The device has
no network and does not upload listening history.

## Can I use an external USB DAC?

Not on the 6G: the iPod is a USB device, not a USB host. The goal is to use the
internal CS42L55 path correctly and measure it. A future target with USB host
could revisit this; it is not a v1 promise.

## Will it work in my car?

Tier 1 is line-out, charging, resume and safe power behaviour. Tier 2 is a subset
of the 30-pin accessory protocol for controls and metadata, with legal and
reverse-engineering risk. Tier 2 is optional and does not block v1. No Apple
certification claim.

## Is anti-corruption self-healing?

No. It detects metadata/data corruption, quarantines the file and reports it.
Repair comes from a backup or a known-good copy. Silent "repair" is not a feature.

## Does it need journaling?

For a library you care about, yes. The release design writes journaled HFS+. A
non-journaled volume is a prototype path on disposable data only, because a
power loss can require `fsck_hfs`. Journal strategy and ownership are accepted
decisions (D-03/D-04); the device power-cut matrix is still `not run`.

## What happens if a write is interrupted?

On the next mount, the journal should replay committed transactions and discard
incomplete ones. If the journal is invalid or replay fails, the volume mounts
read-only and reports the error. It must not silently ignore the journal or
auto-repair.

## Can I migrate my old overlay?

The plan is a host exporter that reads the donor overlay and writes files into
ordinary native HFS+ directories, with verify-before-switch and rollback. The
firmware does not "repair" the old container. Do not delete the only copy of
`/.rockpod-rw` or the library.

## Will the battery improvement be guaranteed?

No. It is a measured goal against a recorded Rockbox baseline on the same
hardware, media and settings. If the measurement does not support the claim, the
claim is withdrawn. D-12 defines the target and rig.

## Can I use it on other MP3 players?

That is exploratory. The architecture has a porting layer, but support requires
hardware, documentation and a maintainer with a test device. It is not a v1
promise.

## How long will this take?

Months to years, not an afternoon. The donor build is a day; the first read-only
player is a larger project; the HFS+ writer, journal, desktop app and device
matrix are the long poles. No release date is promised.

## What can I test without coding?

Toolchain builds, host tests, backups, Disk Mode/DFU, boot, playback, sleep, USB
read-only, `fsck_hfs` runs, battery/RAM measurements, skin corpus, adapter
matrix, crash-image replay, migration rehearsals and the goal scorecard. Use the
evidence template in `docs/verification.md`.

## What should I never do?

Never format, erase, restore or delete the journal to fix a software error.
Never run a write test on the only copy of the music. Never use `--single`.
Never claim a gate passed without a recorded run.

## What license is Helikon-OS under?

GPL-2.0-or-later, matching Rockbox and the donor project. See [`LICENSE`](../LICENSE)
and [`NOTICE`](../NOTICE). The documentation in this repository is under the
same license for now. No code of conduct is currently adopted.

## Is Helikon-OS an official Rockbox or Rockpod project?

No. It is an independent system. Rockbox and Rockpod are upstream lineage and
must be credited, but the product identity is Helikon-OS.

## How can I help?

Test on different 6G/7G revisions, storage adapters, cards, receivers and skin
corpora when a Helikon-OS build exists. Record exact firmware/build hashes,
`fsck_hfs` output and logs. For now, review the design, resolve owner decisions,
and improve the documentation without inventing hardware facts.

## Related

- [`goals.md`](goals.md) — 25 goals and acceptance
- [`device-matrix.md`](device-matrix.md) — support status
- [`decisions.md`](decisions.md) — open decisions
- [`validation.md`](validation.md) — evidence matrix
