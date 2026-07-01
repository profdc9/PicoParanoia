// pico_ntsc — composite scanout engine + text framebuffer.
//
// Monochrome NTSC via one PIO SM (2 bits/sample: GPIO16 sync, GPIO17 luma) fed
// by a self-reloading DMA loop over a precomputed frame. Glyphs are blitted into
// the frame's active region; the cursor is XOR-rendered from a repeating timer.
// See pico_ntsc.h and PORTING.md §1.5.
//
// The system clock is chosen so the horizontal line rate is EXACT (not an
// approximation): sysclk 126 MHz (= 12 MHz * 126 / (6*2)), clkdiv 7 -> sample
// clock exactly 18 MHz. 18 MHz / 1144 = 4.5 MHz / 286 = 15734.2657 Hz, the exact
// NTSC line rate; 262 lines -> 60.06 Hz. (PAL reuses the same 126 MHz / 18 MHz
// with 1152 samples/line -> exactly 15625 Hz and 312 lines -- a two-constant
// swap.) The active window is a NARROW ~35.6 us (640 px) with a fat ~9.8 us back
// porch, matching the proven STM32 ParanoiaBox generator so it sits inside TV
// overscan.
//
// 1144 is not a multiple of 16, so scanlines are not word-aligned; we index the
// framebuffer by ABSOLUTE sample number. The frame total (1144*262 = 299728
// samples = 18733 words) *is* word-aligned, which is all the looping DMA needs.

#include "pico_ntsc.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "ntsc.pio.h"

#define NTSC_PIN_SYNC 16   // out bit 0
#define NTSC_PIN_LUMA 17   // out bit 1

// --- horizontal timing, in 18 MHz samples (1 sample ~= 55.6 ns) ---
#define SAMPLE_CLKDIV 7                    // 126 MHz / 7 = 18 MHz
#define LINE_SAMPLES  1144                 // 63.556 us -> 15734 Hz
#define HSYNC         85                   // 4.7 us leading sync tip
#define ACTIVE_START  300                  // active begins ~16.7 us into the line (horizontal position; tunable)
#define ACTIVE        640                  // 80*8 px (35.6 us) -- narrow, inside overscan
#define VS_TAIL       85                   // vertical-sync line is sync-low except this 4.7 us tail

// --- vertical structure (progressive; two line types only) ---
#define V_LINES       262
#define V_SYNC_LINES  3                    // vertical-blank (sync-low) lines; tunable
#define ACTIVE_LINE0  30                   // first active text scanline (matches STM32 NTSC_VTOP)
#define CELL_H        8
#define CURSOR_BLINK_MS 300

#define FRAME_SAMPLES (LINE_SAMPLES * V_LINES)   // 299728
#define FRAME_WORDS   (FRAME_SAMPLES / 16)       // 18733 (frame total is word-aligned)

// --- state ---
static uint32_t frame[FRAME_WORDS];
static const uint32_t *const frame_src = frame;
static const uint8_t (*cur_font)[8] = pico_ntsc_font_unscii8;
static pico_ntsc_mode_t cur_mode = PICO_NTSC_MODE_80;
static int  cur_cols = 80;
static int  cur_row, cur_col;
static bool cur_visible, cur_shown;
static repeating_timer_t blink_timer;

// One 2-bit sample at ABSOLUTE index i: bit0 = sync (0 = sync tip), bit1 = luma.
static inline void put(int i, int sync, int luma) {
    uint32_t bits = (sync ? 1u : 0u) | (luma ? 2u : 0u);
    int w = i >> 4, shift = (i & 15) * 2;
    frame[w] = (frame[w] & ~(3u << shift)) | (bits << shift);
}
static inline void luma_set(int i, int v) {
    int w = i >> 4, b = (i & 15) * 2 + 1;
    if (v) frame[w] |= (1u << b); else frame[w] &= ~(1u << b);
}
static inline void luma_xor(int i) {
    int w = i >> 4, b = (i & 15) * 2 + 1;
    frame[w] ^= (1u << b);
}

// Sample index of column x within scanline L.
static inline int sample_at(int line, int x) { return line * LINE_SAMPLES + x; }

static void build_frame(void) {
    for (int L = 0; L < V_LINES; L++) {
        int base = L * LINE_SAMPLES;
        if (L < V_SYNC_LINES) {
            // Vertical-blank line: sync low for most of the line, released at the tail.
            for (int i = 0; i < LINE_SAMPLES; i++)
                put(base + i, (i < LINE_SAMPLES - VS_TAIL) ? 0 : 1, 0);
        } else {
            // Standard line: leading HSYNC tip, then blanking (black).
            for (int i = 0; i < LINE_SAMPLES; i++) put(base + i, 1, 0);
            for (int i = 0; i < HSYNC; i++)        put(base + i, 0, 0);
        }
    }
}

// --- blitting ---

void pico_ntsc_put_cell(int row, int col, uint8_t cell) {
    if (row < 0 || row >= PICO_NTSC_ROWS || col < 0 || col >= cur_cols) return;
    int scale = (cur_mode == PICO_NTSC_MODE_40) ? 2 : 1;
    int x0 = ACTIVE_START + col * 8 * scale;
    uint8_t g = cell & 0x7F;
    int reverse = cell & 0x80;

    uint32_t save = save_and_disable_interrupts();
    if (cur_shown && row == cur_row && col == cur_col) cur_shown = false;  // don't fight the cursor
    for (int gr = 0; gr < CELL_H; gr++) {
        int line = ACTIVE_LINE0 + row * CELL_H + gr;
        uint8_t bits = cur_font[g][gr];
        for (int gc = 0; gc < 8; gc++) {
            int lum = ((bits & (0x80 >> gc)) ? 1 : 0) ^ (reverse ? 1 : 0);
            int s = sample_at(line, x0 + gc * scale);
            luma_set(s, lum);
            if (scale == 2) luma_set(s + 1, lum);
        }
    }
    restore_interrupts(save);
}

void pico_ntsc_put_text(int row, int col, const char *s, bool reverse) {
    while (*s && col < cur_cols)
        pico_ntsc_put_cell(row, col++, (uint8_t)*s++ | (reverse ? 0x80 : 0));
}

void pico_ntsc_clear(void) {
    uint32_t save = save_and_disable_interrupts();
    for (int r = 0; r < PICO_NTSC_ROWS; r++)
        for (int gr = 0; gr < CELL_H; gr++) {
            int line = ACTIVE_LINE0 + r * CELL_H + gr;
            for (int i = 0; i < ACTIVE; i++) luma_set(sample_at(line, ACTIVE_START + i), 0);
        }
    cur_shown = false;
    restore_interrupts(save);
}

// --- cursor (XOR of the cursor cell's luma = a reverse-video block) ---

static void cursor_toggle(void) {
    int scale = (cur_mode == PICO_NTSC_MODE_40) ? 2 : 1;
    int x0 = ACTIVE_START + cur_col * 8 * scale;
    for (int gr = 0; gr < CELL_H; gr++) {
        int line = ACTIVE_LINE0 + cur_row * CELL_H + gr;
        for (int gc = 0; gc < 8 * scale; gc++) luma_xor(sample_at(line, x0 + gc));
    }
}

static bool blink_cb(repeating_timer_t *t) {
    (void)t;
    if (cur_visible) { cursor_toggle(); cur_shown = !cur_shown; }
    return true;
}

void pico_ntsc_set_cursor(int row, int col, bool visible) {
    uint32_t save = save_and_disable_interrupts();
    if (cur_shown) { cursor_toggle(); cur_shown = false; }   // hide at old position
    if (row >= 0) cur_row = row;
    if (col >= 0) cur_col = col;
    cur_visible = visible;
    restore_interrupts(save);
}

// --- setup ---

int  pico_ntsc_cols(void) { return cur_cols; }

// Readability default per mode: regular weight at 80 columns (thin washes out at
// that pixel rate on a composite TV), thin at 40 columns (crisper). Overridable
// via pico_ntsc_set_font().
static const uint8_t (*default_font(pico_ntsc_mode_t mode))[8] {
    return (mode == PICO_NTSC_MODE_40) ? pico_ntsc_font_unscii8_thin
                                       : pico_ntsc_font_unscii8;
}

void pico_ntsc_set_font(const uint8_t font[128][8]) {
    cur_font = (const uint8_t (*)[8])font;
}

void pico_ntsc_set_mode(pico_ntsc_mode_t mode) {
    pico_ntsc_clear();
    cur_mode = mode;
    cur_cols = (mode == PICO_NTSC_MODE_40) ? 40 : 80;
    cur_font = default_font(mode);
}

static void start_dma_loop(PIO pio, uint sm) {
    int data_chan   = dma_claim_unused_channel(true);
    int reload_chan = dma_claim_unused_channel(true);

    dma_channel_config dc = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&dc, reload_chan);
    dma_channel_configure(data_chan, &dc, &pio->txf[sm], frame, count_of(frame), false);

    dma_channel_config rc = dma_channel_get_default_config(reload_chan);
    channel_config_set_transfer_data_size(&rc, DMA_SIZE_32);
    channel_config_set_read_increment(&rc, false);
    channel_config_set_write_increment(&rc, false);
    dma_channel_configure(reload_chan, &rc,
                          &dma_hw->ch[data_chan].al3_read_addr_trig, &frame_src, 1, false);

    dma_channel_start(data_chan);
}

bool pico_ntsc_init(pico_ntsc_mode_t mode) {
    if (!set_sys_clock_khz(PICO_NTSC_SYS_CLOCK_KHZ, false)) return false;

    cur_mode = mode;
    cur_cols = (mode == PICO_NTSC_MODE_40) ? 40 : 80;
    cur_font = default_font(mode);
    cur_visible = false;
    cur_shown = false;
    cur_row = cur_col = 0;

    build_frame();

    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &ntsc_program);
    pio_gpio_init(pio, NTSC_PIN_SYNC);
    pio_gpio_init(pio, NTSC_PIN_LUMA);
    pio_sm_set_consecutive_pindirs(pio, sm, NTSC_PIN_SYNC, 2, true);

    pio_sm_config c = ntsc_program_get_default_config(offset);
    sm_config_set_out_pins(&c, NTSC_PIN_SYNC, 2);
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&c, SAMPLE_CLKDIV, 0);
    pio_sm_init(pio, sm, offset, &c);

    start_dma_loop(pio, sm);
    pio_sm_set_enabled(pio, sm, true);

    add_repeating_timer_ms(-CURSOR_BLINK_MS, blink_cb, NULL, &blink_timer);
    return true;
}
