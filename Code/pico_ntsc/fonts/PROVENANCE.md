# Vendored font data (pico_ntsc)

`pico_ntsc_fonts.c` is **generated**, not hand-written. Two 128×8 8×8 tables,
MSB = leftmost pixel. Single-byte cell model: glyph index is `cell & 0x7F`;
bit 7 of the cell selects reverse video (applied by the blitter, not stored in
the glyph). All sources are **public domain**.

## Codepoints 0x20–0x7F — Unscii

- **Source:** Unscii by Viznut — http://viznut.fi/unscii/
- **Files:** `unscii-8.hex` (regular) and `unscii-8-thin.hex` (thin), 8×8 variant.
- **License:** public domain.
- Two weights are kept for readability tuning (PORTING.md §1.5): the regular
  weight has ~2px stems and stays legible at 80 columns where TV luma bandwidth
  attenuates 1px detail; the thin weight is crisper at 40 columns / on
  higher-bandwidth displays. Selection is global (whole screen), runtime-toggleable.

## Codepoints 0x00–0x1F — raster88_font

- **Source:** `raster88_font` from the original ParanoiaBox
  (`libraries/TNTSChar/TNTSCharfont.c`), header note: "These fonts are in the
  public domain."
- These CP437-style control-range UI symbols (card suits, arrows, inverse
  boxes, …) are spliced in because Unscii leaves that range blank. Bit order
  already matches Unscii (MSB = leftmost), verified against the ►/◄ glyphs.

## Regeneration

Tables were produced by parsing the upstream `.hex` files (rows 0–7) for
0x20–0x7F and the first 32 `raster88_font` entries for 0x00–0x1F. If the set
ever changes, regenerate rather than editing `pico_ntsc_fonts.c` by hand.
