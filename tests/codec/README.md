# Codec host fixtures

The `.flac` and `.pcm` pairs are synthetic fixtures for the host decoder test.
The `.pcm` file is the exact source PCM that the FLAC encoder received; the test
decodes the FLAC and compares every output sample with it.

`make_fixtures.py` recreates the set with the system `flac` encoder:

```sh
python3 tests/codec/make_fixtures.py
```

Coverage:

| Fixture | Profile | Subframe/coverage |
|---|---|---|
| `stereo16` | 16-bit, 44.1 kHz, stereo | LPC, mid-side stereo |
| `mono16` | 16-bit, 44.1 kHz, mono | fixed predictor |
| `stereo24` | 24-bit, 48 kHz, stereo | fixed predictor, wasted bits |
| `mono24` | 24-bit, 48 kHz, mono | fixed predictor, wasted bits |
| `constant16` | 16-bit, 44.1 kHz, stereo | constant subframes |
| `verbatim16` | 16-bit, 44.1 kHz, stereo | verbatim subframes |
| `stereo16_48` | 16-bit, 48 kHz, stereo | fixed predictor |
| `left_side16` | 16-bit, 44.1 kHz, stereo | left-side stereo |
| `tagged16` | `stereo16` audio + 8 Vorbis comments | tag reader fixture (`tag/vorbis.c`) |

`tagged16.flac` is a copy of `stereo16.flac` with `metaflac --set-tag` applied
(title, artist, album, album artist, genre, date, track `3/12`, disc `2/2`), so
the Vorbis-comment reader is exercised against a real encoder file as well as
the synthetic blocks built inside `test_codec.c`.

## MP3 fixtures

`make_mp3_fixtures.py` recreates the MP3 set. LAME writes the Xing/Info frame
and the LAME tag every fixture needs; the one Layer II fixture goes through
ffmpeg's MP2 encoder because LAME encodes Layer III only.

```sh
python3 tests/codec/make_mp3_fixtures.py
```

| Fixture | Profile | Coverage |
|---|---|---|
| `mp3_cbr_stereo16` | MPEG-1 Layer III, 44.1 kHz, 128 kbps, stereo | CBR, Xing, sine peak, seek |
| `mp3_vbr_stereo16` | MPEG-1 Layer III, 44.1 kHz, VBR `-V 5`, stereo | Xing TOC, post-seek reservoir rebuild |
| `mp3_cbr_mono16` | MPEG-1 Layer III, 44.1 kHz, 64 kbps, mono | mono duplication to stereo |
| `mp3_id3_stereo16` | as CBR, with an ID3v2 tag | tag skip before the first sync |
| `mp3_mpeg2_stereo16` | MPEG-2 Layer III, 22.05 kHz, 64 kbps | 576-sample frames, LAME trim, frame-index seek |
| `mp3_mpeg25_stereo16` | MPEG-2.5 Layer III, 11.025 kHz, 32 kbps | lowest MPEG family, 576-sample frames |
| `mp3_layer2_stereo44` | MPEG-1 Layer II, 44.1 kHz, 128 kbps | Layer I/II path, no Xing frame, no gapless claim |

The two low-rate fixtures exist because MPEG-2 and 2.5 carry 576 samples per
frame rather than 1152; `check_mp3_family()` pins the sample rate, the total
frame count, the LAME delay and padding, the trimmed output length and a
frame-index seek for each. Layer I has no fixture: no common encoder produces
it, and the wrapper's Layer I branch stays unexercised rather than claimed.

The fixture set is generated content only. It contains no owner library audio.

## Mutation sweep

`test_codec_mutation` corrupts a valid fixture a few fields at a time and drives
the whole decode path over it — probe, open, read, seek, close, then the same
bytes through the streaming source. It asserts nothing about the audio (a corrupt
stream may be refused or decode to noise), only that the decoder terminates,
stays inside its workspace and PCM buffer, and does not trip ASan or UBSan. It
found #106 and #107; both have a hand-written regression test in `test_codec.c`.

```sh
make test_codec_mutation && ./test_codec_mutation          # 600 passes per fixture
./test_codec_mutation 7 20000                              # seed, iterations
MUTATION_TRACE=1 ./test_codec_mutation 7 20000             # a line per pass
```

`make sanitize` runs both binaries with `UBSAN_OPTIONS=halt_on_error=1`. The
vendored libmad objects are built with AddressSanitizer only (`../libmad.mk`):
its fixed-point DSP overflows on corrupt input by design, while a buffer overrun
in it is still caught.
