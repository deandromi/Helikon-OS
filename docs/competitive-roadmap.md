# Competitive roadmap — Rockbox parity and Helikon-OS differentiation

> Purpose: compare Helikon-OS with Rockbox on the iPod Classic 6G and define
> what “a strong alternative in every aspect” means, feature by feature.
> Status: living document; comparison and plan, not evidence
> Owner: product owner; docs lead maintains the matrix
> Last updated: 2026-11-05
> Source: Rockbox iPod Classic manual, version `0d5c2b0604-261105` (built
> 2026-11-04, 285 pages); design paper §23, §28; decisions D-29–D-48

**This document is a plan.** Rockbox capabilities are taken from its published
manual and cited by section. Helikon-OS status is taken from
[`state.md`](state.md); a row is not evidence that a Helikon-OS feature exists.
No Helikon-OS feature is `implemented` because it appears here.

**Canonical-file split:** [`roadmap.md`](roadmap.md) owns phase sequencing and
phase gates; this file owns the competitor matrix, the parity policy, the
differentiation bets and the explicit declines. The two link to each other and
must not duplicate the same content.

The owner's instruction is that Helikon-OS should become a strong alternative
to Rockbox in every aspect and better in several. That is a product direction,
not a release claim. This document turns it into a finite parity set, a
differentiation set and an explicit decline list, so the project does not chase
every Rockbox plugin or codec and call the result progress.

---

## 1. Why a competitive roadmap exists

Rockbox is mature, broad and community-maintained. A new firmware that tries to
match every Rockbox feature at once will lose: it will be wide, shallow and
unsafe. Helikon-OS has a different thesis:

- native HFS+ instead of FAT32 and an overlay;
- a journaled, crash-consistent write path instead of best-effort host writes;
- signed, staged updates instead of an unzip;
- a host-built library index plus a resumable device builder;
- measured battery, RAM and frame budgets;
- a capability-sandboxed extension model instead of native plugins;
- a first-class desktop app and a documented install path.

The competitive roadmap has four jobs:

1. **Prevent a missing basic.** A user who leaves Rockbox must not lose play,
   browse, playlist, resume, sound controls, charging, sleep or recovery.
2. **Make the advantages measurable.** “Better” needs a metric and a recorded
   run; otherwise it is a slogan.
3. **Make the declines explicit.** Full WPS compatibility, Rockbox plugin
   binaries, FAT32 and every codec are not accidental omissions.
4. **Sequence the work behind the writer.** No UI, skin or plugin work may take
   device-testing time from phases 2–4.

---

## 2. Sources and method

### 2.1 Rockbox source

The comparison uses the official *Rockbox Manual for iPod Classic*, version
`0d5c2b0604-261105`, built 2026-11-04. Section numbers below refer to that
manual:

- installation, update and uninstall: §2;
- quick start, controls and USB charging: §3;
- file browser, database, WPS and playlists: §4;
- main menu, quick screen and shortcuts: §5;
- sound settings: §6;
- playback settings: §7;
- general settings, display, system, startup and voice: §8;
- themes and recording settings: §9–§10;
- plugins, viewers and applications: §12;
- advanced topics, theme files, `.cfg` settings, ROLO and battery: §13;
- file formats, audio formats, album art, theme tags and config options:
  Appendices A–E;
- menu overview and “features we will not implement”: Appendices F–G.

### 2.2 Helikon-OS source

Current status comes from [`state.md`](state.md), [`tasks.md`](tasks.md),
[`goals.md`](goals.md) and the accepted decisions D-01–D-28. Proposed decisions
D-29–D-48 are recorded in [`decisions.md`](decisions.md); they are not accepted
until the owner says so.

### 2.3 Rules

- A Rockbox feature is listed only if the manual documents it for the iPod
  Classic target. A feature that exists on another target is not counted.
- A Rockbox limitation is listed only if the manual states it. Do not infer a
  limitation from another target or from memory.
- A Helikon-OS target is marked `planned` or `proposed`; it is never described
  as working.
- A comparison claim needs a recorded Rockbox version/commit and a Helikon-OS
  image hash in [`validation.md`](validation.md) or
  [`device-matrix.md`](device-matrix.md).
- The manual is competitor documentation, not Helikon-OS evidence. Its text is
  summarised, not copied.

---

## 3. Executive summary

### 3.1 What Rockbox does well

Rockbox is ahead of Helikon-OS today in breadth and maturity:

- a large codec and container list, including lossy, lossless, chiptune,
  tracker, video and game-music formats (Appendix B);
- a mature file browser, database, playlist, bookmark, resume, cuesheet,
  shortcut and hotkey model (§4, §5, §8);
- a full theme engine with WPS/SBS viewports, images, fonts and many tags
  (§13.2, Appendix D);
- a large plugin, viewer and game catalogue (§12);
- voice menus, talk clips and many languages (§8.10–§8.11);
- USB mass storage, USB HID and USB keypad modes (§8.6.12);
- car-adapter mode, line-out control, accessory power and charging controls
  (§8.6);
- a parametric EQ, crossfeed, dithering, timestretch, Haas surround,
  perceptual bass, auditory-fatigue reduction and compressor (§6);
- ReplayGain, crossfade, gapless playback, A-B repeat, anti-skip buffer,
  headphone-unplug pause and pitch control (§4.3, §7);
- ROLO firmware loading and a dual-boot bootloader (§2, §13.4);
- release, development and daily channels, plus Rockbox Utility (§2.2);
- a large installed base, a long manual and years of device tuning.

That list is the bar for “does not feel like a downgrade.” It is not a list of
things Helikon-OS must copy in the same form.

### 3.2 Where Helikon-OS can be better

These are the leadership bets. Each needs a metric and a recorded run before it
is published:

| # | Bet | Why it matters | Measurable evidence | Decision |
|---:|---|---|---|---|
| 1 | Install without reformatting | Rockbox on the Classic requires FAT32; HFS+ iPods are unsupported (§2.1). Helikon-OS keeps native HFS+ and backs up before install. | A non-coder installs on an HFS+ 6G with no data loss and a verified backup | D-30 |
| 2 | Transactional data safety | Rockbox has no HFS+ journal path. Helikon-OS owns the journal, transaction and `fsck_hfs` gate. | Power-cut matrix, zero `fsck_hfs` repairs, macOS mount | D-03/D-04, D-32 |
| 3 | Signed, staged updates | Rockbox update is an unzip; ROLO loads a file (§2.4, §13.4.1). Helikon-OS verifies a signed manifest before any image byte. | Bad board/hash/signature rejected; cut during staging leaves the old image bootable | D-01/D-20, D-31 |
| 4 | Library index built for 10k tracks | Rockbox scans on device and has a 1,800-byte metadata budget and a 500-byte item limit on the Classic (Appendix B.2.3). Helikon-OS builds on the host and updates incrementally. | 10k tracks, host/device parity, resume at 25/50/75% | D-33 |
| 5 | Anti-corruption by default | The manual documents no data checksum or scrub. Helikon-OS adds a checksum sidecar, verify-on-open and a charging scrub. | Injected corruption detected, zero false OK, no playback dropout | D-11, D-39 |
| 6 | A real desktop app | Rockbox Utility installs, uninstalls, fonts, themes and voice files (§2.2.1). Helikon-OS adds backup, tagging, ratings, descriptions, playlists, podcast sync and opt-in scrobbling. | macOS task matrix; failed transfer leaves both sides consistent | D-09, D-38 |
| 7 | A modern, legible UI | Rockbox themes are powerful, but the default UI and bitmap fonts are not the Helikon-OS typography. | 30 fps scroll, <200 ms screen open, panel photos, clipping check | D-23/D-24, D-40 |
| 8 | Accessibility as a first-class path | Rockbox has voice menus and talk clips; Helikon-OS adds large text, high contrast, reduced motion and a focus model. | Five task tests with voice/large text; no dead ends | D-37 |
| 9 | A sandbox instead of native plugins | Rockbox plugins are native and broad; Helikon-OS runs signed, capability-limited apps and games. | Escape, quota and crash-isolation tests | D-08, D-44 |
| 10 | Adapter safety and measurement | Rockbox documents SD/CF/SATA quirks and older corruption bugs (§2.1.2). Helikon-OS records identify data, sector size, flush behaviour and a known-bad list. | SD/CF/SSD/HDD boot, play, write, sleep and 100-cycle resume | D-42 |
| 11 | Evidence discipline | Rockbox is mature; Helikon-OS is new and cannot borrow that trust. Every claim is tied to a hash and a run. | `validation.md` rows and `device-matrix.md` entries | D-29, D-40 |
| 12 | Privacy by construction | The device has no network; Helikon-OS adds no telemetry and makes scrobbling host-side and opt-in. | No network stack on device; host export is explicit | D-48 |

These bets are not all v1. Bets 1, 2, 3 and 6 are the strongest reason to choose
Helikon-OS; bets 4, 5, 7, 8, 9, 10 and 11 are v1.x/v2 work. The roadmap must
not pretend otherwise.

### 3.3 What “every aspect” means

“Every aspect” is interpreted as **every P0/P1 use case a Classic owner has**,
not every Rockbox plugin or codec. The parity set is:

- install, first boot, play, browse, pause, seek, next/previous, volume;
- library browse by file, artist, album, genre and track;
- playlists, queue, resume and bookmarks;
- sound controls a normal user expects: volume limit, balance, EQ, ReplayGain,
  gapless and crossfade;
- album art, now-playing screen, settings, clock and sleep;
- USB read-only, charging, safe eject and sleep/wake;
- recovery from a bad update, a corrupt file and a power cut;
- a documented way back to Apple firmware.

A missing plugin, a chiptune format or a full WPS tag is not a v1 regression if
it is documented in §7. A missing play, resume or recovery path is.

---

## 4. Parity policy (D-29 proposed)

Every row in the matrix gets one label:

| Label | Meaning | Acceptance |
|---|---|---|
| `match` | Same user-visible capability as Rockbox. | Side-by-side test on the same device and media. |
| `exceed` | Helikon-OS must be measurably better. | A metric, a baseline and a recorded run. |
| `differ` | Deliberate different design; not a regression. | The tradeoff and the user-visible difference are documented. |
| `decline` | Not planned for the stated horizon. | A reason and an alternative or workaround. |

Rules:

1. A `match` row cannot be claimed from a host test alone. It needs a device
   check or a documented host-equivalent oracle.
2. An `exceed` row needs a baseline from the same Rockbox build/device, not a
   number from a different card or battery.
3. A `differ` row must state what the user loses and what replaces it.
4. A `decline` row must be revisited when a maintainer provides a device, a
   licence or a concrete user demand; silence is not a decline.
5. The matrix is reviewed at every phase gate. A new Rockbox release does not
   automatically create new Helikon-OS scope.
6. “Strong alternative” is not a release note until the v1 threshold in §8
   passes.

---

## 5. Capability matrix

The matrix is grouped by user job. Horizon codes are defined in §6. Tasks are in
[`tasks.md`](tasks.md). “Rockbox” means the iPod Classic manual cited in §2.

### 5.1 Install, update and recovery

| Capability | Rockbox baseline (manual) | Helikon-OS target | Label | Horizon | Task |
|---|---|---|---|---|---|
| Filesystem on the Classic | Requires FAT32; HFS+ is not supported (§2.1) | Native HFS+; no FAT mount; FAT volume reports `NOT_HFS` | `differ` | H1 | T-062 |
| Install path | Rockbox Utility or manual `mks5lboot` DFU; administrator/root for the bootloader (§2.1–§2.2) | Guided macOS-first installer; signed files; DFU only for the bootloader; no admin for music files | `exceed` | H1 | T-062 |
| Reformat before install | FAT32 conversion is required; the manual links a conversion guide (§2.1) | No reformat; full backup, read-back and verify; refuse an unclean volume | `exceed` | H1 | T-062 |
| Original firmware / dual boot | The Rockbox bootloader can choose Rockbox or the original firmware (§2.5, §3.1.3) | Preserve the Apple NOR path; documented boot gesture; tested restore | `match` | H1 | T-063 |
| Uninstall | Rockbox Utility or manual uninstall; delete `.rockbox` (§2.5) | One-click restore; remove Helikon files; verify Apple boot | `match` | H1 | T-063 |
| Update | Unzip a build over the existing install; ROLO can load another `.ipod` file (§2.4, §13.4.1) | Signed staged update; preflight; bootstate fail-closed; manual recovery copy | `exceed` | H1 | T-064 |
| Update signature | No signature check is documented | Ed25519 manifest verified before any image byte (D-01) | `exceed` | H1 | T-064 |
| Release channels | Release, current build and archived daily build (§2.2.1) | Stable, beta and development channels; signed | `match` | H2 | T-086 |
| Support window | Not documented | Published support window, LTS line and security response | `exceed` | H2 | T-086 |
| Install documentation | Long manual and Rockbox Utility | Printable checklist plus app guidance; error codes with next action | `match` | H1 | T-062 |

### 5.2 Storage, filesystem and data safety

| Capability | Rockbox baseline (manual) | Helikon-OS target | Label | Horizon | Task |
|---|---|---|---|---|---|
| HFS+ journal awareness | No HFS+ path; the Classic target is FAT32 (§2.1) | Journal append, commit, replay and read-only refusal (D-03/D-04) | `exceed` | H1 | T-065 |
| Crash consistency | No HFS+ transaction or `fsck_hfs` release gate is documented | Bounded transactions; 346 host cuts; device power-cut matrix; `fsck_hfs` zero repairs (B-05) | `exceed` | H1 | T-065 |
| File operations | View, rename, cut, copy, paste, delete, new directory and properties (§4.1.2) | Same through the VFS and transaction engine | `match` | H1 | T-065 |
| Unicode names | The Classic does not process or normalize textual metadata (Appendix B.2.3) | HFS+ `FastUnicodeCompare`, decomposition and canonical ordering (D-06) | `exceed` | H1 | T-065 |
| Symlinks and hard links | Not documented for the FAT target | Explicit policy: read safely; creation deferred until proven | `differ` | H2 | T-065, D-32 |
| Resource forks / xattrs | Not documented | Preserve on copy; expose only through a capability; never silently drop | `differ` | H2 | T-065 |
| Data checksums and scrub | Not documented | Checksum sidecar, verify-on-open, charging/idle scrub, quarantine (D-11) | `exceed` | H2 | T-078 |
| Adapter quirks | SD/CF/SATA differences; older versions had corruption on flash mods (§2.1.2) | Identify-data quirks table, sector-size handling, flush errors, known-bad list | `match` | H1 | T-081 |
| Large volumes | FAT32 partitions cannot exceed 2 TiB; early 6G Apple firmware has a 128 GiB caveat (§2.1.1) | HFS+ volume limits; the same 6G caveat is documented and tested | `differ` | H2 | T-081 |

### 5.3 Library, database and browsing

| Capability | Rockbox baseline (manual) | Helikon-OS target | Label | Horizon | Task |
|---|---|---|---|---|---|
| File browser | Icons, context menu, virtual keyboard, sort and filter options (§4.1) | Same job; one navigation model and explicit empty/error states | `match` | H1 | T-094 |
| Database | On-device scan, load-to-RAM option, auto update and runtime data (§4.2) | Host-built immutable index plus on-device incremental delta; no full RAM copy | `exceed` | H1–H2 | T-066, T-067 |
| Metadata limits | 1,800 bytes total, 500 bytes per item, one tag type per extension, no normalization on the Classic (Appendix B.2.3) | Bounded but larger, normalized, sidecar-capable; no silent truncation | `exceed` | H1 | T-066 |
| Browse categories | Artist, album, genre, title and runtime data (§4.2.3–§4.2.4) | Same plus album/artist pages with local sidecars | `match` | H1–H2 | T-088 |
| Runtime data | Play count, rating, last played and most played (§4.2.3–§4.2.4) | Same; host-synced; survives a power cut | `match` | H2 | T-067, T-083 |
| Playlists | M3U/M3U8, dynamic current playlist, queue, save/load (§4.4) | Same; transactional save; import from Rockbox | `match` | H1 | T-093 |
| Bookmarks and resume | Bookmarks and automatic resume (§8.8–§8.9) | Same; per-podcast and per-audiobook resume | `match` | H1 | T-093 |
| Search | Database browsing and a Search viewer plugin (§4.2, §12.3.13) | Global search across library and files; bounded results | `match` | H2 | T-094 |
| Shortcuts, hotkeys, quick screen | §4.5, §5.12, §5.13 | Same, with a conflict-free default map | `match` | H2 | T-094 |
| Cuesheets | Cuesheet support, embedded or sidecar (§7.15) | Same; validate and report malformed cues | `match` | H2 | T-093 |
| Album art | BMP/JPEG; no RLE BMP, progressive or multi-scan JPEG (Appendix C) | JPEG/PNG, bounded decode, cache and missing-art fallback | `exceed` | H1 | T-068, T-088 |
| Settings files | `.cfg` files, `fixed.cfg`, save/load profiles (§13.3) | Import/export and profiles; schema-versioned; no silent reset | `match` | H2 | T-095 |

### 5.4 Playback, audio and formats

| Capability | Rockbox baseline (manual) | Helikon-OS target | Label | Horizon | Task |
|---|---|---|---|---|---|
| Codec breadth | Large lossy, lossless and chiptune/tracker list (Appendix B.1) | Tier 1 core first; Tier 2 evaluated; chiptune/tracker declined for v1 | `differ` | H1–H2 | T-068, T-069 |
| Gapless playback | Supported by many codecs (Appendix B.1.4) | Gapless for Tier 1; sample-accurate test | `match` | H1 | T-068 |
| ReplayGain | Track/album gain, prevent clipping, pre-amp (§7.10) | Same, with a measured CPU cost | `match` | H1 | T-070 |
| Equalizer | Up to 10-band parametric EQ, graphical/simple/advanced, presets (§6.9) | Core EQ in v1; advanced bands and presets in v1.x | `match` | H1–H2 | T-070, T-071 |
| Crossfeed | Direct/cross gain, HF attenuation and cutoff (§6.8) | Same; off by default | `match` | H2 | T-071 |
| Dither and noise shaping | High-pass triangular dither and third-order shaper (§6.10) | Optional; measure battery and audio benefit | `match` | H2 | T-071 |
| Timestretch and pitch | Pitch screen; timestretch after reboot (§4.3.3, §6.11) | Podcast/audiobook speed and pitch correction | `match` | H2 | T-071 |
| Compressor, Haas, bass, fatigue | Several advanced DSP effects (§6.12–§6.15) | Selective; no effect on by default; measure CPU | `differ` | H2 | T-071 |
| Crossfade and fade | Fade on stop/pause and configurable crossfade (§7.6, §7.9) | Same, bounded and gapless-safe | `match` | H1 | T-070 |
| A-B repeat | Supported (§7.2) | Same | `match` | H2 | T-093 |
| Anti-skip buffer | 5 seconds to 10 minutes; larger buffer costs power (§7.5) | Power-aware buffer with a measured sleep/wake tradeoff | `exceed` | H1 | T-080 |
| Headphone unplug | Pause and resume options (§7.14) | Same; safe default | `match` | H1 | T-070 |
| Volume limit | Volume limit setting (§6.2) | Same plus a first-boot safe limit | `match` | H1 | T-070 |
| Line-out and car | Car-adapter mode, delay, accessory power, line-out (§8.6.7–§8.6.10) | Tier 1 line-out/charging/resume; Tier 2 control subset (D-14) | `match` | H2 | T-089 |
| Internal DAC | Internal codec path | Measure CS42L55 noise, THD+N, response and channel separation | `match` | H2 | T-092 |
| Sample-rate policy | A playback frequency can be selected; other rates are resampled (§7.21) | Play native rates where the path supports it; document the resampler | `exceed` | H2 | T-092 |
| Video, image, text, chiptune, tracker | Viewers and plugins cover many formats (§12.3) | Not v1; optional sandboxed viewer later | `decline` | H3 | T-069, T-084 |
| Recording and FM | Recording settings exist in the manual; the Classic has no input/tuner | Not applicable to the 6G | `decline` | — | — |

### 5.5 UI, themes and accessibility

| Capability | Rockbox baseline (manual) | Helikon-OS target | Label | Horizon | Task |
|---|---|---|---|---|---|
| Theme engine | WPS/SBS/FMS files, viewports, 52 images, fonts and conditionals (§13.2, Appendix D) | Native declarative theme first; documented WPS subset second | `differ` | H1–H2 | T-072, T-073 |
| Full WPS compatibility | Full Rockbox theme language on the target | Explicitly not promised; unsupported tags are visible and logged | `decline` | — | D-10, D-15 |
| Fonts | `.fnt` bitmap fonts, glyph cache and per-screen fonts (§8.5, §13.2.4) | IBM Plex Sans subsets, anti-aliased atlas, 1-bit fallback (D-23/D-24) | `exceed` | H1 | T-072 |
| Graphics performance | 320×240×16; no published frame budget in the manual | 30 fps list scroll; <200 ms screen open; no clipping or tearing | `exceed` | H1 | T-079 |
| Voice menus | Voice files and talk clips; optional speech support (§2.2.4, §8.11) | Voice menus and talk clips for files, folders and errors | `match` | H2 | T-074 |
| Languages | Language files and a language setting (§8.10) | Localization pipeline with UTF-8, RTL and font fallback | `match` | H2 | T-075 |
| Accessibility | Voice support and a blind-users FAQ reference (§3.1.1, §8.11) | Voice, large text, high contrast, reduced motion and a focus model | `exceed` | H2 | T-074 |
| Custom main menu | Configurable main menu (§13.1.1) | Configurable home with safe defaults | `match` | H2 | T-072 |
| Status bar and peak meter | Status bar, peak meter and many theme tags (Appendix D) | Same information, simpler defaults | `match` | H1 | T-072 |
| Clock and time/date | Time and date settings (§11) | RTC-backed clock; no fake time when the RTC is absent | `match` | H1 | T-072, T-079 |
| Display and backlight | Backlight, brightness, sleep and scrolling settings (§8.5) | Same, with measured power defaults and reduced motion | `match` | H1 | T-079, T-080 |
| Reduced motion | Not documented | A setting; transitions have a frame budget | `exceed` | H2 | T-074 |

### 5.6 Connectivity, desktop and ecosystem

| Capability | Rockbox baseline (manual) | Helikon-OS target | Label | Horizon | Task |
|---|---|---|---|---|---|
| USB mass storage | Mounts as MSC/UMS; drag-and-drop music transfer (§3.1.4) | Read-only LUN; service mode for writes (D-09) | `differ` | H1–H2 | T-076, T-077 |
| Writable USB LUN | The host can write the FAT volume directly | Rejected; every write goes through the VFS/transaction engine | `decline` | — | D-09 |
| USB HID | HID composite and keypad modes (§8.6.12) | HID keyboard/media keys; no writable LUN | `match` | H2 | T-076 |
| Charge-only mode | Hold a button or select charge-only (§3.3) | Same; safe default when the host is unknown | `match` | H1 | T-076 |
| Desktop app | Rockbox Utility handles install, uninstall, themes, fonts and voice (§2.2.1) | Full macOS-first app: backup, tag, rate, describe, playlists, podcast, install, verify, scrobble | `exceed` | H1–H2 | T-083 |
| Podcasts | No dedicated podcast workflow in the Classic manual | Host subscription/download; device resume, speed, chapters, played state | `exceed` | H2 | T-082 |
| Scrobbling | Last.fm Scrobbler plugin and playback log (§7.23, §12.4.12) | Host-side, opt-in, offline queue; no device network | `differ` | H2 | T-083 |
| Car and receiver | Car-adapter mode and line-out controls (§8.6.7–§8.6.10) | Tier 1 first; Tier 2 subset with legal review (D-14) | `match` | H2 | T-089 |
| Inline remote | Apple-compatible inline remote support, not on the 2007 model (§3.1.1) | Support where the hardware revision allows; record the model | `match` | H2 | T-094 |
| Network and cloud | No network stack on the device | No network stack; no telemetry; host does network work | `match` | — | D-48 |

### 5.7 Power, performance and adapters

| Capability | Rockbox baseline (manual) | Helikon-OS target | Label | Horizon | Task |
|---|---|---|---|---|---|
| Battery tuning | Backlight, display off, accessory power, line-out, anti-skip, ReplayGain, codec and DSP guidance (§13.5) | Explicit power states, disk sleep, buffer sizing and charge-aware work | `exceed` | H2 | T-080 |
| Sleep timer and idle poweroff | Sleep timer, idle poweroff, start screen and hold-reset (§8.7) | Same; safe defaults; no hidden button sequence for normal use | `match` | H1 | T-080 |
| Battery measurement | Battery Benchmark plugin and CSV log (§12.4.2) | Same data plus a host report; three-run baseline vs Rockbox | `match` | H2 | T-080 |
| RAM use | Directory cache, buffer and limits settings (§8.6.2–§8.6.3) | Hard budgets, watermark and 72-hour soak (goal 4) | `exceed` | H1 | T-079 |
| Startup time | Not stated in the manual | 6.5 s loader today; target 2–3 s (T-052), then <2 s to UI | `exceed` | H1 | T-052, T-079 |
| UI frame time | Not stated in the manual | 30 fps scroll; <200 ms screen open; measured on device | `exceed` | H1 | T-079 |
| Write speed | No published benchmark in the manual | Measure raw device, journal cost and 1,000-file create/delete | `exceed` | H2 | T-079 |
| Adapter power and quirks | SD adapters omit some ATA power commands; cards vary widely (§2.1.2) | Quirks table, conservative defaults, APM and known-bad list | `match` | H1 | T-081 |
| Thermal behaviour | Not documented | Thermal policy and charge-aware throttling; no unmeasured claim | `exceed` | H2 | T-080 |

### 5.8 Extensibility, community and ports

| Capability | Rockbox baseline (manual) | Helikon-OS target | Label | Horizon | Task |
|---|---|---|---|---|---|
| Plugins | Large native plugin catalogue (§12) | No native plugin ABI; sandboxed apps/games only | `decline` | — | D-08, D-44 |
| Games | Many built-in games (§12.1) | Sandboxed games through the Helikon-OS runtime | `differ` | H3 | T-084 |
| Applications | Viewers and applications in the plugin tree (§12.3–§12.4) | Sandboxed apps with permissions, quotas and signing | `differ` | H3 | T-084 |
| Lua scripting | Lua viewer plugin (§12.3.5) | Not planned for v1; evaluate only after the sandbox exists | `decline` | H3 | T-084 |
| Plugin SDK | Rockbox plugin API | New SDK, host simulator, examples and capability docs | `differ` | H3 | T-084 |
| Theme distribution | Theme files and a themes site | Desktop app install, 20-skin corpus, licence review | `match` | H2 | T-073 |
| Licence | GPL family; the manual is GFDL | GPL-2.0-or-later (D-18) | `match` | — | — |
| Documentation | Large maintained manual | Living docs, install checklist, error codes, competitive matrix | `match` | H1 | T-090 |
| Translations | Language files and community translations | Community localization pipeline | `match` | H2 | T-075 |
| Device support | Several iPod models (§2.1) | 6G reference, 7G after board audit, other players as ports (D-13) | `differ` | H3 | T-087 |
| Release cadence | Release, current and daily builds (§2.2.1) | Stable, beta, development and an LTS line | `match` | H2 | T-086 |
| Governance | Rockbox team and community process | Owner-led with public decisions, docs and evidence; governance still open | `differ` | H2 | T-086 |

---

## 6. Horizon lanes

These lanes run across the phase roadmap; they do not renumber phases 0–12 and
they do not authorize device work ahead of the writer gate. H1 starts only when
its dependency phase has passed.

| Lane | Scope | Starts after | Tasks | Exit criterion |
|---|---|---|---|---|
| H1-A Install and recovery | Native-HFS+ install, backup/verify, dual boot, uninstall, signed update, recovery | Phase 4 + T-051 | T-062, T-063, T-064 | A non-coder installs, updates, rolls back and restores on a 6G with no data loss |
| H1-B Storage and safety | VFS semantics, journal path, adapter matrix, `fsck_hfs` gate | Phase 3 | T-065, T-081 | Crash matrix and adapter matrix pass; zero `fsck_hfs` repairs |
| H1-C Library parity | File browser, playlists, bookmarks, resume, cuesheets, 10k index | Phase 4 | T-066, T-093, T-094 | 10k tracks browse and resume; host/device parity; no playback stall |
| H1-D Playback parity | Tier 1 codecs, gapless, ReplayGain, EQ, crossfade, volume limit | Phase 1 + audio HAL | T-068, T-070 | 20-track codec corpus; gapless and seek pass; no clipping |
| H1-E UI parity | First-party renderer, themes, status bar, now playing, album art | Phase 1 + T-042 | T-072, T-079 | 30 fps scroll, <200 ms open, panel photos, no clipping |
| H1-F Desktop and USB | Read-only LUN, HID, charge-only, macOS app MVP | Phase 9 | T-076, T-083 | Backup, import, tag, install and verify pass on macOS |
| H2-A Data leadership | Checksum sidecar, scrub, quarantine, corruption reporting | H1-B | T-078 | Injected corruption detected; zero false OK; no playback dropout |
| H2-B Index leadership | On-device incremental builder, power-cut resume | H1-C | T-067 | 10k build resumes at 25/50/75%; host parity |
| H2-C Audio leadership | Tier 2 codecs, advanced DSP, DAC measurements | H1-D | T-069, T-071, T-092 | Published measurements; no battery regression without a note |
| H2-D Interface leadership | WPS subset, 20-skin corpus, voice, large text, localization | H1-E | T-073, T-074, T-075 | 20 skins render; voice and large-text tasks pass |
| H2-E Ecosystem | Podcast/audiobook, car Tier 1/2, service mode, migration | H1-F | T-077, T-082, T-085, T-089 | 500 episodes; 100 ignition cycles; migration verifies |
| H2-F Release and support | Channels, LTS, support window, security response | H1-A | T-086 | A signed release train and a published support window |
| H3-A Sandbox | App/game runtime, SDK, permissions, quotas, signing | H2-D + H2-F | T-084 | Escape, quota and crash-isolation tests pass |
| H3-B Ports | HAL contract, board description, 7G readiness | H2-F + owner hardware | T-087 | Board audit and per-target matrix; no support claim before it |
| H4 Continuous | Parity scorecard, differential harness, usability, privacy review | Now | T-060, T-061, T-090, T-091 | Matrix reviewed each phase; no stale claim |

---

## 7. Explicit declines

These are deliberate. A future owner decision may reopen one; an agent may not.

| Declined | Reason | Alternative |
|---|---|---|
| Full Rockbox WPS engine | Large tag language, unbounded assets, conflicts with the lightweight and safety goals | Documented WPS subset plus a native declarative theme (D-10/D-15) |
| Rockbox plugin/game binary compatibility | Native plugins expose broad hardware and are not a capability sandbox | Signed sandboxed apps/games with quotas (D-08/D-44) |
| FAT32 support | The defining product promise is native HFS+ (B-02) | `NOT_HFS` screen; backup and conversion guidance; no hidden FAT fallback |
| Writable USB mass-storage LUN | A raw writable LUN bypasses the transaction engine | Read-only LUN plus a framed service mode (D-09/D-38) |
| Self-healing repair | HFS+ data is not checksummed and silent repair can destroy evidence | Detect, quarantine, restore from backup (D-11/D-39) |
| Video, chiptune and tracker formats in v1 | CPU, code size, battery and test cost; not a P0 use case | Optional sandboxed viewers after v2 |
| External USB DAC on the 6G | The 6G is a USB device, not a USB host | Use and measure the internal CS42L55 path |
| Cloud, streaming and network on device | No network stack; privacy and battery cost | Host-side download, sync and scrobble |
| Apple accessory certification claim | Legal and certification risk; no certification exists | Tier 1 line-out/charging; Tier 2 subset with a legal review |
| 7G support before a board audit | Hardware differences are not documented enough | 6G reference; 7G after owner hardware and audit |
| Every iPod or MP3 player at launch | A port needs a BSP, hardware, maintainer and matrix | 6G, then 7G, then documented ports |
| Telemetry | No consent model and no need for a local player | Local logs and explicit host export |

---

## 8. Release thresholds

### 8.1 v1 — the “no-regret” set

v1 is allowed to have fewer codecs, plugins and themes than Rockbox. It is not
allowed to lose a basic job. The v1 competitive gate:

1. Install on an HFS+ 6G without reformatting, with a verified backup.
2. Boot Helikon-OS and boot Apple firmware through the documented path.
3. Play MP3, AAC-LC, ALAC, FLAC, WAV and AIFF; gapless where the format allows.
4. Browse by file, artist, album, genre and track; open an album in <200 ms.
5. Create, save, load and resume a playlist; bookmark and resume a track.
6. Volume limit, balance, EQ, ReplayGain, crossfade and gapless work without
   clipping.
7. Album art and a now-playing screen render without tearing or clipped text.
8. USB read-only, charge-only, safe eject and 100 sleep/wake cycles pass.
9. A signed update rejects a bad board, hash, signature or truncated file; a
   power cut during staging leaves the old image bootable.
10. A power cut during a write recovers to a `fsck_hfs`-clean volume.
11. Uninstall restores Apple firmware and leaves no Helikon-owned file behind.
12. A non-coder completes install, play, update and restore with the checklist.

If any of these fails, v1 is not a Rockbox alternative regardless of the feature
count.

### 8.2 v1.x — the differentiation set

- host-built and on-device index with 10k-track parity and power-cut resume;
- checksum sidecar, verify-on-open and charging scrub;
- 20-skin WPS corpus plus the native theme;
- voice menus, talk clips, large text and high contrast;
- desktop app tagging, ratings, descriptions, podcast sync and opt-in
  scrobbling;
- Tier 2 codecs and advanced DSP, each with a measured battery/CPU note;
- car Tier 1 and the receiver matrix;
- migration from Rockbox and Apple;
- published battery/RAM/performance results against a recorded baseline.

### 8.3 v2 — the platform set

- sandboxed apps and games with an SDK and adversarial tests;
- service mode for Windows/Linux;
- 7G after a board audit;
- additional ports only with hardware, a maintainer and a matrix;
- LTS and a public support window.

---

## 9. Differential evidence protocol

A comparison is only useful if it is reproducible.

1. Record the Rockbox version, build date/commit and the manual version.
2. Record the Helikon-OS image hash, board revision, adapter/card, battery
   health and filesystem.
3. Use the same album, volume, backlight, output and shuffle/repeat settings.
4. Run the same task list: boot, browse, play, pause, seek, skip, resume,
   playlist, EQ, ReplayGain, USB, charge, sleep, update, restore.
5. Capture screen photos or frame traces where the claim is visual.
6. Capture `fsck_hfs -fn` and `diskutil verifyVolume` after any write.
7. Record the result in [`validation.md`](validation.md) or
   [`device-matrix.md`](device-matrix.md), not in chat.
8. Mark a result `not run` rather than inferring it.

A claim such as “cleaner” or “faster” is not allowed unless the metric and the
baseline are in the row.

---

## 10. Risks and open questions

| Risk / question | Impact | Mitigation / owner decision |
|---|---|---|
| Rockbox breadth is much larger at launch | Users may see missing codecs/plugins as a regression | v1 threshold, explicit declines, tiered codec plan |
| Native HFS+ excludes FAT32 iPods | Smaller install base | Clear `NOT_HFS` guidance; no hidden fallback; owner decision B-02 |
| The writer is the critical path | Every feature lane waits on it | H1 starts after phase 3/4; no side lane consumes device tests |
| Signed updates and key custody | A lost key or bad certificate can block updates | D-20/ADR-0002; T-053; bootloader fail-closed |
| Battery/RAM claims can be wrong | Loss of credibility | Three-run baseline; publish range; withdraw unsupported claims |
| Skin/theme assets have mixed licences | Legal risk | 20-skin corpus with licence review; no bundled proprietary assets |
| Voice files need a TTS licence | Distribution risk | Use user-generated voice files; record the engine and licence |
| AAC/ALAC codec licensing | Distribution risk | Legal review before release; no patent claim |
| Service mode expands the USB stack | Security and schedule risk | Read-only LUN first; framed protocol; auth and fuzzing |
| 7G hardware is not present | Port cannot be validated | Owner hardware and board audit first |
| `fsck_hfs` is the only oracle | Some HFS+ bugs may pass it | Differential macOS tests, catalog/bitmap model checks, corruption injection |

Open questions that need an owner decision:

- Which Rockbox release is the official baseline for v1 comparisons?
- Is booting Apple firmware a v1 requirement or a documented recovery path?
- Do we support HFS+ symlinks and hard links on write, or only read/preserve?
- Which codec licences are acceptable for distribution?
- Is a voice file generated locally, downloaded, or both?
- Does the service mode require a pairing code, a signed host, or both?
- Which 20 skins form the corpus, and what are their licences?
- Which battery/RAM metric is the public headline: hours, percentage, or both?

---

## 11. Design sketches for the largest bets

These are implementation sketches, not frozen interfaces. They give the task
owner a starting shape and name the safety boundary. A frozen interface needs
its own review and a host test before code depends on it.

### 11.1 Installer state machine

```text
DETECT -> VERIFY_VOLUME -> BACKUP -> READ_BACK -> DFU_BOOTLOADER
       -> FIRST_BOOT -> SELF_TEST -> RECORD
```

Abort with a specific error and no write when:

- the volume is not native HFS+ v4 (`NOT_HFS`);
- the journal is dirty or invalid (`JOURNAL_DIRTY`);
- `fsck_hfs` reports a repair (`CORRUPT`);
- the backup read-back hash differs (`BACKUP_MISMATCH`);
- the board/revision is unknown or unsupported (`BOARD_UNSUPPORTED`);
- DFU entry or the bootloader install fails (`DFU_FAILED`);
- the signed image fails magic/board/version/address/size/hash/signature
  (`IMAGE_REJECTED`).

The installer never formats, never deletes the journal and never uses
`--single`. A failed bootloader step must leave the Apple path and DFU
recovery available. T-062 owns the flow; T-063 owns the restore path.

### 11.2 Update state machine

```text
IDLE -> STAGED -> VERIFIED -> ARMED -> TRYING -> GOOD
                                      \-> FAILED -> recovery screen
```

- `STAGED`: files under `/.helikon/update/`, written through `hfs_txn`.
- `VERIFIED`: manifest, hash and signature checked before install and again
  before jump.
- `ARMED`: the user explicitly chose install; no silent update.
- `TRYING`: the new image boots once and must promote itself to `GOOD`.
- `FAILED`: no jump on the next boot; recovery is manual/DFU (D-02/D-31).
- A recovery copy under `/.helikon/recovery/` is a file for manual restore, not
  an automatic A/B slot.

The state record is the existing two-slot bootstate; no new boot path is
created.

### 11.3 Library index

```text
/.helikon/index/
  CURRENT                 pointer to the active generation, written by hfs_txn
  00000001/
    manifest              schema version, generation, counts, checksums
    tracks                typed columns
    strings               normalized name/tag string pool
    art                   artwork references and hashes
    delta                 device-side operations since the generation
```

A track row needs at least: CNID, parent CNID, name, kind, size, mtime,
duration, bitrate, sample rate, channels, title/artist/album/albumartist/
genre/year/track/disc, art reference, optional content hash and source. The
delta log holds add/remove/rename/rating/play-state records with a sequence.
The device applies deltas to an overlay, validates counts and checksums, then
swaps `CURRENT` atomically. A stale or corrupt generation falls back to a
catalog scan and reports `DB_STALE`; it never blocks playback.

### 11.4 Anti-corruption sidecar

```text
record = cnid, path, size, mtime, sha256, tier, last_verified, status
status = OK | MISMATCH | UNREADABLE | NOT_CHECKED
```

Two generations live under `/.helikon/checksums/`, each with its own checksum.
Critical files are verified on open; imported media is hashed on import, write
close and desktop transfer. A full scrub is charging/idle only, rate-limited
and resumable. A mismatch is reported and quarantined, never rewritten or
deleted. A corrupt index is rebuilt by reading files, not trusted.

### 11.5 Service-mode frame

```text
magic "HLKS" | version | op | flags | sequence | payload_len | payload | crc32
```

The frame is bounded and versioned. Operations are file-level: stat, list, open,
read, write, append, rename, delete, mkdir, sync and close. There is no
read-block/write-block/NOR/PMU operation. A session requires explicit user
opt-in and authentication; it times out and returns to the read-only LUN. The
device applies every write through the VFS and transaction engine. T-077 owns
the protocol; T-091 owns the threat review.

### 11.6 Theme package

```text
theme/
  manifest        id, name, version, author, licence, capabilities, checksums
  layout          declarative screens and viewports
  fonts/          licensed font subsets
  images/         bounded image assets
```

The manifest is data only. The parser caps node count, image count and
dimensions, font count and per-frame work. A theme cannot execute code, read
outside its package or change storage/update settings. A malformed package is
rejected with a reason; the first-party UI remains available. WPS compatibility
is a separate parser that maps a documented tag subset to the same scene model.

### 11.7 Sandbox capabilities

| Capability | What it grants | Default | Risk control |
|---|---|---|---|
| `display` | Draw inside the app viewport | Ask | Viewport and frame budget |
| `input` | Receive wheel/button events while focused | Ask | Focus and escape guarantee |
| `audio` | One mixed audio stream with a volume cap | Ask | Channel and duration quota |
| `app_storage` | Read/write `/.helikon/apps/<id>/` | Grant | Quota and path jail |
| `library_read` | Read metadata/art through the VFS capability | Ask | No raw path or block access |
| `library_write` | Transactional writes to a user-approved folder | Deny | Explicit folder grant, audit log |
| `network` | Not available on the device | Deny | No network stack |
| `raw_storage` | Not available to apps | Deny | No block/NOR/PMU/GPIO |

Quotas cover memory, CPU slice, audio channels and save data. A trap or watchdog
returns to the home screen; a crashing app cannot panic the kernel. T-084 owns
the runtime and SDK.

---

## 12. Related

- [`roadmap.md`](roadmap.md) — phases 0–12 and the horizon lanes
- [`tasks.md`](tasks.md) — T-060–T-095
- [`decisions.md`](decisions.md) — D-29–D-48 (proposed)
- [`goals.md`](goals.md) — the 25 README goals and acceptance
- [`validation.md`](validation.md) — release scorecard and evidence
- [`device-matrix.md`](device-matrix.md) — device, adapter and receiver runs
- [`design-paper.md`](design-paper.md) — §23 roadmap, §28 goals, Appendix I
- [`ui.md`](ui.md) — typography and rendering rules
- [`hfsplus-write-engine.md`](hfsplus-write-engine.md) — writer and journal design
- [`release.md`](release.md) — channels, signing and support
- [`faq.md`](faq.md) — user-facing answers
- Rockbox manual, *Rockbox Manual for iPod Classic*, version
  `0d5c2b0604-261105`, 2026-11-04 (competitor reference; not bundled)
