# Dr. Unslopovich — firmware music UI

```text
Dr. Unslopovich's diagnosis
Verdict: The paper and the engraving are the product; the clock slot is a permanent
         placeholder, five of six home cells open a build note, and an empty Tracks
         list is headed EMPTY.
Stack: C firmware renderer for a 320x240 RGB565 panel (iPod Classic 6G), no OS toolkit.
Reviewed: launcher; Music hub; ALBUMS and ARTISTS walls; Artists list (normal, long
          names, missing art); Artist page x3 tabs + empty; Album page (normal, empty,
          corrupt, long text); Now Playing x8 states (playing, paused, opening,
          finished, no art, long text, error-retry, error-terminal). From the
          committed host renders in assets/ui/ plus firmware/ui/*.c. Not run on a
          device; no panel photographs.
Score: 13/24 Admitted  Task path 2 · Truth 1 · Native fit 2 · States 3 · Access ~2 · Specificity 3
Performance: Not profiled. Static leads: every wheel step repaints the whole 320x240
             frame and calls helikon_screen_present_all; each row re-reads strings and
             HDB records (read_string/hdb_* per row, per repaint); the visualizer adds
             one fill-rect pass per frame only while playing. No frame times recorded.

Critical
- firmware/ui/music.c:1095 draw_top_bar() draws a clock slot that is always "--:--"
  (or "FONT FALLBACK"): no code path in music.c formats a wall-clock time. Every
  screen in the flow shows a fake time where the design promises the real one, and
  the only two values it can show are a placeholder and a diagnostic. -> Draw the real
  time from the boot/RTC clock, or delete the slot and keep the battery alone.
- firmware/ui/launcher_run.c:19 five of six launcher cells (Podcasts, Audiobooks,
  Playlists, Games, Settings) open "APP SCREEN COMING NEXT / THIS BUILD PROVES THE
  LAUNCHER SELECTION AND FOCUS PATH" in 2-4x type, with no header, no battery, no
  clock. The most-tapped surface in the product is a note about the build. -> Until a
  cell has a screen, either do not draw the cell or show one line in the product's own
  voice ("NOT IN THIS BUILD YET") with the standard top bar and footer.
- firmware/ui/music.c:1010 run_list() draws the literal header "EMPTY" and the body
  "NO ITEMS" when the track list is empty, which is the first-run state of a fresh
  library; every other list has designed copy. -> Use the designed empty state
  ("NO TRACKS IN LIBRARY") in the shared runner.
- firmware/ui/music.c:2717 make_error_reason() promotes transport/codec tokens such as
  "AUDIO CODEC/12C" to the user-facing cause line. A hex code is not a sentence. ->
  Map the code to a plain cause ("This file's format is not supported") and keep the
  code in the detail block.

Noticeable
- firmware/ui/music.c:1846,1895,1979 the same album/track counts appear up to three
  times on one artist screen: under the genre (y=78), right-aligned inside the tab row,
  and again in the OVERVIEW facts (y=198). -> Keep one canonical count per screen.
- firmware/ui/music.c:1720 "SORT: NAME" sits in the header position other screens use
  for an action, but it is a hardcoded string with no handler; order comes from HDB.
  It is a control that cannot be used and can also become untrue. -> Render it as
  muted metadata ("BY NAME") in the footer band, or make it real.
- firmware/ui/music.c:1802,2086 the Artist and Album pages print no button hints at
  all while eight other screens do; on the Artist page nothing says LEFT/RIGHT changes
  tab. The tab row is unreachable by discovery. -> Give every screen one footer
  grammar, and give the tabs a visible affordance.
- Footer grammar is three languages for the same device: "MENU: BACK" / "SELECT: OPEN"
  (walls, hub, empty frame), "WHEEL SCROLL" + "1–4 / 78" (lists), "PLAY PAUSE ·
  SELECT ALBUM · MENU BACK" (now playing). Footers also sit on four different
  baselines (222, 228, 232, 236). -> One grammar, one baseline.
- firmware/ui/music.c:1304 draw_rating() draws five hollow diamonds when rating==0,
  which is every unrated album: the screen states a rating the user never gave and
  cannot distinguish "unrated" from "zero". -> Draw nothing (or one dash) when unrated.
- Unknown values have four spellings: "YEAR —" (music.c:2146), a bare "—" for genre
  (1854), "SRC — / OUT —" (2371,2396), and a code string (2717). -> One convention;
  prefer dropping a row over printing a dash.
- Missing art has four treatments: framed monogram (artists list), person glyph
  (artists-list mockup only), mountain-and-sun sleeve (album art), generated rings
  (album art in the committed artist-albums asset). The artist-list asset and the code
  disagree. -> One rule: monogram everywhere, since it is the only fallback that
  carries information.
- firmware/ui/music.c:1345 draw_visualizer() draws the inactive baseline in FAINT
  (`0xdeb9`, 1.23:1 on paper): it is present in OPENING, PAUSED and FINISHED, but it is
  invisible, so those states read as a 16 px hole under the title. (Correction,
  2026-11-21: an earlier draft of this item said the baseline was missing; a pixel
  probe of all three committed now-playing assets shows it drawn everywhere. The
  defect is contrast, not layout.) -> Draw the baseline in RULE, and keep the same
  baseline row in every playback state.
- firmware/ui/music.c:2651 the OPENING state replaces the whole footer with "PLEASE
  WAIT" and removes every button mapping, exactly when the user wants to know whether
  MENU backs out of a slow file. "PLEASE WAIT" is also the vaguest string in the UI.
  -> Keep the footer and say "OPENING" in the state line.
- Titles truncate with an ellipsis and never reveal: now playing, list rows, album
  captions (320 px panel, no marquee, no full-title screen). -> Scroll the focused
  title once, or show the untruncated name on the track page.
- firmware/ui/music.c:683 the wall caption joins title and artist with " - " while the
  committed album-wall asset uses "-" and the error identity line uses an em dash.
  -> One separator.
- assets/ui/album-wall/helikon-os-album-wall-320x240.png shows a search magnifier and
  a count; firmware/ui has no search code at all. The committed "binding" asset offers
  a control the product cannot honour. -> Mark the search affordance as not
  implemented in the asset README, or remove it from the asset.
- The committed album mockup shows NOTES and CREDITS rows that only exist behind
  MUSIC_PREVIEW_CREDITS; the shipped page is "NO CREDITS LISTED" unless a track
  carries a composer (music.c:2281-2340). HDB v1 has no notes field. -> Note the gap
  in docs/ui.md rather than drawing a row that is always empty.

Minor
- firmware/ui/music.c:2520 "Press SELECT to retry." and "Return to the album." are
  sentence case with a full stop inside an all-caps UI. -> "SELECT TO RETRY".
- firmware/ui/music.c:2553 the error screen repeats the mapping twice (the instruction
  line and the footer both say SELECT). -> Keep one.
- firmware/ui/music.c:2543 "STOPPED AT 0:00" on a codec-open failure is noise. ->
  Omit the timestamp when the position is zero.
- firmware/ui/music.c:743 draw_wall() has no SELECT hint while the hub does. ->
  Print both or neither.
- firmware/ui/music.c:1808,2091 the MISSING ARTIST / MISSING ALBUM states draw one
  muted line and no way back except the undiscoverable MENU. -> Name the state in the
  user's words and keep the footer.
- The artists list prints three count labels in one header band: section letter,
  "78 ARTISTS", "1–4 / 78", plus the inert "SORT: NAME". -> One count per band.

Ask first
- firmware/ui/music.c:3191 LEFT/RIGHT switch the artist tabs. On an iPod those buttons
  are previous/next track (and back/open in menus); this reassigns a muscle-memory
  gesture and hides the only path to two thirds of the artist page. -> Make the tab row
  a focus stop (wheel moves into it, SELECT switches) and leave LEFT/RIGHT as
  transport. Changes navigation on one screen; needs docs/ui.md and owner sign-off.
- The Music hub is a second launcher: home -> MUSIC -> three cells -> Artists. One hop
  of the most frequent task is spent on a menu of three items. -> Drop the user
  straight into Artists with the wall/list switch as a tab. Changes navigation.
- "FONT FALLBACK" in the clock slot (music.c:1101): keep the honest signal, but not by
  spending the time slot on a diagnostic word. -> Put the signal in the footer band or
  as a broken rule; never trade the battery/time slot for it.

Waived
- Six muted accent colours on the launcher plinths: owner-recorded design (D-23/D-24,
  docs/ui.md). The identity is carried by type, rules and the engraving, not by a
  gradient, so this is brand rather than the generated look.
- 11 px caps secondary text: fixed 320x240 panel and the owner's type scale. Measured
  contrast of MUSIC_MUTED (0x6b2b) on MUSIC_PAPER (0xef7c) is 5.00:1, which passes AA.
- No dark mode, no text scaling: a fixed LCD, not a platform surface.
- The engraved landscape and Benguiat wordmark: owner artwork; licence gate is already
  recorded (D-52, docs/ui.md).

Worth keeping
- The states are real and in this product's voice: "NO CATALOG FACTS", "COUNTS
  UNAVAILABLE", "1 TRACK UNREADABLE", "YEAR —", "HFS+ DETAILS". Nothing says "Oops".
- Selection is a 2 px ink bar, never colour alone.
- PLAY ALL is the only framed control, so the primary action reads as one.
- The error screen says what failed, whether it is retryable, and where it stopped.
- Monogram fallbacks turn missing artwork into a name rather than a grey box.

Not checked
- Performance: not profiled; no device and no frame timings.
- Nothing was run on a 6G: no panel photographs, no measured contrast on the LCD, no
  touch/scroll feel. Geometry comes from the recorded clipping checks (PASS, 16
  screens, 2026-11-08).
- The detector cannot parse C: detect.js scanned 0 files for firmware/ui, so every
  finding here is from code and committed renders by hand.
- Hold button, idle timeout copy, and the font-fallback glyph set were read, not seen.
- The podcast-wall, audiobook-wall and index mockups describe surfaces with no code
  path; they were read as design assets only, not scored.
```
