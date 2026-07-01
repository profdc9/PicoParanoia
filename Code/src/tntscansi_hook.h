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

#endif // TNTSCANSI_HOOK_H
