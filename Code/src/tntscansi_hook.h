// PicoParanoia hook for TNTSCAnsi: blit each changed cell straight into the
// pico_ntsc framebuffer. TNTSCAnsi.cpp auto-includes this if present; without
// it, TNTSCAnsi is an ordinary standalone VT100 text-buffer emulator.
//
// `index` is the linear position into the character buffer, so row = index/cols
// and col = index%cols. `ch` already carries the reverse-video bit in bit 7
// (TNTSCAnsi stores ch | ((attrib & 0x08) << 4)), which is exactly the pico_ntsc
// cell format.
#ifndef TNTSCANSI_HOOK_H
#define TNTSCANSI_HOOK_H

#include "pico_ntsc.h"

#define TNTSCANSI_CELL_CHANGED(cur, index, ch)                       \
    pico_ntsc_put_cell((index) / (cur).columns,                      \
                       (index) % (cur).columns,                      \
                       (unsigned char)(ch))

// Whole-region clear: clear_region() fills a span of cells with blanks and
// fires this once instead of the per-cell hook above, so the framebuffer gets
// one fast solid fill instead of one glyph-blit per cell -- fewer writes to
// the scanned-out framebuffer means less visible flicker on a clear.
// `reverse` is true when the blanks should render as reverse video (white).
#define TNTSCANSI_REGION_CLEARED(cur, y1, x1, y2, x2, reverse)       \
    pico_ntsc_clear_span((y1), (x1), (y2), (x2), (reverse))

#endif // TNTSCANSI_HOOK_H
