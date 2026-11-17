# Vendored libmad (MPEG audio decoder)

Imported per D-49 and `docs/audio-formats.md` §12. Import date: 2026-11-17.

- **Source:** Rockbox mirror of libmad,
  `lib/rbcodec/codecs/libmad/`, pinned Rockbox commit
  `97986f2cdaec1585856d708966858c56f5dedc6b` (master, 2026-11-17).
  Upstream identity: libmad-0.15.1b (2004-02-17), Underbit Technologies.
- **Licence:** GPL-2.0-or-later (`COPYING`); copyright holders in
  `COPYRIGHT`/`CREDITS`. This vendored copy keeps every original file
  verbatim except for the three edits below.
- **Used by:** T-068/T-097 MP3 decode path; wrapped by
  `firmware/codec/mp3/mp3_codec.c` (Helikon-owned, GPL-2.0-or-later).

## Modifications (complete list)

1. `global.h` line 21 and `frame.c` line 32:
   `#include "codeclib.h"` → `#include "libmad_port.h"`.
2. `mad_iram.h` line 29: `#include "config.h"` →
   `#include "libmad_port.h"`.
3. `mad.h` and `fixed.h`: `#include <inttypes.h>` →
   `#include <stdint.h>` (the build is freestanding; `inttypes.h` is not
   available under `-nostdinc`).

`libmad_port.h` is Helikon-owned. It defines the Rockbox placement
attributes (`ICODE_ATTR`, `ICONST_ATTR`, `IDATA_ATTR`, `IBSS_ATTR`,
`MEM_ALIGN_ATTR`) as empty — no IRAM placement in this port — sets
`HAVE_LIMITS_H` for hosted builds (host tests using libc `<limits.h>`)
and defines `INT_MAX`/`UINT_MAX` for the freestanding target build, which
has no `<limits.h>`; it also provides the freestanding
`stdint.h`/`string.h` includes the Rockbox headers assume and `betoh32`
(byteswap on little-endian hosts) for Rockbox's optimized
`mad_bit_read`. Library objects compile with `-DNDEBUG`, which matches
Rockbox's assert-free codec builds.

## Notes for maintainers

- **Known sanitizer report (upstream, not a bug to "fix" here):** under
  UBSan, `synth.c:1155` reports `index -15 out of bounds for type 'const
  mad_fixed_t[32]'`. This is Rockbox's deliberate `D[0] - p` window
  arithmetic (see the comment at the `D1ptr` assignment in `synth_full`):
  the pointer moves before row 0 and the later `prod_sb` offsets land back
  inside the `D[17][32]` table, so every actual read is in bounds; only the
  intermediate pointer arithmetic is UB by the letter of C. Left verbatim
  to stay close to the pinned upstream.
- **Buffer wiring contract:** callers must set `frame.sbsample`,
  `frame.sbsample_prev`, `frame.overlap` and `stream.main_data` BEFORE
  `mad_frame_init` (the init memsets through those pointers), and
  `sbsample`/`sbsample_prev` must point at the SAME buffer on a single-core
  port: the layer decoders write `frame->sbsample` while the Rockbox-modified
  `synth.c` reads `frame->sbsample_prev`. Rockbox's single-core
  `init_mad()` (in its `mpa.c`) does exactly this; the pointer swap exists
  only for the dual-core decode/synth thread split.

## Excluded from the import

- The ARM assembly accelerators (`imdct_l_arm.S`, `synth_full_arm.S`,
  `synth_full_armv5e.S`, `dct32_arm.S`) and the Coldfire assembly
  (`imdct_mcf5249.S`): the port builds the plain C paths (`FPM_DEFAULT`)
  so a host build and the 6G build share the same source. Re-adding an
  ARM accelerator is a later, separately measured change.
- `D_armv5e.dat` (only used by the excluded `synth_full_armv5e.S`).
- Rockbox build files (`libmad.make`).
