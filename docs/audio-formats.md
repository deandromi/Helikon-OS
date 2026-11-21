# Audio formats — Rockbox parity and Helikon-OS support plan

> Purpose: list every audio format Rockbox documents for the iPod Classic 6G,
> define what Helikon-OS must support, and record the 6G hardware/CPU ceiling.
> Status: living document; Tier plan plus the host playback stack (WAV, FLAC,
> MP3, pull sources, read-ahead); no Music-hub caller and no device run.
> Owner: audio lead; firmware lead
> Last updated: 2026-11-21
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
- **D-49** — decoder provenance and licensing (accepted 2026-11-17).
- **D-50** — audio output ceiling, bit depth and resampling (accepted 2026-11-17).
- **D-51** — audio metadata, artwork and gapless policy (accepted 2026-11-17).

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
> image `6a2328ae…`. The T-097 descriptor extensions (seek/gapless/resume
> capabilities, extension lists, workspace/stack/clock declarations, registry
> query) and the MP3 codec (vendored libmad via the pinned Rockbox mirror) are
> host-implemented and host-tested (2026-11-17/18); see §13. No storage-backed
> playback or seek UI yet. The Alpha 2.0.0 Music images are storage-free UI
> demos.

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
fit; use maintained upstream libraries only after the T-096 provenance and the
accepted D-49 licence/patent review. AAC release still requires the explicit
AAC patent/licensing decision (open; the D-49 gate).

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

---

## 12. Codec-framework research and licence evidence (2026-11-17)

Device-free research for the T-097 framework work and the first decoder ports.
Three passes (Rockbox codec-framework internals, decoder selection deep-dive,
Helikon integration plan) plus direct reads of licence headers were pinned to
the official Rockbox GitHub mirror at commit
`97986f2cdaec1585856d708966858c56f5dedc6b` (master, 2026-11-17). Layout note
for anyone following Helikon docs that cite the classic tree: codec, DSP and
metadata sources now live under `lib/rbcodec/` (`codecs/`, `dsp/`,
`metadata/`); the loader and playback orchestration stayed in `apps/`
(`codecs.c`, `codec_thread.c`, `playback.c`, `pcmbuf.c`, `buffering.c`).

### 12.1 Licences of the Rockbox bundled libraries (headers read at the pinned commit)

| Library | Upstream identity | Licence | Helikon-OS verdict |
|---|---|---|---|
| libmad | libmad-0.15.1b (2004-02-17), Underbit Technologies | GPL-2.0-or-later | Clean for MP3; MP3 patents expired (2017, US) |
| libalac | reverse-engineered decoder v0.1.0, David Hammerton 2005 | MIT-style permission notice | Clean for ALAC; preserve the notice. Preferred over Apple's 2011 Apache-2.0 reference decoder, which would force GPL-3 for the combined work |
| libm4a | `demux.c` Hammerton 2005 (MIT-style); `m4a.c/.h` Dave Chapman 2005 (GPL-2.0-or-later) | MIT + GPL-2+ | Clean shared MP4 demuxer for ALAC and AAC-LC |
| libfaad | FAAD2 CVS snapshot 2005-02-02, M. Bakker / Ahead Software AG | GPL-2.0-or-later plus an Ahead-only commercial non-GPL clause in `COPYING` | Licence-compatible; Rockbox's `README.rockbox` pins this snapshot because a "non GPL compatible clause" was added in that same 2005-02-02 CVS commit and later versions are unusable. Pin the snapshot; never take later FAAD2. The SBR/PS (`sbr_*`) and DRM files separate cleanly, enabling the LC-only crop. The Via Licensing AAC patent pool remains the D-49 release gate |
| wrapper codecs (`mpa.c`, `alac.c`, `aac.c`) | Dave Chapman 2005 | GPL-2.0-or-later | Reference material; Helikon writes its own wrappers around its own codec API |
| codec shared glue (`lib/codeclib.*`) | Dave Chapman 2005 et al. | GPL-2.0-or-later | Pattern reference only |
| `ffmpeg_bitstream.c`, `ffmpeg_get_bits.h` | FFmpeg (Bellard 2000-01, Niedermayer 2002-04, Merritt 2010) | LGPL-2.1-or-later | Usable via the LGPL→GPL route; record provenance as LGPL-derived if copied |
| metadata parsers (`mp4.c` Holmgren 2005; `mp3.c` Stenberg 2002) | Rockbox | GPL-2.0-or-later | Second opinions for container sniffing; Helikon probes by header first |
| libtremor, libopus | Xiph.org | BSD-style | Clean for the later Tier 1 ports |

Any port records the upstream commit, import date and file list per D-49, and
keeps a Rockbox-style `README` import note (import date, source version,
modifications) in each ported directory.

### 12.2 Framework contracts Rockbox proves (adopt)

1. The `codec_api` streaming vocabulary — bounded `request_buffer`/`advance_buffer`
   over a readable stream, `seek_buffer`/`seek_complete`, `set_elapsed`/
   `set_offset`, and a `get_command` yield point inside the decode loop — is a
   field-proven match for Helikon's planned `helikon_codec_source` read/seek
   callbacks and `IDLE→…→SEEKING` state machine. Seek stays a codec-owned
   time→offset translation (Xing/LAME TOC for VBR, byte math for CBR, frame
   index for FLAC) over player-owned transport.
2. Per-chunk PCM ring descriptors (`struct chunkdesc`: size, elapsed, offset
   riding with the audio) put the playback clock at the DMA consumer. Helikon's
   four-buffer PL080 ring can carry the same metadata without new hardware.
3. The DSP control surface is minimal and sufficient: set frequency, sample
   depth (fractional bits), stereo mode, replay gain; flush at every
   discontinuity. Helikon's `source_bits`/`output_bits` descriptor fields map
   onto it, including the TPDF-dither-to-16 policy Rockbox uses at output.
4. Storage reads are bounded and chunked (32 KiB) on a separate context with a
   watermark (buffer margin + spinup + 5 s wording for spinning storage); the
   decode loop never spins — it sleeps ~50 ms on a full PCM ring.
5. Host/device parity: IRAM and cache attributes vanish under a host build
   define, which is what makes Rockbox's host codec tests possible; Helikon
   should give its per-codec sections the same compile-time neutrality.

### 12.3 Mechanisms Helikon-OS replaces (do not copy)

1. No on-device code loading. Rockbox loads `.codec` images from
   `/.rockbox/codecs` into a 512 KiB code region; the signed-image invariant
   (I-14) forbids this. Codecs are statically linked into the image behind the
   registry; only the reuse behavior (same-family decoder stays resident)
   carries over.
2. No shrinkable whole-DRAM buffer. Rockbox wagers ~60 MiB on one movable
   audiobuf and shrinks it under pressure by stopping playback; Helikon's
   ADR-0003 fixed 24 MiB audio region prefers explicit `BUFFER_FULL`/
   `NO_SPACE` outcomes, a miniature pinned ring of handles, and stop/re-map/
   resume instead of live shrink.
3. No `malloc` facade. Rockbox's `codeclib` redefines `malloc`/`free` as a bump
   arena over the codec-buffer tail (its `codec_free` is a no-op). Helikon's
   per-codec static workspace stays; upstream libraries get pre-wired static
   structures (the libmad pattern in `mpa.c`, `FAAD_STATIC_ALLOC` in libfaad)
   or a workspace bump arena, all inside the declared budget.
4. No three-thread pipeline under the cooperative runtime. The
   buffering/codec/audio split transfers as function boundaries; execution
   becomes ISR top-halves plus deferred work in the main loop.
5. No extension-only probing and no 1800/500-byte tag caps; Helikon's header
   probe stays, and tag coverage follows the host index (§5.5).
6. Metadata parsing never shares a stack or buffer with decode; in Helikon it
   runs deferred, published by a generation-tagged pointer swap.

### 12.4 Size accounting and the workspace cap

Verified Rockbox 6G numbers: `CODEC_SIZE` 512 KiB, codec IRAM window 80 KiB
(top of IRAM0; IRAM1 unused as slower than DRAM), codec thread stack 9 KiB,
pcmbuf ~529 KiB (3 s at 44.1 kHz s16 stereo, watermark 2 s), metadata pools
1800/500 bytes, clocks 54/216 MHz.

| Codec | Estimated Helikon workspace | Note |
|---|---|---|
| FLAC (native, done) | 65,608 B | measured in the host test |
| MP3 / libmad | **30,976 B measured** (`sizeof(struct mp3_state)`, compile-time asserted) | whole-buffer model: libmad state, one shared `[2][36][32]` subband buffer (libmad writes `frame->sbsample`, the Rockbox-modified synth reads `frame->sbsample_prev`; the Rockbox single-core wiring points both at the same buffer, see §13.2), overlap buffer and the `MAD_BUFFER_MDLEN` main-data copy |
| ALAC / libalac | ~48 KiB plus a 32 KiB IRAM candidate (`outputbuffer[2][4096]`) | the interleaved s32 output buffer dominates; moving it to the codec fast section keeps DRAM workspace comfortably inside 128 KiB |
| AAC-LC / libfaad crop | likely 256–400 KiB — UNVERIFIED, measure | first codec to collide with the 128 KiB `HELIKON_CODEC_WORKSPACE_MAX` |

Rule until T-096/T-101 produce numbers: every descriptor carries a **measured**
`workspace_bytes` and `stack_bytes`; keep `HELIKON_CODEC_WORKSPACE_MAX` at
128 KiB for the MP3 and ALAC ports; if the measured AAC-LC workspace exceeds
128 KiB, raise the cap by a dated amendment inside the 512 KiB
`CODEC_SIZE` envelope and record it as an open question resolved by
measurement — do not reserve the envelope up front.

### 12.5 Port order and rejected candidates

The order stays as §11.3: after WAV and the native FLAC — **MP3 (libmad) →
ALAC (libalac + libm4a) → AAC-LC (libfaad LC crop)**. MP3 is the most common
format, the licence is the cleanest and the patents are expired; ALAC adds the
shared MP4 demuxer that AAC later reuses; AAC-LC lands last because of the
D-49 patent gate, not licence incompatibility. Candidates rejected by the
research passes: minimp3 and dr_mp3 (float DSP), Helix MP3 (Real RPSL),
fdk-aac (non-standard licence, C++), and Apple's Apache-2.0 ALAC reference
decoder (GPL-3 forcing). HE-AAC v1/v2 stays out of the first player per §11.5.

---

## 13. T-097 framework extensions and the MP3 (libmad) port — host implementation (2026-11-17/18)

Status: `implemented` as host code with host tests; `unproven` on a device (no
6G run; the storage-backed wiring remains the T-056 device half and the
functional gate stays T-100/T-101). Nothing here claims device or release
support.

### 13.1 What landed in the framework

- **Descriptor extensions** (`firmware/codec/codec.h`, `codec.c`): every codec
  descriptor now carries `extensions` (NULL-terminated lowercase list, matched
  case-insensitively after the final dot; no dot means no match), a
  `capabilities` mask (`HELIKON_CODEC_CAP_SEEK/GAPLESS/RESUME`) separate from
  the per-file `HELIKON_CODEC_FLAG_SEEK`, and declared `stack_bytes`,
  `min_clock_hz`, `recommended_clock_hz`. `helikon_codec_registry()` exposes
  the registry (wav → flac → mp3) to callers. The per-file `flags` stay inside
  `helikon_codec_info`.
- **Probe policy**: `helikon_codec_probe` refuses a zero-size buffer with
  `HELIKON_ERR_ARGUMENT`, keeps the probe-by-header contract (a wrong
  extension does not matter) and propagates the first non-`NOT_FOUND` probe
  error.
- **FLAC** descriptor extended accordingly (`SEEK | GAPLESS`, 4096 B stack,
  54/216 MHz).

### 13.2 MP3 via vendored libmad

- **Provenance** (`firmware/codec/libmad/README.md`): libmad 0.15.1b as
  carried by the Rockbox mirror at the same pinned commit as §12
  (`97986f2cdaec1585856d708966858c56f5dedc6b`), with the Rockbox edits
  (`README.rockbox`, ported tables, pointer-wired buffers instead of malloc).
  The ARM/Coldfire assembly accelerators are **not** imported: on the 6G the
  C paths are the first implementation and a later slice may add the ARMv5E
  synth after measurement. GPL-2.0-or-later headers preserved; modifications
  documented in that README.
- **Port shim** (`libmad_port.h`): replaces Rockbox's `codeclib.h` and
  `config.h` includes, empties the ICODE/ICONST/IDATA/IBSS placement
  attributes (everything lands in `.text`/`.bss`), supplies `betoh32`, and
  toggles `HAVE_LIMITS_H`: hosted builds (host tests) use libc
  `<limits.h>`; the freestanding target build gets `INT_MAX`/`UINT_MAX`
  from the shim instead.
- **Wrapper** (`firmware/codec/mp3/`): Helikon-owned `mp3_codec.c` around the
  libmad API. Whole-buffer model (no streaming callbacks yet); probe skips
  ID3v2, finds a sync inside 16 KiB, validates the header fields fully, reads
  the Xing/Info tag (frames count + 100-byte TOC) and computes duration;
  decode is libmad with a fixed → s16 clip conversion, mono duplicated to
  stereo; bound of 64 consecutive recoverable errors returns
  `HELIKON_ERR_FORMAT`; `MAD_ERROR_BUFLEN` is a clean end-of-stream.
- **Two wrapper behaviours worth recording**:
  1. `frame->sbsample` and `frame->sbsample_prev` are wired to the **same**
     buffer, matching Rockbox's single-core `init_mad()`: the layer decoders
     write `sbsample` while the Rockbox-modified `synth.c` reads
     `sbsample_prev`; the dual-core pointer swap in Rockbox's `mpa.c` exists
     only for its decode/synth thread split, which Helikon does not have.
  2. On the first end-of-buffer error the wrapper re-buffers the stream tail
     into a workspace bounce buffer padded with `MAD_BUFFER_GUARD` bytes so
     the final frame decodes instead of being lost to libmad's
     "N + guard bytes required" rule; after a seek the wrapper clears the
     stale `synth.pcm.length` so no pre-seek samples replay, and it
     re-aligns the stream to a header of the file's own MPEG version and
     layer (main-data bytes frequently carry plausible-looking foreign-layer
     headers that used to derail the seek).
- **Probe corroboration**: a candidate MP3 header is accepted only when its
  declared successor position (`offset + frame_bytes`) also holds a valid
  header of the same version and layer. This stops lone plausible headers
  inside other formats' audio data (observed with a corrupt-magic FLAC
  stream) from claiming MP3.
- **Workspace**: the descriptor reports the measured
  `sizeof(struct mp3_state)` = **30,976 bytes** (compile-time
  `_Static_assert` keeps it inside `HELIKON_CODEC_WORKSPACE_MAX`); declared
  stack bound 8192 bytes; clocks 54/216 MHz.

### 13.3 Build integration

- `firmware/Makefile`: the AUDIO player build compiles `codec/mp3/` plus the
  seven C-path libmad objects with `-DNDEBUG` and the conversion-warning
  suppressions (per-compiler: GCC also needs
  `-Wno-implicit-fallthrough -Wno-shadow`). Verified on the full
  music-hub image config (`UI=1 HDB=… AUDIO=… EXTRA_CFLAGS=-DHELIKON_DIAG_LAUNCHER`,
  763,276-byte raw image) where the MP3 decode path links into the image; the
  standard F2 artifact check still passes. With the Music-hub storage wiring
  absent, `--gc-sections` drops the codec tree from images that never reach
  it — expected until the T-056 device half lands.
- `tests/codec/Makefile`: builds the libmad objects (plain + sanitizer trees),
  and now defines `HELIKON_HOST_TEST` like every other host suite (without it
  the freestanding `memset`/`memcpy` shims shadow libc and a compiler
  idiom-recognition quirk turns a bounded fill loop into infinite recursion —
  recorded in the work log).

### 13.4 Test evidence (host only, no device run)

- `make -C tests/codec test` and `sanitize`: registry/descriptor contract,
  MP3 CBR stereo decode (non-silent sine, length, EOF), mono duplicate,
  VBR Xing mid-file seek, ID3v2 skip, truncated mid-frame clean EOF,
  sync-with-invalid-header FORMAT probe, MP3 probe corroboration, zero-buffer
  refusal. Sanitizers report one upstream note: `synth.c`'s `D[0] - p`
  pointer arithmetic (pre-existing in Rockbox's code, the accesses land
  inside the `D` table; see the libmad README). `flac_decoder.c`'s negative
  left shift (mid-side doubling) is fixed by multiplication in this change.
- Full regression matrix unchanged: `tests/hfsplus` (107 checks + sanitize),
  `tests/vfs`, `tests/firmware` (test + sanitize after `clean`),
  `tests/bootloader`, `tests/image` (incl. write-diag, outer-diag,
  loader-test), `tests/musicdb`.

### 13.5 Whole-family host coverage (2026-11-21)

Every fixture used to be MPEG-1 Layer III at 44.1 kHz, so the wrapper's other
branches were claims, not code under test. Three fixtures now execute them:

| Fixture | Family | Frame samples | Xing/LAME | Measured |
|---|---|---|---:|---|
| `mp3_mpeg2_stereo16` | MPEG-2 Layer III, 22.05 kHz, 64 kbps | 576 | yes, delay 576 / padding 576 | 4,608 raw frames, 3,456 decoded |
| `mp3_mpeg25_stereo16` | MPEG-2.5 Layer III, 11.025 kHz, 32 kbps | 576 | yes, delay 576 / padding 576 | 3,456 raw frames, 2,304 decoded |
| `mp3_layer2_stereo44` | MPEG-1 Layer II, 44.1 kHz, 128 kbps | 1152 | no | 2,304 raw frames, 2,304 decoded |

`check_mp3_family()` pins the sample rate, the raw frame count, the LAME delay
and padding, the trimmed output length and a frame-index seek for each, and
`tests/codec/README.md` records how the set is regenerated. Nothing was broken:
the 576-sample arithmetic in the header parse, the gapless trim and the seek
divisor all produce the encoder's own numbers. That is the point of adding them
— the family claim is now executed rather than asserted. Layer I stays without a
fixture because no common encoder emits it.

### 13.6 Remaining for the MP3 codec

Streaming source model (bounded callbacks), resume, half-rate decode, the
ARMv5E synth accelerator, Layer I execution coverage, and the T-100/T-101 device
gates. Gapless (LAME delay/padding) is implemented and host-covered for
MPEG-1, MPEG-2 and MPEG-2.5.

## 14. Streaming (pull) source model — host implementation (2026-11-21)

Status: `implemented` as host code with host tests for the codec that has been
converted; `unproven` on a device. FLAC and MP3 are still whole-buffer
decoders and say so. Nothing here claims storage-backed playback; that is
T-058.

### 14.1 Why this came before the next decoder

Until now `helikon_codec_open()` handed a decoder the entire file as one
contiguous buffer (`codec->data`, `codec->size`). On a 6G that caps playback at
what can be resident in DRAM at once, and it bars the VFS (T-056) from ever
feeding the codec: `hfs_fork_read` is offset-based, not a mapped window. Every
decoder added before this seam was fixed would have to be reworked, so it was
done while there were only three.

### 14.2 The contract

- **`struct helikon_codec_source`** (`firmware/codec/codec.h`): an absolute
  `read_at(context, offset, buffer, length, *read)` plus the total `size`
  (0 when the media cannot report one). No seek, no cursor, no rewind: an
  offset-addressed read is what the HFS+ read core and the block layer already
  provide, and it makes a decoder's position its own business.
- **`helikon_codec_open_source()`** opens over such a source. A descriptor with
  `streaming == 0` is refused with `HELIKON_ERR_UNSUPPORTED` rather than being
  handed a 4 KiB header prefix it would mistake for the file.
- **`helikon_codec_memory_span()`** returns 1 only when the open codec really
  holds the whole stream contiguously. `helikon_codec_open()` still exists and
  wraps the caller's buffer in a memory source, so no existing caller, test or
  player path changed; it keeps the buffer pointer valid by construction (the
  adapter's context is the codec, not a stack frame).
- **Probe semantics**: `ops->probe` now receives `(data, size, total_size)`.
  `data/size` is the stream head (at most `HELIKON_CODEC_PROBE_BYTES`, 4096),
  and `total_size` is the true length or 0 when unknown. A probe that cannot
  decide from the head returns the new **`HELIKON_ERR_NEED_MORE`**
  (`firmware/lib/error.h`, `-19`); the registry accepts that only from a
  `streaming` descriptor and lets `open` resolve the head, after which
  `codec->info` is authoritative. `helikon_codec_probe()` always sees the whole
  buffer, so it never returns `NEED_MORE` to existing callers.

### 14.3 WAV is converted

`firmware/player/wav.{c,h}` now has one RIFF chunk walker over two backends (a
resident buffer or a source). `helikon_wav_parse()` keeps its signature for the
blob player; `helikon_wav_parse_prefix()` parses a head that may be shorter than
the stream; `helikon_wav_parse_source()` walks a source. The decoder reads PCM
on demand at `pcm_offset + frame × frame_bytes`, mono is duplicated through a
256-frame stack buffer, and a short read yields only whole frames. WAV is
therefore playable from any `read_at` media without ever holding the payload.

### 14.4 Evidence (host only, no device run)

- `make -C tests/codec test` / `sanitize`: PASS, including four new source
  tests — a fake media that records the largest request and the bytes pulled
  (one decoded chunk requests exactly `chunk × 4` bytes and pulls no more), a
  media limited to 37-byte reads (frame count and sample values still exact), a
  media with no reported length (the RIFF size is the bound), a `JUNK` chunk
  large enough to push `data` past the probe prefix (`NEED_MORE` from the head,
  resolved by `open`, tail frame correct), and the refusals (FLAC/MP3 reject a
  pull source, null arguments, `streaming` flags match behaviour).
- Full matrix re-run: `tests/codec`, `tests/hfsplus`, `tests/vfs`,
  `tests/firmware`, `tests/bootloader`, `tests/image` (incl. write-diag,
  outer-diag, loader-test), `tests/musicdb`, `tests/player`, `tests/ui`.
- Cross-build: plain 6G (77,856 B), music-hub config with the MP3 fixture
  (765,392 B raw, MP3 and WAV source symbols linked), `VERSION=2` `trying`
  image + `check-f2-artifact.sh`: PASS. `helikon_codec_open_source` itself is
  dropped by `--gc-sections` until T-058 calls it — expected, not a link error.

### 14.5 Test-suite repair found on the way

`tests/player` and `tests/ui` had been failing to link since the MP3 merge: both
build the codec layer, whose registry references `helikon_mp3_codec_ops`, but
neither linked the libmad objects. Neither suite was in the T-097 verification
list, which is why it went unnoticed. The libmad object rules now live in one
fragment, `tests/libmad.mk`, included by `tests/codec`, `tests/player` and
`tests/ui`.

### 14.6 Remaining

FLAC streaming (frame headers are already offset-addressable; the residual work
is letting the decoder refill instead of indexing the buffer), then MP3
(libmad keeps `main_data` boundaries into the buffer it was given, so a refill
must preserve the unconsumed tail — the bounce buffer added in §13.2 is the
starting point). Resume, gapless and the T-100/T-101 device gates are unchanged
and belong after the streaming conversions, not before.

## 15. Cold path closed: FLAC and MP3 stream, VFS feeds the codec (2026-11-21)

Status: `implemented` as host code, host-tested; `unproven` on a device. No
storage-backed playback exists yet: the read seam is proven, the audio-ring
wiring is not.

### 15.1 FLAC pulls through a window

`firmware/codec/flac/flac_decoder.{c,h}` now decodes through
`struct flac_reader`: a resident buffer or a pull source with an 8 KiB refill
window (`HELIKON_FLAC_WINDOW`). The bit reader asks for one byte at an absolute
offset and the reader refills on a miss; FLAC access is monotonic within a
frame, so one forward window is enough. Metadata is parsed through the same
reader, which is why a head that ends inside a large padding/metadata block is
reported as `HELIKON_ERR_NEED_MORE` and resolved by `open`. `helikon_flac_open()`
keeps its signature; `helikon_flac_open_source()` is new and requires a source
that reports its length (a FLAC stream has no RIFF-style self-bounding field).
The descriptor is `streaming = 1`, workspace 73,832 B.

Measured cost: a frame is walked twice — once to decode, once to verify the
CRC-16 — so a pull total of roughly 2× the stream is expected, never a
whole-file request. The source tests assert exactly that: the largest single
request is at most the window, and the total is bounded at 2× plus one window.

### 15.2 MP3 refills around libmad's frame boundaries

`firmware/codec/mp3/mp3_codec.c` now keeps a 32 KiB window
(`HELIKON_MP3_WINDOW`) and refills it from `stream.next_frame` onward. That is
the correct cut for this libmad port: the bit reservoir is a *copy* held in
`stream.main_data` (wired to the wrapper's `main_data` array), so a refill never
invalidates it, and everything libmad still needs starts at `next_frame`.

- At open, the ID3v2 tag is skipped by reading its 10-byte header and the
  sync-safe size, not by scanning a buffer; `mp3_window_seek()` then fills the
  window at the first frame and `mp3_parse()` locates the header and the
  Xing/Info frame inside it.
- At end of file the first refill that reads nothing pads the window with
  `MAD_BUFFER_GUARD` zero bytes once (`tail_active`), replacing the old
  standalone tail buffer.
- Refills loop until the window is full, so a media that returns short reads
  (extent boundaries, small transfers) still exposes the whole Xing frame to
  the parser — this was the one real bug found: with 37-byte reads the Xing
  tag was invisible and the silent first frame was emitted.
- A full window that libmad cannot consume is refused with
  `HELIKON_ERR_FORMAT` instead of looping.

Workspace: 62,856 B (`sizeof(struct mp3_state)`, compile-time asserted). Seek
uses the Xing TOC or the CBR frame arithmetic, then re-aligns to a real header
inside the window; the frame-boundary semantics are unchanged.

### 15.3 VFS becomes a codec source

`firmware/player/vfs_media.{c,h}` is the seam the project was missing:
`helikon_vfs_media_init()` binds an open `vfs_file_handle` and its stat size to
a `struct helikon_codec_source`, with `read_at` implemented as an absolute
`vfs_file_seek` + `vfs_file_read`. End of file is a clean zero-byte read; a
short read that ends at the file size is normal. There is no writable path and
no second error taxonomy.

`tests/vfs` now proves the composition end to end on the HFS+ catalog fixture:
mount, stat, open, wrap, absolute reads in arbitrary order (2 → 0 → 3 → EOF →
past EOF), a null-context refusal, and a registry probe of the four-byte
"HELO" payload returning `HELIKON_ERR_UNSUPPORTED` rather than reading past the
file or crashing. `helikon_codec_open_source()` with an explicitly requested
descriptor that does not recognize the head now returns `UNSUPPORTED` (it used
to leak the internal `NOT_FOUND` sentinel).

### 15.4 Evidence (host only, no device run)

- `make -C tests/codec test` / `sanitize`: PASS, with new FLAC and MP3 source
  tests — decode from a pull source byte-identical to the resident-buffer
  decode, short-read media (37 B and 61 B), media without a reported length,
  bounded requests, FLAC source seek compared against the buffer decode.
- `make -C tests/vfs test` / `sanitize`: PASS including the new media test.
- Full matrix: `tests/hfsplus`, `tests/firmware`, `tests/bootloader`,
  `tests/image` (test, write-diag, outer-diag, loader-test), `tests/musicdb`,
  `tests/player`, `tests/ui`.
- Cross-builds: plain 6G 77,856 B; music-hub config with the MP3 fixture
  767,624 B (52 MP3/FLAC symbols linked); `VERSION=2 BOOTSTATE_STATE=trying`
  image plus `check-f2-artifact.sh`: PASS.

### 15.5 Remaining for T-058

The read seam is the host half. Still missing: the read-ahead policy (how many
ring buffers feed a refill, and whether the ring requests frame-aligned spans),
the `vfs_media` call sites in the music hub, an actual I2S/DMA run, and the
device evidence. Resume, gapless bookkeeping and T-100/T-101 stay where they
were.

## 16. ID3v2 tag reader — first metadata slice (2026-11-21)

Status: `implemented` as host code with host tests; no caller in the Music hub
yet and no device run. This is one slice of T-099, not the whole tag matrix.

### 16.1 What it does

`firmware/codec/tag/id3v2.{c,h}` parses ID3v2.2, v2.3 and v2.4 text frames from
a caller-supplied buffer:

- `helikon_id3v2_tag_size()` returns the header arithmetic (10-byte header +
  sync-safe size + optional footer) so a caller can skip the tag without
  duplicating it. The MP3 wrapper now uses it instead of its own copy.
- `helikon_id3v2_parse()` fills a fixed struct: title, artist, album, album
  artist, genre, year, track and disc, plus the tag version. Frames are
  matched by ID (`TIT2`/`TT2`, `TPE1`/`TP1`, `TALB`/`TAL`, `TPE2`/`TP2`,
  `TCON`/`TCO`, `TYER`/`TDRC`/`TYE`, `TRCK`/`TRK`, `TPOS`/`TPA`).
- Text encodings: Latin-1, UTF-16 with or without BOM (surrogate pairs
  combined, unpaired surrogates skipped) and UTF-8 (validated byte-wise,
  truncated sequences dropped). Everything becomes UTF-8, bounded at
  `HELIKON_ID3_TEXT_MAX` (96 bytes) with no allocation.
- Errors are distinguished: no tag is `HELIKON_ERR_NOT_FOUND`, a frame whose
  declared size runs past the tag is `HELIKON_ERR_FORMAT`, and
  unsynchronization, compression, encryption or grouping is
  `HELIKON_ERR_UNSUPPORTED` rather than returning half a tag.

### 16.2 Limits (honest scope)

The parser reads from a buffer the caller already holds; it does not fetch a
tag through a source. A tag larger than that buffer must be presented in full
by the caller. Multiple values are truncated to the first (multi-value comes
with the library/index work), and artwork, ReplayGain, chapters, APE, Vorbis
comments, MP4 and ASF tags are not implemented yet. `TAG_MISSING` and
`TAG_INVALID` map onto `NOT_FOUND` and `FORMAT`; there are no separate codes.

### 16.3 Evidence (host only, no device run)

`make -C tests/codec test` / `sanitize` covers: v2.3 Latin-1 fields with a
`3/12` track pair, v2.4 sync-safe frame sizes with UTF-8 and UTF-16LE+BOM text,
tag size arithmetic, a missing tag, an unsynchronized tag and a frame overrun.
The MP3, FLAC, VFS, player and UI suites stay green; the 6G music-hub image
builds at 767,684 B with the tag reader linked (the MP3 wrapper calls it).

## 17. Gapless trim for MP3 — first slice of T-100 (2026-11-21)

Status: `implemented` as host code with host tests; no player wiring and no
device run. This trims one file correctly; it does not yet join two tracks
without a gap.

### 17.1 What it does

The LAME/FFmpeg extension inside the Xing/Info frame carries the encoder delay
and padding: after the optional frames/bytes/TOC fields there is a 4-byte
quality indicator, then a 36-byte tag whose bytes 21-23 hold 12 bits of delay
and 12 bits of padding. `mp3_xing()` now reads that (accepting `LAME`, `Lavc`
and `Lavf` encoder strings) into `struct mp3_xing`, and the decoder uses it:

- `struct helikon_codec_info` gained `encoder_delay` and `padding` (in output
  frames) plus the per-file `HELIKON_CODEC_FLAG_GAPLESS`. Both the probe and
  open fill them, so a caller can plan the trim before opening.
- Open sets the playable length to `xing.frames × frame_samples - delay -
  padding` and remembers how many samples to drop at the start. The read loop
  discards those samples from the first decoded block and reports end of
  stream once the trimmed length is reached, so a caller that simply reads
  until zero gets gapless sample counts.
- A seek resets the trim (the delay belongs to the start of the stream) and
  maps the requested output frame onto the emitted count, so seeking after a
  trim still ends at the same total.
- Files without a LAME tag (plain Xing, CBR without VBR header) keep playing
  untrimmed and do not claim the flag.

### 17.2 Evidence (host only, no device run)

`tests/codec` (test + sanitize) now checks both committed MP3 fixtures:
`encoder_delay == 576` (the value LAME writes at 44.1 kHz, which pins the tag
offset arithmetic rather than just "non-zero"), `padding > 0`, the GAPLESS
flag present, and the decoded length exactly
`total_frames - delay - padding`; after `seek(0)` the same length is reached
again. The full matrix and the 6G builds stay green (music-hub 768,156 B).

### 17.3 Remaining for T-100

- FLAC needs no trim (lossless frames carry exact sample counts); the WAV path
  is already exact.
- Cross-track gaplessness is player work: it needs a queue that keeps the ring
  fed across a file boundary, plus the matching track-length accounting.
- Resume, MPEG-2/2.5 delay values and the T-100 device run are still open.

## 18. Player opens a pull source — T-058 host half (2026-11-21)

Status: `in progress` as host code with host tests. No Music-hub caller, no
I2S/DMA run and no device run. The player can consume a `read_at` source
(`vfs_media`, a test media) but nothing in the UI opens one yet.

### 18.1 What landed

- **`helikon_codec_probe_source()`** (`firmware/codec/codec.c`) resolves a
  descriptor from a source instead of a resident buffer. It reads one shared
  4 KiB head (`HELIKON_CODEC_PROBE_BYTES`), walks the registry, skips
  `HELIKON_ERR_NOT_FOUND`, and accepts `HELIKON_ERR_NEED_MORE` only from a
  `streaming` descriptor — that case zeroes the info because open resolves the
  real values. When no descriptor claims the head the result is
  `HELIKON_ERR_UNSUPPORTED`. The head buffer is static, so the call is not
  reentrant; the firmware is single-threaded and the player is its only caller.
- **`helikon_player_open_media()`** (`firmware/player/player.c`) is the media
  counterpart of `helikon_player_open()`: probe the source, open the codec into
  the player's embedded workspace, then fill the player's `wav` view from
  `codec.info` (authoritative after a streaming open, where the probe may have
  had only the head) and mark the player compressed. HAL bring-up moved into
  `player_start_audio()`, shared with the buffer open so the two paths cannot
  drift; a HAL failure closes the codec.
- **Short reads in the WAV path.** `wav_read()` loops on the stereo path until
  the request is met or the source returns nothing, using absolute offsets, so
  a chunked adapter (or a VFS that answers with less than it was asked) can no
  longer truncate a chunk or lose frame alignment. The mono path already
  looped. A trailing partial frame is simply requested again, which keeps
  alignment without a carry buffer.

### 18.2 Evidence (host only, no device run)

- `tests/player`: new `check_media_player()` drives a WAV (5000 stereo frames)
  through a media that answers 37 bytes at a time — open succeeds,
  `duration_ms == 113`, the started source reports 5000 frames and 100 decoded
  frames match the fixture pattern (so short reads neither truncate nor
  misalign). The FLAC fixture through the same media reports 44.1 kHz stereo,
  46 ms and a 2048-frame stream; `"HELO"` is refused with
  `HELIKON_ERR_UNSUPPORTED` before the HAL is touched and a NULL player with
  `HELIKON_ERR_ARGUMENT`.
- `tests/player` gained a `sanitize` target (same flags as the codec suite):
  PASS under ASan/UBSan.
- Full matrix green: `tests/codec` (test + sanitize), `tests/vfs` (test +
  sanitize), `tests/player` (test + sanitize), `tests/hfsplus` (test +
  sanitize), `tests/firmware` (clean, test, sanitize), `tests/bootloader`
  (test + sanitize), `tests/image` (test, crash-test, write-diag-test,
  outer-diag-test, loader-test), `tests/musicdb`, `tests/ui`.
- Cross-builds: plain 6G 77,856 B; music-hub config with the MP3 fixture
  768,280 B; `VERSION=2 BOOTSTATE_STATE=trying` image plus
  `./tools/check-f2-artifact.sh build/firmware/6g/out`: PASS.

### 18.3 Remaining for T-058

The read seam and the open path are host-complete. Still missing: the
read-ahead ring (how many buffers, who owns the refill, how an underrun is
reported), the Music-hub call sites that turn a selected track into a VFS
handle and a `vfs_media`, pause/resume/seek through that path at hub level, the
I2S/DMA run and the device evidence. Gapless across a track boundary, resume
and the T-100/T-101 slices stay where they were.

## 19. Seeking through the player — T-058 host slice (2026-11-21)

Status: `in progress` as host code with host tests. No Music-hub caller, no
I2S/DMA run and no device run.

### 19.1 What landed

`helikon_player_seek_ms(player, ms)` is the player-level seek the hub needs; it
owned no code before, even though all three codecs had `seek`:

- The target is `ms × rate / 1000` output frames, clamped to the last frame, so
  `duration_ms` is a legal argument. On a zero-length stream it returns
  `HELIKON_ERR_UNSUPPORTED` instead of dividing by nothing.
- Compressed audio (WAV through a source, FLAC, MP3) seeks the codec first; a
  codec without seek, or a codec that refuses the frame, fails before the stream
  is touched, so a failed seek leaves playback where it was.
- The PCM stream is then restarted at the target: `helikon_audio_stream_stop`
  (which mutes and drops the ring) and a fresh start with the remaining frame
  count. The old ring is never played out, so a seek cannot leak pre-seek audio
  into the new position.
- The resident WAV path restarts at the matching byte offset instead of going
  through a codec.
- The new `start_frame` field makes `helikon_player_position_ms()` report
  `start_frame + frames played in this segment`, so a seek does not reset the
  clock to zero, and `helikon_player_play_chunk()` keeps reporting the right
  offset afterwards.
- A seek that lands on the frame already being emitted is a no-op (no ring
  restart, no audible gap), a paused player is re-paused after the restart, and
  a seek before the first chunk only moves the start point — the caller decides
  when audio begins.

### 19.2 Evidence (host only, no device run)

`tests/player` (`test` and `sanitize`) adds `check_seek_player()`:

- resident WAV: start, seek to 50 ms, restart reports `5000 − 2205` frames, the
  buffer pointer lands on sample `(2205, 2206)`, `position_ms() == 50`; the same
  seek again does not restart; a seek past the end clamps to one frame;
- media WAV with 37-byte reads: seek to 25 ms before the first chunk starts no
  stream, then `play_chunk()` starts at `5000 − 1102` frames and the first ten
  decoded frames are `(1102, 1103)…`; pause then seek keeps `audio.paused`;
- FLAC: seek to 20 ms restarts at `2048 − 882` frames and ten decoded frames
  match the whole-file decode at frame 882, frame for frame;
- MP3: seek to 10 ms restarts at `total_frames − 441` on a trimmed (LAME tag)
  file, so the gapless length and the seek agree;
- a closed player returns `HELIKON_ERR_ARGUMENT`.

Full matrix and the three cross-builds were re-run with this slice: all green
(plain 6G 77,856 B, music-hub 768,396 B, F2 image + artifact check PASS).

### 19.3 Remaining for T-058

Everything §18.3 lists is still missing — read-ahead policy, Music-hub call
sites, I2S/DMA run, device evidence — plus next/previous (a queue) and the
hub-level seek binding. This slice only gives them an API to call.

## 20. Vorbis comments for FLAC — T-099 metadata slice (2026-11-21)

Status: `implemented` as host code with host tests; no library-index caller and
no device run. This is the FLAC half of T-099's tag matrix, not the matrix.

### 20.1 What landed

`firmware/codec/tag/vorbis.{c,h}` reads a FLAC `VORBIS_COMMENT` block (metadata
type 4) into fixed UTF-8 fields — title, artist, album, album artist, genre,
year, track, disc:

- `helikon_vorbis_comment_parse()` takes a block body (vendor length + vendor +
  comment count + length-prefixed `NAME=value` entries) and zeroes the struct
  first, so a block with nothing of interest leaves it empty.
- `helikon_flac_tags_buffer()` checks `fLaC`, walks the metadata chain of a
  resident file and parses the first comment block. A file with no comment block
  is `HELIKON_ERR_NOT_FOUND`, not an error.
- `helikon_flac_tags_source()` does the same over a `read_at` source: it needs a
  reported size (the metadata chain has no other bound; a source with no size is
  `HELIKON_ERR_UNSUPPORTED`), reads only block headers plus at most
  `HELIKON_VORBIS_BLOCK_MAX` (4 KiB) of the comment body, and parses a larger
  block prefix-wise — complete comments are kept, a cut-off trailing comment is
  dropped. The block buffer is static because the codec stack budget is 2 KiB;
  the reader is single-threaded like the probe.
- Values are already UTF-8: a valid sequence is copied up to the 96-byte field
  cap on a code-point boundary, an invalid or truncated sequence drops the whole
  value rather than storing half a character. The first value of a repeated
  field wins, `TRACKNUMBER=3/12` and `DISCNUMBER=2/2` yield the leading number,
  and `DATE`/`YEAR` yield the first four digits. `TRACKTOTAL`, `DISCTOTAL`,
  `ALBUMARTISTSORT` and friends are ignored for now.
- A comment name without `=`, or a length that runs past the block, is
  `HELIKON_ERR_FORMAT`; the parser never walks past the block.

The struct deliberately mirrors `struct helikon_id3v2_tags` instead of sharing
it: the two formats will merge when the library index (T-067) defines the track
record it stores, and merging now would churn the MP3 wrapper for no gain.

### 20.2 Evidence (host only, no device run)

- `tests/codec` (test + sanitize) adds `check_vorbis_tags()`: a synthetic
  `fLaC` + STREAMINFO + comment block with eight tags (including a two-byte
  UTF-8 sequence and `7/12` / `2/2` pairs), first-value-wins, an invalid UTF-8
  value (dropped), a comment without `=`, a comment count past the block end, a
  file with no comment block, a non-FLAC buffer, the same bytes through a
  source answering 61 bytes at a time, a source with no size, and null
  arguments.
- A new committed fixture `fixtures/tagged16.flac` is `stereo16.flac` plus eight
  comments written by `metaflac` (the generator in `make_fixtures.py` recreates
  it). The test reads it from a buffer and through a 173-byte source, and reads
  the untagged `stereo16.flac` to prove an empty comment block is `OK` with
  empty fields.
- `firmware/codec/tag/vorbis.c` is in the AUDIO source set. The 6G music-hub
  image still links at 768,396 B because nothing calls the reader yet and
  `--gc-sections` drops it — the same situation the codec layer was in before
  the hub called it.

### 20.3 Remaining for T-099

The library index (T-067) has to call these readers and store the result; then
Ogg Vorbis comments (a different container for the same block), APE tags, MP4
and ASF tags, multi-value fields, artwork, ReplayGain 1.0/2.0, cuesheets and
chapters remain. Nothing here is device-tested.

## 21. Read-ahead window over the pull source — T-058 host slice (2026-11-21)

Status: `in progress` as host code with host tests. No Music-hub caller, no
I2S/DMA run, no profile on the device and no device run.

### 21.1 Why

The pull-source model (14) gave the decoders absolute-offset reads, and the VFS
adapter (15) served each one with a seek plus a read. That is correct and it is
also one storage access per decoder call: a FLAC frame header (a few bytes), a
4 KiB frame window, an MP3 reservoir refill. On a 1.8" disk the seek is the cost,
not the bytes, so the layer that turns many small pulls into few large reads is
the difference between a decoder that keeps up and one that starves the ring.

### 21.2 What landed

`firmware/player/read_ahead.c/.h` wraps any `struct helikon_codec_source` in a
window the caller provides:

- `helikon_read_ahead_init(read_ahead, buffer, capacity, downstream, source)`
  fills `source`; `source->size` reports the downstream length unchanged, so a
  decoder cannot tell which source it holds. `capacity == 0` is legal and makes
  the wrapper a pass-through, so a caller with no spare buffer still works.
- A request inside the window is answered from memory (a hit). A request that
  straddles the window end is served partly from memory and finished with one
  refill (a straddle). A miss refills with a single `capacity` request at the
  requested offset and loops until the request is satisfied or the stream ends.
- A request at least as large as the window is a bypass: it goes straight to the
  media, because filling the window and copying out of it would cost more than
  the read itself. The window survives a bypass — its bytes are still true.
- Media is immutable while a track is open, so the window is never invalidated,
  not even across a seek: a backward seek the player has already read costs no
  storage access. That is what makes a decoder's reservoir re-read cheap.
- A short read is not end of stream (a fork boundary or a bounce limit returns
  fewer bytes than asked); only a zero-byte read latches the end, so the wrapper
  stops asking. A downstream error is returned unchanged rather than being
  reported as a short read, which a decoder would treat as a truncated file.
- `struct helikon_read_ahead_stats` counts reads, bytes, hits, straddles,
  bypasses and refills. Nothing in the decoder path reads them; the tests do.
- `helikon_player_open_media()` now wraps the caller's source in a
  `HELIKON_READ_AHEAD_BYTES` (16 KiB) window it owns, so the probe and the
  decoder both pull through it. Callers still pass a plain source: the policy
  and its buffer belong to the player, not to the Music hub.

### 21.3 Evidence (host only, no device run)

`tests/player` (`test` and `sanitize`) adds `test_read_ahead`:

- 64 one-KiB pulls over a 64 KiB file cost 16 window reads (`refills == 16`,
  `hits == 48`), each window filled with one 16 KiB request and every byte
  fetched once;
- a backward read inside the filled window costs no storage access;
- a window-sized request bypasses (one read), and the window is still there
  afterwards;
- a request crossing the window end returns contiguous bytes with one refill and
  is counted as a straddle;
- a 5,000-byte file: a read past the end returns the remaining 1,000 bytes, a
  read at the end returns 0 without touching the media, and the end stays
  latched;
- an unknown-size source latches its end from the zero read and keeps the read
  count small;
- every refusal is checked (null read-ahead, downstream, source, buffer) and a
  pass-through with no buffer still returns bytes;
- a media error mid-refill reaches the caller unchanged and leaves no partial
  window behind.

`test_player` gains one assertion at the seam: after opening a WAV through a
media that returns 37 bytes at a time, the probe's 4 KiB request must have
arrived at the media as one 16 KiB request.

Full matrix re-run: `tests/{firmware,hfsplus,vfs,codec,musicdb,player,ui}`,
`tests/image` and `tests/bootloader` pass, and their sanitizer targets pass
(`player`, `hfsplus`, `vfs`, `codec`, `firmware`, `bootloader`). Cross-builds:
plain 6G unchanged, the music-hub image still links at 768,176 B — the window
lives in `.bss` and the raw image does not grow.

### 21.4 Remaining for T-058

The Music-hub call sites (open a track through the VFS, play, seek, next and
previous), the I2S/DMA run, an underrun/read-latency measurement on the device,
and the queue behind next/previous. The window is policy, not evidence: nothing
here says the 6G keeps up.
