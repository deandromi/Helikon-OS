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

The fixture set is generated content only. It contains no owner library audio.
