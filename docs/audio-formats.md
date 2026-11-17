# Audio formats — Rockbox parity and Helikon-OS support plan

> Purpose: list every audio format Rockbox documents for the iPod Classic 6G,
> define what Helikon-OS must support, and record the 6G hardware/CPU ceiling.
> Status: living document; Tier plan plus a first 16-bit PCM/WAV playback
> path; no compressed decoder and no device run.
> Owner: audio lead; firmware lead
> Last updated: 2026-11-07
> Source: Rockbox iPod Classic manual `0d5c2b0604-261105`, Appendix B.1–B.2
> and §4.3.3, §7.21–§7.22, §13.5.8; Rockbox codec performance page, iPod
> Classic run `b320bba`, 2014-12-12; Apple iPod classic technical
> specifications; Rockbox `ipod6g.h`; Cirrus Logic CS42L55 datasheet metadata;
> [`competitive-roadmap.md`](competitive-roadmap.md)

**This is a plan.** No Helikon-OS decoder exists yet. The only measurements in
this document are Rockbox's 2014 iPod Classic benchmarks; they are competitor
reference data, not Helikon-OS results. A format is not supported because it
appears in a table.

The owner's requirement is:

1. **at least every audio format documented in the Rockbox iPod Classic
   manual**;
2. **more than Rockbox where the 6G hardware supports it**, without inventing
   hi-res claims the output path cannot deliver.

This document turns that into a finite format list, a three-tier plan, a
decoder architecture and a test corpus.

---

## 1. Scope

### 1.1 What counts

The authoritative audio list is Rockbox manual Appendix B.1:

- **B.1.1 Lossy codecs** — 13 families;
- **B.1.2 Lossless codecs** — 10 families;
- **B.1.3 Other codecs** — 13 families.

Plus **Opus**, which the manual documents in §7.21 (“Opus files are always
48kHz”) and in the permitted-extension list in §13.1.7, even though it is
missing from the Appendix B.1 table. That makes **37 audio format families**.

A “family” can contain several codecs, profiles or block types:

- MPEG audio covers Layer 1, Layer 2 and Layer 3;
- AAC covers AAC-LC, HE-AAC v1 and HE-AAC v2;
- MPEG-4 audio covers AAC and ALAC in M4A/MP4;
- WAV/W64 cover PCM, IEEE float, a-law/µ-law and several ADPCM variants;
- DSD covers DSD64 and higher, with downsampling;
- VGM/VGZ is one family with two containers.

### 1.2 What does not count

- **Video** — MPEG-1/2, H.264, MP4 video, RealVideo, WMV video. The manual
  lists video viewers and video extensions, but the owner asked for audio
  formats. Video stays out of scope (D-29/competitive roadmap).
- **Recording** — the manual documents recording settings, but the 6G has no
  audio input. Not applicable.
- **FM radio** — the manual has FM screen settings for targets with a tuner;
  the 6G has no tuner. Not applicable.
- **Voice files and talk clips** — an accessibility feature, not a music
  playback format. D-37 owns them.
- **MP3 encoding** — a plugin, not playback. A future encoder is optional and
  not part of format parity.
- **The `.colours` extension list** — a UI colour list, not a codec contract.
  Extensions named only there (`ahn`, `rmt`, `tmc`, `tm8`, `tm2`, `cm4`, `at3`,
  `ace`, `vtx`, `mv`) are recorded in §3.5 as alias research; they are not
  counted as guaranteed codecs until the decoder mapping is verified.

### 1.3 The promise

- **v1:** Tier 1 core (eight families) plays, seeks, reports metadata and
  survives power cuts.
- **v1.x:** Tier 2 — every Rockbox audio family in Appendix B.1 plus Opus —
  plays from the native library with a recorded corpus. This is a **hard
  parity requirement**, not an optional plugin list.
- **Beyond v1.x:** Tier 3 candidates that the hardware can actually support,
  proven by measurement, not by a datasheet guess.

“Support” means: a valid file opens, decodes in real time, reports metadata,
seeks/resumes where the format allows, does not crash on a malformed file, and
has a recorded device run. A format that only decodes on a host is not
supported.

---

## 2. The 6G hardware ceiling

### 2.1 Confirmed platform facts

| Fact | Value | Source |
|---|---|---|
| SoC | Samsung S5L8702, ARM926EJ-S, ARMv5 | The Apple Wiki; Rockbox `ipod6g.h` |
| CPU clock | 216 MHz maximum; adjustable (Rockbox uses 54 MHz unboosted) | Rockbox `ipod6g.h`, codec benchmark |
| RAM | 64 MiB DRAM on the iPod classic | The Apple Wiki |
| Codec buffer | 512 KiB (`CODEC_SIZE`) | Rockbox `firmware/export/config/ipod6g.h` |
| Plugin buffer | 2 MiB (`PLUGIN_BUFFER_SIZE`) | Rockbox `ipod6g.h` |
| Audio codec | Cirrus Logic CS42L55, 24-bit stereo | Cirrus Logic / The Apple Wiki |
| Codec conversion class | 48 kHz | Cirrus Logic CS42L55 datasheet metadata |
| Stock Apple formats | AAC 16–320 kbps, protected AAC, MP3 16–320 kbps, MP3 VBR, Audible 2/3/4, ALAC, WAV, AIFF | Apple iPod classic technical specifications |

### 2.2 What is proven and what is not

**Proven for the competitor:**

- Rockbox decodes the formats in Appendix B.1 on the 6G. Its 2014 benchmark
  table gives per-codec CPU cost at 216 MHz boosted and 54 MHz unboosted.
- The CPU is fast enough for the measured codecs at 216 MHz; at 54 MHz,
  AAC-HE/HEv2 and APE above `-c1000` fail real-time, while the rest of the
  measured set still passes.
- The codec buffer is 512 KiB, so a decoder cannot assume unlimited RAM or a
  private heap.

**Not proven / open:**

- The exact I2S word length and sample-rate path used by the 6G for 24-bit
  output. The CS42L55 is a 24-bit, 48 kHz-class codec, but Apple documents the
  format list, not the internal bit depth.
- Whether the 6G can output 24-bit/44.1 or 24-bit/48 without truncation. This
  needs a measurement (T-098), not a claim.
- Native 88.2/96 kHz output is outside the codec's documented 48 kHz class.
  A 96 kHz file must be decoded and resampled to 48 or 44.1 kHz, or rejected
  with a reason. Non-stock overclocking experiments are not a supported path.
- DSD is a 1-bit format; the CS42L55 is a PCM DAC. DSD must be converted to
  PCM with a documented low-pass filter. There is no native DSD output.

**Design consequence:** Helikon-OS can beat Rockbox on **24-bit/48 kHz PCM and
24-bit lossless decode** only if T-098 proves the path. It cannot honestly claim
24/96, native DSD, or “hi-res” output on the stock 6G. A 24/96 file can still
play after a high-quality downsample; that is compatibility, not hi-res.

### 2.3 CPU budget policy

The measured Rockbox data shows that the 216 MHz ceiling is not needed for
most formats. The player should choose the lowest clock that keeps decode
headroom, then boost only when the codec needs it:

| Clock | Use |
|---|---|
| 54 MHz | MP3, FLAC, Vorbis, Musepack, WMA, WavPack, Cook, MP1/MP2, most chiptunes |
| 108 MHz | AAC-LC, ALAC, TTA, ATRAC3, Opus high bitrate, AC3 |
| 216 MHz | AAC-HE/HEv2, APE `-c1000` near the edge, DSD, worst-case tracker/chiptune bursts |

A codec descriptor declares its `min_clock` and `recommended_clock`. The player
boosts before decode and drops back after a measured idle window. A decoder
that misses its deadline returns `CPU_TOO_SLOW`; it must not stutter silently.
The exact thresholds are targets until T-101 measures Helikon-OS.

---

## 3. Rockbox audio inventory

CPU values are **Rockbox measurements on an iPod Classic at 216 MHz boosted**
(the same page also has 54 MHz unboosted values). “Not measured” means the 2014
page does not list that family; it is not a claim that it is fast or slow.

### 3.1 Lossy codecs

| # | Format | Extensions | Rockbox notes | 6G decode cost (Rockbox) | Tier |
|---:|---|---|---|---|---|
| 1 | ATSC A/52 (AC3) | `.a52`, `.ac3`, `.rm`, `.ra`, `.rmvb` | 5.1 downmixed to stereo | 17.6 MHz (192 kbps) | T2 |
| 2 | ADX | `.adx` | Encrypted ADX unsupported | not measured | T2 |
| 3 | Advanced Audio Coding (AAC) | `.aac`, `.m4a`, `.m4b`, `.mp4`, `.rm`, `.ra`, `.rmvb` | AAC-LC, HE-AAC v1, HE-AAC v2 | LC 37–46 MHz; HE 82 MHz; HEv2 101 MHz | T1 LC, T2 HE/HEv2 |
| 4 | MPEG audio | `.mpa`, `.mpga`, `.mp1`, `.mp2`, `.mp3` | MPEG-1/2/2.5 Layer 1/2/3 | 20–29 MHz | T1 MP3, T2 MP1/MP2 |
| 5 | Musepack | `.mpc` | SV7/SV8, mono/stereo | 22–25 MHz | T2 |
| 6 | Ogg Vorbis | `.ogg`, `.oga` | Some floor-0 files may fail; huge art may be skipped; chained Ogg unsupported | 19–28 MHz | T1 |
| 7 | Sony Audio (ATRAC3) | `.oma`, `.aa3`, `.rm`, `.ra`, `.rmvb` | ATRAC3 | 28.3 MHz | T2 |
| 8 | RealAudio (Cook) | `.rm`, `.ra`, `.rmvb` | RealAudio G2 (Cook) | 12–27 MHz | T2 |
| 9 | Speex | `.spx` | Dialogic/telephony codec | not measured | T2 |
| 10 | Dialogic VOX | `.vox` | Telephony ADPCM | not measured | T2 |
| 11 | Windows Media Audio Standard | `.wma`, `.wmv`, `.asf` | WMA Standard | 27–30 MHz | T2 |
| 12 | Windows Media Audio Professional | `.wma`, `.wmv`, `.asf` | WMA Professional | 23–29 MHz | T2 |
| 13 | Direct Stream Digital (DSD) | `.dsf`, `.dff`, `.iso` | DSD64 and beyond; may downsample; SACD ISO | not measured; heavy | T2 |
| 14 | Opus | `.opus` | Always 48 kHz; documented in §7.21 | 26–50 MHz | T1 |

### 3.2 Lossless codecs

| # | Format | Extensions | Rockbox notes | 6G decode cost (Rockbox) | Tier |
|---:|---|---|---|---|---|
| 15 | Audio Interchange File Format (AIFF) | `.aif`, `.aiff` | PCM 8/16/24/32, float 32/64, a-law/µ-law, QuickTime IMA ADPCM | not measured; low | T1 |
| 16 | Monkey’s Audio (APE) | `.ape`, `.mac` | Only `-c1000` decodes fast enough to be useful | `-c1000` 40 MHz; `-c2000` 57 MHz; `-c3000` 86 MHz; `-c4000` 220 MHz; `-c5000` 932 MHz | T2 (`-c1000` required; higher best-effort) |
| 17 | Sun Audio | `.au`, `.snd` | PCM 8/16/24/32, float 32/64, a-law/µ-law | not measured; low | T2 |
| 18 | Free Lossless Audio (FLAC) | `.flac` | Multichannel/high-res downmixed to 16-bit stereo | 7.5–8.1 MHz | T1 |
| 19 | Apple Lossless (ALAC) | `.m4a`, `.mp4` | Apple Lossless | 34.9 MHz | T1 |
| 20 | Shorten | `.shn` | Seeking unsupported | not measured; low | T2 |
| 21 | True Audio (TTA) | `.tta` | True Audio | 35.6 MHz | T2 |
| 22 | Wave64 | `.w64` | Same formats as WAV | not measured; low | T2 |
| 23 | Waveform audio (WAV) | `.wav` | PCM 8/16/24/32, float, a-law/µ-law, MS/IMA/OKI/Yamaha/Adobe ADPCM | not measured; low | T1 |
| 24 | WavPack | `.wv` | WavPack | 23–28 MHz | T2 |

### 3.3 Other codecs

| # | Format | Extensions | Rockbox notes | 6G decode cost (Rockbox) | Tier |
|---:|---|---|---|---|---|
| 25 | Atari Sound Format | `.cmc`, `.cm3`, `.cmr`, `.cms`, `.dmc`, `.dlt`, `.mpt`, `.mpd` | Atari chip music | not measured; low | T2 |
| 26 | Synthetic Music Mobile Application Format (MMF/SMAF) | `.mmf` | PCM/ADPCM only | not measured; low | T2 |
| 27 | Game Boy Sound Format | `.gbs` | Subtrack progress/seek | not measured; low | T2 |
| 28 | AY Sound Chip Music | `.ay` | Subtrack progress/seek | not measured; low | T2 |
| 29 | Hudson Entertainment System Sound | `.hes` | Subtrack progress/seek | not measured; low | T2 |
| 30 | MSX Konami Sound System | `.kss` | Subtrack progress/seek | not measured; low | T2 |
| 31 | SMS/GG/CV Sound Format | `.sgc` | Subtrack progress/seek | not measured; low | T2 |
| 32 | Video Game Music | `.vgm`, `.vgz` | Seek/resume | not measured; moderate | T2 |
| 33 | MOD | `.mod` | Tracker module | not measured; moderate | T2 |
| 34 | NES Sound Format | `.nsf`, `.nsfe` | Subtrack seek; manual warns it may not be real-time on all devices | not measured; moderate | T2 |
| 35 | Atari SAP | `.sap` | Chip music | not measured; low | T2 |
| 36 | Sound Interface Device (SID) | `.sid` | Subtrack seek; manual warns it may not be real-time on all devices | not measured; moderate | T2 |
| 37 | SPC700 | `.spc` | SNES chip music | not measured; low | T2 |

### 3.4 Feature matrix

Rockbox documents seek/resume/gapless per format in Appendix B.1.4. Helikon-OS
must match or exceed the following, with a per-format test in T-100:

| Capability group | Formats |
|---|---|
| Seek + resume + gapless | AAC, MPEG audio, Musepack, Vorbis, DSD, AIFF, APE, AU/SND, FLAC, ALAC, TTA, W64, WAV, WavPack, Opus |
| Seek + resume (no gapless claim) | AC3, ATRAC3, RealAudio, Speex, VOX, WMA Standard/Professional, MMF |
| Seek only | ADX, Atari, Shorten, VGM/VGZ |
| Subtrack seek instead of seconds | GBS, AY, HES, KSS, SGC, NSF/NSFE, SID |
| No documented seek | MOD, SAP, SPC |

Notes:

- Shorten seeking is unsupported in Rockbox. Helikon-OS may keep that
  limitation with a clear `SEEK_UNSUPPORTED` message, or implement an index.
- Chiptune formats use the tracker’s subtrack (often one second) as the seek
  unit. The seek bar must say so; it must not pretend to be sample-accurate.
- Gapless for chiptunes means continuous synthesis, not decoder delay removal.
- APE `-c2000` and above are not real-time on the 6G in Rockbox’s own data.
  Helikon-OS may support them best-effort with a warning, or reject them with
  `CPU_TOO_SLOW`; it must not stutter silently.

### 3.5 Alias research

The manual’s `.colours` extension list and the Rockbox source tree mention
formats outside Appendix B.1. These are **not** part of the hard parity set yet:

| Alias | Likely meaning | Action |
|---|---|---|
| `.at3` | ATRAC3 alias | Treat as ATRAC3 in T2 |
| `.vtx` | Vortex tracker | Verify against Rockbox `vtx.c`; add to T2 if audio and maintained |
| `.cm4` | Atari chip music | Verify mapping; likely T2 |
| `.rmt`, `.tmc`, `.tm8`, `.tm2` | Atari/AdLib tracker families | Verify decoder mapping; add only with a decoder and test file |
| `.ahn` | AHX/Abyss tracker (unconfirmed) | `open question` |
| `.ace` | ACE variant (unconfirmed) | `open question` |
| `.mv`, `.rmvb`, `.wmv` | Often video containers | Video out of scope; audio-only streams may be handled by the audio decoder |

A format is not added to the parity set because an extension appears in a UI
colour list. It is added when the manual, the Rockbox source and a decoder
mapping agree.

---

## 4. Support tiers

### 4.1 Tier 1 — v1 core (eight families)

| Format | Why v1 | Decoder plan |
|---|---|---|
| MP3 (MPEG Layer III) | Universal music format | Rockbox `mpa` or libmad; fixed-point |
| AAC-LC | Apple’s main lossy format | libfaad/Rockbox `aac`; LC first |
| ALAC | Apple lossless; stock-iPod compatible | libalac/Rockbox `alac` |
| FLAC | Most common open lossless | libFLAC/Rockbox `flac` |
| WAV | PCM exchange and mastering | Rockbox `wav` + PCM/ADPCM |
| AIFF | Mac PCM exchange | Rockbox `aiff` |
| Ogg Vorbis | Open lossy standard | libtremor/Rockbox `vorbis` |
| Opus | Modern low-bitrate standard; manual documents it | libopus/Rockbox `opus` |

Tier 1 is the v1 release gate. It is not the full promise.

### 4.2 Tier 2 — Rockbox audio parity (v1.x)

All remaining Appendix B.1 families:

AC3, ADX, AAC-HE/HEv2, MP1/MP2, Musepack, ATRAC3, RealAudio Cook, Speex, VOX,
WMA Standard, WMA Professional, DSD, APE, AU/SND, Shorten, TTA, W64, WavPack,
Atari, MMF, GBS, AY, HES, KSS, SGC, VGM/VGZ, MOD, NSF/NSFE, SAP, SID, SPC.

This is a **hard v1.x parity requirement**. A missing Tier 2 family is a
documented release gap, not a silent omission. The release notes must say which
families are in, which are best-effort and which are blocked by a licence or a
measured CPU limit.

### 4.3 Tier 3 — beyond Rockbox, only if the 6G supports it

These are candidates, not promises. Each needs a hardware measurement and a
licence review:

| Candidate | What it would add | Gate |
|---|---|---|
| 24-bit/44.1 and 24-bit/48 PCM output | Preserve 24-bit lossless instead of truncating to 16-bit | T-098 must show the I2S/DAC path accepts 24-bit without truncation or noise |
| 24-bit FLAC/ALAC/WAV/AIFF | No 16-bit downmix for high-res lossless | T-098 + T-102 |
| 24/96 or 24/192 decode with downsample | Play hi-res files on a 48 kHz DAC | A documented resampler; no “hi-res output” claim |
| CAF (Core Audio Format) | Apple container not in Rockbox | Decoder/container work; licence review |
| WMA Lossless | Rockbox does not decode it | CPU and patent review; likely too heavy |
| WMA Voice | Rockbox source has it; manual does not list it | Verify manual/behaviour; optional |
| Chained Ogg Vorbis | Rockbox does not support chained Ogg | Parser work; bounded memory |
| 32-bit float PCM preserved to 24-bit output | Better mastering-file compatibility | T-098; otherwise convert to 16-bit with dither |
| DSD128 with a high-quality filter | Better DSD compatibility than a basic downsample | CPU/battery measurement; otherwise DSD64 |
| MIDI (Midiplay-style) | General MIDI playback | Synth size/CPU; optional plugin, not core |
| AHX/VTX/other tracker aliases | More tracker formats | Decoder mapping and test corpus |

The strongest plausible differentiators are **24-bit/48 lossless**, **chained
Ogg**, **multi-value tags**, **larger metadata**, **progressive JPEG art** and
**a better resampler**. The strongest non-claims are native 24/96, native DSD
and “hi-res” output.

### 4.4 Not supported

| Item | Reason | User-visible result |
|---|---|---|
| Encrypted ADX | Rockbox does not support it; no key path | `UNSUPPORTED` with the reason |
| APE `-c5000` real-time | Rockbox measures 23% realtime at 216 MHz | `CPU_TOO_SLOW`; offer conversion |
| Native DSD output | CS42L55 is a PCM DAC | Downsample to PCM or reject |
| Native >48 kHz output | Codec is 48 kHz-class; no stock 96 kHz path | Downsample or reject |
| Video/containers | Not audio scope | Not listed as audio; video is separate |
| Recording/FM | 6G has no input/tuner | Not applicable |

---

## 5. Decoder architecture

### 5.1 Module boundary

```text
player -> audio pipeline -> codec registry -> decoder module
                              |                  |
                              v                  v
                         metadata parser     VFS read/seek
```

The decoder never touches the filesystem directly. It receives a bounded read
callback and a seek callback. It never allocates from a general heap; it uses a
fixed workspace sized by its descriptor. The player owns the ring buffer,
resampling, volume and output.

### 5.2 Codec descriptor

Every decoder exports a static descriptor:

```text
id, name, extensions, MIME hints, profiles
caps: seek, resume, gapless, subtrack_seek, channels, max_rate, max_bits
min_clock, recommended_clock, workspace_bytes, stack_bytes
probe(), open(), decode(), seek(), metadata(), close()
```

`probe()` checks the header, not only the extension. A wrong extension must not
crash the decoder. A missing/unknown format returns `UNSUPPORTED`, not a
generic decode failure.

### 5.3 Memory and CPU rules

- Total codec workspace must fit the existing 512 KiB `CODEC_SIZE` budget;
  a decoder that needs more must justify a new budget, not silently grow.
- No `malloc` in the decode path. Fixed workspace, bounded stack, no
  variable-length arrays.
- Prefer fixed-point decoders; float is allowed only where measured and where
  the CPU cost is acceptable.
- Probe and metadata parsing are bounded; a malformed file cannot make the
  parser read the whole volume.
- A decoder declares its minimum clock. The player boosts before decode and
  drops after an idle window.
- A missed deadline is `CPU_TOO_SLOW` and a user-visible explanation, not a
  stutter loop.

### 5.4 Sample-rate and bit-depth policy

1. Decode at the file’s native rate into the pipeline.
2. If the rate is 44.1 or 48 kHz and the output path supports it, pass it
   through.
3. If the rate is 88.2/96/176.4/192 kHz, resample to 48 or 44.1 kHz with a
   documented polyphase filter; never truncate samples.
4. If the source is 24-bit or 32-bit float and the output path is proven
   24-bit, preserve it; otherwise convert to 16-bit with dither/noise shaping.
5. DSD is converted to PCM with a documented low-pass filter; native DSD is
   not claimed.
6. The UI shows the source rate/bit depth and the output rate/bit depth
   separately. Do not label a downsample as “hi-res”.

### 5.5 Metadata and artwork

Rockbox metadata coverage (manual Appendix B.2):

| Tag family | Formats |
|---|---|
| ID3v1/v1.1/v2.2/v2.3/v2.4 | MP1/MP2/MP3, TTA, DSF, RealAudio |
| APE v1/v2 | MPC, APE, WV |
| Vorbis comments | OGG/OGA, SPX, FLAC |
| MP4 | M4A/M4B/MP4 |
| ASF | WMA/WMV/ASF |
| Codec-specific | MMF, MOD, NSF/NSFE, SAP, SID, SPC, GBS, AY, KSS, SGC, VGM |
| None | AC3, ADX, OMA/AA3, AIF/AIFF, AU/SND, SHN, VOX, W64, WAV, Atari, HES, VGZ, DFF |

Helikon-OS must support at least these tag families. It should exceed Rockbox
by:

- allowing multiple values (genre, artist) instead of only the first;
- normalizing Unicode and matching HFS+ names (D-06);
- removing Rockbox’s 1,800-byte total / 500-byte item cap; the host index is
  the bound, not a fixed decode buffer;
- supporting embedded JPEG, PNG and BMP, plus external art and progressive
  JPEG where the decoder budget allows;
- reading ReplayGain 1.0 and 2.0 tags, cuesheets and chapters;
- reporting missing/malformed tags as `TAG_MISSING`/`TAG_INVALID` instead of
  silently substituting a filename.

Album art rules:

- embedded art is decoded only to the screen/thumbnail size;
- the image cache is bounded and separate from the codec workspace;
- a huge or malformed image is rejected without failing playback;
- no image is written back to the audio file during playback.

### 5.6 Gapless, seek and resume

- Gapless uses encoder delay/padding metadata where the format provides it;
  otherwise the decoder reports `GAPLESS_UNKNOWN`.
- Seek uses the container index where available; Shorten may report
  `SEEK_UNSUPPORTED`.
- Chiptune/tracker seek is by subtrack and the UI says so.
- Resume stores the track, sample position, codec profile and a generation
  token; a changed file invalidates the resume instead of seeking into the
  wrong place.
- A power cut during a resume save leaves the previous resume valid
  (transactional settings state).

### 5.7 Error taxonomy

| Code | Meaning | User action |
|---|---|---|
| `UNSUPPORTED` | No decoder for the format/profile | Convert or skip; name the format |
| `DECODE_ERROR` | Bitstream is corrupt | Skip; preserve the file |
| `CPU_TOO_SLOW` | Decoder missed its deadline at the allowed clock | Convert to a lighter profile or accept stutter only with a warning |
| `OOM` | Workspace or buffer exhausted | Skip; report the codec name |
| `SEEK_UNSUPPORTED` | Format has no seek index | Restart or skip |
| `TAG_INVALID` | Metadata malformed | Play with filename fallback; report |
| `TAG_MISSING` | No usable tags | Play with filename fallback |
| `ART_INVALID` | Artwork malformed/too large | Show placeholder; keep playing |

---

## 6. Performance evidence

The Rockbox 2014 iPod Classic table (`b320bba`, 2014-12-12, 216 MHz boosted /
54 MHz unboosted) is the only per-codec 6G data available. Selected results:

| Codec | 216 MHz | 54 MHz | 54 MHz realtime |
|---|---:|---:|---:|
| FLAC | 7.5–8.1 MHz | 7.1–7.7 MHz | 699–758% |
| MP3 | 20–29 MHz | 19.8–28.4 MHz | 190–273% |
| Vorbis | 19–28 MHz | 17–24.9 MHz | 217–317% |
| Opus | 26–50 MHz | 23.9–44.4 MHz | 122–226% |
| AAC-LC | 37–46 MHz | 27.6–36 MHz | 150–196% |
| AAC-HE | 82 MHz | 69.9 MHz | 77% (fails) |
| AAC-HEv2 | 101 MHz | 87.9 MHz | 61% (fails) |
| ALAC | 34.9 MHz | 34.6 MHz | 156% |
| APE `-c1000` | 40.2 MHz | 40.2 MHz | 134% |
| APE `-c2000` | 57.4 MHz | 57.3 MHz | 94% (fails) |
| AC3 | 17.6 MHz | 17.3 MHz | 312% |
| WMA Std | 27–30 MHz | 20.4–22.9 MHz | 236–265% |
| WMA Pro | 23–29 MHz | 21.1–26.1 MHz | 207–256% |
| WavPack | 23–28 MHz | 22.8–27.4 MHz | 197–237% |
| TTA | 35.6 MHz | 35.6 MHz | 152% |
| Musepack | 22–25 MHz | 18.1–20.6 MHz | 262–299% |

This supports four conclusions:

1. The 6G can decode nearly the whole Rockbox audio set at 54 MHz, which is
   the battery-friendly clock.
2. AAC-HE/HEv2 need a boost; they are still usable at 216 MHz but cost battery.
3. APE above `-c1000` is not a real-time format on this hardware; “APE support”
   must state the profile.
4. Chiptune/tracker formats are not in the table; they need their own
   benchmark, because synth engines can spike even when the average is low.

These are **competitor measurements**, not Helikon-OS evidence. T-101 must
repeat the measurement on Helikon-OS with the same files and a recorded image
hash.

---

## 7. Test corpus and acceptance

### 7.1 Corpus

- At least one valid file per Tier 1 and Tier 2 family.
- Three bitrates or compression levels for lossy codecs; three compression
  levels for APE/FLAC/WavPack.
- 44.1 and 48 kHz; 16-bit and 24-bit where the format allows; mono, stereo and
  one multichannel AC3/FLAC file.
- One file with full tags, one with missing tags, one with malformed tags, one
  with Unicode/combining marks, one with embedded art, one with external art.
- One gapless reference album per gapless codec.
- One long file (at least 2 hours) for seek/resume.
- One corrupt/truncated file per family to test `DECODE_ERROR`.
- One file with an unusual profile (AAC-HE, APE `-c2000`, DSD128, 24/96) to
  test the documented fallback.
- All test files must be freely distributable or generated; no copyrighted
  music is committed.

### 7.2 Acceptance

A format is `pass` only when:

1. it opens by header probe, even with a wrong extension;
2. it decodes for 30 minutes without underrun at the declared clock;
3. CPU load is below 80% of the chosen clock, or the format declares a higher
   clock and the battery cost is recorded;
4. seek is within 200 ms where supported, or returns `SEEK_UNSUPPORTED`;
5. resume restores the same sample position and survives a power cut;
6. metadata and artwork match the source;
7. a malformed file returns a specific error and does not crash or hang;
8. `fsck_hfs` stays clean after playback and resume writes;
9. the device run records image hash, adapter, battery state and output.

### 7.3 Per-format evidence table

The release evidence belongs in [`validation.md`](validation.md) and
[`device-matrix.md`](device-matrix.md). The minimum table is:

| Format | Tier | File/hash | Device | CPU clock | Decode result | Seek | Resume | Gapless | Metadata | Status |
|---|---|---|---|---|---|---|---|---|---|---|
| MP3 | T1 | | 6G | | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |
| AAC-LC | T1 | | 6G | | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |
| FLAC | T1 | | 6G | | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |
| … | | | | | | | | | | |

No row is `pass` without a recorded run.

---

## 8. Decisions and tasks

### 8.1 Decisions

- **D-34** — codec and container tiers. Revised by this document: Tier 2 is
  full Rockbox audio parity, not an optional subset.
- **D-49** — decoder provenance and licensing (proposed).
- **D-50** — audio output ceiling, bit depth and resampling (proposed).
- **D-51** — audio metadata, artwork and gapless policy (proposed).

### 8.2 Tasks

| ID | Task | Tier | Depends |
|---|---|---|---|
| T-068 | Implement the Tier 1 core codecs and gapless corpus. | T1 | T-097 |
| T-069 | Implement Rockbox audio parity (Tier 2). | T2 | T-068, T-097 |
| T-096 | Publish the audio-format corpus and decoder provenance matrix. | T1/T2 | D-34, D-49 |
| T-097 | Implement the codec framework, registry and probe/seek interfaces. | T1 | T-058 |
| T-098 | Measure the 6G audio output ceiling (16/24-bit, 44.1/48/96 kHz). | T1/T3 | T-068, D-50 |
| T-099 | Implement metadata, artwork, ReplayGain and cuesheet coverage. | T1/T2 | T-067, T-097, D-51 |
| T-100 | Implement the per-codec gapless/seek/resume matrix. | T1/T2 | T-097, T-068 |
| T-101 | Benchmark codec CPU, battery and RAM on the 6G. | T1/T2 | T-079, T-068 |
| T-102 | Evaluate the Tier 3 beyond-Rockbox candidates. | T3 | T-098, T-069 |

T-068 and T-069 are the existing roadmap tasks. T-096–T-102 support them; they
do not create a second implementation path. The old “evaluate Tier 2” wording is
superseded: Tier 2 is a hard v1.x parity requirement, not an optional study.

---

## 9. Open questions

- Does the 6G I2S/DAC path accept 24-bit words without truncation? T-098.
- Is 24-bit/48 output audibly better than 16-bit/48 on this codec? Measure, do
  not assume.
- Is 24/96 downsampled playback worth the CPU/battery cost, or should the UI
  simply say “48 kHz maximum”?
- Which APE profiles are “supported” versus “best effort”? The manual says only
  `-c1000` is useful; Rockbox data shows `-c2000` fails at 54 MHz.
- Does the 6G benefit from a better resampler for 44.1↔48 conversion, or is
  the stock path sufficient?
- Is WMA Lossless worth the CPU/patent cost? No v1 claim.
- Is native DSD output impossible on the CS42L55 (yes, it is a PCM DAC), and
  what low-pass filter is good enough for DSD64/128?
- Which chiptune/tracker families must be in v1.x versus a plugin pack?
- Which Rockbox decoders can be ported under GPL-2.0-or-later with attribution,
  and which need an upstream library instead?
- Do any codec patents require a distribution review before release?

---

## 10. Related

- [`competitive-roadmap.md`](competitive-roadmap.md) — competitor matrix and
  differentiation bets
- [`roadmap.md`](roadmap.md) — H1-D playback parity and H2-C audio leadership
- [`tasks.md`](tasks.md) — T-068, T-069, T-096–T-102
- [`decisions.md`](decisions.md) — D-34, D-49, D-50, D-51
- [`validation.md`](validation.md) — release evidence
- [`device-matrix.md`](device-matrix.md) — device, adapter and output runs
- [`firmware-architecture.md`](firmware-architecture.md) — audio pipeline and
  module boundaries
- [`goals.md`](goals.md) — goals 16 and 17
- Rockbox manual, *Rockbox Manual for iPod Classic*, version
  `0d5c2b0604-261105`, Appendix B.1–B.2, §7.21
- Rockbox codec performance page, iPod Classic `b320bba`, 2014-12-12; archived
  copy consulted 2026-11-05
- Apple, *iPod classic — Technical Specifications* (audio support list)
- Rockbox `firmware/export/config/ipod6g.h` (CPU, codec buffer, plugin buffer)
- The Apple Wiki, *iPod classic* and *S5L8702* (CPU core, RAM, CS42L55)
- Cirrus Logic CS42L55 datasheet metadata (24-bit codec, 48 kHz class)

## 11. Functional four-codec player — next implementation plan

> Status: the PCM/WAV path is device-proven (four-buffer PL080 LLI ring, bounded
> WAV player, pause/resume); the first compressed tranche (bounded codec
> registry + native FLAC, 16-bit ring output) is merged and device-proven on
> image `6a2328ae…`. No storage-backed playback, seek UI or MP3/ALAC/AAC yet.
> The Alpha 2.0.0 Music images are storage-free UI demos.

### 11.1 First release scope

The first functional player targets the four requested codecs with explicit
profiles. It is a subset of Tier 1; WAV, AIFF, Vorbis and Opus follow before the
full Tier 1 gate.

| Format | First functional profile | Later |
|---|---|---|
| MP3 | MPEG-1/2/2.5 Layer III, CBR/VBR, 32–320 kbps, 32/44.1/48 kHz, mono/stereo | MP1/MP2 (Tier 2) |
| AAC | AAC-LC in ADTS and MP4/M4A, 44.1/48 kHz, mono/stereo | HE-AAC v1/v2 (216 MHz), multichannel |
| FLAC | Native FLAC, 16/24-bit, mono/stereo, 44.1/48 native; 88.2/96/192 downsampled | Multichannel, 24-bit output after T-098 |
| ALAC | M4A, 16/24-bit, mono/stereo, 44.1/48 native; higher rates downsampled | Multichannel, 24-bit output after T-098 |

Output is 16-bit/44.1 or 48 kHz until T-098 measures the CS42L55 path. Native
>48 kHz output and native DSD are not claimed. The UI must show source and
output rates separately.

### 11.2 Milestones

1. **Audio bench (T-058 first slice).** CS42L55 init over I2C, I2S clocks, DMA
   output, volume, headphone and line-out. Play an embedded PCM WAV; no codec
   and no storage. Exit: first sound on a 6G and a sane line-out/headphone
   level. **Implemented**: `firmware/hal/6g/audio.c` and `firmware/player/`
   provide 16-bit stereo/mono WAV at 44.1/48 kHz, bounded DMA chunks, pause and
   a Now Playing screen. Device audio run pending.
2. **Player core and FLAC (T-097/T-058).** Bounded read/seek callbacks, codec
   descriptor registry, read-ahead and PCM rings, the playback state machine
   (`IDLE → OPENING → BUFFERING → PLAYING → PAUSED → SEEKING → DRAINING`),
   DMA playback clock, underrun handling and a minimal now-playing screen.
   Play an embedded FLAC file.
3. **Four codecs (T-068 subset).** MP3, ALAC and AAC-LC, with the MP4/M4A
   demuxer for AAC/ALAC. Each decoder declares profiles, workspace, stack and
   minimum clock; no `malloc`; stay inside the 512 KiB codec budget.
4. **Read-only storage (T-056).** Mount HFS+ read-only through the ATA read
   HAL, add bounded file handles (`read`/`seek`/`size`), and read the HDB and
   audio files from the iPod. The blocked F2/T-055 write path is not required.
   The volume hash must stay unchanged.
5. **Functional gate (T-100/T-101).** Per-codec seek, pause, next/previous,
   corrupt-file errors, 30-minute runs, CPU/battery measurements, line-out and
   headphone checks, and a recorded device matrix row.

The first slice builds as a single-track embedded preview. The WAV is local and
is not committed:

```sh
afconvert -f WAVE -d LEI16@44100 -c 2 input.m4a /tmp/track.wav
# or: ffmpeg -i input.m4a -t 20 -ar 44100 -ac 2 -sample_fmt s16 /tmp/track.wav

make -C firmware BOARD=6g VERSION=14 BOOTSTATE_STATE=good UI=1 \
  HDB=../build/helikondb/first100/music.hdb \
  AUDIO=/tmp/track.wav AUDIO_TRACK=0 \
  EXTRA_CFLAGS='-DHELIKON_DIAG_LAUNCHER' \
  BUILD=../build/firmware/6g-music-audio image
```

`AUDIO_TRACK` is the HDB track-table index that owns the embedded excerpt;
other tracks keep the "playback coming next" state. This is a hardware/output
gate, not the four-codec player. The 20 s 44.1 kHz stereo excerpt used for the
2026-11-07 image makes the signed image about 4.1 MiB.

### 11.3 Decoder order and provenance

Implement in this order: **WAV → FLAC → MP3 → ALAC → AAC-LC**. WAV proves the
output path; FLAC is the cheapest real decoder; MP3 is the most common; ALAC
and AAC bring the MP4 container. Prefer Rockbox fixed-point decoders where they
fit; use maintained upstream libraries only after the T-096 provenance and
D-49 licence/patent review. AAC release requires an explicit licensing
decision.

### 11.4 Acceptance

A codec is not supported until it: probes by header, decodes for 30 minutes
without underrun at its declared clock, seeks within 250 ms where supported,
pauses/resumes, handles next/previous, returns a specific error on a malformed
file, works through headphone and line-out, performs no filesystem write during
playback, and has a recorded 6G run with image hash, adapter and battery state.

### 11.5 Not in the first player

HE-AAC v1/v2, native >48 kHz output, 24-bit output before T-098, native DSD,
multichannel downmix, gapless across all formats, ReplayGain/EQ and transactional
resume. Gapless and resume are hardening gates, not prerequisites for first
sound.
